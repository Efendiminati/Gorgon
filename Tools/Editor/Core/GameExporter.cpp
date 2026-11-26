#include "GameExporter.h"

#include "SceneSerializer.h"
#include "ZipArchive.h"

#include <Gorgon/Encoding/JSON.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

namespace Gorgon :: Editor {

    namespace {
        using Gorgon::Encoding::JSON;
        using Gorgon::Encoding::Json;

        namespace fs = std::filesystem;

        /// One referenced file: the path it takes inside the bundle and where it lives on disk.
        struct CollectedFile {
            std::string bundlePath;
            std::string sourcePath;
        };

        std::string NormalizeRelative(const std::string &path) {
            if(path.empty())
                return {};

            std::string out = fs::path(path).lexically_normal().generic_string();
            while(out.size() > 1 && out.back() == '/')
                out.pop_back();
            if(out == ".")
                out.clear();
            return out;
        }

        std::string JoinRelative(const std::string &dir, const std::string &path) {
            if(path.empty())
                return {};

            const fs::path p(path);
            if(p.is_absolute() || dir.empty())
                return NormalizeRelative(path);

            return NormalizeRelative(dir + "/" + p.generic_string());
        }

        /// `dir` expressed relative to `base` ("" when they are the same directory).
        std::string RelativeDirectory(const std::string &base, const std::string &dir) {
            if(base.empty() || dir.empty())
                return {};

            std::error_code ec;
            const fs::path rel = fs::relative(fs::path(dir), fs::path(base), ec);
            if(ec)
                return {};

            std::string out = rel.generic_string();
            if(out == ".")
                out.clear();
            return out;
        }

        /// Directory holding the running executable. Falls back to the working directory when the
        /// platform does not expose it (the caller only uses it as a search hint).
        std::string ExecutableDirectory() {
#ifdef _WIN32
            char buffer[MAX_PATH * 4] = {0};
            const DWORD length = ::GetModuleFileNameA(nullptr, buffer, DWORD(sizeof(buffer)));
            if(length > 0 && length < DWORD(sizeof(buffer))) {
                return fs::path(std::string(buffer, length)).parent_path().generic_string();
            }
#elif defined(__linux__)
            std::error_code symEc;
            const fs::path exePath = fs::read_symlink("/proc/self/exe", symEc);
            if(!symEc && !exePath.empty()) {
                return exePath.parent_path().generic_string();
            }
#endif
            std::error_code ec;
            return fs::current_path(ec).generic_string();
        }

        bool FileExists(const std::string &path) {
            if(path.empty())
                return false;
            std::error_code ec;
            return fs::is_regular_file(fs::path(path), ec);
        }

        class Collector {
        public:
            Collector(const std::string &baseDir, std::vector<std::string> &warnings)
                : baseDir_(baseDir), warnings_(warnings) { }

            void add(const std::string &raw, const std::string &scopeDir) {
                if(raw.empty())
                    return;

                // A prefab's own asset paths are usually stored the same way a scene stores them
                // (relative to the scene folder), so the scene-relative spelling wins. Only when
                // that file does not exist does the prefab's own folder get a say.
                std::string scope = scopeDir;
                const fs::path rawPath(raw);
                if(!rawPath.is_absolute() && !scope.empty()) {
                    const std::string sceneRelative = baseDir_.empty()
                        ? NormalizeRelative(raw)
                        : NormalizeRelative(baseDir_ + "/" + raw);
                    const std::string scopedRelative = baseDir_.empty()
                        ? JoinRelative(scope, raw)
                        : NormalizeRelative(baseDir_ + "/" + scope + "/" + raw);

                    if(FileExists(sceneRelative) || !FileExists(scopedRelative))
                        scope.clear();
                }

                std::string bundlePath;
                if(rawPath.is_absolute()) {
                    // Absolute reference: keep the file's name, preserving any directory it sits
                    // in relative to the scene folder so the bundle stays self-contained.
                    const std::string absoluteScope = RelativeDirectory(
                        baseDir_, rawPath.parent_path().generic_string());
                    bundlePath = JoinRelative(absoluteScope, rawPath.filename().generic_string());
                } else {
                    bundlePath = JoinRelative(scope, raw);
                }

                if(!bundlePath.empty())
                    addResolved(bundlePath,
                                baseDir_.empty()
                                    ? bundlePath
                                    : NormalizeRelative(baseDir_ + "/" + bundlePath));
            }

