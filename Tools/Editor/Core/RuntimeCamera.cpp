#include "RuntimeCamera.h"

namespace Gorgon :: Editor {

    Gorgon::Geometry::Pointf RuntimeCamera::WorldToScreen(float wx, float wy, float viewW, float viewH) const {
        return {
            (wx - panX) * zoom + viewW * 0.5f,
            (wy - panY) * zoom + viewH * 0.5f
        };
    }

    Gorgon::Geometry::Pointf RuntimeCamera::ScreenToWorld(float sx, float sy, float viewW, float viewH) const {
        return {
            (sx - viewW * 0.5f) / zoom + panX,
            (sy - viewH * 0.5f) / zoom + panY
        };
    }

    void RuntimeCamera::CenterOn(float wx, float wy) {
        panX = wx;
        panY = wy;
    }

    void RuntimeCamera::Reset() {
        panX = 0.0f;
        panY = 0.0f;
        zoom = 1.0f;
    }

} // namespace Gorgon::Editor
