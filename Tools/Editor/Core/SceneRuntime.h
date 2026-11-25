#pragma once

#include "CollisionSystem.h"
#include "InputState.h"
#include "Scene.h"

#include <Gorgon/Graphics/Layer.h>

#include <memory>
#include <string>
#include <vector>

namespace Gorgon :: Editor {

    /// Orthographic 2D camera used by SceneRuntime. `panX/panY` is the world position shown at
    /// the centre of the viewport, `zoom` is viewport pixels per world unit.
    struct RuntimeCamera {
        float panX = 0.0f;
        float panY = 0.0f;
        float zoom = 1.0f;

        Gorgon::Geometry::Pointf WorldToScreen(float wx, float wy, float viewW, float viewH) const;
        Gorgon::Geometry::Pointf ScreenToWorld(float sx, float sy, float viewW, float viewH) const;

        void CenterOn(float wx, float wy);
        void Reset();
    };

    /// Controls what SceneRuntime::Render draws. Colliders are debug wireframes, off by default.
    struct SceneRenderOptions {
        bool drawBackground = true;
        bool drawEntities = true;
        bool drawColliders = false;
    };

    /**
     * Runtime loader for `.gscene.json` files.
     *
     * The editor writes scenes with relative sprite and script paths. SceneRuntime loads the file,
     * resolves those paths against the scene file's directory (or an explicit base dir), renders the
     * visible entities onto a Graphics::Layer you own, and steps every attached GScript
     * `Update(self, dt)` hook on Update().
     *
     * Typical use:
     * @code
     * Gorgon::Editor::SceneRuntime runtime;
     * if(!runtime.Load("Scene/DemoScene.gscene.json"))
     *     std::cerr << runtime.GetLastError();
     *
     * while(alive) {
     *     runtime.Update(Gorgon::Time::DeltaTime() / 1000.0f);
     *     layer.Clear();
     *     runtime.Render(layer);
     *     Gorgon::NextFrame();
     * }
     * @endcode
     *
     * Render() does not clear the layer, and only draws; nothing else touches the scene.
     *
     * Scripts drive entity transforms exactly like play-in-editor: the transform of the entity the
     * script is attached to is exposed as `self` with writable x, y, rotation, scaleX and scaleY.
     * Unlike the editor there is no snapshot/restore: script changes stay in the scene for the
     * lifetime of the runtime (the restore-on-stop snapshot only matters for editing).
     *
     * Input works the same way: scripts `using Input` can call IsKeyDown/IsKeyPressed/IsKeyReleased/
     * IsMouseDown/MouseX/MouseY. The caller owns the state — fill GetInput() before Update(dt),
     * usually from a window (KeyNameFromCode()/MouseButtonName() turn Gorgon's keycodes and button
     * constants into the names the API expects, and GetCamera().ScreenToWorld() turns the mouse
     * pixel position into the world position the scripts read). Pressed/released are one-frame edge
     * flags: Update() consumes them.
     */
    class SceneRuntime {
    public:
        SceneRuntime();
        ~SceneRuntime();

        SceneRuntime(const SceneRuntime &) = delete;
        SceneRuntime &operator=(const SceneRuntime &) = delete;

        /// Loads a `.gscene.json` file. Sprite and script paths are resolved against the file's
        /// directory unless SetBaseDir() overrides it. Returns false and fills outError on failure.
        bool Load(const std::string &sceneFile, std::string *outError = nullptr);

        /// Takes ownership of an in-memory scene. `baseDir` resolves its relative asset paths.
        /// Compiles every attached script, collecting failures into GetLastError().
        bool LoadFromScene(Scene scene, const std::string &baseDir = std::string(),
                           std::string *outError = nullptr);

        /// Drops the loaded scene, script VMs and cached bitmaps.
        void Unload();

        bool IsLoaded() const;

        /// Steps every attached script by dt seconds (clamped to [0, 0.25]). The state pushed into
        /// GetInput() is handed to the scripts for this step; pressed/released edge flags are
        /// consumed here, so set them again for the next frame.
        void Update(float dt);

        /// Keyboard/mouse state the scripts read. Fill it before Update(): set key and button
        /// names through SetKeyDown()/SetMouseButtonDown() and the world-space position through
        /// SetMousePosition(). Held state persists across Update() calls until it is released.
        InputState &GetInput();
        const InputState &GetInput() const;

        /// Draws the scene into target, using the target's own size for the camera projection.
        void Render(Gorgon::Graphics::Layer &target);
        void Render(Gorgon::Graphics::Layer &target, int viewWidth, int viewHeight);

        Scene &GetScene();
        const Scene &GetScene() const;

        RuntimeCamera &GetCamera();
        const RuntimeCamera &GetCamera() const;
        void SetCamera(const RuntimeCamera &camera);

        /// Pans the camera so the visible entities are centred; keeps the current zoom.
        void CenterCamera(int viewWidth, int viewHeight);

        SceneRenderOptions &GetRenderOptions();
        const SceneRenderOptions &GetRenderOptions() const;

        const std::string &GetScenePath() const;

        /// Directory relative sprite and script paths are resolved against.
        const std::string &GetBaseDir() const;
        void SetBaseDir(const std::string &baseDir);

        /// Resolves a relative asset/script path against GetBaseDir(); absolute paths pass through.
        std::string ResolveAssetPath(const std::string &path) const;

        /// Number of entity scripts that compiled and are being ticked.
        int GetScriptCount() const;

        /// Overlapping collider pairs from the last Update(). Empty while no scripts run, because a
        /// scriptless scene has no play session to resolve collisions in.
        const std::vector<CollisionPair> &GetActiveOverlaps() const;

        /// True when the last Update() reported an overlap between the two entities (either order).
        bool IsOverlapping(EntityId a, EntityId b) const;

        /// Number of sprite bitmaps successfully loaded so far.
        int GetLoadedSpriteCount() const;

        /// Last error from loading or script execution ("" when none).
        const std::string &GetLastError() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace Gorgon::Editor
