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

        Scene snapshot;

        std::unique_ptr<Gorgon::Scripting::Library> library;
        Gorgon::Scripting::MappedReferenceType<ScriptTransform> *entityType = nullptr;

        struct ScriptState {
            EntityId entityId = InvalidEntityId;
            std::string path;
            std::unique_ptr<Gorgon::Scripting::VirtualMachine> vm;
            const Gorgon::Scripting::Function *update = nullptr;
            ScriptTransform values;
        };

        std::vector<std::unique_ptr<ScriptState>> scripts;
        ScriptState *compiling = nullptr;

        bool running = false;
        std::string lastError;
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
    } // namespace

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

                // Custom libraries are not flattened into the scope automatically (only the
                // builtin Integrals and Keywords namespaces are), so bring ours in first.
                code += "\nusing EditorPlay\nRegisterUpdate(Update)\n";
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
    }

    void PlaySession::Stop(Scene &scene) {
        auto &impl = *impl_;

        if(!impl.running && impl.scripts.empty() && !impl.library)
            return;

        impl.scripts.clear();
        impl.compiling = nullptr;
        impl.entityType = nullptr;
        impl.library.reset();

        if(impl.running)
            scene = impl.snapshot;

        impl.running = false;
    }

    bool PlaySession::IsRunning() const {
        return impl_->running;
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

    const std::string &PlaySession::GetLastError() const {
        return impl_->lastError;
    }

    const std::string &PlaySession::GetOutput() const {
        // ostringstream::str() returns by value, so cache it for the reference accessor.
        impl_->output = impl_->log.str();
        return impl_->output;
    }

} // namespace Gorgon::Editor