            /// Recursively scans a `.gprefab.json` referenced by the scene (or by another prefab).
            void addPrefab(const std::string &raw, const std::string &scopeDir) {
                if(raw.empty())
                    return;

                // Prefab references are stored scene-relative; a prefab living in a subfolder may
                // also store them relative to itself, so both spellings are tried.
                std::vector<std::string> candidates;
                const fs::path p(raw);
                if(p.is_absolute()) {
                    candidates.push_back(p.lexically_normal().generic_string());
                } else {
                    if(!baseDir_.empty())
                        candidates.push_back(NormalizeRelative(baseDir_ + "/" + raw));
                    if(!scopeDir.empty() && !baseDir_.empty())
                        candidates.push_back(NormalizeRelative(
                            baseDir_ + "/" + scopeDir + "/" + raw));
                    if(baseDir_.empty())
                        candidates.push_back(NormalizeRelative(raw));
                }

                std::string prefabPath;
                for(const auto &candidate : candidates) {
                    if(FileExists(candidate)) {
                        prefabPath = candidate;
                        break;
                    }
                }

                if(prefabPath.empty()) {
                    if(!candidates.empty())
                        warnings_.push_back("Prefab not found on disk: " + raw);
                    return;
                }

                if(!visitedPrefabs_.insert(prefabPath).second)
                    return;

                // The prefab file itself ships with the game: a scene keeps `prefabSource`, and
                // Scene.SpawnPrefab() loads it at runtime.
                {
                    const fs::path prefabFsPath(prefabPath);
                    const std::string scope = baseDir_.empty()
                        ? std::string()
                        : RelativeDirectory(baseDir_, prefabFsPath.parent_path().generic_string());
                    const std::string bundlePath =
                        JoinRelative(scope, prefabFsPath.filename().generic_string());
                    if(!bundlePath.empty())
                        addResolved(bundlePath, prefabPath);
                }

                PrefabData prefab;
                std::string error;
                if(!SceneSerializer::LoadPrefabFromFile(prefabPath, prefab, &error)) {
                    warnings_.push_back("Prefab could not be read: " + raw +
                                        (error.empty() ? std::string() : (": " + error)));
                    return;
                }

                const std::string prefabDir = fs::path(prefabPath).parent_path().generic_string();
                const std::string scope = RelativeDirectory(baseDir_, prefabDir);

                for(const auto &entity : prefab.entities) {
                    scanEntity(entity, scope);
                }
            }

            void scanEntity(const Entity &entity, const std::string &scopeDir) {
                if(entity.sprite.has_value())
                    add(entity.sprite->asset, scopeDir);
                if(entity.spriteAnim.has_value())
                    add(entity.spriteAnim->asset, scopeDir);
                if(entity.tilemap.has_value())
                    add(entity.tilemap->tilesetAsset, scopeDir);
                if(entity.audio.has_value())
                    add(entity.audio->asset, scopeDir);
                if(entity.script.has_value())
                    add(entity.script->path, scopeDir);
                if(!entity.prefabSource.empty())
                    addPrefabClean(entity.prefabSource, scopeDir);
            }

            const std::vector<CollectedFile> &files() const { return files_; }

        private:
            /// Records one file once, keeping the first bundle path that claimed it.
            void addResolved(const std::string &bundlePath, const std::string &sourcePath) {
                if(bundlePath.empty() || !seen_.insert(bundlePath).second)
                    return;

                files_.push_back({bundlePath, sourcePath});
            }

