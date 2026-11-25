#pragma once

#include "InputState.h"
#include "Scene.h"

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
     * Scripts are compiled against two libraries:
     *   - EditorPlay: EntityTransform with writable x, y, rotation, scaleX, scaleY, plus
     *     RegisterUpdate(handler), appended to the source by the runtime, so a script only has to
     *     define `function Update(self, dt)`.
     *   - Input (bring it in with `using Input`): IsKeyDown(name), IsKeyPressed(name),
     *     IsKeyReleased(name), IsMouseDown(button), MouseX() and MouseY(). The state they read is
     *     whatever was pushed into GetInput() before Tick(); pressed/released are edge flags that
     *     are cleared at the end of every Tick().
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
        /// Pressed/released edge flags of GetInput() are cleared at the end of the call.
        void Tick(Scene &scene, float dt);

        /// Keyboard/mouse state play scripts read through `using Input`. Push the current frame's
        /// state in before calling Tick(); it stays valid for the life of the session.
        InputState &GetInput();
        const InputState &GetInput() const;

        /// Stops the session and restores the pre-play snapshot. Safe to call repeatedly.
        void Stop(Scene &scene);

        bool IsRunning() const;

        /// Number of entity scripts that compiled and registered an Update function.
        int GetScriptCount() const;

        /// Entity ids currently driven by a script, in scene order.
        std::vector<EntityId> GetScriptedEntities() const;

        /// Last error produced while starting or ticking ("" when none).
        const std::string &GetLastError() const;

        /// Accumulated script output (echo and friends), for diagnostics and tests.
        const std::string &GetOutput() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace Gorgon::Editor
