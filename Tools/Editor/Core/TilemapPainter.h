#pragma once

#include "Components.h"

#include <cstddef>
#include <string>

namespace Gorgon :: Editor {

    /// Which tile-editing tool the viewport applies while it is armed.
    enum class TilemapTool {
        /// Paints the current tile index into every cell the drag crosses.
        Brush = 0,
        /// Floods the rectangle spanned by a press-drag with the current tile index.
        Fill,
        /// Paints "empty" into every cell the drag crosses.
        Eraser
    };

    inline std::string TilemapToolToString(TilemapTool tool) {
        switch(tool) {
            case TilemapTool::Fill:   return "fill";
            case TilemapTool::Eraser: return "eraser";
            case TilemapTool::Brush:
            default:                  return "brush";
        }
    }

    inline bool TilemapToolFromString(const std::string &str, TilemapTool &out) {
        if(str == "brush")  { out = TilemapTool::Brush;  return true; }
        if(str == "fill")   { out = TilemapTool::Fill;   return true; }
        if(str == "eraser") { out = TilemapTool::Eraser; return true; }
        return false;
    }

    /// A cell address inside a tilemap grid. Values are allowed to fall outside the map: a drag
    /// that starts off the grid is clamped by FillRect, which is how a fill dragged in from
    /// outside still fills the overlapping part.
    struct TilemapCell {
        int col = -1;
        int row = -1;

        bool operator==(const TilemapCell &other) const {
            return col == other.col && row == other.row;
        }
        bool operator!=(const TilemapCell &other) const { return !(*this == other); }
    };

    /// Result of mapping a point onto a tilemap grid. `col`/`row` always name the cell the point
    /// falls in (even when it is off the map); `inside` says whether that cell exists.
    struct TilemapHit {
        bool inside = false;
        int col = -1;
        int row = -1;

        bool operator==(const TilemapHit &other) const {
            return inside == other.inside && col == other.col && row == other.row;
        }
        bool operator!=(const TilemapHit &other) const { return !(*this == other); }
    };

    /**
     * Geometry of the tile palette strip the viewport draws over the bottom of the view.
     *
     * The layout is pure arithmetic on screen pixels so the viewport can draw swatches at exactly
     * the rectangles the picker hit-tests, and the Catch2 suite can verify picking without a
     * window. Swatches are laid out left to right, wrapping after `columns`; only the tiles that
     * fit inside `maxRows` rows are shown (the caller decides how many tiles the tileset packs).
     */
    struct TilePaletteLayout {
        float x = 0.0f;       // strip left edge
        float y = 0.0f;       // strip top edge
        float width = 0.0f;   // strip width, including the surrounding padding
        float height = 0.0f;  // strip height, including the surrounding padding
        int swatch = 32;      // side of one square swatch, pixels
        int gap = 6;          // space between swatches and around the strip
        int columns = 0;      // swatches per row that fit in `width`
        int rows = 1;         // rows actually used
        int count = 0;        // tile indices shown (0..count-1)

        /// Lays out up to `count` swatches in a strip of `maxWidth` starting at (originX, originY).
        static TilePaletteLayout Compute(float originX, float originY, float maxWidth, int count,
                                         int swatch = 32, int gap = 6, int maxRows = 1);

        /// Screen rectangle of the swatch for `index`. Returns false when it is not shown.
        bool SwatchRect(int index, float &outX, float &outY, float &outW, float &outH) const;

        /// True when the point falls inside the strip (its padding included).
        bool Contains(float sx, float sy) const;

        /// Tile index under a screen point, or -1 when the point misses every swatch.
        int HitTest(float sx, float sy) const;
    };

    /**
     * Pure tile-editing geometry and mutation shared by the viewport, the palette strip, the
     * smoke test and the Catch2 suite. Nothing here touches Gorgon rendering or the undo stack,
     * so it runs headless and can be unit-tested directly.
     *
     * Grid model (identical to the editor and runtime renderers): cells are row-major, the map is
     * centred on the entity origin, so cell (0, 0) starts at
     * `(-cols * tileW / 2, -rows * tileH / 2)` in entity-local space. A negative tile index means
     * "empty".
     */
    class TilemapPainter {
    public:
        /// Number of cells the component addresses (negative cols/rows count as 0).
        static std::size_t GetCellCount(const TilemapComponent &map);

        /// True when (col, row) addresses a cell of the map.
        static bool InBounds(const TilemapComponent &map, int col, int row);

        /// True when the component has a grid a brush can address: a positive tile size and at
        /// least one cell. The viewport arms tile painting only for these.
        static bool Paintable(const TilemapComponent &map);

        /// Grows or shrinks `tiles` to exactly cols * rows, filling new cells with -1 (empty).
        /// Existing cells keep their row-major values. Returns true when the size changed.
        static bool EnsureSize(TilemapComponent &map);

        /// Writes `tile` into one cell. Out-of-bounds writes are ignored. Returns true when the
        /// stored value actually changed, so callers can skip a no-op undo step.
        static bool PaintCell(TilemapComponent &map, int col, int row, int tile);

        /// Clears a single cell (the eraser writes the empty tile).
        static bool EraseCell(TilemapComponent &map, int col, int row);

        /// Paints the inclusive rectangle spanned by the two corners. Corners may be given in any
        /// order and may lie outside the map: the rectangle is clamped to the grid, so a rect that
        /// starts outside still fills the part that overlaps it. Returns the number of cells whose
        /// value changed.
        static int FillRect(TilemapComponent &map, int col0, int row0, int col1, int row1, int tile);

        /// Paints every cell on the straight line between two cells (Bresenham), which is what
        /// keeps a fast brush drag from leaving gaps. Returns the number of changed cells.
        static int PaintLine(TilemapComponent &map, int col0, int row0, int col1, int row1, int tile);

        /// Local-space (entity space) top-left corner of a cell.
        static void CellOrigin(const TilemapComponent &map, int col, int row, float &outX, float &outY);

        /// Maps a world point into the map's local space, undoing the entity's world transform
        /// (translation, rotation and scale).
        static void WorldToLocal(const TilemapComponent &map, const Transform &world,
                                 float wx, float wy, float &outX, float &outY);

        /// Cell under a world point, honouring the entity's world transform.
        static TilemapHit WorldToCell(const TilemapComponent &map, const Transform &world,
                                      float wx, float wy);

        /// Cell under a viewport pixel. `camPanX`/`camPanY` are the world point at the centre of
        /// the view and `camZoom` is viewport pixels per world unit, exactly like ViewportCamera.
        static TilemapHit ScreenToCell(const TilemapComponent &map, const Transform &world,
                                       float camPanX, float camPanY, float camZoom,
                                       float sx, float sy, float viewW, float viewH);

        /// Number of tiles the tileset image packs (row-major, `tilesetCols` columns). Falls back
        /// to `fallbackCount` when there is no usable image, which is what an editor palette shows
        /// for a tileset-less map drawn with per-index colours.
        static int TilesetTileCount(const TilemapComponent &map, int imageW, int imageH,
                                    int fallbackCount = 16);
    };

} // namespace Gorgon::Editor
