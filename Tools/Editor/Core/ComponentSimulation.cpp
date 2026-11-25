#include "ComponentSimulation.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Gorgon :: Editor {

    namespace {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr std::size_t kMaxParticlesPerEmitter = 512;
    }

    void ComponentSimulation::Reset() {
        emitters_.clear();
        audioStarted_.clear();
        triggeredSounds_.clear();
        rng_ = 0x9e3779b9u;
    }

    float ComponentSimulation::random01() {
        // xorshift32: deterministic so a test can rely on the emitter producing particles.
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return float(rng_ & 0x00ffffffu) / float(0x01000000u);
    }

    const std::vector<Particle> *ComponentSimulation::GetParticles(EntityId id) const {
        auto it = emitters_.find(id);
        return it == emitters_.end() ? nullptr : &it->second.particles;
    }

    std::size_t ComponentSimulation::GetParticleCount(EntityId id) const {
        auto it = emitters_.find(id);
        return it == emitters_.end() ? 0 : it->second.particles.size();
    }

    std::uint32_t ComponentSimulation::GetEmittedCount(EntityId id) const {
        auto it = emitters_.find(id);
        return it == emitters_.end() ? 0 : it->second.emitted;
    }

    void ComponentSimulation::Step(Scene &scene, float dt) {
        if(!(dt > 0.0f))
            dt = 0.0f;
        dt = std::min(dt, 0.25f);

        std::unordered_set<EntityId> liveEmitters;

        // Ids are captured up front: a script may have spawned entities this frame, and looking the
        // entity up again per id keeps reallocation out of the picture.
        std::vector<EntityId> ids;
        ids.reserve(scene.GetEntityCount());
        for(const auto &entity : scene.GetEntities())
            ids.push_back(entity.id);

        for(EntityId entityId : ids) {
            Entity *entityPtr = scene.FindEntity(entityId);
            if(!entityPtr)
                continue;
            Entity &entity = *entityPtr;
            // ---- Sprite sheet animation ------------------------------------------
            if(entity.spriteAnim.has_value()) {
                auto &anim = *entity.spriteAnim;
                if(anim.frameCount < 1)
                    anim.frameCount = 1;
                if(anim.currentFrame < 0 || anim.currentFrame >= anim.frameCount)
                    anim.currentFrame = 0;

                if(anim.playing && anim.frameCount > 1 && anim.fps > 0.0f && dt > 0.0f) {
                    const float frameDuration = 1.0f / anim.fps;
                    anim.frameTime += dt;
                    // A very long frame must not spin forever; the clamp keeps the loop bounded.
                    int advances = 0;
                    while(anim.frameTime >= frameDuration && advances < 4096) {
                        anim.frameTime -= frameDuration;
                        ++advances;
                        if(anim.currentFrame + 1 >= anim.frameCount) {
                            if(anim.loop) {
                                anim.currentFrame = 0;
                            }
                            else {
                                anim.currentFrame = anim.frameCount - 1;
                                anim.playing = false;
                                anim.frameTime = 0.0f;
                                break;
                            }
                        }
                        else {
                            ++anim.currentFrame;
                        }
                    }
                }
            }

            // ---- Particle emitter -------------------------------------------------
            if(entity.particles.has_value()) {
                liveEmitters.insert(entity.id);
                auto &state = emitters_[entity.id];
                auto &comp = *entity.particles;

                // Integrate the particles that already exist; expire the old ones.
                for(auto &particle : state.particles) {
                    particle.age += dt;
                    particle.x += particle.vx * dt;
                    particle.y += particle.vy * dt;
                }
                state.particles.erase(
                    std::remove_if(state.particles.begin(), state.particles.end(),
                                   [](const Particle &p) { return p.age >= p.life; }),
                    state.particles.end()
                );

                if(comp.active && dt > 0.0f && comp.emitRate > 0.0f) {
                    state.emissionAccumulator += comp.emitRate * dt;
                    while(state.emissionAccumulator >= 1.0f &&
                          state.particles.size() < kMaxParticlesPerEmitter) {
                        state.emissionAccumulator -= 1.0f;

                        Particle particle;
                        const float angle = random01() * 2.0f * kPi;
                        const float speed = comp.speed * (0.5f + random01());
                        particle.x = 0.0f;
                        particle.y = 0.0f;
                        particle.vx = std::cos(angle) * speed;
                        particle.vy = std::sin(angle) * speed;
                        particle.age = 0.0f;
                        particle.life = std::max(0.01f, comp.lifetime);
                        state.particles.push_back(particle);
                        ++state.emitted;
                    }
                    if(state.emissionAccumulator > 1.0f)
                        state.emissionAccumulator = 1.0f;
                }
            }

            // ---- Audio ------------------------------------------------------------
            if(entity.audio.has_value() && entity.audio->playOnStart && !entity.audio->asset.empty()) {
                auto it = audioStarted_.find(entity.id);
                if(it == audioStarted_.end()) {
                    audioStarted_[entity.id] = true;
                    triggeredSounds_.push_back(entity.audio->asset);
                    if(soundSink_)
                        soundSink_(entity.audio->asset);
                }
            }
        }

        // Forget emitters whose component (or entity) went away so ids are not reused with stale
        // particles.
        for(auto it = emitters_.begin(); it != emitters_.end();) {
            if(liveEmitters.find(it->first) == liveEmitters.end())
                it = emitters_.erase(it);
            else
                ++it;
        }
    }

} // namespace Gorgon::Editor
