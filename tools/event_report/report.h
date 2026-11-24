// event_report - the part without a Windows header: which log entries are the game's, the fields taken from each (a
// list of the fields kept; nothing else reaches the report), Windows Error Reporting's Report.wer read from its bytes,
// the files loaded into the game sorted by where they lie, the replacement of user and computer names, e-mail
// addresses and SIDs, the escape of control characters, and the report's text. Built natively too (test/native).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace event_report
  {
  /// the period looked at
  inline constexpr int days{30};
  /// entries of one kind shown, the newest
  inline constexpr std::size_t shown{20};
  /// entries read from one log, at most
  inline constexpr int scanned_most{5000};
  /// a Report.wer larger than this is not read
  inline constexpr std::size_t wer_bytes_most{4u << 20};
  /// report folders looked at in one store, the newest
  inline constexpr int wer_folders_most{500};
  /// loaded files taken from one Report.wer
  inline constexpr std::size_t wer_modules_most{1024};
  /// a value from a log entry is cut to this many bytes
  inline constexpr std::size_t value_most{200};
  /// a name shorter than this is not looked for in the text (it would be found inside other words)
  inline constexpr std::size_t token_least{4};

  struct local_time_t
    {
    int year{};
    int month{};
    int day{};
    int hour{};
    int minute{};
    int second{};
    int offset_minutes{};  ///< local time - UTC
    };

  ///\brief "2026-11-20 21:14:07"
  [[nodiscard]]
  auto time_text(local_time_t const & t) -> std::string;

  ///\brief "+01:00", "-03:30"
  [[nodiscard]]
  auto offset_text(int minutes) -> std::string;

  ///\brief UTF-16 LE bytes as UTF-8; an unpaired surrogate becomes U+FFFD, an odd last byte is dropped
  [[nodiscard]]
  auto utf16le_to_utf8(std::string_view bytes) -> std::string;

  ///\brief equal but for the case of ASCII letters (the native tests' comparison; the program compares as Windows does)
  [[nodiscard]]
  auto equal_ascii_fold(std::string_view a, std::string_view b) noexcept -> bool;

  ///\brief the part of a path after its last '\' or '/'
  [[nodiscard]]
  auto file_name_of(std::string_view path) noexcept -> std::string_view;

  ///\brief a hexadecimal number with or without "0x" ("c0000005", "0x40000015"); nothing for anything else
  [[nodiscard]]
  auto hex_value(std::string_view text) noexcept -> std::optional<std::uint64_t>;

  ///\brief a few words on an exception code, empty for one not described
  [[nodiscard]]
  auto exception_text(std::uint64_t code) noexcept -> std::string_view;

  ///\brief a few words on a stop (bug check) code, empty for one not described
  [[nodiscard]]
  auto bugcheck_text(std::uint64_t code) noexcept -> std::string_view;

  enum struct kind_e
    {
    game_crash,           ///< Application Error 1000 of the game
    game_hang,            ///< Application Hang 1002 of the game
    launcher_crash,       ///< Application Error 1000 or .NET Runtime 1026 of the launcher
    wer_kernel,           ///< Windows Error Reporting 1001: LiveKernelEvent (a graphics hang and the like)
    wer_game,             ///< Windows Error Reporting 1001 of the game or the launcher, not a crash or a hang
    display_reset,        ///< Display 4101
    graphics_driver,      ///< an error of nvlddmkm, amdkmdag or an igfx* driver
    restart,              ///< Microsoft-Windows-Kernel-Power 41
    blue_screen,          ///< Microsoft-Windows-WER-SystemErrorReporting 1001
    unexpected_shutdown,  ///< EventLog 6008
    };

  ///\brief whether a log entry is one the report shows, and as what: by its provider, its id and (for programs) the
  /// program's name in its values
  [[nodiscard]]
  auto kind_of(std::string_view provider, unsigned id, std::span<std::string const> values) -> std::optional<kind_e>;

  ///\brief a log entry: its values (EventData in order) as text, as the log gave them
  struct record_t
    {
    kind_e kind{};
    local_time_t time;
    std::string provider;
    unsigned id{};
    std::vector<std::string> values;
    };

  ///\brief what Report.wer holds that the report uses
  struct wer_t
    {
    std::string event_type;
    std::string report_id;
    std::string app_name;
    std::string app_path;  ///< for sorting the loaded files only, never in the report
    std::vector<std::pair<std::string, std::string>> signature;  ///< Sig[n].Name, Sig[n].Value
    std::vector<std::string> modules;                            ///< LoadedModule[n], whole paths
    bool modules_cut{};
    local_time_t time;  ///< the folder's time, filled by the caller
    };

  enum struct wer_error_e
    {
    none,
    too_large,
    not_utf16,
    no_event_type,
    };

  ///\brief a Report.wer from its bytes: UTF-16 LE with its byte order mark, "key=value" lines
  [[nodiscard]]
  auto parse_wer(std::string_view bytes, wer_t & out) -> wer_error_e;

  ///\brief why a Report.wer was not read, for the report's "Skipped"
  [[nodiscard]]
  auto wer_error_text(wer_error_e e) noexcept -> std::string_view;

  enum struct place_e
    {
    graphics_driver,  ///< a driver store: a graphics driver's files
    game_folder,
    windows,
    elsewhere,  ///< what may have been put into the game
    };

  ///\brief where a loaded file lies; `game_dir` and `windows_dir` end with '\'; empty `game_dir` matches nothing
  [[nodiscard]]
  auto place_of(std::string_view path, std::string_view game_dir, std::string_view windows_dir) noexcept -> place_e;

  /// case-insensitive equality of UTF-8 texts of the same length
  using fold_equal_t = bool (*)(std::string_view, std::string_view);

  struct token_t
    {
    std::string text;              ///< UTF-8
    std::string_view replacement;  ///< "<user>", "<computer>"
    };

  struct cleaned_t
    {
    std::string text;
    int replaced{};
    };

  ///\brief `text` with each token (of token_least bytes or more, at word boundaries, compared by `equal`), each SID
  /// ("S-1-5-21-..."), each e-mail address and each "OneDrive - <name>" replaced; the count of replacements
  [[nodiscard]]
  auto clean(std::string_view text, std::span<token_t const> tokens, fold_equal_t equal) -> cleaned_t;

  ///\brief control characters (but tab and line ends), DEL, and the marks that turn the direction of text, as "\u...";
  /// bytes that are not UTF-8 as "\x.."
  [[nodiscard]]
  auto escape_controls(std::string_view text) -> std::string;

  struct dump_t
    {
    std::string name;
    std::uint64_t bytes{};
    local_time_t time;
    };

  ///\brief one log or folder looked at
  struct source_t
    {
    std::string name;      ///< "the Application log", "Windows Error Reporting's machine archive"
    int read{};            ///< entries or folders read
    bool at_limit{};       ///< the reading stopped at its limit
    std::string failure;   ///< why it could not be read; empty when it was
    };

  struct input_t
    {
    local_time_t now;
    std::string windows_version;  ///< "10.0.26100"
    std::vector<source_t> sources;
    std::vector<record_t> records;  ///< newest first
    std::vector<wer_t> reports;     ///< newest first
    std::vector<dump_t> dumps;      ///< newest first
    std::vector<std::string> skipped;  ///< "2 saved reports not read: not UTF-16"
    std::vector<std::string> notes;    ///< "the user name is too short to look for: check it yourself"
    std::string windows_dir;           ///< ends with '\'; for sorting only
    };

  ///\brief the report's text but its head: the summary and the sections (to be cleaned)
  [[nodiscard]]
  auto body(input_t const & in) -> std::string;

  ///\brief the head: the program, what it checks and writes, `replaced` replacements made in the body
  [[nodiscard]]
  auto head(input_t const & in, int replaced) -> std::string;

  ///\brief the whole report: head and body, cleaned by `tokens` and escaped; lines end with '\n'
  [[nodiscard]]
  auto report(input_t const & in, std::span<token_t const> tokens, fold_equal_t equal) -> std::string;

  ///\brief the result file's name for a run started at `t`, "event_report-20261124-131500.txt"; the `n`-th try for that
  /// second (from 2) gets "-<n>" before ".txt", so no run replaces another's result
  [[nodiscard]]
  auto result_name(local_time_t const & t, int n = 1) -> std::string;

  ///\brief a whole number as Windows' logs give it: decimal, or hexadecimal with "0x"
  [[nodiscard]]
  auto number_value(std::string_view text) noexcept -> std::optional<std::uint64_t>;
  }  // namespace event_report
