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
                // Written only when set, so scenes that predate these flags stay byte identical.
                if(e.collider->isTrigger)
                    colObj.Set("isTrigger", true);
                if(e.collider->isStatic)
                    colObj.Set("isStatic", true);
                eObj.Set("collider", std::move(colObj));
            }

            if(e.script.has_value()) {
                JSON::Value scObj;
                scObj.Set("path", e.script->path);
                eObj.Set("script", std::move(scObj));
            }

            if(e.spriteAnim.has_value()) {
                const auto &a = *e.spriteAnim;
                JSON::Value o;
                o.Set("asset", a.asset);
                o.Set("frameW", a.frameW);
                o.Set("frameH", a.frameH);
                o.Set("frameCount", a.frameCount);
                o.Set("fps", static_cast<double>(a.fps));
                o.Set("loop", a.loop);
                o.Set("playing", a.playing);
                o.Set("currentFrame", a.currentFrame);
                o.Set("tint", colorToJson(a.tint));
                eObj.Set("spriteAnim", std::move(o));
            }

            if(e.text.has_value()) {
                const auto &t = *e.text;
                JSON::Value o;
                o.Set("text", t.text);
                o.Set("color", colorToJson(t.color));
                o.Set("fontSize", static_cast<double>(t.fontSize));
                o.Set("style", t.style);
                o.Set("align", TextAlignToString(t.align));
                eObj.Set("text", std::move(o));
            }

            if(e.audio.has_value()) {
                const auto &a = *e.audio;
                JSON::Value o;
                o.Set("asset", a.asset);
                o.Set("volume", static_cast<double>(a.volume));
                o.Set("loop", a.loop);
                o.Set("playOnStart", a.playOnStart);
                eObj.Set("audio", std::move(o));
            }

            if(e.camera.has_value()) {
                const auto &c = *e.camera;
                JSON::Value o;
                o.Set("active", c.active);
                o.Set("zoom", static_cast<double>(c.zoom));
                o.Set("followTarget", static_cast<int>(c.followTarget));
                eObj.Set("camera", std::move(o));
            }

            if(e.tilemap.has_value()) {
                const auto &t = *e.tilemap;
                JSON::Value o;
                o.Set("tileW", static_cast<double>(t.tileW));
                o.Set("tileH", static_cast<double>(t.tileH));
                o.Set("cols", t.cols);
                o.Set("rows", t.rows);
                o.Set("tilesetCols", t.tilesetCols);
                o.Set("tilesetAsset", t.tilesetAsset);
                JSON::Array tilesArr;
                tilesArr.reserve(t.tiles.size());
                for(int index : t.tiles)
                    tilesArr.emplace_back(index);
                o.Set("tiles", JSON::Value(std::move(tilesArr)));
                eObj.Set("tilemap", std::move(o));
            }

            if(e.particles.has_value()) {
                const auto &p = *e.particles;
                JSON::Value o;
                o.Set("emitRate", static_cast<double>(p.emitRate));
                o.Set("lifetime", static_cast<double>(p.lifetime));
                o.Set("speed", static_cast<double>(p.speed));
                o.Set("startColor", colorToJson(p.startColor));
                o.Set("endColor", colorToJson(p.endColor));
                o.Set("size", static_cast<double>(p.size));
                o.Set("active", p.active);
                eObj.Set("particles", std::move(o));
            }

            // Written only when non-empty, so scenes that carry neither stay byte identical.
            if(!e.tags.empty()) {
                JSON::Array tagArr;
                tagArr.reserve(e.tags.size());
                for(const auto &tag : e.tags)
                    tagArr.emplace_back(tag);
                eObj.Set("tags", JSON::Value(std::move(tagArr)));
            }

            if(!e.properties.empty()) {
                JSON::Object props;
                for(const auto &entry : e.properties)
                    props.emplace(entry.first, JSON::Value(entry.second));
                eObj.Set("properties", JSON::Value(std::move(props)));
            }

            if(!e.prefabSource.empty()) {
                eObj.Set("prefabSource", e.prefabSource);
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
                comp.isTrigger = getBoolField(col, "isTrigger", false);
                comp.isStatic = getBoolField(col, "isStatic", false);
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

            if(eObj.Has("tags") && !eObj["tags"].IsNull()) {
                const auto &tagsVal = eObj["tags"];
                if(!tagsVal.IsArray()) {
                    setError(outError, "Entity " + std::to_string(rawId) + " 'tags' must be an array");
                    return false;
                }
                for(int i = 0; i < tagsVal.GetCount(); ++i) {
                    if(!tagsVal[i].IsString()) {
                        setError(outError, "Entity " + std::to_string(rawId) + " has a non-string tag");
                        return false;
                    }
                    e.tags.push_back(tagsVal[i].Get<std::string>());
                }
            }

            if(eObj.Has("properties") && !eObj["properties"].IsNull()) {
                const auto &propsVal = eObj["properties"];
                if(!propsVal.IsObject()) {
                    setError(outError, "Entity " + std::to_string(rawId) + " 'properties' must be an object");
                    return false;
                }
                const auto obj = propsVal.Get<JSON::Object>();
                for(const auto &entry : obj) {
                    if(!entry.second.IsString()) {
                        setError(outError, "Entity " + std::to_string(rawId) + " property '" + entry.first +
                                           "' must be a string");
                        return false;
                    }
                    e.properties[entry.first] = entry.second.Get<std::string>();
                }
            }

            if(eObj.Has("spriteAnim") && !eObj["spriteAnim"].IsNull()) {
                const auto &o = eObj["spriteAnim"];
                if(!o.IsObject()) {
                    setError(outError, "Entity " + std::to_string(rawId) + " 'spriteAnim' must be an object");
                    return false;
                }
                SpriteAnimComponent comp;
                comp.asset = getStringField(o, "asset", "");
                comp.frameW = getIntField(o, "frameW", 32);
                comp.frameH = getIntField(o, "frameH", 32);
                comp.frameCount = std::max(1, getIntField(o, "frameCount", 1));
                comp.fps = std::max(0.0f, getFloatField(o, "fps", 12.0f));
                comp.loop = getBoolField(o, "loop", true);
                comp.playing = getBoolField(o, "playing", true);
                comp.currentFrame = getIntField(o, "currentFrame", 0);
                if(comp.currentFrame < 0 || comp.currentFrame >= comp.frameCount)
                    comp.currentFrame = 0;
                if(o.Has("tint") && !o["tint"].IsNull()) {
                    if(!colorFromJson(o["tint"], comp.tint, outError, "spriteAnim.tint"))
                        return false;
                }
                e.spriteAnim = comp;
            }

            if(eObj.Has("text") && !eObj["text"].IsNull()) {
                const auto &o = eObj["text"];
                if(!o.IsObject()) {
                    setError(outError, "Entity " + std::to_string(rawId) + " 'text' must be an object");
                    return false;
                }
                TextComponent comp;
                comp.text = getStringField(o, "text", "");
                comp.fontSize = std::max(1.0f, getFloatField(o, "fontSize", 16.0f));
                comp.style = getStringField(o, "style", "regular");
                if(o.Has("color") && !o["color"].IsNull()) {
                    if(!colorFromJson(o["color"], comp.color, outError, "text.color"))
                        return false;
                }
                const std::string align = getStringField(o, "align", "left");
                if(!TextAlignFromString(align, comp.align)) {
                    setError(outError, "Unknown text alignment: '" + align + "'");
                    return false;
                }
                e.text = comp;
            }

            if(eObj.Has("audio") && !eObj["audio"].IsNull()) {
                const auto &o = eObj["audio"];
                if(!o.IsObject()) {
                    setError(outError, "Entity " + std::to_string(rawId) + " 'audio' must be an object");
                    return false;
                }
                AudioComponent comp;
                comp.asset = getStringField(o, "asset", "");
                comp.volume = std::clamp(getFloatField(o, "volume", 1.0f), 0.0f, 1.0f);
                comp.loop = getBoolField(o, "loop", false);
                comp.playOnStart = getBoolField(o, "playOnStart", true);
                e.audio = comp;
            }

            if(eObj.Has("camera") && !eObj["camera"].IsNull()) {
                const auto &o = eObj["camera"];
                if(!o.IsObject()) {
                    setError(outError, "Entity " + std::to_string(rawId) + " 'camera' must be an object");
                    return false;
                }
                CameraComponent comp;
                comp.active = getBoolField(o, "active", true);
                comp.zoom = std::max(0.01f, getFloatField(o, "zoom", 1.0f));
                const int follow = getIntField(o, "followTarget", 0);
                if(follow < 0) {
                    setError(outError, "Entity " + std::to_string(rawId) + " has negative camera followTarget");
                    return false;
                }
                comp.followTarget = static_cast<EntityId>(follow);
                e.camera = comp;
            }

            if(eObj.Has("tilemap") && !eObj["tilemap"].IsNull()) {
                const auto &o = eObj["tilemap"];
                if(!o.IsObject()) {
                    setError(outError, "Entity " + std::to_string(rawId) + " 'tilemap' must be an object");
                    return false;
                }
                TilemapComponent comp;
                comp.tileW = std::max(1.0f, getFloatField(o, "tileW", 32.0f));
                comp.tileH = std::max(1.0f, getFloatField(o, "tileH", 32.0f));
                comp.cols = std::max(0, getIntField(o, "cols", 0));
                comp.rows = std::max(0, getIntField(o, "rows", 0));
                comp.tilesetCols = std::max(1, getIntField(o, "tilesetCols", 8));
                comp.tilesetAsset = getStringField(o, "tilesetAsset", "");
                if(o.Has("tiles") && !o["tiles"].IsNull()) {
                    const auto &tilesVal = o["tiles"];
                    if(!tilesVal.IsArray()) {
                        setError(outError, "Entity " + std::to_string(rawId) + " 'tilemap.tiles' must be an array");
                        return false;
                    }
                    comp.tiles.reserve(static_cast<std::size_t>(tilesVal.GetCount()));
                    for(int i = 0; i < tilesVal.GetCount(); ++i) {
                        if(!tilesVal[i].IsNumber()) {
                            setError(outError, "Entity " + std::to_string(rawId) + " has a non-numeric tile index");
                            return false;
                        }
                        comp.tiles.push_back(static_cast<int>(tilesVal[i].Get<double>()));
                    }
                }
                e.tilemap = comp;
            }

            if(eObj.Has("particles") && !eObj["particles"].IsNull()) {
                const auto &o = eObj["particles"];
                if(!o.IsObject()) {
                    setError(outError, "Entity " + std::to_string(rawId) + " 'particles' must be an object");
                    return false;
                }
                ParticleComponent comp;
                comp.emitRate = std::max(0.0f, getFloatField(o, "emitRate", 20.0f));
                comp.lifetime = std::max(0.01f, getFloatField(o, "lifetime", 1.0f));
                comp.speed = getFloatField(o, "speed", 60.0f);
                comp.size = std::max(0.5f, getFloatField(o, "size", 4.0f));
                comp.active = getBoolField(o, "active", true);
                if(o.Has("startColor") && !o["startColor"].IsNull()) {
                    if(!colorFromJson(o["startColor"], comp.startColor, outError, "particles.startColor"))
                        return false;
                }
                if(o.Has("endColor") && !o["endColor"].IsNull()) {
                    if(!colorFromJson(o["endColor"], comp.endColor, outError, "particles.endColor"))
                        return false;
                }
                e.particles = comp;
            }

            e.prefabSource = getStringField(eObj, "prefabSource", "");

            return true;
        }

        /// Copies every optional component plus tags/properties from src onto dst, leaving the
        /// identity (id, name, parent, transform) alone. Used by paste/prefab paths so a new
        /// component never has to be added to four different copy loops.
        void copyComponentSet(Entity &dst, const Entity &src) {
            dst.tags = src.tags;
            dst.properties = src.properties;
            dst.sprite = src.sprite;
            dst.collider = src.collider;
            dst.script = src.script;
            dst.spriteAnim = src.spriteAnim;
            dst.text = src.text;
            dst.audio = src.audio;
            dst.camera = src.camera;
            dst.tilemap = src.tilemap;
            dst.particles = src.particles;
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
                copyComponentSet(created, src);
                created.prefabSource = src.prefabSource;

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

    // ---- Prefabs (.gprefab.json) --------------------------------------------

    Gorgon::Encoding::JSON::Value SceneSerializer::PrefabToJson(
        const Scene &scene,
        EntityId rootId,
        const std::string &prefabName
    ) {
        using Gorgon::Encoding::JSON;
        JSON::Value root;
        root.Set("schemaVersion", PrefabData::CurrentSchemaVersion);
        root.Set("type", std::string("gorgon.prefab"));

        const Entity *rootEntity = scene.FindEntity(rootId);
        if(!rootEntity) {
            root.Set("name", prefabName);
            root.Set("rootId", 0);
            JSON::Array emptyArr;
            root.Set("entities", JSON::Value(std::move(emptyArr)));
            return root;
        }

        const std::string resolvedName = prefabName.empty() ? rootEntity->name : prefabName;
        root.Set("name", resolvedName);

        // Collect root + descendants in DFS order so parents always precede children.
        std::vector<EntityId> orderedIds;
        std::unordered_set<EntityId> visited;
        std::function<void(EntityId)> collect = [&](EntityId id) {
            if(!visited.insert(id).second) return;
            if(!scene.HasEntity(id)) return;
            orderedIds.push_back(id);
            for(EntityId childId : scene.GetChildIds(id)) {
                collect(childId);
            }
        };
        collect(rootId);

        // Normalize IDs to 1..N so prefab files are self-contained and deterministic.
        std::unordered_map<EntityId, EntityId> localIdMap;
        localIdMap.reserve(orderedIds.size());
        EntityId nextLocalId = 1;
        for(EntityId oldId : orderedIds) {
            localIdMap[oldId] = nextLocalId++;
        }

        root.Set("rootId", static_cast<int>(localIdMap[rootId]));

        JSON::Array entitiesArr;
        entitiesArr.reserve(orderedIds.size());
        for(EntityId oldId : orderedIds) {
            const Entity *src = scene.FindEntity(oldId);
            if(!src) continue;

            Entity copy = *src;
            copy.id = localIdMap[oldId];
            EntityId mappedParent = InvalidEntityId;
            if(oldId == rootId) {
                copy.parentId = InvalidEntityId;
                copy.transform.x = 0.0f;
                copy.transform.y = 0.0f;
                copy.prefabSource.clear();
            } else {
                auto pit = localIdMap.find(src->parentId);
                mappedParent = (pit != localIdMap.end()) ? pit->second : InvalidEntityId;
                copy.parentId = mappedParent;
            }
            entitiesArr.push_back(entityToJson(copy, mappedParent));
        }
        root.Set("entities", std::move(entitiesArr));
        return root;
    }

    std::string SceneSerializer::PrefabToJsonString(
        const Scene &scene,
        EntityId rootId,
        const std::string &prefabName,
        int indent
    ) {
        return Json.Encode(PrefabToJson(scene, rootId, prefabName), indent);
    }

    bool SceneSerializer::SavePrefabToFile(
        const Scene &scene,
        EntityId rootId,
        const std::string &filePath,
        std::string *outError,
        int indent
    ) {
        const Entity *rootEntity = scene.FindEntity(rootId);
        if(!rootEntity) {
            setError(outError, "Cannot save prefab: entity #" + std::to_string(rootId) + " not found");
            return false;
        }
        try {
            const std::string text = PrefabToJsonString(scene, rootId, rootEntity->name, indent);
            std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
            if(!out) {
                setError(outError, "Failed to open prefab file for writing: " + filePath);
                return false;
            }
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            if(!out) {
                setError(outError, "Failed while writing prefab file: " + filePath);
                return false;
            }
            return true;
        } catch(const std::exception &err) {
            setError(outError, err.what());
            return false;
        }
    }

    bool SceneSerializer::LoadPrefabFromJson(
        const Gorgon::Encoding::JSON::Value &root,
        PrefabData &outPrefab,
        std::string *outError
    ) {
        try {
            if(!root.IsObject()) {
                setError(outError, "Prefab root must be a JSON object");
                return false;
            }

            const int version = getIntField(root, "schemaVersion", 1);
            if(version < 1 || version > PrefabData::CurrentSchemaVersion) {
                setError(outError, "Unsupported prefab schemaVersion: " + std::to_string(version));
                return false;
            }

            if(!root.Has("entities") || !root["entities"].IsArray()) {
                setError(outError, "Prefab JSON missing 'entities' array");
                return false;
            }

            const auto &arr = root["entities"];
            if(arr.GetCount() == 0) {
                setError(outError, "Prefab 'entities' array is empty");
                return false;
            }

            PrefabData parsed;
            parsed.schemaVersion = version;
            parsed.name = getStringField(root, "name", "");
            parsed.entities.reserve(static_cast<std::size_t>(arr.GetCount()));

            std::unordered_set<EntityId> seenIds;
            for(int i = 0; i < arr.GetCount(); ++i) {
                Entity e;
                if(!entityFromJson(arr[i], i, e, outError)) {
                    return false;
                }
                if(!seenIds.insert(e.id).second) {
                    setError(outError, "Duplicate entity id in prefab: " + std::to_string(e.id));
                    return false;
                }
                parsed.entities.push_back(std::move(e));
            }

            const int declaredRoot = getIntField(root, "rootId", 0);
            if(declaredRoot > 0) {
                parsed.rootId = static_cast<EntityId>(declaredRoot);
            } else {
                for(const auto &e : parsed.entities) {
                    if(e.parentId == InvalidEntityId) {
                        parsed.rootId = e.id;
                        break;
                    }
                }
            }

            if(parsed.rootId == InvalidEntityId || seenIds.find(parsed.rootId) == seenIds.end()) {
                setError(outError, "Prefab missing valid root entity");
                return false;
            }

            std::unordered_map<EntityId, EntityId> parentMap;
            parentMap.reserve(parsed.entities.size());
            for(auto &e : parsed.entities) {
                if(e.id == parsed.rootId) {
                    e.parentId = InvalidEntityId;
                } else if(e.parentId == InvalidEntityId || seenIds.find(e.parentId) == seenIds.end()) {
                    setError(outError, "Prefab entity " + std::to_string(e.id) + " has invalid parentId ");
                    return false;
                }
                parentMap[e.id] = e.parentId;
            }

            // Cycle check
            for(const auto &e : parsed.entities) {
                EntityId cur = e.parentId;
                std::size_t hops = 0;
                while(cur != InvalidEntityId) {
                    if(cur == e.id || ++hops > parsed.entities.size()) {
                        setError(outError, "Cycle detected in prefab hierarchy");
                        return false;
                    }
                    cur = parentMap[cur];
                }
            }

            if(parsed.name.empty()) {
                for(const auto &e : parsed.entities) {
                    if(e.id == parsed.rootId) {
                        parsed.name = e.name;
                        break;
                    }
                }
            }

            outPrefab = std::move(parsed);
            return true;
        } catch(const JSON::Error &err) {
            setError(outError, err.what());
            return false;
        } catch(const std::exception &err) {
            setError(outError, err.what());
            return false;
        }
    }

    bool SceneSerializer::LoadPrefabFromJsonString(
        const std::string &jsonText,
        PrefabData &outPrefab,
        std::string *outError
    ) {
        try {
            JSON::Value val = Json.Parse(jsonText);
            return LoadPrefabFromJson(val, outPrefab, outError);
        } catch(const JSON::Error &err) {
            setError(outError, err.what());
            return false;
        } catch(const std::exception &err) {
            setError(outError, err.what());
            return false;
        }
    }

    bool SceneSerializer::LoadPrefabFromFile(
        const std::string &filePath,
        PrefabData &outPrefab,
        std::string *outError
    ) {
        std::ifstream in(filePath, std::ios::binary);
        if(!in) {
            setError(outError, "Failed to open prefab file: " + filePath);
            return false;
        }
        std::ostringstream oss;
        oss << in.rdbuf();
        return LoadPrefabFromJsonString(oss.str(), outPrefab, outError);
    }

    bool SceneSerializer::InstantiatePrefab(
        const PrefabData &prefab,
        Scene &targetScene,
        float worldX,
        float worldY,
        const std::string &prefabSource,
        EntityId *outRootId,
        std::vector<EntityId> *outAllIds,
        std::string *outError
    ) {
        if(!prefab.IsValid()) {
            setError(outError, "Cannot instantiate invalid or empty prefab");
            return false;
        }

        std::unordered_set<std::string> usedNames;
        for(const auto &existing : targetScene.GetEntities()) {
            usedNames.insert(existing.name);
        }

        std::unordered_map<EntityId, EntityId> idMap;
        idMap.reserve(prefab.entities.size());
        std::vector<EntityId> createdIds;
        createdIds.reserve(prefab.entities.size());

        for(const auto &src : prefab.entities) {
            const bool isRoot = (src.id == prefab.rootId);
            const std::string desiredName = src.name.empty() ? (isRoot && !prefab.name.empty() ? prefab.name : std::string("Prefab")) : src.name;
            const std::string uniqueName = isRoot ? makeUniqueEntityName(desiredName, usedNames) : desiredName;
            usedNames.insert(uniqueName);

            Entity &created = targetScene.CreateEntity(uniqueName, InvalidEntityId);
            created.visible = src.visible;
            created.locked = src.locked;
            created.zOrder = src.zOrder;
            created.transform = src.transform;
            copyComponentSet(created, src);
            created.prefabSource = isRoot ? prefabSource : src.prefabSource;

            if(isRoot) {
                created.transform.x = worldX;
                created.transform.y = worldY;
            }

            idMap[src.id] = created.id;
            createdIds.push_back(created.id);
        }

        for(const auto &src : prefab.entities) {
            if(src.id == prefab.rootId || src.parentId == InvalidEntityId) {
                continue;
            }
            auto pit = idMap.find(src.parentId);
            if(pit == idMap.end()) continue;
            if(Entity *dst = targetScene.FindEntity(idMap[src.id])) {
                dst->parentId = pit->second;
            }
        }

        const EntityId newRootId = idMap[prefab.rootId];
        if(outRootId) {
            *outRootId = newRootId;
        }
        if(outAllIds) {
            *outAllIds = std::move(createdIds);
        }
        return true;
    }

    bool SceneSerializer::InstantiatePrefabFromFile(
        const std::string &filePath,
        Scene &targetScene,
        float worldX,
        float worldY,
        const std::string &prefabSource,
        EntityId *outRootId,
        std::vector<EntityId> *outAllIds,
        std::string *outError
    ) {
        PrefabData prefab;
        if(!LoadPrefabFromFile(filePath, prefab, outError)) {
            return false;
        }
        return InstantiatePrefab(prefab, targetScene, worldX, worldY, prefabSource, outRootId, outAllIds, outError);
    }

    bool SceneSerializer::RevertPrefabInstance(
        Scene &scene,
        EntityId instanceRootId,
        const PrefabData &prefab,
        std::string *outError
    ) {
        if(!prefab.IsValid()) {
            setError(outError, "Cannot revert from invalid prefab");
            return false;
        }
        Entity *instanceRoot = scene.FindEntity(instanceRootId);
        if(!instanceRoot) {
            setError(outError, "Prefab instance entity #" + std::to_string(instanceRootId) + " not found");
            return false;
        }

        const Entity *prefabRoot = nullptr;
        for(const auto &e : prefab.entities) {
            if(e.id == prefab.rootId) {
                prefabRoot = &e;
                break;
            }
        }
        if(!prefabRoot) {
            setError(outError, "Prefab root entity missing");
            return false;
        }

        // Preserve instance placement, parenting, and prefab link.
        const float keepX = instanceRoot->transform.x;
        const float keepY = instanceRoot->transform.y;
        const EntityId keepParent = instanceRoot->parentId;
        const std::string keepSource = instanceRoot->prefabSource;

        // Remove existing descendants of the instance.
        const std::vector<EntityId> directChildren = scene.GetChildIds(instanceRootId);
        for(EntityId childId : directChildren) {
            scene.RemoveEntity(childId, true);
        }

        // Re-fetch pointer after removals.
        instanceRoot = scene.FindEntity(instanceRootId);
        if(!instanceRoot) {
            setError(outError, "Prefab instance root lost during revert");
            return false;
        }

        instanceRoot->name = prefabRoot->name;
        instanceRoot->visible = prefabRoot->visible;
        instanceRoot->locked = prefabRoot->locked;
        instanceRoot->zOrder = prefabRoot->zOrder;
        instanceRoot->transform = prefabRoot->transform;
        instanceRoot->transform.x = keepX;
        instanceRoot->transform.y = keepY;
        instanceRoot->parentId = keepParent;
        instanceRoot->sprite = prefabRoot->sprite;
        instanceRoot->collider = prefabRoot->collider;
        instanceRoot->script = prefabRoot->script;
        copyComponentSet(*instanceRoot, *prefabRoot);
        instanceRoot->prefabSource = keepSource;

        // Recreate prefab descendants under instanceRootId.
        std::unordered_map<EntityId, EntityId> idMap;
        idMap[prefab.rootId] = instanceRootId;
        for(const auto &src : prefab.entities) {
            if(src.id == prefab.rootId) continue;
            Entity &created = scene.CreateEntity(src.name, InvalidEntityId);
            created.visible = src.visible;
            created.locked = src.locked;
            created.zOrder = src.zOrder;
            created.transform = src.transform;
            copyComponentSet(created, src);
            created.prefabSource = src.prefabSource;
            idMap[src.id] = created.id;
        }
        for(const auto &src : prefab.entities) {
            if(src.id == prefab.rootId) continue;
            auto pit = idMap.find(src.parentId);
            if(pit == idMap.end()) continue;
            if(Entity *dst = scene.FindEntity(idMap[src.id])) {
                dst->parentId = pit->second;
            }
        }

        return true;
    }

} // namespace Gorgon::Editor
