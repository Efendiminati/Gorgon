#pragma once

#include "Scene.h"

#include <Gorgon/Encoding/JSON.h>
#include <string>

namespace Gorgon :: Editor {

    class SceneSerializer {
    public:
        static Gorgon::Encoding::JSON::Value ToJson(const Scene &scene);
        static std::string ToJsonString(const Scene &scene, int indent = 2);

        static bool FromJson(const Gorgon::Encoding::JSON::Value &root, Scene &outScene, std::string *outError = nullptr);
        static bool FromJsonString(const std::string &jsonText, Scene &outScene, std::string *outError = nullptr);

        static bool SaveToFile(const Scene &scene, const std::string &filePath, std::string *outError = nullptr, int indent = 2);
        static bool LoadFromFile(const std::string &filePath, Scene &outScene, std::string *outError = nullptr);
    };

} // namespace Gorgon::Editor
