#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Gorgon { namespace Editor {

    /// Type of filesystem change detected by FileWatcher.
    enum class FileChangeKind {
        Added = 0,
        Modified,
        Removed,
    };

    /// A single file change reported by FileWatcher::Poll / Tick.
    struct FileChange {
        FileChangeKind kind = FileChangeKind::Modified;
        /// Normalized forward-slash absolute path on disk.
        std::string path;
        /// Forward-slash path relative to the watched root directory.
        std::string relativePath;
        /// Lowercased file extension including the leading dot (e.g. ".png", ".gs").
        std::string extension;
    };

    /// Lightweight, cross-platform directory watcher backed by std::filesystem polling.
    ///
    /// Maintains a snapshot of regular files under a root directory (mtime + file size)
    /// and emits Added / Modified / Removed events when polled or ticked at a fixed
    /// interval (default 0.5 seconds). Zero external dependencies.
    class FileWatcher {
    public:
        FileWatcher() = default;

        /// Sets the root directory to watch and captures the initial baseline snapshot.
        /// Existing files are not emitted as Added on the next Poll(). Passing an empty
        /// or non-existent path clears the watcher.
        void SetRoot(const std::string &directory);

        /// Normalized forward-slash root directory currently being watched (empty if none).
        const std::string &GetRoot() const { return root_; }

        /// Sets the polling interval in seconds used by Tick() (clamped to >= 0).
        void SetIntervalSeconds(double seconds);
        double GetIntervalSeconds() const { return intervalSeconds_; }

        /// Caps the number of tracked files so opening a huge tree cannot stall the UI.
        void SetMaxEntries(std::size_t maxEntries) { maxEntries_ = maxEntries; }
        std::size_t GetMaxEntries() const { return maxEntries_; }

        /// Resets the accumulated Tick() timer without clearing the file snapshot.
        void ResetTimer() { elapsedSeconds_ = 0.0; }

        /// Immediately scans the root directory, updates the snapshot, and returns all
        /// changes since the previous snapshot.
        std::vector<FileChange> Poll();

        /// Advances the internal timer by dtSeconds and calls Poll() once the interval
        /// has elapsed. Returns an empty vector between polls.
        std::vector<FileChange> Tick(double dtSeconds);

        /// Number of files currently tracked in the snapshot.
        std::size_t GetTrackedFileCount() const { return entries_.size(); }

        /// Normalizes a filesystem path to forward-slash generic form for comparison.
        static std::string NormalizePath(const std::filesystem::path &path);

        /// Compares two paths after normalization (case-insensitive on Windows).
        static bool PathsEqual(const std::string &a, const std::string &b);

    private:
        struct FileStamp {
            std::filesystem::file_time_type writeTime{};
            std::uintmax_t fileSize = 0;
            std::string relativePath;
            std::string extension;
        };

        std::unordered_map<std::string, FileStamp> scanDirectory() const;

        std::string root_;
        double intervalSeconds_ = 0.5;
        double elapsedSeconds_ = 0.0;
        std::size_t maxEntries_ = 8192;
        std::unordered_map<std::string, FileStamp> entries_;
    };

} } // namespace Gorgon::Editor