            /// The prefab a prefab's own entity points at may itself be scene-relative; both the
            /// outer scene scope and the prefab's own scope are attempted by addPrefab().
            void addPrefabClean(const std::string &raw, const std::string &scopeDir) {
                if(raw.empty())
                    return;

                std::string candidate = raw;
                if(!fs::path(raw).is_absolute() && !scopeDir.empty()) {
                    // A nested prefab link is usually stored relative to the scene; only fall back
                    // to the prefab's own folder when the scene-relative form does not exist.
                    const std::string sceneRelative = baseDir_.empty()
                        ? NormalizeRelative(raw)
                        : NormalizeRelative(baseDir_ + "/" + raw);
                    if(!FileExists(sceneRelative))
                        candidate = JoinRelative(scopeDir, raw);
                }

                addPrefab(candidate, scopeDir);
            }

            const std::string &baseDir_;
            std::vector<std::string> &warnings_;
            std::set<std::string> seen_;
            std::set<std::string> visitedPrefabs_;
            std::vector<CollectedFile> files_;
        };

        std::vector<CollectedFile> CollectFiles(const Scene &scene, const std::string &sceneBaseDir,
                                                std::vector<std::string> &warnings) {
            Collector collector(sceneBaseDir, warnings);
            for(const auto &entity : scene.GetEntities()) {
                collector.scanEntity(entity, std::string());
            }

            auto files = collector.files();
            std::sort(files.begin(), files.end(),
                      [](const CollectedFile &a, const CollectedFile &b) {
                          return a.bundlePath < b.bundlePath;
                      });
            return files;
        }

        bool CopyFile(const std::string &from, const std::string &to, std::string *outError) {
            std::error_code ec;
            const fs::path target(to);
            if(target.has_parent_path())
                fs::create_directories(target.parent_path(), ec);

            ec.clear();
            if(!fs::copy_file(fs::path(from), target, fs::copy_options::overwrite_existing, ec)) {
                if(outError)
                    *outError = "Could not copy '" + from + "' to '" + to + "'" +
                                (ec ? ": " + ec.message() : std::string());
                return false;
            }
            return true;
        }

        std::string GetString(const JSON::Value &obj, const std::string &key,
                              const std::string &defVal) {
            if(!obj.Has(key) || !obj[key].IsString())
                return defVal;
            return obj[key].Get<std::string>();
        }

        int GetInt(const JSON::Value &obj, const std::string &key, int defVal) {
            if(!obj.Has(key) || !obj[key].IsNumber())
                return defVal;
            return static_cast<int>(obj[key].Get<double>());
        }
    } // namespace

    // ---- Manifest ------------------------------------------------------------

    std::string GameExporter::ManifestToJsonString(const GameManifest &manifest, int indent) {
        JSON::Value root;
        root.Set("schemaVersion", manifest.schemaVersion);
        root.Set("title", manifest.title);
        root.Set("width", manifest.width);
        root.Set("height", manifest.height);
        root.Set("startScene", manifest.startScene);
        return Json.Encode(root, indent);
    }

    bool GameExporter::LoadManifestFromJsonString(const std::string &jsonText,
                                                  GameManifest &outManifest,
                                                  std::string *outError) {
        try {
            const JSON::Value root = Json.Parse(jsonText);
            if(!root.IsObject()) {
                if(outError) *outError = "game.json root must be an object";
                return false;
            }

            GameManifest manifest;
            if(root.Has("schemaVersion")) {
                if(!root["schemaVersion"].IsNumber()) {
                    if(outError) *outError = "game.json 'schemaVersion' must be a number";
                    return false;
                }
                manifest.schemaVersion = GetInt(root, "schemaVersion", manifest.schemaVersion);
                if(manifest.schemaVersion < 1) {
                    if(outError) *outError = "Unsupported game.json schemaVersion: " +
                                             std::to_string(manifest.schemaVersion);
                    return false;
                }
            }

            manifest.title = GetString(root, "title", manifest.title);
            manifest.width = GetInt(root, "width", manifest.width);
            manifest.height = GetInt(root, "height", manifest.height);

            if(!root.Has("startScene") || !root["startScene"].IsString() ||
               root["startScene"].Get<std::string>().empty()) {
                if(outError) *outError = "game.json is missing 'startScene'";
                return false;
            }
            manifest.startScene = root["startScene"].Get<std::string>();

            if(manifest.width <= 0 || manifest.height <= 0) {
                if(outError) *outError = "game.json has an invalid window size";
                return false;
            }

            outManifest = manifest;
            return true;
        } catch(const JSON::Error &err) {
            if(outError) *outError = err.what();
            return false;
        } catch(const std::exception &err) {
            if(outError) *outError = err.what();
            return false;
        }
    }

