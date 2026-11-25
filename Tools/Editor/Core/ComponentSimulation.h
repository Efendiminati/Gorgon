#pragma once

#include "Scene.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Gorgon :: Editor {

    /**
     * Steps the components that carry their own time: sprite-sheet animations, CPU particle
     * emitters and the play-on-start audio trigger.
     *
     * The serialized Entity only stores settings, never simulation state, so the live particle
     * lists and the "audio already started" bookkeeping live here. Both PlaySession (play-in-editor)
     * and SceneRuntime own an instance; the runtime owns its own so a scene without a single script
     * still animates.
     */
    class ComponentSimulation {
    public:
        /// Called with the raw (unresolved) audio asset path when an AudioComponent fires.
        using SoundSink = std::function<void(const std::string &asset)>;

        /// Drops every live particle and forgets which audio components already fired.
        void Reset();

        void SetSoundSink(SoundSink sink) { soundSink_ = std::move(sink); }

        /// Advances animations and particles by dt seconds (clamped to [0, 0.25]) and fires the
        /// audio components that have not fired yet. Dead emitters are dropped.
        void Step(Scene &scene, float dt);

        /// Live particles of an emitter entity, or nullptr when the entity has none (yet).
        const std::vector<Particle> *GetParticles(EntityId id) const;

        /// Number of live particles of an emitter entity (0 when it has none).
        std::size_t GetParticleCount(EntityId id) const;

        /// Audio assets triggered since the last Reset(), in trigger order.
        const std::vector<std::string> &GetTriggeredSounds() const { return triggeredSounds_; }

        /// Total emitted particles of one emitter, including the ones already expired. Test hook.
        std::uint32_t GetEmittedCount(EntityId id) const;

    private:
        struct EmitterState {
            std::vector<Particle> particles;
            float emissionAccumulator = 0.0f;
            std::uint32_t emitted = 0;
        };

        float random01();

        std::unordered_map<EntityId, EmitterState> emitters_;
        std::unordered_map<EntityId, bool> audioStarted_;
        std::vector<std::string> triggeredSounds_;
        SoundSink soundSink_;
        std::uint32_t rng_ = 0x9e3779b9u;
    };

} // namespace Gorgon::Editor
