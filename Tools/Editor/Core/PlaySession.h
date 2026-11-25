#pragma once

#include "CollisionSystem.h"
#include "ComponentSimulation.h"
#include "InputState.h"
#include "RuntimeCamera.h"
#include "Scene.h"
#include "ScriptLog.h"

#include <memory>
#include <string>
#include <vector>

namespace Gorgon :: Editor {

    /// Transform values a play script may read and write for the entity it is attached to.
    struct ScriptTransform {
        float x = 0.0f;
        float y = 0.0f;
        float rotation = 0.0f;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
    };

    /**
     * Play-in-editor runtime.
     *
     * Start() snapshots the scene and compiles every attached ScriptComponent with
     * Gorgon/Scripting. Every frame the editor calls Tick(), which hands the entity transform and
     * the frame delta time to the script's Update(self, dt) and writes the resulting transform
     * back into the scene. Stop() tears the VMs down and restores the exact pre-play snapshot, so
     * playing never dirties the scene.
     *
     * Optional script entry points are registered the same way Update is: the runtime appends a
     * registration line for each one the source defines.
     *   - `function Start(self)` runs once, when the session starts or when the entity is spawned.
     *   - `function Update(self, dt)` runs every frame.
     *   - `function OnDestroy(self)` runs before the entity is destroyed and before Play stops.
     *
     * Scripts are compiled against these libraries:
     *   - EditorPlay: EntityTransform with writable x, y, rotation, scaleX, scaleY, plus the
     *     RegisterUpdate/RegisterStart/RegisterDestroy registration helpers and Log()/Print(), which
     *     append a line to the console (GetLog()).
     *   - Input (bring it in with `using Input`): IsKeyDown(name), IsKeyPressed(name),
     *     IsKeyReleased(name), IsMouseDown(button), MouseX() and MouseY(). The state they read is
     *     whatever was pushed into GetInput() before Tick(); pressed/released are edge flags that
     *     are cleared at the end of every Tick().
     *   - Physics (bring it in with `using Physics`): Overlaps(idA, idB), OverlapPoint(x, y) and
     *     IsOverlapping(id). They read the live collider shapes of the currently stepped scene.
     *   - Scene (bring it in with `using Scene`): FindByName, Spawn, SpawnPrefab, Destroy,
     *     GetX/GetY/SetPos/SetVisible/GetName and the camera setters/getters. Destroy is deferred to
     *     the end of the tick, so a script may destroy entities (even itself) while it iterates.
     *   - Audio (bring it in with `using Audio`): PlaySound(path) loads and plays a wav/ogg file
     *     through Gorgon::Audio when a device is present, and records the path either way so a
     *     headless test can still verify the call.
     *
     * Collision runs after the scripts of a frame have moved their entities, so a script never
     * sees a stale shape in the same frame it moved into one. Entities whose collider is not a
     * trigger and not static are pushed apart along the minimum translation vector; overlapping
     * pairs are diffed frame to frame and reported to the scripts through the optional hooks
     * OnCollisionEnter/OnCollisionExit(self, otherId) and, when either collider is a trigger,
     * OnTriggerEnter/OnTriggerExit(self, otherId).
     */
    class PlaySession {
    public:
        PlaySession();
        ~PlaySession();

        PlaySession(const PlaySession &) = delete;
        PlaySession &operator=(const PlaySession &) = delete;

        /// Snapshots the scene and starts every script. `baseDir` resolves relative script paths.
        /// Returns false with a message (also in GetLastError()) when no script could be started.
        bool Start(Scene &scene, const std::string &baseDir, std::string *outError = nullptr);

        /// Advances all running scripts by dt seconds. dt is clamped to [0, 0.25].
        /// Pressed/released edge flags of GetInput() are cleared at the end of the call. Scripts of
        /// entities spawned during the tick get their Start hook here; entities destroyed during
        /// the tick are removed after every Update ran, so no iteration is invalidated.
        void Tick(Scene &scene, float dt);

        /// Keyboard/mouse state play scripts read through `using Input`. Push the current frame's
        /// state in before calling Tick(); it stays valid for the life of the session.
        InputState &GetInput();
        const InputState &GetInput() const;

