#include "SceneSerializer.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Gorgon :: Editor {

    namespace {
        using Gorgon::Encoding::JSON;
        using Gorgon::Encoding::Json;

        void setError(std::string *outError, const std::string &msg) {
            if(outError) {
                *outError = msg;
            }
        }

        std::uint8_t clampByte(int v) {
            if(v < 0) return 0;
            if(v > 255) return 255;
            return static_cast<std::uint8_t>(v);
        }

        JSON::Value colorToJson(const ColorRGBA &c) {
            JSON::Array arr;
            arr.reserve(4);
            arr.emplace_back(static_cast<int>(c.r));
            arr.emplace_back(static_cast<int>(c.g));
            arr.emplace_back(static_cast<int>(c.b));
            arr.emplace_back(static_cast<int>(c.a));
            return JSON::Value(std::move(arr));
        }

        bool colorFromJson(const JSON::Value &val, ColorRGBA &out, std::string *outError, const std::string &field) {
            if(!val.IsArray() || val.GetCount() != 4) {
                setError(outError, "Expected 4-element RGBA array for '" + field + "'");
                return false;
            }
            for(int i = 0; i < 4; ++i) {
                if(!val[i].IsNumber()) {
                    setError(outError, "Non-numeric channel in '" + field + "'");
                    return false;
                }
            }
            out.r = clampByte(static_cast<int>(val[0].Get<double>()));
            out.g = clampByte(static_cast<int>(val[1].Get<double>()));
            out.b = clampByte(static_cast<int>(val[2].Get<double>()));
            out.a = clampByte(static_cast<int>(val[3].Get<double>()));
            return true;
        }

        float getFloatField(const JSON::Value &obj, const std::string &key, float defVal) {
            if(!obj.Has(key) || obj[key].IsNull()) {
                return defVal;
            }
            return static_cast<float>(obj[key].Get<double>());
        }

        int getIntField(const JSON::Value &obj, const std::string &key, int defVal) {
            if(!obj.Has(key) || obj[key].IsNull()) {
                return defVal;
            }
            return static_cast<int>(obj[key].Get<double>());
        }

        bool getBoolField(const JSON::Value &obj, const std::string &key, bool defVal) {
            if(!obj.Has(key) || obj[key].IsNull()) {
                return defVal;
            }
            return obj[key].Get<bool>();
        }

        std::string getStringField(const JSON::Value &obj, const std::string &key, const std::string &defVal) {
            if(!obj.Has(key) || obj[key].IsNull()) {
                return defVal;
            }
            return obj[key].Get<std::string>();
        }

        JSON::Value entityToJson(const Entity &e, EntityId serializedParentId) {
            JSON::Value eObj;
            eObj.Set("id", static_cast<int>(e.id));
            eObj.Set("parentId", static_cast<int>(serializedParentId));
            eObj.Set("name", e.name);
            eObj.Set("visible", e.visible);
            eObj.Set("locked", e.locked);
            eObj.Set("zOrder", e.zOrder);

            JSON::Value trObj;
            trObj.Set("x", static_cast<double>(e.transform.x));
            trObj.Set("y", static_cast<double>(e.transform.y));
            trObj.Set("scaleX", static_cast<double>(e.transform.scaleX));
            trObj.Set("scaleY", static_cast<double>(e.transform.scaleY));
            trObj.Set("rotation", static_cast<double>(e.transform.rotation));
            eObj.Set("transform", std::move(trObj));

            if(e.sprite.has_value()) {
                JSON::Value spObj;
                spObj.Set("asset", e.sprite->asset);
                spObj.Set("tint", colorToJson(e.sprite->tint));
                eObj.Set("sprite", std::move(spObj));
            }

            if(e.collider.has_value()) {
                JSON::Value colObj;
                colObj.Set("type", ColliderTypeToString(e.collider->type));
                colObj.Set("w", static_cast<double>(e.collider->width));
                colObj.Set("h", static_cast<double>(e.collider->height));
                colObj.Set("radius", static_cast<double>(e.collider->radius));
                colObj.Set("offsetX", static_cast<double>(e.collider->offsetX));
                colObj.Set("offsetY", static_cast<double>(e.collider->offsetY));
                eObj.Set("collider", std::move(colObj));
            }

            if(e.script.has_value()) {
                JSON::Value scObj;
                scObj.Set("path", e.script->path);
                eObj.Set("script", std::move(scObj));
            }

            return eObj;
        }

        bool entityFromJson(const JSON::Value &eObj, int index, Entity &e, std::string *outError) {
            if(!eObj.IsObject()) {
                setError(outError, "Entity at index " + std::to_string(index) + " is not an object");
                return false;
            }

            if(!eObj.Has("id") || !eObj["id"].IsNumber()) {
                setError(outError, "Entity at index " + std::to_string(index) + " missing numeric 'id'");
                return false;
            }
            const int rawId = static_cast<int>(eObj["id"].Get<double>());
            if(rawId <= 0) {
                setError(outError, "Entity at index " + std::to_string(index) + " has non-positive id");
                return false;
            }

            e = Entity{};
            e.id = static_cast<EntityId>(rawId);
            const int rawParent = getIntField(eObj, "parentId", 0);
            if(rawParent < 0) {
                setError(outError, "Entity " + std::to_string(rawId) + " has negative parentId");
                return false;
            }
            e.parentId = static_cast<EntityId>(rawParent);
            e.name = getStringField(eObj, "name", "Entity");
            e.visible = getBoolField(eObj, "visible", true);
            e.locked = getBoolField(eObj, "locked", false);
            e.zOrder = getIntField(eObj, "zOrder", 0);

            if(eObj.Has("transform") && !eObj["transform"].IsNull()) {
                const auto &tr = eObj["transform"];
                if(!tr.IsObject()) {
                    setError(outError, "Entity " + std::to_string(rawId) + " 'transform' must be an object");
                    return false;
                }
                e.transform.x = getFloatField(tr, "x", 0.0f);
                e.transform.y = getFloatField(tr, "y", 0.0f);
                e.transform.scaleX = getFloatField(tr, "scaleX", 1.0f);
                e.transform.scaleY = getFloatField(tr, "scaleY", 1.0f);
                e.transform.rotation = getFloatField(tr, "rotation", 0.0f);
            }

            if(eObj.Has("sprite") && !eObj["sprite"].IsNull()) {
                const auto &sp = eObj["sprite"];
                if(!sp.IsObject()) {
                    setError(outError, "Entity " + std::to_string(rawId) + " 'sprite' must be an object");
                    return false;
                }
                SpriteComponent comp;
                comp.asset = getStringField(sp, "asset", "");
                if(sp.Has("tint") && !sp["tint"].IsNull()) {
                    if(!colorFromJson(sp["tint"], comp.tint, outError, "sprite.tint")) {
                        return false;
                    }
                }
                e.sprite = std::move(comp);
            }

            if(eObj.Has("collider") && !eObj["collider"].IsNull()) {
                const auto &col = eObj["collider"];
                if(!col.IsObject()) {
                    setError(outError, "Entity " + std::to_string(rawId) + " 'collider' must be an object");
                    return false;
                }
                ColliderComponent comp;
                const std::string typeStr = getStringField(col, "type", "none");
                if(!ColliderTypeFromString(typeStr, comp.type)) {
                    setError(outError, "Unknown collider type: '" + typeStr + "'");
                    return false;
                }
                comp.width = getFloatField(col, "w", 32.0f);
                comp.height = getFloatField(col, "h", 32.0f);
                comp.radius = getFloatField(col, "radius", 16.0f);
                comp.offsetX = getFloatField(col, "offsetX", 0.0f);
                comp.offsetY = getFloatField(col, "offsetY", 0.0f);
                e.collider = comp;
            }

            if(eObj.Has("script") && !eObj["script"].IsNull()) {
                const auto &sc = eObj["script"];
                if(!sc.IsObject()) {
                    setError(outError, "Entity " + std::to_string(rawId) + " 'script' must be an object");
                    return false;
                }
                ScriptComponent comp;
                comp.path = getStringField(sc, "path", "");
                e.script = std::move(comp);
            }

            return true;
        }

        std::string makeUniqueEntityName(const std::string &desired, std::unordered_set<std::string> &usedNames) {
            const std::string name = desired.empty() ? std::string("Entity") : desired;
            if(usedNames.find(name) == usedNames.end()) {
                usedNames.insert(name);
                return name;
            }

            std::string base = name;
            const std::string copySuffix = " Copy";
            if(base.size() > copySuffix.size() &&
               base.compare(base.size() - copySuffix.size(), copySuffix.size(), copySuffix) == 0) {
                base.erase(base.size() - copySuffix.size());
            } else {
                const std::string copySpace = " Copy ";
                const auto pos = base.rfind(copySpace);
                if(pos != std::string::npos && pos + copySpace.size() < base.size()) {
                    bool allDigits = true;
                    for(std::size_t i = pos + copySpace.size(); i < base.size(); ++i) {
                        if(!std::isdigit(static_cast<unsigned char>(base[i]))) {
                            allDigits = false;
                            break;
                        }
                    }
                    if(allDigits) {
                        base.erase(pos);
                    }
                }
            }

            std::string candidate = base + copySuffix;
            if(usedNames.find(candidate) == usedNames.end()) {
                usedNames.insert(candidate);
                return candidate;
            }

            for(int n = 2; ; ++n) {
                candidate = base + " Copy " + std::to_string(n);
                if(usedNames.find(candidate) == usedNames.end()) {
                    usedNames.insert(candidate);
                    return candidate;
                }
            }
        }
    } // namespace

    Gorgon::Encoding::JSON::Value SceneSerializer::ToJson(const Scene &scene) {
        JSON::Value root;
        root.Set("schemaVersion", Scene::CurrentSchemaVersion);
        root.Set("name", scene.name);

        JSON::Value settingsObj;
        settingsObj.Set("gridSize", scene.settings.gridSize);
        settingsObj.Set("backgroundColor", colorToJson(scene.settings.backgroundColor));
        root.Set("settings", std::move(settingsObj));

        JSON::Array entitiesArr;
        entitiesArr.reserve(scene.GetEntityCount());

        for(const auto &e : scene.GetEntities()) {
            entitiesArr.push_back(entityToJson(e, e.parentId));
        }

        root.Set("entities", JSON::Value(std::move(entitiesArr)));
        return root;
    }

    std::string SceneSerializer::ToJsonString(const Scene &scene, int indent) {
        return Json.Encode(ToJson(scene), indent);
    }

    bool SceneSerializer::FromJson(const Gorgon::Encoding::JSON::Value &root, Scene &outScene, std::string *outError) {
        try {
            if(!root.IsObject()) {
                setError(outError, "Scene JSON root must be an object");
                return false;
            }

            if(!root.Has("schemaVersion") || !root["schemaVersion"].IsNumber()) {
                setError(outError, "Missing or invalid 'schemaVersion' field");
                return false;
            }
            const int version = static_cast<int>(root["schemaVersion"].Get<double>());
            if(version != Scene::CurrentSchemaVersion) {
                setError(outError, "Unsupported schemaVersion: " + std::to_string(version));
                return false;
            }

            Scene loaded;
            loaded.name = getStringField(root, "name", "Untitled");

            if(root.Has("settings") && !root["settings"].IsNull()) {
                const auto &sObj = root["settings"];
                if(!sObj.IsObject()) {
                    setError(outError, "'settings' must be an object");
                    return false;
                }
                loaded.settings.gridSize = getIntField(sObj, "gridSize", 32);
                if(sObj.Has("backgroundColor") && !sObj["backgroundColor"].IsNull()) {
                    if(!colorFromJson(sObj["backgroundColor"], loaded.settings.backgroundColor, outError, "settings.backgroundColor")) {
                        return false;
                    }
                }
            }

            if(root.Has("entities") && !root["entities"].IsNull()) {
                const auto &entitiesVal = root["entities"];
                if(!entitiesVal.IsArray()) {
                    setError(outError, "'entities' must be an array");
                    return false;
                }

                const int count = entitiesVal.GetCount();
                for(int i = 0; i < count; ++i) {
                    Entity e;
                    if(!entityFromJson(entitiesVal[i], i, e, outError)) {
                        return false;
                    }
                    const EntityId rawId = e.id;
                    if(!loaded.AddEntityWithId(std::move(e))) {
                        setError(outError, "Duplicate entity id: " + std::to_string(rawId));
                        return false;
                    }
                }

                // Validate parent links and reject cycles in loaded hierarchy
                for(const auto &e : loaded.GetEntities()) {
                    if(e.parentId == InvalidEntityId) {
                        continue;
                    }
                    if(e.parentId == e.id) {
                        setError(outError, "Entity " + std::to_string(e.id) + " cannot be its own parent");
                        return false;
                    }
                    if(!loaded.HasEntity(e.parentId)) {
                        setError(outError, "Entity " + std::to_string(e.id) + " references missing parentId " + std::to_string(e.parentId));
                        return false;
                    }
                    // Cycle check by walking ancestors
                    std::unordered_set<EntityId> visited;
                    EntityId cur = e.id;
                    while(cur != InvalidEntityId) {
                        if(!visited.insert(cur).second) {
                            setError(outError, "Cycle detected in entity hierarchy involving entity " + std::to_string(e.id));
                            return false;
                        }
                        const Entity *node = loaded.FindEntity(cur);
                        cur = node ? node->parentId : InvalidEntityId;
                    }
                }
            }

            outScene = std::move(loaded);
            return true;
        } catch(const JSON::Error &err) {
            setError(outError, err.what());
            return false;
        } catch(const std::exception &err) {
            setError(outError, err.what());
            return false;
        }
    }

    bool SceneSerializer::FromJsonString(const std::string &jsonText, Scene &outScene, std::string *outError) {
        try {
            JSON::Value val = Json.Parse(jsonText);
            return FromJson(val, outScene, outError);
        } catch(const JSON::Error &err) {
            setError(outError, err.what());
            return false;
        } catch(const std::exception &err) {
            setError(outError, err.what());
            return false;
        }
    }

    bool SceneSerializer::SaveToFile(const Scene &scene, const std::string &filePath, std::string *outError, int indent) {
        std::ofstream out(filePath, std::ios::out | std::ios::trunc | std::ios::binary);
        if(!out.is_open()) {
            setError(outError, "Failed to open file for writing: " + filePath);
            return false;
        }
        const std::string encoded = ToJsonString(scene, indent);
        out.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
        out.put('\n');
        if(!out.good()) {
            setError(outError, "Failed while writing scene file: " + filePath);
            return false;
        }
        return true;
    }

    bool SceneSerializer::LoadFromFile(const std::string &filePath, Scene &outScene, std::string *outError) {
        std::ifstream in(filePath, std::ios::in | std::ios::binary);
        if(!in.is_open()) {
            setError(outError, "Failed to open file for reading: " + filePath);
            return false;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        if(in.bad()) {
            setError(outError, "Failed while reading scene file: " + filePath);
            return false;
        }
        return FromJsonString(ss.str(), outScene, outError);
    }

    Gorgon::Encoding::JSON::Value SceneSerializer::EntitiesToJson(const Scene &scene, const std::vector<EntityId> &entityIds) {
        JSON::Value root;
        root.Set("schemaVersion", Scene::CurrentSchemaVersion);
        root.Set("clipboard", true);

        // Deduplicate and keep only IDs that exist in the scene, preserving order.
        std::vector<EntityId> uniqueIds;
        std::unordered_set<EntityId> seen;
        uniqueIds.reserve(entityIds.size());
        for(EntityId id : entityIds) {
            if(id != InvalidEntityId && scene.HasEntity(id) && seen.insert(id).second) {
                uniqueIds.push_back(id);
            }
        }

        // Keep only top-level roots so selecting both parent and child never duplicates the child.
        std::vector<EntityId> topRoots;
        topRoots.reserve(uniqueIds.size());
        for(EntityId id : uniqueIds) {
            bool ancestorSelected = false;
            for(EntityId other : uniqueIds) {
                if(other != id && scene.IsAncestorOf(other, id)) {
                    ancestorSelected = true;
                    break;
                }
            }
            if(!ancestorSelected) {
                topRoots.push_back(id);
            }
        }

        JSON::Array entitiesArr;
        std::unordered_set<EntityId> emitted;
        std::function<void(EntityId, bool)> emitSubtree = [&](EntityId id, bool isRoot) {
            if(!emitted.insert(id).second) {
                return;
            }
            const Entity *e = scene.FindEntity(id);
            if(!e) {
                return;
            }
            entitiesArr.push_back(entityToJson(*e, isRoot ? InvalidEntityId : e->parentId));
            for(EntityId childId : scene.GetChildIds(id)) {
                emitSubtree(childId, false);
            }
        };

        for(EntityId rootId : topRoots) {
            emitSubtree(rootId, true);
        }

        root.Set("entities", JSON::Value(std::move(entitiesArr)));
        return root;
    }

    std::string SceneSerializer::EntitiesToJsonString(const Scene &scene, const std::vector<EntityId> &entityIds, int indent) {
        return Json.Encode(EntitiesToJson(scene, entityIds), indent);
    }

    bool SceneSerializer::PasteEntitiesFromJson(
        const Gorgon::Encoding::JSON::Value &root,
        Scene &targetScene,
        std::vector<EntityId> *outRootIds,
        float offsetX,
        float offsetY,
        std::string *outError,
        std::vector<EntityId> *outAllPastedIds
    ) {
        try {
            if(!root.IsObject()) {
                setError(outError, "Clipboard JSON root must be an object");
                return false;
            }
            if(!root.Has("schemaVersion") || !root["schemaVersion"].IsNumber()) {
                setError(outError, "Missing or invalid 'schemaVersion' field");
                return false;
            }
            const int version = static_cast<int>(root["schemaVersion"].Get<double>());
            if(version != Scene::CurrentSchemaVersion) {
                setError(outError, "Unsupported schemaVersion: " + std::to_string(version));
                return false;
            }
            if(!root.Has("entities") || !root["entities"].IsArray()) {
                setError(outError, "Clipboard JSON missing 'entities' array");
                return false;
            }

            const auto &entitiesVal = root["entities"];
            const int count = entitiesVal.GetCount();
            if(count <= 0) {
                setError(outError, "Clipboard contains no entities");
                return false;
            }

            std::vector<Entity> parsed;
            parsed.reserve(static_cast<std::size_t>(count));
            std::unordered_map<EntityId, std::size_t> byOldId;
            byOldId.reserve(static_cast<std::size_t>(count));

            for(int i = 0; i < count; ++i) {
                Entity e;
                if(!entityFromJson(entitiesVal[i], i, e, outError)) {
                    return false;
                }
                if(!byOldId.emplace(e.id, parsed.size()).second) {
                    setError(outError, "Duplicate entity id in clipboard: " + std::to_string(e.id));
                    return false;
                }
                parsed.push_back(std::move(e));
            }

            // Validate parent links and reject cycles before mutating targetScene.
            for(const auto &e : parsed) {
                if(e.parentId == InvalidEntityId) {
                    continue;
                }
                if(e.parentId == e.id) {
                    setError(outError, "Clipboard entity " + std::to_string(e.id) + " cannot be its own parent");
                    return false;
                }
                if(byOldId.find(e.parentId) == byOldId.end()) {
                    setError(outError, "Clipboard entity " + std::to_string(e.id) + " references missing parentId " + std::to_string(e.parentId));
                    return false;
                }
                std::unordered_set<EntityId> visited;
                EntityId cur = e.id;
                while(cur != InvalidEntityId) {
                    if(!visited.insert(cur).second) {
                        setError(outError, "Cycle detected in clipboard hierarchy involving entity " + std::to_string(e.id));
                        return false;
                    }
                    auto it = byOldId.find(cur);
                    cur = (it != byOldId.end()) ? parsed[it->second].parentId : InvalidEntityId;
                }
            }

            std::unordered_set<std::string> usedNames;
            usedNames.reserve(targetScene.GetEntityCount() + parsed.size());
            for(const auto &existing : targetScene.GetEntities()) {
                usedNames.insert(existing.name);
            }

            std::unordered_map<EntityId, EntityId> idMap;
            idMap.reserve(parsed.size());
            std::vector<EntityId> pastedRoots;
            std::vector<EntityId> allPasted;
            allPasted.reserve(parsed.size());

            for(const auto &src : parsed) {
                const std::string uniqueName = makeUniqueEntityName(src.name, usedNames);
                Entity &created = targetScene.CreateEntity(uniqueName, InvalidEntityId);
                created.visible = src.visible;
                created.locked = src.locked;
                created.zOrder = src.zOrder;
                created.transform = src.transform;
                created.sprite = src.sprite;
                created.collider = src.collider;
                created.script = src.script;

                if(src.parentId == InvalidEntityId) {
                    created.transform.x += offsetX;
                    created.transform.y += offsetY;
                    pastedRoots.push_back(created.id);
                }
                idMap[src.id] = created.id;
                allPasted.push_back(created.id);
            }

            // Remap internal parentIds now that every pasted entity has its new ID.
            for(const auto &src : parsed) {
                if(src.parentId == InvalidEntityId) {
                    continue;
                }
                Entity *dst = targetScene.FindEntity(idMap[src.id]);
                if(dst) {
                    dst->parentId = idMap[src.parentId];
                }
            }

            if(outRootIds) {
                *outRootIds = std::move(pastedRoots);
            }
            if(outAllPastedIds) {
                *outAllPastedIds = std::move(allPasted);
            }
            return true;
        } catch(const JSON::Error &err) {
            setError(outError, err.what());
            return false;
        } catch(const std::exception &err) {
            setError(outError, err.what());
            return false;
        }
    }

    bool SceneSerializer::PasteEntitiesFromJsonString(
        const std::string &jsonText,
        Scene &targetScene,
        std::vector<EntityId> *outRootIds,
        float offsetX,
        float offsetY,
        std::string *outError,
        std::vector<EntityId> *outAllPastedIds
    ) {
        try {
            JSON::Value val = Json.Parse(jsonText);
            return PasteEntitiesFromJson(val, targetScene, outRootIds, offsetX, offsetY, outError, outAllPastedIds);
        } catch(const JSON::Error &err) {
            setError(outError, err.what());
            return false;
        } catch(const std::exception &err) {
            setError(outError, err.what());
            return false;
        }
    }

} // namespace Gorgon::Editor
