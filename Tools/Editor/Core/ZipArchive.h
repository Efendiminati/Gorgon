#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace Gorgon::Editor {

    /// Minimal ZIP archive writer (deflate through zlib, stored otherwise, no zip64) used to hand
    /// a game bundle over as one file. Every entry is written under the archive-relative path it
    /// was registered with, so unzipping reproduces the bundle's folder layout exactly.
    ///
    /// Writing is deferred to Save(): AddFile() only records what to pack, so a large bundle is
    /// never held in memory all at once. Sizes are checked against the 4 GiB / 65535 entry limits
    /// of the classic format and reported as errors instead of producing a corrupt archive.
    class ZipArchive {
    public:
        /// One packed entry, as it appears in the archive's central directory.
        struct Entry {
            /// Archive-relative path, forward slashes.
            std::string path;
            /// Uncompressed size in bytes.
            std::uint32_t size = 0;
            /// Bytes the entry occupies inside the archive.
            std::uint32_t packedSize = 0;
            /// True when the entry was deflated, false when it was stored verbatim.
            bool deflated = false;
        };

        /// Records `sourcePath` to be packed under `archivePath` (relative, forward slashes).
        bool AddFile(const std::string &archivePath, const std::string &sourcePath,
                     std::string *outError = nullptr);

        /// Records an in-memory buffer to be packed under `archivePath`.
        bool AddData(const std::string &archivePath, const std::string &contents,
                     std::string *outError = nullptr);

        /// The entries written by the last Save(), in archive order. Empty before the first Save().
        const std::vector<Entry> &GetEntries() const { return entries_; }

        /// True when nothing has been added: Save() then writes a valid, empty archive.
        bool IsEmpty() const { return sources_.empty(); }

        /// Writes the archive to `zipPath`, creating its parent directory when needed.
        /// `outArchiveBytes` receives the archive's file size, `outEntries` the entry count.
        bool Save(const std::string &zipPath,
                  std::uint64_t *outArchiveBytes = nullptr,
                  std::size_t *outEntries = nullptr,
                  std::string *outError = nullptr);

    private:
        /// What to pack: a file to read at Save() time, or an in-memory buffer.
        struct Source {
            std::string archivePath;
            std::string filePath;
            std::string data;
        };

        bool Add(const std::string &archivePath, Source source, std::string *outError);

        std::vector<Source> sources_;
        std::vector<Entry> entries_;
        std::set<std::string> names_;
    };

} // namespace Gorgon::Editor
