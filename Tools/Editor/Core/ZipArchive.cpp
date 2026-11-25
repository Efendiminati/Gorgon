#include "ZipArchive.h"

#include <zlib.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

namespace Gorgon::Editor {

    namespace {
        namespace fs = std::filesystem;

        constexpr std::uint32_t LocalHeaderSignature = 0x04034b50u;
        constexpr std::uint32_t CentralHeaderSignature = 0x02014b50u;
        constexpr std::uint32_t EndOfCentralDirectorySignature = 0x06054b50u;
        /// ZIP 2.0 is the first format version that understands deflate.
        constexpr std::uint16_t FormatVersion = 20;
        /// Names are written as UTF-8, so asset names outside ASCII survive the round-trip.
        constexpr std::uint16_t Utf8NameFlag = 0x0800;
        constexpr std::uint16_t MethodStored = 0;
        constexpr std::uint16_t MethodDeflate = 8;
        /// Fixed 1980-01-01 00:00 stamp in DOS format. A stable timestamp keeps two exports of the
        /// same bundle byte-identical, which makes the archive diffable in tests and in CI.
        constexpr std::uint16_t DosDate = (1 << 5) | 1;
        constexpr std::uint16_t DosTime = 0;
        /// Below this, deflating costs more than it saves.
        constexpr std::size_t DeflateThreshold = 64;
        /// Classic ZIP stores sizes as 32-bit fields.
        constexpr std::uint64_t MaxEntrySize = 0xFFFFFFFEull;
        constexpr std::uint64_t MaxArchiveSize = 0xFFFFFFFEull;

        void Put16(std::string &out, std::uint16_t value) {
            out.push_back(char(value & 0xFF));
            out.push_back(char((value >> 8) & 0xFF));
        }

        void Put32(std::string &out, std::uint32_t value) {
            for(int byte = 0; byte < 4; ++byte)
                out.push_back(char((value >> (8 * byte)) & 0xFF));
        }

        bool ReadWholeFile(const std::string &path, std::string &out, std::string *outError) {
            std::error_code ec;
            const auto size = fs::file_size(path, ec);
            if(ec) {
                if(outError)
                    *outError = "Could not read '" + path + "': " + ec.message();
                return false;
            }

            if(size > MaxEntrySize) {
                if(outError)
                    *outError = "'" + path + "' is too large for a zip archive (4 GiB limit)";
                return false;
            }

            std::ifstream in(path, std::ios::binary);
            if(!in) {
                if(outError)
                    *outError = "Could not open '" + path + "' for reading";
                return false;
            }

            out.assign(std::size_t(size), '\0');
            if(size > 0)
                in.read(out.data(), std::streamsize(size));
            if(!in) {
                if(outError)
                    *outError = "Could not read '" + path + "'";
                return false;
            }
            return true;
        }

        /// Raw deflate (RFC 1951) — what a ZIP entry holds, not the zlib-wrapped stream of
        /// compress2(). windowBits = -15 selects the header-less variant.
        bool DeflateRaw(const std::string &input, std::string &out) {
            z_stream stream{};
            if(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8,
                            Z_DEFAULT_STRATEGY) != Z_OK)
                return false;

            const uLong bound = deflateBound(&stream, uLong(input.size()));
            out.assign(std::size_t(bound), '\0');

            stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.data()));
            stream.avail_in = uInt(input.size());
            stream.next_out = reinterpret_cast<Bytef *>(out.data());
            stream.avail_out = uInt(bound);

            const int status = deflate(&stream, Z_FINISH);
            const uLong written = stream.total_out;
            deflateEnd(&stream);

            if(status != Z_STREAM_END) {
                out.clear();
                return false;
            }

            out.resize(std::size_t(written));
            return true;
        }

        std::uint32_t Crc32Of(const std::string &data) {
            uLong crc = crc32(0L, Z_NULL, 0);
            std::size_t done = 0;
            while(done < data.size()) {
                const std::size_t chunk =
                    std::min<std::size_t>(data.size() - done, std::size_t(1) << 16);
                crc = crc32(crc, reinterpret_cast<const Bytef *>(data.data() + done), uInt(chunk));
                done += chunk;
            }
            return std::uint32_t(crc);
        }

        /// Rejects names an unzip tool would treat as a path escape or a drive letter.
        bool SanitizeName(const std::string &archivePath, std::string &outName,
                          std::string *outError) {
            std::string name = archivePath;
            std::replace(name.begin(), name.end(), '\\', '/');
            while(name.rfind("./", 0) == 0)
                name.erase(0, 2);

            if(name.empty() || name == ".") {
                if(outError)
                    *outError = "The archive needs a relative path for every entry";
                return false;
            }

            if(name.front() == '/' || (name.size() > 1 && name[1] == ':')) {
                if(outError)
                    *outError = "The archive entry must be relative: '" + archivePath + "'";
                return false;
            }

            std::size_t start = 0;
            while(start <= name.size()) {
                const std::size_t end = name.find('/', start);
                const std::string segment =
                    name.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if(segment == "..") {
                    if(outError)
                        *outError = "The archive entry must stay inside the bundle: '" +
                                    archivePath + "'";
                    return false;
                }
                if(end == std::string::npos)
                    break;
                start = end + 1;
            }

            if(name.size() > std::numeric_limits<std::uint16_t>::max()) {
                if(outError)
                    *outError = "The archive entry name is too long: '" + archivePath + "'";
                return false;
            }

            outName = name;
            return true;
        }
    } // namespace

    bool ZipArchive::AddFile(const std::string &archivePath, const std::string &sourcePath,
                             std::string *outError) {
        Source source;
        source.filePath = sourcePath;
        return Add(archivePath, std::move(source), outError);
    }

    bool ZipArchive::AddData(const std::string &archivePath, const std::string &contents,
                             std::string *outError) {
        Source source;
        source.data = contents;
        return Add(archivePath, std::move(source), outError);
    }

    bool ZipArchive::Add(const std::string &archivePath, Source source, std::string *outError) {
        std::string name;
        if(!SanitizeName(archivePath, name, outError))
            return false;

        if(!names_.insert(name).second) {
            if(outError)
                *outError = "The archive already contains '" + name + "'";
            return false;
        }

        source.archivePath = name;
        sources_.push_back(std::move(source));
        return true;
    }

    bool ZipArchive::Save(const std::string &zipPath, std::uint64_t *outArchiveBytes,
                          std::size_t *outEntries, std::string *outError) {
        if(outArchiveBytes)
            *outArchiveBytes = 0;
        if(outEntries)
            *outEntries = 0;
        entries_.clear();

        if(zipPath.empty()) {
            if(outError)
                *outError = "No archive path given";
            return false;
        }

        if(sources_.size() > std::numeric_limits<std::uint16_t>::max()) {
            if(outError)
                *outError = "Too many files for a zip archive (65535 limit)";
            return false;
        }

        std::error_code ec;
        const fs::path target(zipPath);
        if(target.has_parent_path())
            fs::create_directories(target.parent_path(), ec);

        std::ofstream out(zipPath, std::ios::binary | std::ios::trunc);

        // A half-written archive is worse than none: never leave the partial file behind.
        const auto fail = [&](const std::string &message) {
            if(out.is_open())
                out.close();
            std::error_code removeEc;
            fs::remove(target, removeEc);
            if(outError)
                *outError = message;
            return false;
        };

        if(!out)
            return fail("Could not write '" + zipPath + "'");

        std::string central;
        std::uint64_t offset = 0;

        for(const auto &source : sources_) {
            std::string content;
            if(!source.filePath.empty()) {
                std::string error;
                if(!ReadWholeFile(source.filePath, content, &error))
                    return fail(error);
            } else {
                content = source.data;
            }

            if(content.size() > MaxEntrySize)
                return fail("'" + source.archivePath + "' is too large for a zip archive");

            std::string packed;
            bool deflated = false;
            if(content.size() >= DeflateThreshold) {
                std::string compressed;
                if(DeflateRaw(content, compressed) && compressed.size() < content.size()) {
                    packed = std::move(compressed);
                    deflated = true;
                }
            }
            if(!deflated)
                packed = content;

            const std::uint32_t crc = Crc32Of(content);
            const auto size = std::uint32_t(content.size());
            const auto packedSize = std::uint32_t(packed.size());
            const std::uint16_t method = deflated ? MethodDeflate : MethodStored;
            const auto nameLength = std::uint16_t(source.archivePath.size());
            const auto localOffset = std::uint32_t(offset);

            std::string local;
            local.reserve(30 + source.archivePath.size());
            Put32(local, LocalHeaderSignature);
            Put16(local, FormatVersion);
            Put16(local, Utf8NameFlag);
            Put16(local, method);
            Put16(local, DosTime);
            Put16(local, DosDate);
            Put32(local, crc);
            Put32(local, packedSize);
            Put32(local, size);
            Put16(local, nameLength);
            Put16(local, 0); // extra field length
            local += source.archivePath;

            out.write(local.data(), std::streamsize(local.size()));
            out.write(packed.data(), std::streamsize(packed.size()));
            if(!out)
                return fail("Could not write '" + zipPath + "'");

            offset += local.size() + packed.size();
            if(offset > MaxArchiveSize)
                return fail("The bundle is too large for a zip archive (4 GiB limit)");

            Put32(central, CentralHeaderSignature);
            Put16(central, FormatVersion); // version made by
            Put16(central, FormatVersion); // version needed to extract
            Put16(central, Utf8NameFlag);
            Put16(central, method);
            Put16(central, DosTime);
            Put16(central, DosDate);
            Put32(central, crc);
            Put32(central, packedSize);
            Put32(central, size);
            Put16(central, nameLength);
            Put16(central, 0); // extra field length
            Put16(central, 0); // comment length
            Put16(central, 0); // disk number
            Put16(central, 0); // internal attributes
            Put32(central, 0); // external attributes
            Put32(central, localOffset);
            central += source.archivePath;

            Entry entry;
            entry.path = source.archivePath;
            entry.size = size;
            entry.packedSize = packedSize;
            entry.deflated = deflated;
            entries_.push_back(std::move(entry));
        }

        const auto centralOffset = std::uint32_t(offset);
        out.write(central.data(), std::streamsize(central.size()));

        std::string end;
        Put32(end, EndOfCentralDirectorySignature);
        Put16(end, 0); // this disk
        Put16(end, 0); // disk holding the central directory
        Put16(end, std::uint16_t(entries_.size()));
        Put16(end, std::uint16_t(entries_.size()));
        Put32(end, std::uint32_t(central.size()));
        Put32(end, centralOffset);
        Put16(end, 0); // comment length
        out.write(end.data(), std::streamsize(end.size()));
        out.flush();

        if(!out)
            return fail("Could not write '" + zipPath + "'");

        out.close();

        std::error_code sizeEc;
        const auto bytes = fs::file_size(zipPath, sizeEc);
        if(sizeEc)
            return fail("Could not read back '" + zipPath + "': " + sizeEc.message());

        if(outArchiveBytes)
            *outArchiveBytes = std::uint64_t(bytes);
        if(outEntries)
            *outEntries = entries_.size();
        return true;
    }

} // namespace Gorgon::Editor
