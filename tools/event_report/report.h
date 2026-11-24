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
  /// 100 ns intervals (a FILETIME's unit) in the period looked at
  inline constexpr std::uint64_t period_filetime{std::uint64_t(days) * 24 * 60 * 60 * 10'000'000};

  /// case-insensitive equality of UTF-8 texts of the same length
  using fold_equal_t = bool (*)(std::string_view, std::string_view);

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

  ///\brief whether `a` is later than `b`, by the times in UTC (their offsets taken off: an hour put back in autumn
  /// sorts right)
  [[nodiscard]]
  auto later(local_time_t const & a, local_time_t const & b) noexcept -> bool;

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

  ///\brief whether a LiveKernelEvent's code is a graphics one (0x117, 0x141, 0x193, 0x1b0, 0x1b8); others (a USB
  /// driver's 0x144) are shown apart
  [[nodiscard]]
  auto graphics_kernel_code(std::uint64_t code) noexcept -> bool;

  enum struct kind_e
    {
    game_crash,           ///< Application Error 1000 of the game
    game_hang,            ///< Application Hang 1002 of the game
    launcher_crash,       ///< Application Error 1000 or .NET Runtime 1026 of the launcher
    wer_kernel,           ///< Windows Error Reporting 1001: LiveKernelEvent (graphics_kernel_code() or another)
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
    std::string report_id;             ///< ReportIdentifier
    std::string integrator_report_id;  ///< IntegratorReportIdentifier (newer Windows' id in Application Error 1000)
    std::string app_name;              ///< may be a name for people ("Elite Dangerous"), not the exe's
    std::string app_path;  ///< for the exe's name and sorting the loaded files only, never in the report
    std::uint64_t event_time{};  ///< EventTime: a FILETIME (UTC); 0 when not there
    std::vector<std::pair<std::string, std::string>> signature;  ///< Sig[n].Name, Sig[n].Value
    std::vector<std::string> modules;                            ///< LoadedModule[n], whole paths
    bool modules_cut{};
    local_time_t time;  ///< from event_time (the folder's time when there is none), filled by the caller
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

  ///\brief whether a report folder's name is one of a crash, a hang or the kernel ("AppCrash_", "AppHang_", "Kernel_"),
  /// any case; the others are not read
  [[nodiscard]]
  auto wer_folder_wanted(std::string_view folder) noexcept -> bool;

  ///\brief the exe a saved report is about: AppPath's file name, AppName when there is no AppPath
  [[nodiscard]]
  auto wer_program(wer_t const & w) noexcept -> std::string_view;

  ///\brief whether a saved report is one the report shows: the game's or the launcher's (its exe, AppName or Sig[0]'s
  /// value, compared by `equal`) or a LiveKernelEvent
  [[nodiscard]]
  auto wer_wanted(wer_t const & w, fold_equal_t equal) -> bool;

  ///\brief whether `id` (Application Error 1000's report id) is the saved report's ReportIdentifier or its
  /// IntegratorReportIdentifier
  [[nodiscard]]
  auto wer_has_id(wer_t const & w, std::string_view id) noexcept -> bool;

  ///\brief whether a crash dump's file name is the game's or the launcher's ("EliteDangerous...", "EDLaunch..."), any
  /// case
  [[nodiscard]]
  auto dump_of_game(std::string_view name) noexcept -> bool;

  ///\brief whether a System log provider is Intel's graphics driver ("igfx..."), any case
  [[nodiscard]]
  auto intel_graphics(std::string_view provider) noexcept -> bool;

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
  /// ("S-1-5-21-...", Azure AD's "S-1-12-1-..."), each e-mail address and each "OneDrive - <name>" replaced; the count
  /// of replacements
  [[nodiscard]]
  auto clean(std::string_view text, std::span<token_t const> tokens, fold_equal_t equal) -> cleaned_t;

  ///\brief control characters (but tab and line ends), DEL, and the marks that turn the direction of text, as "\u...";
  /// bytes that are not UTF-8 as "\x.."
  [[nodiscard]]
  auto escape_controls(std::string_view text) -> std::string;

  struct dump_t
    {
    std::string name;
    std::optional<std::uint64_t> bytes;  ///< nothing when its size could not be read
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
    std::vector<record_t> records;  ///< any order: the report sorts them
    std::vector<wer_t> reports;     ///< any order
    std::vector<dump_t> dumps;      ///< any order
    std::vector<std::string> skipped;  ///< "2 saved reports not read: not UTF-16" (the program's words and numbers)
    std::vector<std::string> notes;    ///< "the user name is too short to look for: check it yourself" (the same)
    std::string windows_dir;           ///< ends with '\'; for sorting only
    };

  ///\brief `in`'s values from Windows (the logs' values and providers, the saved reports' fields, the dumps' names,
  /// the Windows folder, cleaned alike so the loaded files sort the same) cleaned by clean(); the count of
  /// replacements. The program's own words are not looked at: a user named "User" leaves "the user's" as it is.
  [[nodiscard]]
  auto clean_input(input_t & in, std::span<token_t const> tokens, fold_equal_t equal) -> int;

  ///\brief the report's text but its head: the summary and the sections, from `in` as it is
  [[nodiscard]]
  auto body(input_t const & in) -> std::string;

  ///\brief the head: the program, what it checks and writes, `replaced` replacements made in the body
  [[nodiscard]]
  auto head(input_t const & in, int replaced) -> std::string;

  ///\brief the whole report: head and the body of a copy of `in` put through clean_input(), escaped; lines end with
  /// '\n'
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
