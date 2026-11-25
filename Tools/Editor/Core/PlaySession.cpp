#include "PlaySession.h"

#include "SceneSerializer.h"

#include <Gorgon/Audio.h>
#include <Gorgon/Audio/Controllers.h>
#include <Gorgon/Multimedia/Wave.h>
#include <Gorgon/Scripting.h>
#include <Gorgon/Scripting/Embedding.h>
#include <Gorgon/Scripting/Input.h>
#include <Gorgon/Scripting/VirtualMachine.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

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

        // The scene being stepped. Set by Start()/Tick(), cleared by Stop(); the Scene/Physics
        // libraries and the collision pass all read it.
        Scene *scene = nullptr;

        // Root relative script/asset paths are resolved against (the scene's directory).
        std::string baseDir;

        // Camera the Scene library reads and writes. Points at ownedCamera unless a host installed
        // its own with SetCamera(); cameraChanged tells the host a script moved it.
        RuntimeCamera ownedCamera;
        RuntimeCamera *camera = &ownedCamera;
        bool cameraChanged = false;

        // Every path passed to Audio::PlaySound(), and the play voices behind them.
        std::vector<std::string> playedSounds;
#ifdef GORGON_AUDIO_SUPPORT
        std::unordered_map<std::string, std::unique_ptr<Gorgon::Multimedia::Wave>> soundCache;
        std::vector<std::unique_ptr<Gorgon::Audio::BasicController>> soundVoices;
        std::size_t nextVoice = 0;
