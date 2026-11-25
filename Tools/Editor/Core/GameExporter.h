#pragma once

#include "Scene.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Gorgon :: Editor {

    /// Manifest written alongside GorgonPlayer as `game.json`.
    struct GameManifest {
        int schemaVersion = 1;
        std::string title = "Gorgon Game";
        int width = 1280;
        int height = 720;
        /// Relative path to the starting `.gscene.json` file (e.g. `scene.gscene.json`).
        std::string startScene = "scene.gscene.json";

        bool operator==(const GameManifest &o) const {
            return schemaVersion == o.schemaVersion
                && title == o.title
                && width == o.width
                && height == o.height
                && startScene == o.startScene;
        }
        bool operator!=(const GameManifest &o) const { return !(*this == o); }
    };

    /// Options controlling standalone game export via GameExporter::Export().
    struct ExportOptions {
        /// Window title written into game.json. Defaults to the scene's name if empty.
        std::string title;
        /// Initial window dimensions written into game.json.
        int width = 1280;
        int height = 720;
        /// Filename for the exported scene inside outputDir (relative).
        std::string startSceneFile = "scene.gscene.json";
        /// Directory used to resolve relative asset/script/prefab paths in `scene`.
        std::string sceneBaseDir;
        /// Path to the prebuilt GorgonPlayer executable to copy into the bundle.
        /// If empty, export still writes game.json, scene, and assets, and records a warning.
        std::string playerBinaryPath;
        /// Destination directory for the standalone game bundle (created if missing).
        std::string outputDir;
        /// Copy sibling runtime DLLs (e.g. vulkan-1.dll) found next to playerBinaryPath.
        bool copyRuntimeDlls = true;
        /// Pack the finished bundle into `<outputDir>.zip` (default on). The folder itself is
        /// still written, so the editor keeps its folder export; the archive is a copy for handing
        /// over. Set to false for a folder-only export.
        bool makeArchive = true;
    };

    /// Structured outcome of a GameExporter::Export() call.
    struct ExportResult {
        bool ok = false;
        std::string error;
        std::string outputDir;
        std::string manifestPath;
        std::string scenePath;
        std::string playerExecutablePath;
        /// Relative asset paths collected from the scene (and recursively from prefabs).
        std::vector<std::string> collectedAssets;
        /// Relative paths of assets successfully copied into outputDir.
        std::vector<std::string> copiedAssets;
        /// Relative paths of assets referenced by the scene/prefabs that were missing on disk.
        std::vector<std::string> missingAssets;
        /// Filenames of sibling runtime DLLs copied beside the player executable.
        std::vector<std::string> copiedRuntimeDlls;
        /// Path of the zip archive written next to the bundle (empty when none was requested or
        /// the archive could not be written; see `warnings`).
        std::string archivePath;
        /// Size of the archive in bytes (0 when no archive was written).
        std::uint64_t archiveBytes = 0;
        /// Number of files packed into the archive (0 when no archive was written).
        std::size_t archiveFiles = 0;
        /// Non-fatal warnings encountered during export.
        std::vector<std::string> warnings;
    };

    /// Standalone game packager and `game.json` manifest codec.
    class GameExporter {
    public:
        /// Collects all unique, normalized relative asset paths referenced by `scene`:
        ///   - SpriteComponent::asset
        ///   - SpriteAnimComponent::asset
        ///   - TilemapComponent::tilesetAsset
        ///   - AudioComponent::asset
        ///   - ScriptComponent::path
        ///   - Entity::prefabSource (and recursively any assets/prefabs referenced inside
        ///     loaded `.gprefab.json` files resolved against `sceneBaseDir`).
        ///
        /// Results are sorted and deduplicated. Cycle-safe for nested prefabs.
        static std::vector<std::string> CollectReferencedAssets(
            const Scene &scene,
            const std::string &sceneBaseDir = {}
        );

        /// Serializes a GameManifest to pretty-printed JSON.
        static std::string ManifestToJsonString(const GameManifest &manifest, int indent = 2);

        /// Parses a GameManifest from a JSON string.
        static bool LoadManifestFromJsonString(
            const std::string &jsonText,
            GameManifest &outManifest,
            std::string *outError = nullptr
        );

        /// Writes `manifest` to `filePath` (typically `<outputDir>/game.json`).
        static bool SaveManifestToFile(
            const GameManifest &manifest,
            const std::string &filePath,
            std::string *outError = nullptr
        );

        /// Loads `game.json` from `filePath`.
        static bool LoadManifestFromFile(
            const std::string &filePath,
            GameManifest &outManifest,
            std::string *outError = nullptr
        );

        /// Locates a `GorgonPlayer` binary near `searchDir` (typically the directory containing
        /// `GorgonEditor.exe`). Returns empty string if not found.
        static std::string FindPlayerBinary(const std::string &searchDir);

        /// Exports `scene` into a self-contained runnable folder at `options.outputDir`:
        ///   1. Collects all referenced assets (recursively scanning prefabs).
        ///   2. Creates `outputDir` and writes `<outputDir>/<startSceneFile>`.
        ///   3. Writes `<outputDir>/game.json`.
        ///   4. Copies every found asset preserving its relative path under `outputDir`.
        ///   5. Copies `GorgonPlayer` executable and sibling runtime DLLs (e.g. `vulkan-1.dll`).
        ///   6. Unless `options.makeArchive` is false, packs the folder into `<outputDir>.zip`.
        static ExportResult Export(const Scene &scene, const ExportOptions &options);

        /// Packs every file under `bundle.outputDir` into the zip at `zipPath`, preserving the
        /// bundle's relative paths. Needs no window, so tests and tools can pack a folder they
        /// built by hand.
        ///
        /// `outArchiveBytes` receives the archive's size and `outFileCount` the number of packed
        /// entries. Returns false (with `outError`) when the folder is missing, when a file cannot
        /// be read, or when a name would escape the bundle.
        static bool PackBundle(const ExportResult &bundle, const std::string &zipPath,
                               std::string *outError = nullptr,
                               std::uint64_t *outArchiveBytes = nullptr,
                               std::size_t *outFileCount = nullptr);

        /// The archive path for a bundle folder: `Export/MyGame` -> `Export/MyGame.zip`.
        /// Returns an empty string when `outputDir` has no last path component.
        static std::string ArchivePathFor(const std::string &outputDir);
    };

} // namespace Gorgon::Editor
