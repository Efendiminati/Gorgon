#include "CollisionSystem.h"

#include <algorithm>
#include <cmath>

namespace Gorgon :: Editor {

    namespace {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

        /// Penetration of a circle into a box, with the normal pointing the way the *box* has to
        /// travel to leave the circle. Returns false when they do not overlap.
        bool BoxCirclePenetration(const CollisionShape &box, const CollisionShape &circle,
                                  float &outNormalX, float &outNormalY, float &outDepth) {
            const float minX = box.centerX - box.halfWidth;
            const float maxX = box.centerX + box.halfWidth;
            const float minY = box.centerY - box.halfHeight;
            const float maxY = box.centerY + box.halfHeight;

            const float closestX = std::clamp(circle.centerX, minX, maxX);
            const float closestY = std::clamp(circle.centerY, minY, maxY);

            const float dx = closestX - circle.centerX;
            const float dy = closestY - circle.centerY;
            const float distSq = dx * dx + dy * dy;
            const float radiusSq = circle.radius * circle.radius;

            if(distSq > 1e-8f) {
                if(distSq >= radiusSq)
                    return false;

                const float dist = std::sqrt(distSq);
                outNormalX = dx / dist;
                outNormalY = dy / dist;
                outDepth = circle.radius - dist;
                return true;
            }

            // The circle centre is inside the box: leave through the nearest face. The box travels
            // away from the circle, so the normal is the opposite of that face's outward direction.
            const float toLeft = circle.centerX - minX;   // box leaves to the right
            const float toRight = maxX - circle.centerX;  // box leaves to the left
            const float toTop = circle.centerY - minY;    // box leaves downwards (+y)
            const float toBottom = maxY - circle.centerY; // box leaves upwards (-y)

            float best = toRight;
            outNormalX = -1.0f;
            outNormalY = 0.0f;

            if(toLeft < best) {
                best = toLeft;
                outNormalX = 1.0f;
                outNormalY = 0.0f;
            }
            if(toTop < best) {
                best = toTop;
                outNormalX = 0.0f;
                outNormalY = 1.0f;
            }
            if(toBottom < best) {
                best = toBottom;
                outNormalX = 0.0f;
                outNormalY = -1.0f;
            }

            outDepth = best + circle.radius;
            return true;
        }
    } // namespace

    bool CollisionSystem::HasCollider(const Entity &entity) {
        return entity.collider.has_value() && entity.collider->type != ColliderType::None;
    }

    bool CollisionSystem::ComputeShape(const Scene &scene, const Entity &entity, CollisionShape &out) {
        if(!HasCollider(entity))
            return false;

        const auto &collider = *entity.collider;
        const Transform world = scene.GetWorldTransform(entity.id);

        const float scaleX = std::abs(world.scaleX);
        const float scaleY = std::abs(world.scaleY);

        // The offset is local to the entity, so it is scaled and rotated with it.
        const float offsetX = collider.offsetX * world.scaleX;
        const float offsetY = collider.offsetY * world.scaleY;
        const float rad = world.rotation * kDegToRad;
        const float c = std::cos(rad);
        const float s = std::sin(rad);

        CollisionShape shape;
        shape.centerX = world.x + offsetX * c - offsetY * s;
        shape.centerY = world.y + offsetX * s + offsetY * c;

        if(collider.type == ColliderType::AABB) {
            shape.kind = CollisionShape::Kind::Box;
            shape.halfWidth = std::abs(collider.width) * 0.5f * scaleX;
            shape.halfHeight = std::abs(collider.height) * 0.5f * scaleY;
            if(shape.halfWidth <= 0.0f || shape.halfHeight <= 0.0f)
                return false;
        }
        else if(collider.type == ColliderType::Circle) {
            shape.kind = CollisionShape::Kind::Circle;
            shape.radius = std::abs(collider.radius) * std::max(scaleX, scaleY);
            if(shape.radius <= 0.0f)
                return false;
        }
        else {
            return false;
        }

        out = shape;
        return true;
    }

    bool CollisionSystem::ComputeShape(const Scene &scene, EntityId id, CollisionShape &out) {
        const Entity *entity = scene.FindEntity(id);
        if(!entity)
            return false;

        return ComputeShape(scene, *entity, out);
    }

    OverlapResult CollisionSystem::ComputeOverlap(const CollisionShape &a, const CollisionShape &b) {
        OverlapResult result;

        const bool aIsBox = a.kind == CollisionShape::Kind::Box;
        const bool bIsBox = b.kind == CollisionShape::Kind::Box;

        // ---- box / box ---------------------------------------------------------
        if(aIsBox && bIsBox) {
            const float dx = a.centerX - b.centerX;
            const float dy = a.centerY - b.centerY;
            const float overlapX = (a.halfWidth + b.halfWidth) - std::abs(dx);
            const float overlapY = (a.halfHeight + b.halfHeight) - std::abs(dy);

            // Touching faces (zero penetration) do not count as an overlap.
            if(overlapX <= 0.0f || overlapY <= 0.0f)
                return result;

            result.overlaps = true;
            if(overlapX < overlapY) {
                result.normalX = dx < 0.0f ? -1.0f : 1.0f;
                result.depth = overlapX;
            }
            else {
                result.normalY = dy < 0.0f ? -1.0f : 1.0f;
                result.depth = overlapY;
            }
            return result;
        }

        // ---- circle / circle ---------------------------------------------------
        if(!aIsBox && !bIsBox) {
            const float dx = a.centerX - b.centerX;
            const float dy = a.centerY - b.centerY;
            const float sum = a.radius + b.radius;
            const float distSq = dx * dx + dy * dy;

            if(distSq >= sum * sum)
                return result;

            result.overlaps = true;
            const float dist = std::sqrt(distSq);
            if(dist <= 1e-5f) {
                // Concentric circles have no direction; pick one so the caller still separates.
                result.normalX = 1.0f;
                result.depth = sum;
            }
            else {
                result.normalX = dx / dist;
                result.normalY = dy / dist;
                result.depth = sum - dist;
            }
            return result;
        }

        // ---- box / circle ------------------------------------------------------
        float normalX = 0.0f, normalY = 0.0f, depth = 0.0f;
        if(aIsBox) {
            if(!BoxCirclePenetration(a, b, normalX, normalY, depth))
                return result;
            result.normalX = normalX;
            result.normalY = normalY;
        }
        else {
            // Computed as "push the box (b) out of the circle (a)"; the normal we report has to
            // point the other way, because it describes how to move a, not b.
            if(!BoxCirclePenetration(b, a, normalX, normalY, depth))
                return result;
            result.normalX = -normalX;
            result.normalY = -normalY;
        }

        result.overlaps = true;
        result.depth = depth;
        return result;
    }

    bool CollisionSystem::Overlaps(const CollisionShape &a, const CollisionShape &b) {
        return ComputeOverlap(a, b).overlaps;
    }

    bool CollisionSystem::ContainsPoint(const CollisionShape &shape, float x, float y) {
        if(shape.kind == CollisionShape::Kind::Box) {
            return std::abs(x - shape.centerX) <= shape.halfWidth &&
                   std::abs(y - shape.centerY) <= shape.halfHeight;
        }

        const float dx = x - shape.centerX;
        const float dy = y - shape.centerY;
        return dx * dx + dy * dy <= shape.radius * shape.radius;
    }

} // namespace Gorgon::Editor
