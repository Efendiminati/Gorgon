#include "TilemapPainter.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Gorgon :: Editor {

    namespace {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

        /// A near-zero scale would divide by ~0; treat it as "unscaled" instead of producing
        /// infinities that then index wildly out of range.
        float SafeScale(float value) {
            return std::abs(value) > 1e-6f ? value : 1.0f;
        }
    } // namespace

    std::size_t TilemapPainter::GetCellCount(const TilemapComponent &map) {
        if(map.cols <= 0 || map.rows <= 0)
            return 0;
        return static_cast<std::size_t>(map.cols) * static_cast<std::size_t>(map.rows);
    }

    bool TilemapPainter::InBounds(const TilemapComponent &map, int col, int row) {
        return col >= 0 && row >= 0 && col < map.cols && row < map.rows;
    }

    bool TilemapPainter::Paintable(const TilemapComponent &map) {
        return map.tileW > 0.0f && map.tileH > 0.0f && map.cols > 0 && map.rows > 0;
    }

    bool TilemapPainter::EnsureSize(TilemapComponent &map) {
        const std::size_t count = GetCellCount(map);
        if(map.tiles.size() == count)
            return false;

        map.tiles.resize(count, -1);
        return true;
    }

    bool TilemapPainter::PaintCell(TilemapComponent &map, int col, int row, int tile) {
        if(!InBounds(map, col, row))
            return false;

        // A freshly created component can carry a grid without its tile array yet; grow it so the
        // write lands instead of being silently dropped.
        const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(map.cols) +
                                  static_cast<std::size_t>(col);
        if(index >= map.tiles.size()) {
            EnsureSize(map);
            if(index >= map.tiles.size())
                return false;
        }

        if(map.tiles[index] == tile)
            return false;

        map.tiles[index] = tile;
        return true;
    }

    bool TilemapPainter::EraseCell(TilemapComponent &map, int col, int row) {
        return PaintCell(map, col, row, -1);
    }

    int TilemapPainter::FillRect(TilemapComponent &map, int col0, int row0, int col1, int row1, int tile) {
        if(map.cols <= 0 || map.rows <= 0)
            return 0;

        int c0 = std::min(col0, col1);
        int c1 = std::max(col0, col1);
        int r0 = std::min(row0, row1);
        int r1 = std::max(row0, row1);

        // Clamp to the map: a drag that starts outside (negative or past the edge) still fills
        // the part of the rectangle that overlaps the grid.
        c0 = std::max(c0, 0);
        r0 = std::max(r0, 0);
        c1 = std::min(c1, map.cols - 1);
        r1 = std::min(r1, map.rows - 1);

        if(c0 > c1 || r0 > r1)
            return 0;

        int changed = 0;
        for(int row = r0; row <= r1; ++row) {
            for(int col = c0; col <= c1; ++col) {
                if(PaintCell(map, col, row, tile))
                    ++changed;
            }
        }
        return changed;
    }

    int TilemapPainter::PaintLine(TilemapComponent &map, int col0, int row0, int col1, int row1, int tile) {
        int changed = 0;
        int col = col0;
        int row = row0;
        const int dx = std::abs(col1 - col0);
        const int dy = -std::abs(row1 - row0);
        const int stepX = col0 < col1 ? 1 : -1;
        const int stepY = row0 < row1 ? 1 : -1;
        int error = dx + dy;

        for(;;) {
            if(PaintCell(map, col, row, tile))
                ++changed;

            if(col == col1 && row == row1)
                break;

            const int twice = 2 * error;
            if(twice >= dy) {
                error += dy;
                col += stepX;
            }
            if(twice <= dx) {
                error += dx;
                row += stepY;
            }
        }

        return changed;
    }

    void TilemapPainter::CellOrigin(const TilemapComponent &map, int col, int row, float &outX, float &outY) {
        const float originX = -static_cast<float>(std::max(0, map.cols)) * map.tileW * 0.5f;
        const float originY = -static_cast<float>(std::max(0, map.rows)) * map.tileH * 0.5f;
        outX = originX + static_cast<float>(col) * map.tileW;
        outY = originY + static_cast<float>(row) * map.tileH;
    }

    void TilemapPainter::WorldToLocal(const TilemapComponent &, const Transform &world,
                                      float wx, float wy, float &outX, float &outY) {
        const float dx = wx - world.x;
        const float dy = wy - world.y;

        // Undo the world rotation, then the world scale.
        const float rad = -world.rotation * kDegToRad;
        const float c = std::cos(rad);
        const float s = std::sin(rad);
        const float rx = dx * c - dy * s;
        const float ry = dx * s + dy * c;

        outX = rx / SafeScale(world.scaleX);
        outY = ry / SafeScale(world.scaleY);
    }

    TilemapHit TilemapPainter::WorldToCell(const TilemapComponent &map, const Transform &world,
                                           float wx, float wy) {
        TilemapHit hit;

        if(map.tileW <= 0.0f || map.tileH <= 0.0f)
            return hit;

        float lx = 0.0f;
        float ly = 0.0f;
        WorldToLocal(map, world, wx, wy, lx, ly);

        const float originX = -static_cast<float>(std::max(0, map.cols)) * map.tileW * 0.5f;
        const float originY = -static_cast<float>(std::max(0, map.rows)) * map.tileH * 0.5f;

        hit.col = static_cast<int>(std::floor((lx - originX) / map.tileW));
        hit.row = static_cast<int>(std::floor((ly - originY) / map.tileH));
        hit.inside = InBounds(map, hit.col, hit.row);
        return hit;
    }

    TilemapHit TilemapPainter::ScreenToCell(const TilemapComponent &map, const Transform &world,
                                            float camPanX, float camPanY, float camZoom,
                                            float sx, float sy, float viewW, float viewH) {
        // Same convention as ViewportCamera::ScreenToWorld, so a panned/zoomed viewport paints
        // the tile the user actually sees under the cursor.
        const float zoom = camZoom > 1e-6f ? camZoom : 1e-6f;
        const float wx = (sx - viewW * 0.5f) / zoom + camPanX;
        const float wy = (sy - viewH * 0.5f) / zoom + camPanY;
        return WorldToCell(map, world, wx, wy);
    }

    int TilemapPainter::TilesetTileCount(const TilemapComponent &map, int imageW, int imageH,
                                         int fallbackCount) {
        const int fallback = std::max(0, fallbackCount);
        if(imageW <= 0 || imageH <= 0 || map.tileW <= 0.0f || map.tileH <= 0.0f)
            return fallback;

        const int tileW = std::max(1, static_cast<int>(map.tileW));
        const int tileH = std::max(1, static_cast<int>(map.tileH));
        const int perRow = map.tilesetCols > 0 ? map.tilesetCols : std::max(1, imageW / tileW);
        const int perColumn = imageH / tileH;
        if(perRow <= 0 || perColumn <= 0)
            return fallback;

        return perRow * perColumn;
    }

    TilePaletteLayout TilePaletteLayout::Compute(float originX, float originY, float maxWidth,
                                                 int count, int swatch, int gap, int maxRows) {
        TilePaletteLayout layout;
        layout.swatch = std::max(4, swatch);
        layout.gap = std::max(0, gap);
        layout.count = std::max(0, count);
        layout.x = originX;
        layout.y = originY;
        layout.rows = std::max(1, maxRows);

        const float step = static_cast<float>(layout.swatch + layout.gap);
        // Room between the strip's two padding bands, then however many swatches fit.
        const float usable = std::max(0.0f, maxWidth - 2.0f * static_cast<float>(layout.gap));
        layout.columns = std::max(1, static_cast<int>(std::floor((usable + layout.gap) / step)));
        layout.columns = std::min(layout.columns, std::max(1, layout.count));

        const int visibleRows = (layout.count + layout.columns - 1) / std::max(1, layout.columns);
        layout.rows = std::min(layout.rows, std::max(1, visibleRows));
        layout.count = std::min(layout.count, layout.columns * layout.rows);

        layout.width = usable > 0.0f ? maxWidth
                                     : static_cast<float>(layout.columns) * step - layout.gap +
                                           2.0f * static_cast<float>(layout.gap);
        layout.height = static_cast<float>(layout.rows) * step -
                        static_cast<float>(layout.gap) + 2.0f * static_cast<float>(layout.gap);
        return layout;
    }

    bool TilePaletteLayout::SwatchRect(int index, float &outX, float &outY, float &outW, float &outH) const {
        if(index < 0 || index >= count || columns <= 0)
            return false;

        const float step = static_cast<float>(swatch + gap);
        const int column = index % columns;
        const int row = index / columns;

        outX = x + static_cast<float>(gap) + static_cast<float>(column) * step;
        outY = y + static_cast<float>(gap) + static_cast<float>(row) * step;
        outW = static_cast<float>(swatch);
        outH = static_cast<float>(swatch);
        return true;
    }

    bool TilePaletteLayout::Contains(float sx, float sy) const {
        if(count <= 0 || width <= 0.0f || height <= 0.0f)
            return false;
        return sx >= x && sy >= y && sx < x + width && sy < y + height;
    }

    int TilePaletteLayout::HitTest(float sx, float sy) const {
        if(!Contains(sx, sy))
            return -1;

        for(int index = 0; index < count; ++index) {
            float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
            if(!SwatchRect(index, cx, cy, cw, ch))
                continue;
            if(sx >= cx && sy >= cy && sx < cx + cw && sy < cy + ch)
                return index;
        }
        return -1;
    }

} // namespace Gorgon::Editor
