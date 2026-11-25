#include "PlaySession.h"

#include <Gorgon/Scripting.h>
#include <Gorgon/Scripting/Embedding.h>
#include <Gorgon/Scripting/Input.h>
#include <Gorgon/Scripting/VirtualMachine.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Gorgon :: Editor {

    /// Gorgon's default type-to-string helper streams the mapped value, so give the script
    /// transform one. Found by ADL from Scripting::MappedReferenceType.
    std::ostream &operator<<(std::ostream &out, const ScriptTransform &values) {
        out << "(" << values.x << ", " << values.y << ") rot " << values.rotation;
        return out;
    }

    struct PlaySession::Impl {
        // Declared first so they outlive every VM that reads/writes them.
        std::ostringstream log;
        std::istringstream input;
        mutable std::string output;

        // Keyboard/mouse state the Input library reads. Lives here so every VM sees one state.
        Editor::InputState inputState;

        Scene snapshot;

        // The scene being stepped. Set by Start()/Tick(), cleared by Stop(); the Physics library
        // and the collision pass both read it.
        Scene *scene = nullptr;

        std::unique_ptr<Gorgon::Scripting::Library> library;
        std::unique_ptr<Gorgon::Scripting::Library> inputLibrary;
        std::unique_ptr<Gorgon::Scripting::Library> physicsLibrary;
        Gorgon::Scripting::MappedReferenceType<ScriptTransform> *entityType = nullptr;

        struct ScriptState {
            EntityId entityId = InvalidEntityId;
            std::string path;
            std::unique_ptr<Gorgon::Scripting::VirtualMachine> vm;
            const Gorgon::Scripting::Function *update = nullptr;
            // Optional collision hooks. A hook the script does not define stays null and is skipped.
            const Gorgon::Scripting::Function *onCollisionEnter = nullptr;
            const Gorgon::Scripting::Function *onCollisionExit = nullptr;
            const Gorgon::Scripting::Function *onTriggerEnter = nullptr;
            const Gorgon::Scripting::Function *onTriggerExit = nullptr;
            ScriptTransform values;
        };

        std::vector<std::unique_ptr<ScriptState>> scripts;
        ScriptState *compiling = nullptr;

        // Overlap pairs of the last and the current collision pass; their difference is what drives
        // the enter/exit hooks.
        std::vector<CollisionPair> activePairs;
        std::vector<CollisionPair> previousPairs;

        bool running = false;
        std::string lastError;

        void ResetCollisionState();
        void StepCollisions(Scene &scene);
        void DispatchPair(Scene &scene, const CollisionPair &pair, bool entered);

        /// Calls a collision hook with (self, otherId), writes any transform change back, and drops
        /// the hook when the script throws so a broken handler is not retried every frame.
        void FireHook(Scene &scene, ScriptState &state, const Gorgon::Scripting::Function *&hook,
                      EntityId otherId);
    };


    namespace {
        std::string ReadTextFile(const std::string &path) {
            std::ifstream in(path, std::ios::binary);
            if(!in)
                return {};
            std::ostringstream out;
            out << in.rdbuf();
            return out.str();
        }

        /// Cheap guard so a script without an entry point reports a readable error instead of a
        /// symbol-not-found thrown by the registration line the runtime appends.
        bool DefinesUpdate(const std::string &code) {
            return code.find("function Update") != std::string::npos;
        }

        ScriptTransform ReadTransform(const Entity &entity) {
            ScriptTransform values;
            values.x = entity.transform.x;
            values.y = entity.transform.y;
            values.rotation = entity.transform.rotation;
            values.scaleX = entity.transform.scaleX;
            values.scaleY = entity.transform.scaleY;
            return values;
        }

        void WriteTransform(const ScriptTransform &values, Entity &entity) {
            entity.transform.x = values.x;
            entity.transform.y = values.y;
            entity.transform.rotation = values.rotation;
            entity.transform.scaleX = values.scaleX;
            entity.transform.scaleY = values.scaleY;
        }

        /// Applies a world-space push to an entity by writing its local transform. The parent's
        /// world scale is divided out so a scaled parent neither shrinks nor amplifies the push;
        /// a rotated parent is not compensated (the push would be skewed by it).
        void MoveEntity(Scene &scene, EntityId id, float worldDx, float worldDy) {
            Entity *entity = scene.FindEntity(id);
            if(!entity)
                return;

            float scaleX = 1.0f;
            float scaleY = 1.0f;
            if(entity->parentId != InvalidEntityId) {
                const Transform parent = scene.GetWorldTransform(entity->parentId);
                if(std::abs(parent.scaleX) > 1e-6f)
                    scaleX = parent.scaleX;
                if(std::abs(parent.scaleY) > 1e-6f)
                    scaleY = parent.scaleY;
            }

            entity->transform.x += worldDx / scaleX;
            entity->transform.y += worldDy / scaleY;
        }

        /// True when the script source defines `function <name>`. Used to decide whether the
        /// optional collision hooks are registered at all: referencing a name the script never
        /// defined would fail the whole compile, so the check has to happen before registration.
        bool DefinesFunction(const std::string &code, const char *name) {
            return code.find(std::string("function") + " " + name) != std::string::npos;
        }

        bool HasPair(const std::vector<CollisionPair> &pairs, const CollisionPair &pair) {
            for(const auto &candidate : pairs) {
                if(candidate.a == pair.a && candidate.b == pair.b)
                    return true;
            }
            return false;
        }
    } // namespace

    void PlaySession::Impl::ResetCollisionState() {
        activePairs.clear();
        previousPairs.clear();
    }

    void PlaySession::Impl::FireHook(Scene &scene, ScriptState &state,
                                     const Gorgon::Scripting::Function *&hook, EntityId otherId) {
        if(!hook || !state.vm)
            return;

        Entity *entity = scene.FindEntity(state.entityId);
        if(!entity)
            return;

        try {
            state.values = ReadTransform(*entity);
            Gorgon::Scripting::Data self(entityType, &state.values, true, false);
            Gorgon::Scripting::Data other(Gorgon::Scripting::Types::Int(), int(otherId));
            state.vm->ExecuteFunction(hook, {self, other}, false);
            // A hook may move its entity (a trigger that teleports, a bounce that nudges back).
            WriteTransform(state.values, *entity);
        }
        catch(const std::exception &ex) {
            lastError = std::string("Script hook error: ") + ex.what();
            hook = nullptr;
        }
        catch(...) {
            lastError = "Script hook error";
            hook = nullptr;
        }
    }

    void PlaySession::Impl::DispatchPair(Scene &scene, const CollisionPair &pair, bool entered) {
        for(auto &state : scripts) {
            EntityId other = InvalidEntityId;
            if(state->entityId == pair.a)
                other = pair.b;
            else if(state->entityId == pair.b)
                other = pair.a;
            else
                continue;

            if(pair.trigger) {
                FireHook(scene, *state, entered ? state->onTriggerEnter : state->onTriggerExit, other);
            }
            else {
                FireHook(scene, *state, entered ? state->onCollisionEnter : state->onCollisionExit, other);
            }
        }
    }

    void PlaySession::Impl::StepCollisions(Scene &scene) {
        struct Entry {
            EntityId id = InvalidEntityId;
            CollisionShape shape;
            bool trigger = false;
            bool isStatic = false;
        };

        std::vector<Entry> entries;
        entries.reserve(scene.GetEntityCount());
        for(const auto &entity : scene.GetEntities()) {
            if(!CollisionSystem::HasCollider(entity))
                continue;

            CollisionShape shape;
            if(!CollisionSystem::ComputeShape(scene, entity, shape))
                continue;

            Entry entry;
            entry.id = entity.id;
            entry.shape = shape;
            entry.trigger = entity.collider->isTrigger;
            entry.isStatic = entity.collider->isStatic;
            entries.push_back(entry);
        }

        previousPairs = std::move(activePairs);
        activePairs.clear();

        for(std::size_t i = 0; i < entries.size(); ++i) {
            for(std::size_t j = i + 1; j < entries.size(); ++j) {
                auto &first = entries[i];
                auto &second = entries[j];

                const OverlapResult overlap = CollisionSystem::ComputeOverlap(first.shape, second.shape);
                if(!overlap.overlaps)
                    continue;

                CollisionPair pair;
                pair.a = std::min(first.id, second.id);
                pair.b = std::max(first.id, second.id);
                pair.trigger = first.trigger || second.trigger;
                activePairs.push_back(pair);

                // A trigger reports the overlap but never moves anything, and two static colliders
                // have no degree of freedom to resolve with.
                if(pair.trigger || (first.isStatic && second.isStatic))
                    continue;

                const float depth = overlap.depth;
                if(first.isStatic) {
                    second.shape.Translate(-overlap.normalX * depth, -overlap.normalY * depth);
                    MoveEntity(scene, second.id, -overlap.normalX * depth, -overlap.normalY * depth);
                }
                else if(second.isStatic) {
                    first.shape.Translate(overlap.normalX * depth, overlap.normalY * depth);
                    MoveEntity(scene, first.id, overlap.normalX * depth, overlap.normalY * depth);
                }
                else {
                    // Equal mass: each collider travels half of the minimum translation vector.
                    const float halfX = overlap.normalX * depth * 0.5f;
                    const float halfY = overlap.normalY * depth * 0.5f;
                    first.shape.Translate(halfX, halfY);
                    second.shape.Translate(-halfX, -halfY);
                    MoveEntity(scene, first.id, halfX, halfY);
                    MoveEntity(scene, second.id, -halfX, -halfY);
                }
            }
        }

        // Enter fires for pairs that were not there last frame, exit for the ones that vanished.
        for(const auto &pair : activePairs) {
            if(!HasPair(previousPairs, pair))
                DispatchPair(scene, pair, true);
        }
        for(const auto &pair : previousPairs) {
            if(!HasPair(activePairs, pair))
                DispatchPair(scene, pair, false);
        }
    }

    PlaySession::PlaySession() : impl_(new Impl) { }

    PlaySession::~PlaySession() = default;

    bool PlaySession::Start(Scene &scene, const std::string &baseDir, std::string *outError) {
        // A previous session must restore its snapshot before a new one is taken.
        Stop(scene);

        // Built-in types (Float, Function, ...) are created by the scripting initializer, which
        // is idempotent and also runs from Gorgon::Initialize.
        Gorgon::Scripting::Initialize();

        auto &impl = *impl_;
        impl.lastError.clear();
        impl.snapshot = scene;
        impl.running = true;
        // A key pressed while editing must not fire IsKeyPressed() on the first play frame.
        impl.inputState.ClearEdges();
        impl.scene = &scene;
        impl.ResetCollisionState();

        impl.library = std::unique_ptr<Gorgon::Scripting::Library>(
            new Gorgon::Scripting::Library("EditorPlay", "Play-in-editor script bindings")
        );

        impl.entityType = new Gorgon::Scripting::MappedReferenceType<ScriptTransform>(
            "EntityTransform", "Transform of the entity the running script is attached to"
        );

        // The transform values are 32bit floats, while GScript arithmetic (and Math) produces
        // doubles and integer literals produce ints. Mapping the members directly would copy the
        // raw bytes of whatever the script assigned, silently corrupting the value, so the members
        // are exposed through function accessors instead: reads return the float reference, and
        // writes go through a typed function call which converts the value to float.
        auto member = [](float ScriptTransform::*slot, const std::string &name, const std::string &help,
                         const Gorgon::Scripting::Type *parent) {
            return new Gorgon::Scripting::MappedInstanceMember_Function(
                [slot](ScriptTransform &values) -> float & { return values.*slot; },
                [slot](ScriptTransform &values, float value) { values.*slot = value; },
                name, help,
                Gorgon::Scripting::Types::Float(), parent,
                false, true, false, false
            );
        };

        impl.entityType->AddMembers({
            member(&ScriptTransform::x, "x", "World position on x", impl.entityType),
            member(&ScriptTransform::y, "y", "World position on y", impl.entityType),
            member(&ScriptTransform::rotation, "rotation", "Rotation in degrees", impl.entityType),
            member(&ScriptTransform::scaleX, "scaleX", "Scale on x", impl.entityType),
            member(&ScriptTransform::scaleY, "scaleY", "Scale on y", impl.entityType),
        });
        impl.library->AddMembers({impl.entityType});

        // One registration helper per optional hook, so every hook is wired the same way
        // RegisterUpdate is: handler(self, otherId) stored on the script state being compiled.
        auto hookRegistrar = [this](const char *name, const char *help,
                                    const Gorgon::Scripting::Function *Impl::ScriptState::*slot) {
            return new Gorgon::Scripting::Function(
                name, help, nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [this, slot](const Gorgon::Scripting::Function *handler) {
                            if(impl_->compiling)
                                impl_->compiling->*slot = handler;
                        },
                        nullptr,
                        {
                            Gorgon::Scripting::Parameter(
                                "Handler", "Function to call as handler(self, otherId)",
                                Gorgon::Scripting::Types::Function(), Gorgon::Scripting::ConstTag
                            )
                        }
                    )
                }
            );
        };

        impl.library->AddMembers({
            new Gorgon::Scripting::Function(
                "RegisterUpdate",
                "Registers the script function called every frame as handler(self, dt)",
                nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [this](const Gorgon::Scripting::Function *handler) {
                            if(impl_->compiling)
                                impl_->compiling->update = handler;
                        },
                        nullptr,
                        {
                            Gorgon::Scripting::Parameter(
                                "Handler", "Function to call every frame", Gorgon::Scripting::Types::Function(),
                                Gorgon::Scripting::ConstTag
                            )
                        }
                    )
                }
            ),
            // Optional collision callbacks. Each one mirrors RegisterUpdate exactly: a single
            // handler parameter, registered by a line the runtime appends only when the script
            // defines that hook.
            hookRegistrar(
                "RegisterCollisionEnter", "Registers the handler(self, otherId) called when a solid collision starts",
                &Impl::ScriptState::onCollisionEnter
            ),
            hookRegistrar(
                "RegisterCollisionExit", "Registers the handler(self, otherId) called when a solid collision ends",
                &Impl::ScriptState::onCollisionExit
            ),
            hookRegistrar(
                "RegisterTriggerEnter", "Registers the handler(self, otherId) called when a trigger overlap starts",
                &Impl::ScriptState::onTriggerEnter
            ),
            hookRegistrar(
                "RegisterTriggerExit", "Registers the handler(self, otherId) called when a trigger overlap ends",
                &Impl::ScriptState::onTriggerExit
            )
        });

        // ---- Input library ---------------------------------------------------
        // A separate library, so a script opts into the input API with `using Input`. Every
        // function returns a value (Int for the predicates, Float for the mouse position) so the
        // results can be used in arithmetic as well as in conditions. The state lives in the
        // session, not in the library, so all scripts of one session share it.
        impl.inputLibrary = std::unique_ptr<Gorgon::Scripting::Library>(
            new Gorgon::Scripting::Library("Input", "Keyboard and mouse queries for play scripts")
        );

        auto *inputState = &impl.inputState;

        auto keyQuery = [inputState](const std::string &name, const std::string &help,
                                     bool (InputState::*query)(const std::string &) const) {
            return new Gorgon::Scripting::Function(
                name, help, nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [inputState, query](std::string key) -> int {
                            return (inputState->*query)(key) ? 1 : 0;
                        },
                        Gorgon::Scripting::Types::Int(),
                        {
                            Gorgon::Scripting::Parameter(
                                "Key",
                                "Key name such as \"W\", \"Left\", \"Space\" or \"Enter\"",
                                Gorgon::Scripting::Types::String()
                            )
                        }
                    )
                }
            );
        };

        impl.inputLibrary->AddMembers({
            keyQuery("IsKeyDown", "Returns 1 while the named key is held down, 0 otherwise.",
                     &InputState::IsKeyDown),
            keyQuery("IsKeyPressed", "Returns 1 on the frame the named key went down, 0 otherwise.",
                     &InputState::IsKeyPressed),
            keyQuery("IsKeyReleased", "Returns 1 on the frame the named key went up, 0 otherwise.",
                     &InputState::IsKeyReleased),
            new Gorgon::Scripting::Function(
                "IsMouseDown",
                "Returns 1 while the named mouse button (\"Left\", \"Right\", \"Middle\") is held down.",
                nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [inputState](std::string button) -> int {
                            return inputState->IsMouseDown(button) ? 1 : 0;
                        },
                        Gorgon::Scripting::Types::Int(),
                        {
                            Gorgon::Scripting::Parameter(
                                "Button", "Mouse button name: \"Left\", \"Right\" or \"Middle\"",
                                Gorgon::Scripting::Types::String()
                            )
                        }
                    )
                }
            ),
            new Gorgon::Scripting::Function(
                "MouseX",
                "World position of the mouse on x.",
                nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [inputState]() -> float { return inputState->mouseX; },
                        Gorgon::Scripting::Types::Float(), {}
                    )
                }
            ),
            new Gorgon::Scripting::Function(
                "MouseY",
                "World position of the mouse on y.",
                nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [inputState]() -> float { return inputState->mouseY; },
                        Gorgon::Scripting::Types::Float(), {}
                    )
                }
            )
        });

        // ---- Physics library -------------------------------------------------
        // Opt in with `using Physics`, like Input. The queries are answered from the scene the
        // session is currently stepping, so they reflect the transforms the scripts have written so
        // far this frame; IsOverlapping reads the overlap set of the previous collision pass.
        impl.physicsLibrary = std::unique_ptr<Gorgon::Scripting::Library>(
            new Gorgon::Scripting::Library("Physics", "2D collision queries for play scripts")
        );

        auto *session = &impl;

        auto entityShape = [session](int rawId, CollisionShape &out) -> bool {
            if(rawId <= 0 || !session->scene)
                return false;

            return CollisionSystem::ComputeShape(*session->scene, EntityId(rawId), out);
        };

        impl.physicsLibrary->AddMembers({
            new Gorgon::Scripting::Function(
                "Overlaps",
                "Returns 1 when the colliders of the two entities overlap right now, 0 otherwise.",
                nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [entityShape](int idA, int idB) -> int {
                            CollisionShape a, b;
                            if(!entityShape(idA, a) || !entityShape(idB, b))
                                return 0;
                            return CollisionSystem::Overlaps(a, b) ? 1 : 0;
                        },
                        Gorgon::Scripting::Types::Int(),
                        {
                            Gorgon::Scripting::Parameter("EntityA", "Id of the first entity",
                                                        Gorgon::Scripting::Types::Int()),
                            Gorgon::Scripting::Parameter("EntityB", "Id of the second entity",
                                                        Gorgon::Scripting::Types::Int())
                        }
                    )
                }
            ),
            new Gorgon::Scripting::Function(
                "OverlapPoint",
                "Returns the id of the topmost entity whose collider contains the world point, "
                "0 when nothing does.",
                nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [session](float x, float y) -> int {
                            if(!session->scene)
                                return 0;

                            EntityId best = InvalidEntityId;
                            int bestZ = 0;
                            for(const auto &entity : session->scene->GetEntities()) {
                                if(!CollisionSystem::HasCollider(entity))
                                    continue;

                                CollisionShape shape;
                                if(!CollisionSystem::ComputeShape(*session->scene, entity, shape))
                                    continue;
                                if(!CollisionSystem::ContainsPoint(shape, x, y))
                                    continue;

                                // Highest zOrder wins; equal z keeps the later entity (draw order).
                                if(best == InvalidEntityId || entity.zOrder >= bestZ) {
                                    best = entity.id;
                                    bestZ = entity.zOrder;
                                }
                            }

                            return int(best);
                        },
                        Gorgon::Scripting::Types::Int(),
                        {
                            Gorgon::Scripting::Parameter("X", "World position on x",
                                                        Gorgon::Scripting::Types::Float()),
                            Gorgon::Scripting::Parameter("Y", "World position on y",
                                                        Gorgon::Scripting::Types::Float())
                        }
                    )
                }
            ),
            new Gorgon::Scripting::Function(
                "IsOverlapping",
                "Returns 1 when the entity took part in an overlap during the last collision pass.",
                nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [session](int rawId) -> int {
                            if(rawId <= 0)
                                return 0;

                            const EntityId id = EntityId(rawId);
                            for(const auto &pair : session->activePairs) {
                                if(pair.a == id || pair.b == id)
                                    return 1;
                            }
                            return 0;
                        },
                        Gorgon::Scripting::Types::Int(),
                        {
                            Gorgon::Scripting::Parameter("Entity", "Id of the entity to test",
                                                        Gorgon::Scripting::Types::Int())
                        }
                    )
                }
            )
        });


        for(const auto &entity : scene.GetEntities()) {
            if(!entity.script || entity.script->path.empty())
                continue;

            auto state = std::unique_ptr<Impl::ScriptState>(new Impl::ScriptState);
            state->entityId = entity.id;
            state->path = entity.script->path;
            state->values = ReadTransform(entity);

            const std::filesystem::path relative(state->path);
            std::filesystem::path full = relative;
            if(relative.is_relative() && !baseDir.empty())
                full = std::filesystem::path(baseDir) / relative;

            std::string code = ReadTextFile(full.string());
            if(code.empty()) {
                impl.lastError = "Script could not be read: " + full.string();
                continue;
            }
            if(!DefinesUpdate(code)) {
                impl.lastError = full.string() + " does not define Update(self, dt)";
                continue;
            }

            try {
                state->vm = std::unique_ptr<Gorgon::Scripting::VirtualMachine>(
                    new Gorgon::Scripting::VirtualMachine(true, impl.log, impl.input)
                );
                state->vm->AddLibrary(*impl.library);
                state->vm->AddLibrary(*impl.inputLibrary);
                state->vm->AddLibrary(*impl.physicsLibrary);

                // Custom libraries are not flattened into the scope automatically (only the
                // builtin Integrals and Keywords namespaces are), so bring ours in first. The
                // collision hooks are optional, so each one is only registered when the source
                // defines it; registering a name that does not exist would fail the compile.
                std::string prologue = "\nusing EditorPlay\nusing Input\nusing Physics\nRegisterUpdate(Update)\n";
                if(DefinesFunction(code, "OnCollisionEnter"))
                    prologue += "RegisterCollisionEnter(OnCollisionEnter)\n";
                if(DefinesFunction(code, "OnCollisionExit"))
                    prologue += "RegisterCollisionExit(OnCollisionExit)\n";
                if(DefinesFunction(code, "OnTriggerEnter"))
                    prologue += "RegisterTriggerEnter(OnTriggerEnter)\n";
                if(DefinesFunction(code, "OnTriggerExit"))
                    prologue += "RegisterTriggerExit(OnTriggerExit)\n";
                code += prologue;
                std::istringstream source(code);
                Gorgon::Scripting::StreamInput input(
                    source, Gorgon::Scripting::InputProvider::Programming, full.string()
                );

                impl.compiling = state.get();
                state->vm->Begin(input);
                state->vm->Run();
                impl.compiling = nullptr;
            }
            catch(const std::exception &ex) {
                impl.compiling = nullptr;
                impl.lastError = std::string("Script error in ") + full.string() + ": " + ex.what();
                continue;
            }
            catch(...) {
                impl.compiling = nullptr;
                impl.lastError = "Script error in " + full.string();
                continue;
            }

            if(!state->update) {
                impl.lastError = full.string() + " did not register an Update function";
                continue;
            }

            impl.scripts.push_back(std::move(state));
        }

        if(impl.scripts.empty()) {
            const std::string message = impl.lastError.empty()
                ? std::string("No entity has an attached script that defines Update(self, dt).")
                : impl.lastError;
            impl.lastError = message;
            if(outError)
                *outError = message;
            // Nothing to run: restore immediately so play never leaves a half state behind.
            Stop(scene);
            impl.lastError = message;
            return false;
        }

        if(outError)
            outError->clear();
        return true;
    }

    void PlaySession::Tick(Scene &scene, float dt) {
        auto &impl = *impl_;
        if(!impl.running)
            return;

        if(!(dt > 0.0f))
            dt = 0.0f;
        dt = std::min(dt, 0.25f);

        for(auto &state : impl.scripts) {
            if(!state->update)
                continue;

            Entity *entity = scene.FindEntity(state->entityId);
            if(!entity)
                continue;

            const Transform original = entity->transform;
            state->values = ReadTransform(*entity);

            try {
                Gorgon::Scripting::Data self(impl.entityType, &state->values, true, false);
                Gorgon::Scripting::Data delta(Gorgon::Scripting::Types::Float(), dt);
                state->vm->ExecuteFunction(state->update, {self, delta}, false);
            }
            catch(const std::exception &ex) {
                impl.lastError = std::string("Script error: ") + ex.what();
                entity->transform = original;
                // Do not keep calling a broken script every frame.
                state->update = nullptr;
                continue;
            }
            catch(...) {
                impl.lastError = "Script error";
                entity->transform = original;
                state->update = nullptr;
                continue;
            }

            WriteTransform(state->values, *entity);
        }

        // Collision runs on the transforms the scripts just produced, so a script that moved into
        // an overlap this frame is separated in the same frame instead of a frame late.
        if(impl.running)
            impl.StepCollisions(scene);

        // Edge flags belong to one frame: a press is visible to every script of this Tick and to
        // nobody afterwards. Held keys and buttons stay down until their up event arrives.
        impl.inputState.ClearEdges();
    }

    void PlaySession::Stop(Scene &scene) {
        auto &impl = *impl_;

        if(!impl.running && impl.scripts.empty() && !impl.library)
            return;

        impl.scripts.clear();
        impl.compiling = nullptr;
        impl.entityType = nullptr;
        impl.physicsLibrary.reset();
        impl.inputLibrary.reset();
        impl.library.reset();
        impl.ResetCollisionState();
        impl.scene = nullptr;

        if(impl.running)
            scene = impl.snapshot;

        impl.running = false;
    }

    bool PlaySession::IsRunning() const {
        return impl_->running;
    }

    InputState &PlaySession::GetInput() {
        return impl_->inputState;
    }

    const InputState &PlaySession::GetInput() const {
        return impl_->inputState;
    }

    int PlaySession::GetScriptCount() const {
        return static_cast<int>(impl_->scripts.size());
    }

    std::vector<EntityId> PlaySession::GetScriptedEntities() const {
        std::vector<EntityId> ids;
        ids.reserve(impl_->scripts.size());
        for(const auto &state : impl_->scripts)
            ids.push_back(state->entityId);
        return ids;
    }

    const std::vector<CollisionPair> &PlaySession::GetActiveOverlaps() const {
        return impl_->activePairs;
    }

    bool PlaySession::IsOverlapping(EntityId a, EntityId b) const {
        if(a == InvalidEntityId || b == InvalidEntityId)
            return false;

        if(a > b)
            std::swap(a, b);

        for(const auto &pair : impl_->activePairs) {
            if(pair.a == a && pair.b == b)
                return true;
        }
        return false;
    }

    const std::string &PlaySession::GetLastError() const {
        return impl_->lastError;
    }

    const std::string &PlaySession::GetOutput() const {
        // ostringstream::str() returns by value, so cache it for the reference accessor.
        impl_->output = impl_->log.str();
        return impl_->output;
    }

} // namespace Gorgon::Editor
