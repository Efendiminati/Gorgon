#pragma once

#include <Gorgon/Geometry/Point.h>

namespace Gorgon :: Editor {

    /// Orthographic 2D camera shared by SceneRuntime, the play session and any host that draws a
    /// scene. `panX/panY` is the world position shown at the centre of the viewport, `zoom` is
    /// viewport pixels per world unit.
    struct RuntimeCamera {
        float panX = 0.0f;
        float panY = 0.0f;
        float zoom = 1.0f;

        Gorgon::Geometry::Pointf WorldToScreen(float wx, float wy, float viewW, float viewH) const;
        Gorgon::Geometry::Pointf ScreenToWorld(float sx, float sy, float viewW, float viewH) const;

        void CenterOn(float wx, float wy);
        void Reset();
    };

} // namespace Gorgon::Editor