#endif

        std::unique_ptr<Gorgon::Scripting::Library> library;
        std::unique_ptr<Gorgon::Scripting::Library> inputLibrary;
        std::unique_ptr<Gorgon::Scripting::Library> physicsLibrary;
        std::unique_ptr<Gorgon::Scripting::Library> sceneLibrary;
        std::unique_ptr<Gorgon::Scripting::Library> audioLibrary;
        Gorgon::Scripting::MappedReferenceType<ScriptTransform> *entityType = nullptr;

        struct ScriptState {
            EntityId entityId = InvalidEntityId;
            std::string path;
            std::unique_ptr<Gorgon::Scripting::VirtualMachine> vm;
            const Gorgon::Scripting::Function *update = nullptr;
            // Optional lifecycle hooks, registered the same way Update is.
            const Gorgon::Scripting::Function *start = nullptr;
            const Gorgon::Scripting::Function *onDestroy = nullptr;
            // Optional collision hooks. A hook the script does not define stays null and is skipped.
            const Gorgon::Scripting::Function *onCollisionEnter = nullptr;
            const Gorgon::Scripting::Function *onCollisionExit = nullptr;
            const Gorgon::Scripting::Function *onTriggerEnter = nullptr;
            const Gorgon::Scripting::Function *onTriggerExit = nullptr;
            ScriptTransform values;
            bool started = false;
        };

        std::vector<std::unique_ptr<ScriptState>> scripts;
        ScriptState *compiling = nullptr;

        // Work the running scripts queued while a VM was executing: entities whose scripts still
        // have to be compiled (spawned this tick) and entities to remove at the end of the tick.
        std::vector<EntityId> pendingCompile;
        std::vector<EntityId> pendingDestroy;

        // Overlap pairs of the last and the current collision pass; their difference is what drives
        // the enter/exit hooks.
        std::vector<CollisionPair> activePairs;
        std::vector<CollisionPair> previousPairs;

        bool running = false;
        std::string lastError;

        void ResetCollisionState();
        void ResetQueues();
        void StepCollisions(Scene &scene);
        void DispatchPair(Scene &scene, const CollisionPair &pair, bool entered);

        /// Calls a collision hook with (self, otherId), writes any transform change back, and drops
        /// the hook when the script throws so a broken handler is not retried every frame.
        void FireHook(Scene &scene, ScriptState &state, const Gorgon::Scripting::Function *&hook,
                      EntityId otherId);

        /// Calls a lifecycle hook with (self) and writes any transform change back.
        void FireSelfHook(Scene &scene, ScriptState &state, const Gorgon::Scripting::Function *hook);

        /// Compiles the script attached to `entityId` into a new ScriptState. Returns false (and
        /// sets lastError) when the script cannot be read or compiled.
        bool CompileEntity(EntityId entityId);

        /// Runs Start() for every compiled script that has not run it yet.
        void RunStartHooks(Scene &scene);

        /// Compiles the scripts of everything Spawn() created since the last pass, then starts them.
        void ProcessSpawnedScripts(Scene &scene);

        /// Fires OnDestroy for the subtree of everything queued with Destroy() and removes it.
        void ProcessDestroyQueue(Scene &scene);

        /// Queues the entity and its descendants for script compilation (a spawn may be a subtree).
        void QueueCompile(EntityId rootId);

        /// Duplicates the entity named `name` (or creates a fresh one when there is no such entity)
        /// at the given world position and queues its script for compilation.
        EntityId SpawnEntity(const std::string &name, float x, float y);

        /// Instantiates a prefab file at the given world position and queues its scripts.
        EntityId SpawnPrefabEntity(const std::string &prefabPath, float x, float y);

        /// Plays a wav/ogg file through Gorgon::Audio when a device exists. The path is recorded in
        /// playedSounds either way so a headless run still proves the call happened.
        int PlaySound(const std::string &assetPath);
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

        /// Resolves an asset/script path against baseDir unless it is already absolute.
        std::string ResolveAsset(const std::string &baseDir, const std::string &path) {
            if(path.empty())
                return {};

            const std::filesystem::path p(path);
            if(p.is_absolute() || baseDir.empty())
                return p.string();

            return (std::filesystem::path(baseDir) / p).string();
        }

        /**
         * Writes a world position into an entity by converting it into the parent's local space.
         * The parent's world scale is divided out and its rotation undone, so SetPos() of a child
         * reports the same value back through GetX()/GetY().
         */
        void SetWorldPosition(Scene &scene, EntityId id, float x, float y) {
            Entity *entity = scene.FindEntity(id);
            if(!entity)
                return;

            if(entity->parentId == InvalidEntityId) {
                entity->transform.x = x;
                entity->transform.y = y;
                return;
            }

            const Transform parent = scene.GetWorldTransform(entity->parentId);
            const float scaleX = std::abs(parent.scaleX) > 1e-6f ? parent.scaleX : 1.0f;
            const float scaleY = std::abs(parent.scaleY) > 1e-6f ? parent.scaleY : 1.0f;

            const float dx = (x - parent.x) / scaleX;
            const float dy = (y - parent.y) / scaleY;

            constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
            const float rad = -parent.rotation * kDegToRad;
            const float c = std::cos(rad);
            const float s = std::sin(rad);

            entity->transform.x = dx * c - dy * s;
            entity->transform.y = dx * s + dy * c;
        }

        /// Appends rootId and every descendant to out (parents before children).
        void CollectSubtreeIds(const Scene &scene, EntityId rootId, std::vector<EntityId> &out) {
            out.push_back(rootId);
            for(const auto &entity : scene.GetEntities()) {
                if(entity.parentId == rootId)
                    CollectSubtreeIds(scene, entity.id, out);
            }
        }

        bool ContainsId(const std::vector<EntityId> &ids, EntityId id) {
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        }
    } // namespace

    void PlaySession::Impl::ResetCollisionState() {
        activePairs.clear();
        previousPairs.clear();
    }

    void PlaySession::Impl::ResetQueues() {
        pendingCompile.clear();
        pendingDestroy.clear();
    }

    void PlaySession::Impl::FireSelfHook(Scene &scene, ScriptState &state,
                                        const Gorgon::Scripting::Function *hook) {
        if(!hook || !state.vm)
            return;

        Entity *entity = scene.FindEntity(state.entityId);
        if(!entity)
            return;

        try {
            // GScript keeps a raw pointer to the last activated VM of a thread and uses it for
            // reference counting while a reference Data is built. Activating ours first is not
            // optional: after another script's VM is destroyed that pointer is dangling and the
            // Data constructor dereferences freed memory.
            state.vm->Activate();
            state.values = ReadTransform(*entity);
            Gorgon::Scripting::Data self(entityType, &state.values, true, false);
            state.vm->ExecuteFunction(hook, {self}, false);
            // The hook may have spawned entities, which reallocates the scene's storage; look the
            // entity up again instead of writing through the pointer taken before the call.
            if(Entity *after = scene.FindEntity(state.entityId))
                WriteTransform(state.values, *after);
        }
        catch(const std::exception &ex) {
            lastError = std::string("Script lifecycle error: ") + ex.what();
        }
        catch(...) {
            lastError = "Script lifecycle error";
        }
    }

    bool PlaySession::Impl::CompileEntity(EntityId entityId) {
        if(!scene)
            return false;

        const Entity *entity = scene->FindEntity(entityId);
        if(!entity || !entity->script || entity->script->path.empty())
            return false;

        // Copy everything the compilation needs: the scene's storage may be reallocated while this
        // state is compiled (Spawn()) and the reference would dangle.
        const std::string path = entity->script->path;
        const ScriptTransform values = ReadTransform(*entity);

        auto state = std::unique_ptr<ScriptState>(new ScriptState);
        state->entityId = entityId;
        state->path = path;
        state->values = values;

        const std::string full = ResolveAsset(baseDir, path);

        std::string code = ReadTextFile(full);
        if(code.empty()) {
            lastError = "Script could not be read: " + full;
            return false;
        }
        if(!DefinesUpdate(code)) {
            lastError = full + " does not define Update(self, dt)";
            return false;
        }

        try {
            state->vm = std::unique_ptr<Gorgon::Scripting::VirtualMachine>(
                new Gorgon::Scripting::VirtualMachine(true, log, input)
            );
            state->vm->AddLibrary(*library);
            state->vm->AddLibrary(*inputLibrary);
            state->vm->AddLibrary(*physicsLibrary);
            if(sceneLibrary)
                state->vm->AddLibrary(*sceneLibrary);
            if(audioLibrary)
                state->vm->AddLibrary(*audioLibrary);

            // Custom libraries are not flattened into the scope automatically (only the builtin
            // Integrals and Keywords namespaces are), so bring ours in first. The lifecycle and
            // collision hooks are optional, so each one is only registered when the source defines
            // it; registering a name that does not exist would fail the compile.
            std::string prologue =
                "\nusing EditorPlay\nusing Input\nusing Physics\nusing Scene\nusing Audio\n"
                "RegisterUpdate(Update)\n";
            if(DefinesFunction(code, "Start"))
                prologue += "RegisterStart(Start)\n";
            if(DefinesFunction(code, "OnDestroy"))
                prologue += "RegisterDestroy(OnDestroy)\n";
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
            Gorgon::Scripting::StreamInput input_(source, Gorgon::Scripting::InputProvider::Programming, full);

            compiling = state.get();
            state->vm->Begin(input_);
            state->vm->Run();
            compiling = nullptr;
        }
        catch(const std::exception &ex) {
            compiling = nullptr;
            lastError = std::string("Script error in ") + full + ": " + ex.what();
            return false;
        }
        catch(...) {
            compiling = nullptr;
            lastError = "Script error in " + full;
            return false;
        }

        if(!state->update) {
            lastError = full + " did not register an Update function";
            return false;
        }

        scripts.push_back(std::move(state));
        return true;
    }

    void PlaySession::Impl::RunStartHooks(Scene &scene) {
        for(auto &state : scripts) {
            if(state->started)
                continue;

            // Flag first: a Start hook that spawns would otherwise be able to re-enter itself.
            state->started = true;
            FireSelfHook(scene, *state, state->start);
        }
    }

    void PlaySession::Impl::ProcessSpawnedScripts(Scene &scene) {
        // A Start hook may spawn as well, so keep draining until the queue stays empty (bounded in
        // case a script spawns forever).
        for(int pass = 0; pass < 32 && !pendingCompile.empty(); ++pass) {
            std::vector<EntityId> queue;
            queue.swap(pendingCompile);

            for(EntityId id : queue) {
                if(!scene.HasEntity(id))
                    continue;

                bool alreadyScripted = false;
                for(const auto &state : scripts) {
                    if(state->entityId == id) {
                        alreadyScripted = true;
                        break;
                    }
                }
                if(alreadyScripted)
                    continue;

                CompileEntity(id);
            }

            RunStartHooks(scene);
        }

        pendingCompile.clear();
    }

    void PlaySession::Impl::ProcessDestroyQueue(Scene &scene) {
        for(int pass = 0; pass < 32 && !pendingDestroy.empty(); ++pass) {
            std::vector<EntityId> queue;
            queue.swap(pendingDestroy);

            // Scene::RemoveEntity removes the subtree, so the descendants' OnDestroy hooks have to
            // run as well.
            std::vector<EntityId> removing;
            for(EntityId id : queue) {
                if(scene.HasEntity(id))
                    CollectSubtreeIds(scene, id, removing);
            }

            if(removing.empty())
                continue;

            const std::unordered_set<EntityId> destroySet(removing.begin(), removing.end());

            for(auto &state : scripts) {
                if(destroySet.count(state->entityId) > 0)
                    FireSelfHook(scene, *state, state->onDestroy);
            }

            scripts.erase(
                std::remove_if(scripts.begin(), scripts.end(),
                    [&](const std::unique_ptr<ScriptState> &state) {
                        return destroySet.count(state->entityId) > 0;
                    }),
                scripts.end()
            );

            for(EntityId id : queue) {
                if(scene.HasEntity(id))
                    scene.RemoveEntity(id, true);
            }

            // Pairs that mention a destroyed entity must not survive into the next frame's diff.
            const auto dead = [&](const CollisionPair &pair) {
                return !scene.HasEntity(pair.a) || !scene.HasEntity(pair.b);
            };
            activePairs.erase(std::remove_if(activePairs.begin(), activePairs.end(), dead), activePairs.end());
            previousPairs.erase(std::remove_if(previousPairs.begin(), previousPairs.end(), dead), previousPairs.end());
        }

        pendingDestroy.clear();
    }

    void PlaySession::Impl::QueueCompile(EntityId rootId) {
        if(!scene || !scene->HasEntity(rootId))
            return;

        std::vector<EntityId> subtree;
        CollectSubtreeIds(*scene, rootId, subtree);
        for(EntityId id : subtree) {
            if(!ContainsId(pendingCompile, id))
                pendingCompile.push_back(id);
        }
    }

    EntityId PlaySession::Impl::SpawnEntity(const std::string &name, float x, float y) {
        if(!scene)
            return InvalidEntityId;

        EntityId newId = InvalidEntityId;
        const EntityId templateId = scene->FindEntityByName(name);
        if(templateId != InvalidEntityId) {
            // Spawning an existing entity duplicates its whole subtree (components and scripts
            // included) with fresh ids; the copy gets a unique " Copy" name.
            newId = scene->DuplicateEntity(templateId, true);
        }
        else {
            Entity &created = scene->CreateEntity(name.empty() ? std::string("Entity") : name);
            newId = created.id;
        }

        if(newId == InvalidEntityId)
            return InvalidEntityId;

        SetWorldPosition(*scene, newId, x, y);
        QueueCompile(newId);
        return newId;
    }

    EntityId PlaySession::Impl::SpawnPrefabEntity(const std::string &prefabPath, float x, float y) {
        if(!scene || prefabPath.empty())
            return InvalidEntityId;

        const std::string full = ResolveAsset(baseDir, prefabPath);

        PrefabData prefab;
        std::string error;
        if(!SceneSerializer::LoadPrefabFromFile(full, prefab, &error)) {
            lastError = error.empty() ? ("Prefab could not be loaded: " + full) : error;
            return InvalidEntityId;
        }

        EntityId rootId = InvalidEntityId;
        if(!SceneSerializer::InstantiatePrefab(prefab, *scene, x, y, full, &rootId, nullptr, &error)) {
            lastError = error.empty() ? ("Prefab could not be instantiated: " + full) : error;
            return InvalidEntityId;
        }

        QueueCompile(rootId);
        return rootId;
    }

    int PlaySession::Impl::PlaySound(const std::string &assetPath) {
        if(assetPath.empty())
            return 0;

        const std::string resolved = ResolveAsset(baseDir, assetPath);
        playedSounds.push_back(resolved);

#ifdef GORGON_AUDIO_SUPPORT
        // No device (headless, no audio session): the call still happened, it just stays silent.
        if(!Gorgon::Audio::IsAvailable())
            return 0;

        auto it = soundCache.find(resolved);
        if(it == soundCache.end()) {
            auto wave = std::unique_ptr<Gorgon::Multimedia::Wave>(new Gorgon::Multimedia::Wave);
            bool loaded = false;
            try {
                // Import() picks the decoder from the extension (wav, ogg/vorbis, flac).
                loaded = wave->Import(resolved) && wave->HasData();
            }
            catch(...) {
                loaded = false;
            }
            it = soundCache.emplace(resolved, loaded ? std::move(wave) : nullptr).first;
        }

        if(!it->second)
            return 0;

        constexpr std::size_t kVoiceCount = 8;
        if(soundVoices.empty()) {
            soundVoices.reserve(kVoiceCount);
            for(std::size_t i = 0; i < kVoiceCount; ++i)
                soundVoices.push_back(std::unique_ptr<Gorgon::Audio::BasicController>(
                    new Gorgon::Audio::BasicController
                ));
        }

        auto &voice = soundVoices[nextVoice % soundVoices.size()];
        nextVoice++;

        try {
            // A pooled voice may still be playing the previous sound; restart it on the new one.
            voice->Pause();
            voice->SetData(*it->second);
            voice->SetVolume(1.0f);
            voice->Play();
        }
        catch(...) {
            return 0;
        }
        return 1;
#else
        return 0;
#endif
    }

    void PlaySession::Impl::FireHook(Scene &scene, ScriptState &state,
                                     const Gorgon::Scripting::Function *&hook, EntityId otherId) {
        if(!hook || !state.vm)
            return;

        Entity *entity = scene.FindEntity(state.entityId);
        if(!entity)
            return;

        try {
            // See FireSelfHook: the VM has to be the thread's active one before any Data is built.
            state.vm->Activate();
            state.values = ReadTransform(*entity);
            Gorgon::Scripting::Data self(entityType, &state.values, true, false);
            Gorgon::Scripting::Data other(Gorgon::Scripting::Types::Int(), int(otherId));
            state.vm->ExecuteFunction(hook, {self, other}, false);
            // A hook may move its entity (a trigger that teleports, a bounce that nudges back) and
            // may spawn something, which reallocates the scene; look the entity up again.
            if(Entity *after = scene.FindEntity(state.entityId))
                WriteTransform(state.values, *after);
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
        impl.baseDir = baseDir;
        impl.ResetCollisionState();
        impl.ResetQueues();

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

        // The lifecycle hooks take no second argument, so their handler signature is handler(self).
        auto selfHookRegistrar = [this](const char *name, const char *help,
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
                                "Handler", "Function to call as handler(self)",
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
            // Optional lifecycle and collision callbacks. Each one mirrors RegisterUpdate exactly:
            // a single handler parameter, registered by a line the runtime appends only when the
            // script defines that hook.
            selfHookRegistrar(
                "RegisterStart", "Registers the handler(self) called once when the script starts",
                &Impl::ScriptState::start
            ),
            selfHookRegistrar(
                "RegisterDestroy", "Registers the handler(self) called before the entity is destroyed",
                &Impl::ScriptState::onDestroy
            ),
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

        // ---- Scene library ---------------------------------------------------
        // Opt in with `using Scene`. This is the script's window onto the whole scene: lookup by
        // name, spawning (an existing entity becomes the template, a prefab file is instantiated),
        // destruction deferred to the end of the tick, and the camera. Positions are world space, so
        // GetX/GetY of a child returns the value SetPos just wrote rather than its local one.
        impl.sceneLibrary = std::unique_ptr<Gorgon::Scripting::Library>(
            new Gorgon::Scripting::Library("Scene", "Entity lookup, spawning, destruction and camera")
        );

        auto worldX = [session](int rawId) -> float {
            if(rawId <= 0 || !session->scene)
                return 0.0f;
            return session->scene->GetWorldTransform(EntityId(rawId)).x;
        };

        auto worldY = [session](int rawId) -> float {
            if(rawId <= 0 || !session->scene)
                return 0.0f;
            return session->scene->GetWorldTransform(EntityId(rawId)).y;
        };

        impl.sceneLibrary->AddMembers({
            new Gorgon::Scripting::Function(
                "FindByName",
                "Returns the id of the first entity with this name, 0 when there is none.",
                nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [session](std::string name) -> int {
                            if(!session->scene)
                                return 0;
                            return int(session->scene->FindEntityByName(name));
                        },
                        Gorgon::Scripting::Types::Int(),
                        {
                            Gorgon::Scripting::Parameter("Name", "Entity name to look for",
                                                        Gorgon::Scripting::Types::String())
                        }
                    )
                }
            ),
            new Gorgon::Scripting::Function(
                "Spawn",
                "Creates a copy of the named entity (whole subtree) at the world position and "
                "returns its id. When no entity has that name a fresh empty entity is created.",
                nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [session](std::string name, float x, float y) -> int {
                            return int(session->SpawnEntity(name, x, y));
                        },
                        Gorgon::Scripting::Types::Int(),
                        {
                            Gorgon::Scripting::Parameter("Name", "Entity name to copy, or the name of the new entity",
                                                        Gorgon::Scripting::Types::String()),
                            Gorgon::Scripting::Parameter("X", "World position on x",
                                                        Gorgon::Scripting::Types::Float()),
                            Gorgon::Scripting::Parameter("Y", "World position on y",
                                                        Gorgon::Scripting::Types::Float())
                        }
                    )
                }
            ),
            new Gorgon::Scripting::Function(
                "SpawnPrefab",
                "Instantiates a .gprefab.json file at the world position and returns its root id "
                "(0 when the file cannot be loaded).",
                nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [session](std::string path, float x, float y) -> int {
                            return int(session->SpawnPrefabEntity(path, x, y));
                        },
                        Gorgon::Scripting::Types::Int(),
                        {
                            Gorgon::Scripting::Parameter("PrefabPath", "Prefab file path, relative to the scene",
                                                        Gorgon::Scripting::Types::String()),
                            Gorgon::Scripting::Parameter("X", "World position on x",
                                                        Gorgon::Scripting::Types::Float()),
                            Gorgon::Scripting::Parameter("Y", "World position on y",
                                                        Gorgon::Scripting::Types::Float())
                        }
                    )
                }
            ),
            new Gorgon::Scripting::Function(
                "Destroy",
                "Queues an entity (and its children) for removal at the end of the tick and "
                "returns 1. Returns 0 when the id is unknown.",
                nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [session](int rawId) -> int {
                            if(rawId <= 0 || !session->scene)
                                return 0;
                            if(!session->scene->HasEntity(EntityId(rawId)))
                                return 0;

                            const EntityId id = EntityId(rawId);
                            if(!ContainsId(session->pendingDestroy, id))
                                session->pendingDestroy.push_back(id);
                            return 1;
                        },
                        Gorgon::Scripting::Types::Int(),
                        {
                            Gorgon::Scripting::Parameter("Entity", "Id of the entity to destroy",
                                                        Gorgon::Scripting::Types::Int())
                        }
                    )
                }
            ),
            new Gorgon::Scripting::Function(
                "GetX", "World position of the entity on x (0 when the id is unknown).", nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        worldX, Gorgon::Scripting::Types::Float(),
                        {Gorgon::Scripting::Parameter("Entity", "Id of the entity",
                                                     Gorgon::Scripting::Types::Int())}
                    )
                }
            ),
            new Gorgon::Scripting::Function(
                "GetY", "World position of the entity on y (0 when the id is unknown).", nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        worldY, Gorgon::Scripting::Types::Float(),
                        {Gorgon::Scripting::Parameter("Entity", "Id of the entity",
                                                     Gorgon::Scripting::Types::Int())}
                    )
                }
            ),
            new Gorgon::Scripting::Function(
                "SetPos",
                "Moves the entity to a world position (converted into its parent's space) and "
                "returns 1, or 0 when the id is unknown.",
                nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [session](int rawId, float x, float y) -> int {
                            if(rawId <= 0 || !session->scene || !session->scene->HasEntity(EntityId(rawId)))
                                return 0;

                            SetWorldPosition(*session->scene, EntityId(rawId), x, y);
                            return 1;
                        },
                        Gorgon::Scripting::Types::Int(),
                        {
                            Gorgon::Scripting::Parameter("Entity", "Id of the entity",
                                                        Gorgon::Scripting::Types::Int()),
                            Gorgon::Scripting::Parameter("X", "World position on x",
                                                        Gorgon::Scripting::Types::Float()),
                            Gorgon::Scripting::Parameter("Y", "World position on y",
                                                        Gorgon::Scripting::Types::Float())
                        }
                    )
                }
            ),
            new Gorgon::Scripting::Function(
                "SetVisible",
                "Shows (1) or hides (0) the entity and returns 1, or 0 when the id is unknown.",
                nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [session](int rawId, int visible) -> int {
                            if(rawId <= 0 || !session->scene)
                                return 0;
                            Entity *entity = session->scene->FindEntity(EntityId(rawId));
                            if(!entity)
                                return 0;

                            entity->visible = visible != 0;
                            return 1;
                        },
                        Gorgon::Scripting::Types::Int(),
                        {
                            Gorgon::Scripting::Parameter("Entity", "Id of the entity",
                                                        Gorgon::Scripting::Types::Int()),
                            Gorgon::Scripting::Parameter("Visible", "1 to show, 0 to hide",
                                                        Gorgon::Scripting::Types::Int())
                        }
                    )
                }
            ),
            new Gorgon::Scripting::Function(
                "GetName", "Name of the entity, empty when the id is unknown.", nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [session](int rawId) -> std::string {
                            if(rawId <= 0 || !session->scene)
                                return {};
                            const Entity *entity = session->scene->FindEntity(EntityId(rawId));
                            return entity ? entity->name : std::string();
                        },
                        Gorgon::Scripting::Types::String(),
                        {Gorgon::Scripting::Parameter("Entity", "Id of the entity",
                                                     Gorgon::Scripting::Types::Int())}
                    )
                }
            ),
            new Gorgon::Scripting::Function(
                "SetCameraPos", "Centres the camera on a world position.", nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [session](float x, float y) -> int {
                            session->camera->panX = x;
                            session->camera->panY = y;
                            session->cameraChanged = true;
                            return 1;
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
                "SetCameraZoom", "Sets the camera zoom (viewport pixels per world unit).", nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [session](float zoom) -> int {
                            // A zero/negative zoom would make the projection degenerate.
                            session->camera->zoom = zoom > 0.01f ? zoom : 0.01f;
                            session->cameraChanged = true;
                            return 1;
                        },
                        Gorgon::Scripting::Types::Int(),
                        {Gorgon::Scripting::Parameter("Zoom", "Pixels per world unit",
                                                     Gorgon::Scripting::Types::Float())}
                    )
                }
            ),
            new Gorgon::Scripting::Function(
                "GetCameraX", "World position the camera is centred on, on x.", nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [session]() -> float { return session->camera->panX; },
                        Gorgon::Scripting::Types::Float(), {}
                    )
                }
            ),
            new Gorgon::Scripting::Function(
                "GetCameraY", "World position the camera is centred on, on y.", nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [session]() -> float { return session->camera->panY; },
                        Gorgon::Scripting::Types::Float(), {}
                    )
                }
            )
        });

        // ---- Audio library ---------------------------------------------------
        // Opt in with `using Audio`. PlaySound() resolves the path against the scene, plays it on a
        // pooled voice when Gorgon::Audio has a device, and records the path either way.
        impl.audioLibrary = std::unique_ptr<Gorgon::Scripting::Library>(
            new Gorgon::Scripting::Library("Audio", "Sound playback for play scripts")
        );

        impl.audioLibrary->AddMembers({
            new Gorgon::Scripting::Function(
                "PlaySound",
                "Plays a wav/ogg file and returns 1 when playback started. Returns 0 when no audio "
                "device is available or the file cannot be decoded (the call is still recorded).",
                nullptr,
                {
                    Gorgon::Scripting::MapFunction(
                        [session](std::string path) -> int { return session->PlaySound(path); },
                        Gorgon::Scripting::Types::Int(),
                        {
                            Gorgon::Scripting::Parameter("AssetPath", "Sound file path, relative to the scene",
                                                         Gorgon::Scripting::Types::String())
                        }
                    )
                }
            )
        });


        // Compile everything the scene starts with. CompileEntity reports its own failures into
        // lastError; one broken script must not stop the rest from running.
        for(const auto &entity : scene.GetEntities()) {
            if(!entity.script || entity.script->path.empty())
                continue;

            impl.CompileEntity(entity.id);
        }

        // A Start hook may spawn or destroy; both are drained before the first frame is ticked.
        for(int pass = 0; pass < 4; ++pass) {
            impl.ProcessSpawnedScripts(scene);
            impl.ProcessDestroyQueue(scene);
            impl.RunStartHooks(scene);

            if(impl.pendingCompile.empty() && impl.pendingDestroy.empty())
                break;
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
                // The script may run after a destroyed script's VM was the active one; bind ours
                // before a reference Data is built for it (see FireSelfHook).
                state->vm->Activate();
                Gorgon::Scripting::Data self(impl.entityType, &state->values, true, false);
                Gorgon::Scripting::Data delta(Gorgon::Scripting::Types::Float(), dt);
                state->vm->ExecuteFunction(state->update, {self, delta}, false);
            }
            catch(const std::exception &ex) {
                impl.lastError = std::string("Script error: ") + ex.what();
                // Do not keep calling a broken script every frame.
                state->update = nullptr;
                // Spawning inside the failing frame may have reallocated the scene: re-resolve.
                if(Entity *after = scene.FindEntity(state->entityId))
                    after->transform = original;
                continue;
            }
            catch(...) {
                impl.lastError = "Script error";
                state->update = nullptr;
                if(Entity *after = scene.FindEntity(state->entityId))
                    after->transform = original;
                continue;
            }

            // The script may have spawned an entity, which reallocates the scene's storage; never
            // write through the pointer taken before the call.
            if(Entity *after = scene.FindEntity(state->entityId))
                WriteTransform(state->values, *after);
        }

        // Spawns are compiled and started, destroys run their OnDestroy hooks and remove the
        // entities, all after every Update of this frame has run, so nothing iterates a scene that
        // changed underneath it. A Start/OnDestroy hook may itself spawn or destroy, hence the loop.
        for(int pass = 0; pass < 4; ++pass) {
            impl.ProcessSpawnedScripts(scene);
            impl.ProcessDestroyQueue(scene);

            if(impl.pendingCompile.empty() && impl.pendingDestroy.empty())
                break;
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

        // Leaving Play is a destruction of every entity the session kept alive: fire the hooks
        // before the VMs and the played scene go away. The snapshot restore below discards whatever
        // OnDestroy wrote into the scene, which is exactly what leaving Play means.
        if(impl.running) {
            for(auto &state : impl.scripts) {
                if(!state->onDestroy || !state->vm)
                    continue;

                if(!scene.HasEntity(state->entityId))
                    continue;

                impl.FireSelfHook(scene, *state, state->onDestroy);
            }
        }

        impl.scripts.clear();
        impl.compiling = nullptr;
        impl.entityType = nullptr;
        impl.audioLibrary.reset();
        impl.sceneLibrary.reset();
        impl.physicsLibrary.reset();
        impl.inputLibrary.reset();
        impl.library.reset();
        impl.ResetCollisionState();
        impl.ResetQueues();
        impl.playedSounds.clear();
#ifdef GORGON_AUDIO_SUPPORT
        impl.soundVoices.clear();
        impl.soundCache.clear();
        impl.nextVoice = 0;
#endif
        impl.scene = nullptr;
        impl.baseDir.clear();

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

    RuntimeCamera &PlaySession::GetCamera() {
        return *impl_->camera;
    }

    const RuntimeCamera &PlaySession::GetCamera() const {
        return *impl_->camera;
    }

    void PlaySession::SetCamera(RuntimeCamera *camera) {
        impl_->camera = camera ? camera : &impl_->ownedCamera;
    }

    bool PlaySession::ConsumeCameraChange() {
        const bool changed = impl_->cameraChanged;
        impl_->cameraChanged = false;
        return changed;
    }

    const std::vector<std::string> &PlaySession::GetPlayedSounds() const {
        return impl_->playedSounds;
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
