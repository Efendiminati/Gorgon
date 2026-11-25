#pragma once

#include "Scene.h"

#include <Gorgon/Encoding/JSON.h>
#include <string>
#include <vector>

namespace Gorgon :: Editor {

    /// Reusable entity blueprint (.gprefab.json): a root entity plus its descendant
    /// subtree with local transforms and components.
    struct PrefabData {
        static constexpr int CurrentSchemaVersion = 1;

        int schemaVersion = CurrentSchemaVersion;
        std::string name;
        EntityId rootId = InvalidEntityId;
        std::vector<Entity> entities;

        bool IsValid() const {
            return rootId != InvalidEntityId && !entities.empty();
        }
    };

    class SceneSerializer {
    public:
        static Gorgon::Encoding::JSON::Value ToJson(const Scene &scene);
        static std::string ToJsonString(const Scene &scene, int indent = 2);

        static bool FromJson(const Gorgon::Encoding::JSON::Value &root, Scene &outScene, std::string *outError = nullptr);
        static bool FromJsonString(const std::string &jsonText, Scene &outScene, std::string *outError = nullptr);

        static bool SaveToFile(const Scene &scene, const std::string &filePath, std::string *outError = nullptr, int indent = 2);
        static bool LoadFromFile(const std::string &filePath, Scene &outScene, std::string *outError = nullptr);

        /// Serializes the given entities (plus their descendant subtrees, deduplicated)
        /// into a self-contained clipboard JSON payload preserving internal parent-child links.
        static Gorgon::Encoding::JSON::Value EntitiesToJson(const Scene &scene, const std::vector<EntityId> &entityIds);
        static std::string EntitiesToJsonString(const Scene &scene, const std::vector<EntityId> &entityIds, int indent = 2);

        /// Deserializes a clipboard JSON payload into targetScene with fresh EntityIds,
        /// remapped internal parentIds, unique names, and an optional offset on pasted roots.
        static bool PasteEntitiesFromJson(
            const Gorgon::Encoding::JSON::Value &root,
            Scene &targetScene,
            std::vector<EntityId> *outRootIds = nullptr,
            float offsetX = 0.0f,
            float offsetY = 0.0f,
            std::string *outError = nullptr,
            std::vector<EntityId> *outAllPastedIds = nullptr
        );

        static bool PasteEntitiesFromJsonString(
            const std::string &jsonText,
            Scene &targetScene,
            std::vector<EntityId> *outRootIds = nullptr,
            float offsetX = 0.0f,
            float offsetY = 0.0f,
            std::string *outError = nullptr,
            std::vector<EntityId> *outAllPastedIds = nullptr
        );

        /// Serializes a single root entity and its descendant subtree into a .gprefab.json
        /// payload with normalized local IDs (1..N) and the root placed at local origin (0, 0).
        static Gorgon::Encoding::JSON::Value PrefabToJson(
            const Scene &scene,
            EntityId rootId,
            const std::string &prefabName = std::string()
        );
        static std::string PrefabToJsonString(
            const Scene &scene,
            EntityId rootId,
            const std::string &prefabName = std::string(),
            int indent = 2
        );
        static bool SavePrefabToFile(
            const Scene &scene,
            EntityId rootId,
            const std::string &filePath,
            std::string *outError = nullptr,
            int indent = 2
        );

        /// Loads a .gprefab.json payload into a validated PrefabData structure.
        static bool LoadPrefabFromJson(
            const Gorgon::Encoding::JSON::Value &root,
            PrefabData &outPrefab,
            std::string *outError = nullptr
        );
        static bool LoadPrefabFromJsonString(
            const std::string &jsonText,
            PrefabData &outPrefab,
            std::string *outError = nullptr
        );
        static bool LoadPrefabFromFile(
            const std::string &filePath,
            PrefabData &outPrefab,
            std::string *outError = nullptr
        );

        /// Instantiates a prefab into targetScene at (worldX, worldY) with fresh EntityIds,
        /// remapped internal parentIds, and prefabSource recorded on the instantiated root.
        static bool InstantiatePrefab(
            const PrefabData &prefab,
            Scene &targetScene,
            float worldX = 0.0f,
            float worldY = 0.0f,
            const std::string &prefabSource = std::string(),
            EntityId *outRootId = nullptr,
            std::vector<EntityId> *outAllIds = nullptr,
            std::string *outError = nullptr
        );
        static bool InstantiatePrefabFromFile(
            const std::string &filePath,
            Scene &targetScene,
            float worldX = 0.0f,
            float worldY = 0.0f,
            const std::string &prefabSource = std::string(),
            EntityId *outRootId = nullptr,
            std::vector<EntityId> *outAllIds = nullptr,
            std::string *outError = nullptr
        );

        /// Reverts an existing prefab instance in scene to match prefab while preserving
        /// the instance root's EntityId, parentId, world/local position (x, y), and prefabSource.
        static bool RevertPrefabInstance(
            Scene &scene,
            EntityId instanceRootId,
            const PrefabData &prefab,
            std::string *outError = nullptr
        );
    };

} // namespace Gorgon::Editor
