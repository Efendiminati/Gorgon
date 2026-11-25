#pragma once

#include "Entity.h"
#include "Scene.h"

namespace Gorgon :: Editor {

    /// World-space 2D shape of a collider.
    ///
    /// An AABB collider becomes an axis-aligned box in world space: the entity's world position
    /// places its centre, the world scale sizes it, and the rotation only swings the collider
    /// offset around (a rotated box is no longer an AABB, so rotation does not tilt the box).
    /// A circle collider becomes a circle scaled by the larger of the two world scale axes.
    struct CollisionShape {
        enum class Kind {
            Box,
            Circle
        };

        Kind kind = Kind::Box;
        float centerX = 0.0f;
        float centerY = 0.0f;
        float halfWidth = 0.0f;
        float halfHeight = 0.0f;
        float radius = 0.0f;

        /// Moves the shape in world units; used by the solver after it pushes an entity.
        void Translate(float dx, float dy) {
            centerX += dx;
            centerY += dy;
        }
    };

    /**
     * Overlap result of two shapes.
     *
     * `normalX/normalY` is a unit vector pointing from the second shape towards the first, and
     * `depth` is how far they overlap along it. Moving the first shape by `normal * depth` and
     * the second by `-normal * depth` separates them: that is the minimum translation vector the
     * solid solver applies.
     */
    struct OverlapResult {
        bool overlaps = false;
        float normalX = 0.0f;
        float normalY = 0.0f;
        float depth = 0.0f;
    };

    /// One overlapping pair of colliders for one frame of play. `a` is always the smaller id.
    struct CollisionPair {
        EntityId a = InvalidEntityId;
        EntityId b = InvalidEntityId;
        /// True when either collider is a trigger volume (it reports overlaps but never pushes).
        bool trigger = false;

        bool operator==(const CollisionPair &other) const {
            return a == other.a && b == other.b;
        }
        bool operator!=(const CollisionPair &other) const {
            return !(*this == other);
        }
    };

    /**
     * Lightweight 2D collision queries shared by PlaySession and SceneRuntime.
     *
     * A collider is turned into a world-space shape first (ComputeShape) and only then tested, so
     * every query accounts for the entity's composed world transform (position, scale, rotation)
     * and for the collider's own local offset.
     *
     * Nothing here mutates the scene: the resolver that pushes entities apart lives in
     * PlaySession, which applies the overlap results reported here.
     */
    class CollisionSystem {
    public:
        /// True when the entity carries a collider whose type is not None.
        static bool HasCollider(const Entity &entity);

        /// Builds the world-space shape of the entity's collider. Returns false (leaving `out`
        /// untouched) when the entity has no collider or its extents are degenerate (zero sized).
        static bool ComputeShape(const Scene &scene, const Entity &entity, CollisionShape &out);
        static bool ComputeShape(const Scene &scene, EntityId id, CollisionShape &out);

        /// Overlap predicate. Handles box/box, circle/circle and box/circle.
        static bool Overlaps(const CollisionShape &a, const CollisionShape &b);

        /// Overlap test plus the minimum translation vector that separates the two shapes.
        static OverlapResult ComputeOverlap(const CollisionShape &a, const CollisionShape &b);

        /// True when the world point lies inside the shape (edges count as inside).
        static bool ContainsPoint(const CollisionShape &shape, float x, float y);
    };

} // namespace Gorgon::Editor
