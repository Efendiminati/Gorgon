#include "ScriptLog.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace Gorgon :: Editor {

    namespace {
        std::string CurrentTimeText() {
            const std::time_t now = std::time(nullptr);
            std::tm parts{};
#ifdef _MSC_VER
            localtime_s(&parts, &now);
#else
            localtime_r(&now, &parts);
#endif
            char buffer[16] = {};
            std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d",
                          parts.tm_hour, parts.tm_min, parts.tm_sec);
            return std::string(buffer);
        }

        double SecondsSinceStart() {
            using Clock = std::chrono::steady_clock;
            static const Clock::time_point start = Clock::now();
            return std::chrono::duration<double>(Clock::now() - start).count();
        }
    } // namespace

    const char *LogSeverityName(LogSeverity severity) {
        switch(severity) {
            case LogSeverity::Warn:  return "WARN";
            case LogSeverity::Error: return "ERROR";
            case LogSeverity::Info:
            default:                 return "INFO";
        }
    }

    std::string LogEntry::Format() const {
        std::string out = "[" + time + "] ";

        // Pad the level to the width of "ERROR" so the message column lines up in a fixed width
        // console font even though the levels differ in length.
        const std::string level = LogSeverityName(severity);
        out += level;
        for(std::size_t i = level.size(); i < 5; ++i)
            out += ' ';

        out += ' ';

        if(!source.empty())
            out += source + ": ";

        out += message;
        return out;
    }

    ScriptLog::ScriptLog(std::size_t capacity) :
        capacity_(capacity == 0 ? DefaultCapacity : capacity),
        elapsedOrigin_(SecondsSinceStart())
    {
    }

    void ScriptLog::SetCapacity(std::size_t capacity) {
        capacity_ = capacity == 0 ? DefaultCapacity : capacity;
        while(entries_.size() > capacity_)
            entries_.pop_front();
        ++version_;
    }

    void ScriptLog::Add(LogSeverity severity, const std::string &source, const std::string &message) {
        LogEntry entry;
        entry.severity = severity;
        entry.time = CurrentTimeText();
        entry.elapsed = SecondsSinceStart() - elapsedOrigin_;
        entry.source = source;
        entry.message = message;

        entries_.push_back(std::move(entry));

        // Ring buffer: the oldest line goes once the buffer is full.
        while(entries_.size() > capacity_)
            entries_.pop_front();

        ++version_;
    }

    void ScriptLog::Info(const std::string &source, const std::string &message) {
        Add(LogSeverity::Info, source, message);
    }

    void ScriptLog::Warn(const std::string &source, const std::string &message) {
        Add(LogSeverity::Warn, source, message);
    }

    void ScriptLog::Error(const std::string &source, const std::string &message) {
        Add(LogSeverity::Error, source, message);
    }

    void ScriptLog::Clear() {
        if(entries_.empty())
            return;

        entries_.clear();
        elapsedOrigin_ = SecondsSinceStart();
        ++version_;
    }

    const LogEntry *ScriptLog::GetLast() const {
        if(entries_.empty())
            return nullptr;

        return &entries_.back();
    }

    int ScriptLog::CountOf(LogSeverity severity) const {
        return static_cast<int>(std::count_if(entries_.begin(), entries_.end(),
            [severity](const LogEntry &entry) { return entry.severity == severity; }));
    }

    std::string ScriptLog::FormatAll() const {
        std::string out;
        for(const auto &entry : entries_) {
            out += entry.Format();
            out += '\n';
        }
        return out;
    }

    // ---- ConsoleView --------------------------------------------------------

    void ConsoleView::SetSource(const ScriptLog *log) {
        log_ = log;
        scrollOffset_ = 0;
        visibleLines_ = std::max(1, visibleLines_);
    }

    void ConsoleView::SetErrorsOnly(bool value) {
        if(errorsOnly_ == value)
            return;

        errorsOnly_ = value;
        scrollOffset_ = 0;
    }

    std::vector<const LogEntry *> ConsoleView::GetLines() const {
        std::vector<const LogEntry *> lines;
        if(!log_)
            return lines;

        lines.reserve(log_->GetCount());
        for(const auto &entry : log_->GetEntries()) {
            // "Errors" also shows warnings: a warning is a problem the user wants to see, and the
            // console is the only place they surface.
            if(errorsOnly_ && entry.severity == LogSeverity::Info)
                continue;

            lines.push_back(&entry);
        }
        return lines;
    }

    int ConsoleView::GetLineCount() const {
        return static_cast<int>(GetLines().size());
    }

    std::string ConsoleView::GetLineText(int index) const {
        const auto lines = GetLines();
        if(index < 0 || index >= static_cast<int>(lines.size()))
            return {};

        return lines[std::size_t(index)]->Format();
    }

    void ConsoleView::SetVisibleLines(int count) {
        visibleLines_ = std::max(1, count);
        ScrollTo(scrollOffset_);
    }

    int ConsoleView::GetMaxScroll() const {
        return std::max(0, GetLineCount() - visibleLines_);
    }

    void ConsoleView::ScrollTo(int line) {
        scrollOffset_ = std::clamp(line, 0, GetMaxScroll());
    }

    void ConsoleView::ScrollBy(int lines) {
        ScrollTo(scrollOffset_ + lines);
    }

    void ConsoleView::ScrollToBottom() {
        ScrollTo(GetMaxScroll());
    }

    int ConsoleView::GetFirstLine() const {
        return std::clamp(scrollOffset_, 0, GetMaxScroll());
    }

    bool ConsoleView::Sync() {
        const std::size_t version = log_ ? log_->GetVersion() : 0;
        if(version == version_)
            return false;

        version_ = version;
        // Reading the log again may have moved the content under the offset; keep it in range.
        ScrollTo(scrollOffset_);
        return true;
    }

    std::string ConsoleView::GetStatusText() const {
        if(!log_)
            return "0 entries";

        const std::size_t count = log_->GetCount();
        std::string text = std::to_string(count) + (count == 1 ? " entry" : " entries");

        const int errors = log_->CountOf(LogSeverity::Error);
        const int warnings = log_->CountOf(LogSeverity::Warn);
        if(errors > 0)
            text += ", " + std::to_string(errors) + (errors == 1 ? " error" : " errors");
        if(warnings > 0)
            text += ", " + std::to_string(warnings) + (warnings == 1 ? " warning" : " warnings");

        return text;
    }

} // namespace Gorgon::Editor
