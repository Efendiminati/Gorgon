#include "Core/FileWatcher.h"

#include <algorithm>
#include <cctype>

namespace Gorgon { namespace Editor {

    namespace {

        std::string ToLowerAscii(std::string value) {
            for(char &ch : value) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        bool ShouldSkipDirectory(const std::filesystem::path &dirPath) {
            try {
                const std::string name = dirPath.filename().string();
                if(name.empty())
                    return false;
                // Skip hidden/VCS directories like .git, .vs, .vscode.
                return name[0] == '.';
            } catch(...) {
                return true;
            }
        }

        std::string CanonicalKey(const std::string &path) {
            std::string norm = FileWatcher::NormalizePath(std::filesystem::path(path));
#ifdef _WIN32
            return ToLowerAscii(std::move(norm));
#else
            return norm;
#endif
        }

    } // namespace

    std::string FileWatcher::NormalizePath(const std::filesystem::path &path) {
        if(path.empty())
            return {};
        try {
            std::error_code ec;
            std::filesystem::path norm = std::filesystem::weakly_canonical(path, ec);
            if(ec || norm.empty()) {
                ec.clear();
                norm = std::filesystem::absolute(path, ec);
                if(ec || norm.empty()) {
                    norm = path;
                }
                norm = norm.lexically_normal();
            }
            std::string result = norm.generic_string();
            while(result.size() > 1 && result.back() == '/') {
                if(result.size() == 3 && result[1] == ':')
                    break;
                result.pop_back();
            }
            return result;
        } catch(...) {
            return {};
        }
    }

    bool FileWatcher::PathsEqual(const std::string &a, const std::string &b) {
        if(a.empty() || b.empty())
            return a.empty() && b.empty();
        return CanonicalKey(a) == CanonicalKey(b);
    }

    void FileWatcher::SetRoot(const std::string &directory) {
        entries_.clear();
        elapsedSeconds_ = 0.0;
        root_.clear();

        if(directory.empty())
            return;

        std::error_code ec;
        std::filesystem::path rootPath(directory);
        if(!std::filesystem::exists(rootPath, ec) || !std::filesystem::is_directory(rootPath, ec))
            return;

        root_ = NormalizePath(rootPath);
        entries_ = scanDirectory();
    }

    void FileWatcher::SetIntervalSeconds(double seconds) {
        intervalSeconds_ = seconds < 0.0 ? 0.0 : seconds;
    }

    std::unordered_map<std::string, FileWatcher::FileStamp> FileWatcher::scanDirectory() const {
        std::unordered_map<std::string, FileStamp> snapshot;
        if(root_.empty())
            return snapshot;

        std::error_code ec;
        const std::filesystem::path rootPath(root_);
        if(!std::filesystem::exists(rootPath, ec) || !std::filesystem::is_directory(rootPath, ec))
            return snapshot;

        const auto options = std::filesystem::directory_options::skip_permission_denied;
        std::filesystem::recursive_directory_iterator it(rootPath, options, ec);
        const std::filesystem::recursive_directory_iterator end;

        for(; !ec && it != end; it.increment(ec)) {
            if(snapshot.size() >= maxEntries_)
                break;

            const auto &entry = *it;
            std::error_code entryEc;
            if(entry.is_directory(entryEc)) {
                if(!entryEc && ShouldSkipDirectory(entry.path())) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if(entryEc || !entry.is_regular_file(entryEc) || entryEc)
                continue;

            const auto writeTime = entry.last_write_time(entryEc);
            if(entryEc)
                continue;
            const auto fileSize = entry.file_size(entryEc);
            if(entryEc)
                continue;

            try {
                const std::string fullPath = NormalizePath(entry.path());
                if(fullPath.empty())
                    continue;
                std::filesystem::path rel = std::filesystem::relative(entry.path(), rootPath, entryEc);
                std::string relStr = (!entryEc && !rel.empty()) ? rel.generic_string() : entry.path().filename().generic_string();
                std::string ext = ToLowerAscii(entry.path().extension().string());

                snapshot.emplace(fullPath, FileStamp{writeTime, fileSize, std::move(relStr), std::move(ext)});
            } catch(...) {
                continue;
            }
        }

        return snapshot;
    }

    std::vector<FileChange> FileWatcher::Poll() {
        std::vector<FileChange> changes;
        if(root_.empty())
            return changes;

        auto current = scanDirectory();

        // Detect Added and Modified files.
        for(const auto &kv : current) {
            const auto prevIt = entries_.find(kv.first);
            if(prevIt == entries_.end()) {
                changes.push_back(FileChange{
                    FileChangeKind::Added,
                    kv.first,
                    kv.second.relativePath,
                    kv.second.extension
                });
            } else if(prevIt->second.writeTime != kv.second.writeTime ||
                      prevIt->second.fileSize != kv.second.fileSize) {
                changes.push_back(FileChange{
                    FileChangeKind::Modified,
                    kv.first,
                    kv.second.relativePath,
                    kv.second.extension
                });
            }
        }

        // Detect Removed files.
        for(const auto &kv : entries_) {
            if(current.find(kv.first) == current.end()) {
                changes.push_back(FileChange{
                    FileChangeKind::Removed,
                    kv.first,
                    kv.second.relativePath,
                    kv.second.extension
                });
            }
        }

        entries_ = std::move(current);

        std::sort(changes.begin(), changes.end(), [](const FileChange &a, const FileChange &b) {
            if(a.relativePath != b.relativePath)
                return a.relativePath < b.relativePath;
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        });

        return changes;
    }

    std::vector<FileChange> FileWatcher::Tick(double dtSeconds) {
        if(root_.empty())
            return {};
        if(dtSeconds > 0.0) {
            elapsedSeconds_ += dtSeconds;
        }
        if(elapsedSeconds_ < intervalSeconds_)
            return {};
        elapsedSeconds_ = 0.0;
        return Poll();
    }

} } // namespace Gorgon::Editor