    bool GameExporter::SaveManifestToFile(const GameManifest &manifest, const std::string &filePath,
                                          std::string *outError) {
        if(filePath.empty()) {
            if(outError) *outError = "No game.json path given";
            return false;
        }

        try {
            std::error_code ec;
            const fs::path path(filePath);
            if(path.has_parent_path())
                fs::create_directories(path.parent_path(), ec);

            std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
            if(!out) {
                if(outError) *outError = "Could not write " + filePath;
                return false;
            }

            out << ManifestToJsonString(manifest, 2) << "\n";
            out.flush();
            if(!out) {
                if(outError) *outError = "Could not write " + filePath;
                return false;
            }
            return true;
        } catch(const std::exception &err) {
            if(outError) *outError = err.what();
            return false;
        }
    }

    bool GameExporter::LoadManifestFromFile(const std::string &filePath, GameManifest &outManifest,
                                            std::string *outError) {
        try {
            std::ifstream in(filePath, std::ios::binary);
            if(!in) {
                if(outError) *outError = "Could not open " + filePath;
                return false;
            }

            std::ostringstream ss;
            ss << in.rdbuf();
            if(in.bad()) {
                if(outError) *outError = "Could not read " + filePath;
                return false;
            }

            return LoadManifestFromJsonString(ss.str(), outManifest, outError);
        } catch(const std::exception &err) {
            if(outError) *outError = err.what();
            return false;
        }
    }

    // ---- Discovery -----------------------------------------------------------

    std::string GameExporter::FindPlayerBinary(const std::string &searchDir) {
        std::vector<std::string> roots;
        if(!searchDir.empty())
            roots.push_back(searchDir);
        else
            roots.push_back(ExecutableDirectory());

        const char *names[] = {"GorgonPlayer.exe", "GorgonPlayer"};
        const char *subDirs[] = {"", "Bin/Release/", "Bin/Debug/", "Bin/", "Release/", "Debug/"};

        for(const auto &root : roots) {
            // Walk up a few levels so a search next to the editor still finds the build output.
            std::string dir = root;
            for(int level = 0; level < 6 && !dir.empty(); ++level) {
                for(const char *name : names) {
                    for(const char *sub : subDirs) {
                        const std::string candidate =
                            NormalizeRelative(dir + "/" + sub + name);
                        if(FileExists(candidate))
                            return candidate;
                    }
                }

                const fs::path parent = fs::path(dir).parent_path();
                const std::string next = parent.generic_string();
                if(next == dir)
                    break;
                dir = next;
            }
        }

        return {};
    }

    // ---- Asset collection ----------------------------------------------------

    std::vector<std::string> GameExporter::CollectReferencedAssets(const Scene &scene,
                                                                  const std::string &sceneBaseDir) {
        std::vector<std::string> warnings;
        const auto files = CollectFiles(scene, sceneBaseDir, warnings);

        std::vector<std::string> out;
        out.reserve(files.size());
        for(const auto &file : files)
            out.push_back(file.bundlePath);

        return out;
    }

    // ---- Export --------------------------------------------------------------

