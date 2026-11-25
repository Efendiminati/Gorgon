#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace Gorgon :: Editor {

    /// Severity of a console entry. Info is ordinary script output, Warn a recoverable problem and
    /// Error something the script could not do (a compile failure or a runtime exception).
    enum class LogSeverity {
        Info = 0,
        Warn,
        Error
    };

    /// Fixed name of a severity ("INFO", "WARN", "ERROR").
    const char *LogSeverityName(LogSeverity severity);

    /// One line of the script console.
    struct LogEntry {
        LogSeverity severity = LogSeverity::Info;
        /// Wall clock time the line was written, "HH:MM:SS".
        std::string time;
        /// Seconds since the log this entry belongs to was created.
        double elapsed = 0.0;
        /// Entity/script the line came from, or a host label such as "Editor".
        std::string source;
        std::string message;

        /// "[HH:MM:SS] LEVEL source: message" (the source part is skipped when it is empty).
        std::string Format() const;
    };

    /**
     * Ring buffer behind the editor console.
     *
     * The play session writes every script Log()/Print() call, every compile failure and every
     * runtime error here, so the editor can keep showing them after play stopped tore its VMs
     * down. Once `capacity` entries are stored the oldest is dropped, which keeps a script that
     * prints every frame from growing without bound.
     */
    class ScriptLog {
    public:
        static constexpr std::size_t DefaultCapacity = 500;

        explicit ScriptLog(std::size_t capacity = DefaultCapacity);

        void Add(LogSeverity severity, const std::string &source, const std::string &message);
        void Info(const std::string &source, const std::string &message);
        void Warn(const std::string &source, const std::string &message);
        void Error(const std::string &source, const std::string &message);

        /// Drops every stored entry. The version keeps counting so a viewer notices the change.
        void Clear();

        std::size_t GetCapacity() const { return capacity_; }
        /// Shrinks the buffer to the given size, dropping the oldest entries when it does.
        void SetCapacity(std::size_t capacity);

        std::size_t GetCount() const { return entries_.size(); }
        bool IsEmpty() const { return entries_.empty(); }
        const std::deque<LogEntry> &GetEntries() const { return entries_; }

        /// Most recently added entry, nullptr when the log is empty.
        const LogEntry *GetLast() const;

        /// Number of stored entries with the given severity.
        int CountOf(LogSeverity severity) const;

        /// Increments on every Add()/Clear(); a viewer redraws only when it moved.
        std::size_t GetVersion() const { return version_; }

        /// Every entry joined with newlines.
        std::string FormatAll() const;

    private:
        std::size_t capacity_;
        std::deque<LogEntry> entries_;
        std::size_t version_ = 0;
        double elapsedOrigin_ = 0.0;
    };

    /**
     * Widget-free state of the console panel: the severity filter and the scroll position. Kept
     * apart from the widget code so filtering and scrolling can be exercised headlessly.
     */
    class ConsoleView {
    public:
        /// Points the view at a log (nullptr empties it). Does not take ownership.
        void SetSource(const ScriptLog *log);
        const ScriptLog *GetSource() const { return log_; }

        void SetErrorsOnly(bool value);
        bool GetErrorsOnly() const { return errorsOnly_; }

        /// Number of entries that pass the filter.
        int GetLineCount() const;
        /// Entries that pass the filter, oldest first.
        std::vector<const LogEntry *> GetLines() const;
        /// Formatted text of the index-th visible entry (0 is the oldest one passing the filter).
        std::string GetLineText(int index) const;

        /// How many entries fit into the panel; drives the scroll clamp.
        void SetVisibleLines(int count);
        int GetVisibleLines() const { return visibleLines_; }
        int GetMaxScroll() const;
        void ScrollTo(int line);
        void ScrollBy(int lines);
        void ScrollToBottom();
        int GetScrollOffset() const { return scrollOffset_; }
        /// Index of the first drawn entry once the offset is clamped to the content.
        int GetFirstLine() const;

        /// True when the log content changed since the last call.
        bool Sync();

        /// "12 entries, 2 errors" style summary of the whole log (filter independent).
        std::string GetStatusText() const;

    private:
        const ScriptLog *log_ = nullptr;
        bool errorsOnly_ = false;
        int scrollOffset_ = 0;
        int visibleLines_ = 1;
        std::size_t version_ = 0;
    };

} // namespace Gorgon::Editor
