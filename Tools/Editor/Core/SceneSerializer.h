#pragma once

#include "Scene.h"

#include <Gorgon/Encoding/JSON.h>
#include <string>
#include <vector>

namespace Gorgon :: Editor {

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
    };

} // namespace Gorgon::Editor