    ExportResult GameExporter::Export(const Scene &scene, const ExportOptions &options) {
        ExportResult result;
        result.outputDir = options.outputDir;

        if(options.outputDir.empty()) {
            result.error = "No output directory given";
            return result;
        }

        try {
            std::error_code ec;
            const fs::path outputDir(options.outputDir);
            fs::create_directories(outputDir, ec);
            if(!fs::is_directory(outputDir, ec)) {
                result.error = "Could not create " + options.outputDir;
                return result;
            }

            const std::string startScene = options.startSceneFile.empty()
                ? std::string("scene.gscene.json")
                : NormalizeRelative(options.startSceneFile);

            // 1. The scene itself, with the same relative asset layout the project folder had.
            const std::string scenePath =
                NormalizeRelative(options.outputDir + "/" + startScene);
            std::string error;
            if(!SceneSerializer::SaveToFile(scene, scenePath, &error)) {
                result.error = error.empty() ? ("Could not write " + scenePath) : error;
                return result;
            }
            result.scenePath = scenePath;

            // 2. game.json manifest.
            GameManifest manifest;
            manifest.title = options.title;
            if(manifest.title.empty())
                manifest.title = scene.name.empty() ? std::string("Gorgon Game") : scene.name;
            manifest.width = options.width > 0 ? options.width : 1280;
            manifest.height = options.height > 0 ? options.height : 720;
            manifest.startScene = startScene;

            const std::string manifestPath =
                NormalizeRelative(options.outputDir + "/game.json");
            if(!SaveManifestToFile(manifest, manifestPath, &error)) {
                result.error = error.empty() ? ("Could not write " + manifestPath) : error;
                return result;
            }
            result.manifestPath = manifestPath;

            // 3. Every referenced asset, copied with its bundle-relative path preserved.
            std::vector<std::string> warnings;
            const auto files = CollectFiles(scene, options.sceneBaseDir, warnings);
            result.warnings.insert(result.warnings.end(), warnings.begin(), warnings.end());

            for(const auto &file : files) {
                result.collectedAssets.push_back(file.bundlePath);

                if(!FileExists(file.sourcePath)) {
                    result.missingAssets.push_back(file.bundlePath);
                    continue;
                }

                const std::string target =
                    NormalizeRelative(options.outputDir + "/" + file.bundlePath);
                if(CopyFile(file.sourcePath, target, &error))
                    result.copiedAssets.push_back(file.bundlePath);
                else
                    result.warnings.push_back(error);
            }

            // 4. The player executable plus the runtime DLLs that sit beside it.
            std::string player = options.playerBinaryPath;
            if(player.empty())
                player = FindPlayerBinary(ExecutableDirectory());

            if(player.empty() || !FileExists(player)) {
                result.warnings.push_back(
                    "GorgonPlayer binary not found; the bundle has no executable yet");
            } else {
                const std::string playerTarget = NormalizeRelative(
                    options.outputDir + "/" + fs::path(player).filename().generic_string());
                if(CopyFile(player, playerTarget, &error)) {
#ifndef _WIN32
                    std::error_code permEc;
                    fs::permissions(
                        fs::path(playerTarget),
                        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add, permEc);
#endif
                    result.playerExecutablePath = playerTarget;
                } else {
                    result.warnings.push_back(error);
                }

                if(options.copyRuntimeDlls) {
                    const fs::path playerDir = fs::path(player).parent_path();
                    std::error_code iterEc;
                    for(fs::directory_iterator it(playerDir, iterEc), end; !iterEc && it != end;
                        it.increment(iterEc)) {
                        std::error_code typeEc;
                        if(!it->is_regular_file(typeEc))
                            continue;

                        std::string extension = it->path().extension().generic_string();
                        std::transform(extension.begin(), extension.end(), extension.begin(),
                                       [](unsigned char c) { return char(std::tolower(c)); });
                        if(extension != ".dll")
                            continue;

                        const std::string dllName = it->path().filename().generic_string();
                        const std::string dllTarget =
                            NormalizeRelative(options.outputDir + "/" + dllName);
                        if(CopyFile(it->path().generic_string(), dllTarget, &error))
                            result.copiedRuntimeDlls.push_back(dllName);
                        else
                            result.warnings.push_back(error);
                    }
                }
            }

            std::sort(result.collectedAssets.begin(), result.collectedAssets.end());
            std::sort(result.copiedAssets.begin(), result.copiedAssets.end());
            std::sort(result.missingAssets.begin(), result.missingAssets.end());
            std::sort(result.copiedRuntimeDlls.begin(), result.copiedRuntimeDlls.end());

            // 5. One file to hand over. The folder stays exactly as it was, so an export that
            // cannot zip (or asks not to) is still a complete, runnable bundle.
            if(options.makeArchive) {
                const std::string zipPath = ArchivePathFor(options.outputDir);
                std::string archiveError;
                std::uint64_t archiveBytes = 0;
                std::size_t archiveFiles = 0;
                if(zipPath.empty()) {
                    result.warnings.push_back(
                        "The bundle folder has no name to derive an archive path from");
                } else if(PackBundle(result, zipPath, &archiveError, &archiveBytes,
                                     &archiveFiles)) {
                    result.archivePath = NormalizeRelative(zipPath);
                    result.archiveBytes = archiveBytes;
                    result.archiveFiles = archiveFiles;
                } else {
                    result.warnings.push_back(
                        "The bundle was exported, but the zip archive was not written: " +
                        archiveError);
                }
            }

            result.ok = true;
            return result;
        } catch(const std::exception &err) {
            result.ok = false;
            result.error = err.what();
            return result;
        } catch(...) {
            result.ok = false;
            result.error = "Export failed with an unknown error";
            return result;
        }
    }