        /**
         * Camera state `using Scene` scripts read and write through SetCameraPos/SetCameraZoom/
         * GetCameraX/GetCameraY. It defaults to a camera owned by the session; a host that renders
         * the scene (SceneRuntime, the editor viewport) points it at its own camera with SetCamera()
         * so a script's camera move shows up in the next Render().
         */
        RuntimeCamera &GetCamera();
        const RuntimeCamera &GetCamera() const;

        /// Redirects the camera the scripts read and write. Pass nullptr to go back to the
        /// session's own camera. The camera is not part of the scene snapshot and survives Stop().
        void SetCamera(RuntimeCamera *camera);

        /// True when a script changed the camera since the last call; reading it clears the flag.
        /// A host uses it to copy the session camera into its viewport only when a script moved it.
        bool ConsumeCameraChange();

        /// Stops the session (firing every remaining OnDestroy hook) and restores the pre-play
        /// snapshot. Safe to call repeatedly.
        void Stop(Scene &scene);

        bool IsRunning() const;

        /// Number of entity scripts that compiled and registered an Update function.
        int GetScriptCount() const;

        /// Entity ids currently driven by a script, in scene order.
        std::vector<EntityId> GetScriptedEntities() const;

        /// Overlapping collider pairs detected by the last Tick(). Cleared whenever the session
        /// starts or stops. `trigger` is true when either collider of the pair is a trigger.
        const std::vector<CollisionPair> &GetActiveOverlaps() const;

        /// True when the last Tick() reported an overlap between the two entities (either order).
        bool IsOverlapping(EntityId a, EntityId b) const;

        /// Resolved paths passed to Audio::PlaySound(), in call order. Recorded whether or not an
        /// audio device was available, which is what makes the audio hook testable headlessly.
        const std::vector<std::string> &GetPlayedSounds() const;

        /// Plays (or records, headlessly) a sound the way Audio::PlaySound() does. Used by the
        /// AudioComponent trigger and by a host that wants to fire a component sound.
        void TriggerSound(const std::string &assetPath);

        /// Live particles of an entity's ParticleComponent, or nullptr when it has none. The list
        /// is stepped by Tick(); a renderer reads it to draw the emitter.
        const std::vector<Particle> *GetParticles(EntityId id) const;

        /// The simulation that steps sprite animations, particles and play-on-start audio.
        const ComponentSimulation &GetComponentSimulation() const;

        /// Last error produced while starting or ticking ("" when none).
        const std::string &GetLastError() const;

        /// Accumulated script output (echo and friends), for diagnostics and tests.
        const std::string &GetOutput() const;

        // ---- Script console -------------------------------------------------
        /// The console ring buffer: every script Log()/Print() call, compile failure, runtime
        /// error and warning, each with a timestamp, a severity and the entity/script it came from.
        /// It survives Stop() so a failure can still be read after play ended, and it is cleared
        /// when a new session starts.
        ScriptLog &GetLog();
        const ScriptLog &GetLog() const;

        /// Empties the console log.
        void ClearLog();

        /// Writes one line to the console log from the host side (the editor's own messages).
        /// `source` defaults to "Editor"; pass e.g. "Script Editor" to say where it came from.
        void Log(LogSeverity severity, const std::string &message,
                 const std::string &source = std::string("Editor"));

        /**
         * Compiles `source` against the same libraries a play script is given (EditorPlay, Input,
         * Physics, Scene, Audio) without executing a single statement, so a script editor can offer
         * "Check Syntax" on an unsaved buffer. The registration prologue is only appended for the
         * hooks the source actually defines, so checking a half-written script does not fail on a
         * missing Update. Returns true when it compiles; on failure `outError` carries the compiler
         * message and the return value is false. Nothing is added to the console log.
         */
        bool CheckSyntax(const std::string &source, const std::string &displayPath = std::string(),
                         std::string *outError = nullptr);

    private:
        /// Builds the EditorPlay/Input/Physics/Scene/Audio libraries once. Called by Start() and by
        /// CheckSyntax(), which needs the same symbol table to compile an unsaved script.
        void buildLibraries();

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace Gorgon::Editor