    // ---- Packaging -----------------------------------------------------------

    std::string GameExporter::ArchivePathFor(const std::string &outputDir) {
        if(outputDir.empty())
            return {};

        fs::path dir = fs::path(outputDir).lexically_normal();
        if(!dir.has_filename())
            dir = dir.parent_path();

        const std::string name = dir.filename().generic_string();
        if(name.empty() || name == "." || name == "..")
            return {};

        return (dir.parent_path() / (name + ".zip")).generic_string();
    }

    bool GameExporter::PackBundle(const ExportResult &bundle, const std::string &zipPath,
                                  std::string *outError, std::uint64_t *outArchiveBytes,
                                  std::size_t *outFileCount) {
        if(outArchiveBytes)
            *outArchiveBytes = 0;
        if(outFileCount)
            *outFileCount = 0;

        if(zipPath.empty()) {
            if(outError)
                *outError = "No archive path given";
            return false;
        }

        if(bundle.outputDir.empty()) {
            if(outError)
                *outError = "The bundle has no folder to pack";
            return false;
        }

        const fs::path root = fs::path(bundle.outputDir).lexically_normal();
        std::error_code ec;
        if(!fs::is_directory(root, ec)) {
            if(outError)
                *outError = "The bundle folder does not exist: " + root.generic_string();
            return false;
        }

        // Everything in the folder is packed — what the exporter wrote plus anything the user
        // dropped in beside it — in sorted order so the same bundle always produces the same
        // archive.
        std::vector<std::string> relativePaths;
        for(fs::recursive_directory_iterator it(
                root, fs::directory_options::skip_permission_denied, ec),
            end;
            !ec && it != end; it.increment(ec)) {
            std::error_code fileEc;
            if(!it->is_regular_file(fileEc))
                continue;

            // Lexical only: the archive path must be the folder's own spelling, never a resolved
            // absolute path.
            const fs::path relative = it->path().lexically_relative(root);
            if(relative.empty() || relative == ".")
                continue;

            relativePaths.push_back(relative.generic_string());
        }

        if(ec) {
            if(outError)
                *outError = "Could not read the bundle folder: " + ec.message();
            return false;
        }

        std::sort(relativePaths.begin(), relativePaths.end());

        // A previous archive living in the bundle folder must not be packed into this one.
        const std::string archiveName = fs::path(zipPath).filename().generic_string();

        ZipArchive archive;
        for(const auto &relative : relativePaths) {
            if(relative == archiveName)
                continue;

            std::string error;
            const std::string source = (root / fs::path(relative)).generic_string();
            if(!archive.AddFile(relative, source, &error)) {
                if(outError)
                    *outError = error;
                return false;
            }
        }

        std::uint64_t bytes = 0;
        std::size_t entries = 0;
        std::string error;
        if(!archive.Save(zipPath, &bytes, &entries, &error)) {
            if(outError)
                *outError = error;
            return false;
        }

        if(outArchiveBytes)
            *outArchiveBytes = bytes;
        if(outFileCount)
            *outFileCount = entries;
        return true;
    }

} // namespace Gorgon::Editor
