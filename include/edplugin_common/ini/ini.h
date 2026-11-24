// edplugin_common - a plugin's ini file (code of the watch in src/ini.cc): read into the settings by the table
// (ini_table.h), completed with the settings it lacks, and watched while the game runs. Nothing here logs: the results
// go back to the plugin, which logs them.
//
// The start, one order for every plugin: the log opened; complete_ini (the values written are the ones the plugin runs
// with, its defaults); watch.mark_seen(); read_ini_file into the defaults; the notes logged; live.start (live.h).
// While the game runs, on the publishing thread: watch.look(now) - changed: read_ini_file into new defaults, the
// clamps, at_start_changes, live.publish, the notes logged; gone (the file taken away, once until it is back): nothing
// read, the settings stay. An ini that cannot be read: nothing read, watch.unreadable() (true the first time: say it
// once), read again at the next look; watch.readable() after a read.
//
// The files: a new one in UTF-8 with "\r\n"; lines added to one that is there take its own line ends; one with UTF-16's
// byte order mark is neither read nor added to (the plugin says once to save it as UTF-8, and runs on its defaults).
#pragma once

#include <edplugin_common/file/file.h>
#include <edplugin_common/ini/ini_table.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace edplugin_common
  {
  enum struct ini_file_e
    {
    read,        ///< read into the settings (also an empty file)
    none,        ///< no such file: the settings as they were
    unreadable,  ///< there, but it could not be read: the settings as they were
    utf16        ///< UTF-16: not read, the settings as they were
    };

  ///\brief the ini at `path` read into `s` by the table (read_ini_text); throws only what allocation throws
  template<typename settings_t, typename group_t>
  [[nodiscard]]
  auto read_ini_file(std::wstring const & path, ini_table_t<settings_t, group_t> const & table, settings_t & s, ini_read_t & r,
                     ini_told_t & told) -> ini_file_e
    {
    r.lines.assign(table.rows.size(), 0);
    std::optional<std::string> const text{read_text(path)};
    if(not text)
      return ini_file_e::unreadable;
    if(text->empty() and write_time(path) == 0)
      return ini_file_e::none;
    if(ini_is_utf16(*text))
      return ini_file_e::utf16;
    read_ini_text(*text, table, s, r, told);
    return ini_file_e::read;
    }

  enum struct ini_complete_e
    {
    nothing,       ///< every setting named already (or a file of its name made meanwhile by another program)
    made,          ///< a new file, or a blank one, filled: `made` and every setting
    added,         ///< `added` and the settings it lacked appended
    unreadable,    ///< there, but it could not be read: nothing written
    utf16,         ///< UTF-16: nothing written
    write_failed,  ///< not written whole: error says why, undone whether the file is as before (taken away, cut back)
    no_memory      ///< the text could not be made
    };

  struct ini_complete_t
    {
    ini_complete_e status;
    std::size_t count;     ///< the settings written with made and added
    unsigned long error;   ///< GetLastError() with write_failed
    bool undone;           ///< with write_failed: the part written taken back
    };

  ///\brief for complete_ini: `text` appended to the ini, a new file when there was none (append_e::create_new), else
  /// at the end of the one that is there (append_e::append_existing)
  [[nodiscard]]
  auto ini_append(std::wstring const & path, std::string const & text, bool there, ini_complete_e status,
                  std::size_t count) noexcept -> ini_complete_t;

  ///\brief the ini completed with the settings it names nowhere, as comments with `values` (ini_completion); `texts`
  /// over them. Never throws
  template<typename settings_t, typename group_t>
  [[nodiscard]]
  auto complete_ini(std::wstring const & path, ini_table_t<settings_t, group_t> const & table, settings_t const & values,
                    ini_texts_t const & texts) noexcept -> ini_complete_t
    {
    try
      {
      bool const there{write_time(path) != 0};
      std::optional<std::string> const text{there ? read_text(path) : std::optional<std::string>{std::string{}}};
      if(not text)
        return {ini_complete_e::unreadable, 0, 0, true};
      if(ini_is_utf16(*text))
        return {ini_complete_e::utf16, 0, 0, true};
      ini_missing_t const missing{ini_completion(*text, table, values, texts)};
      if(missing.count == 0)
        return {ini_complete_e::nothing, 0, 0, true};
      return ini_append(path, missing.text, there, missing.blank ? ini_complete_e::made : ini_complete_e::added, missing.count);
      }
    catch(...)
      {
      return {ini_complete_e::no_memory, 0, 0, true};
      }
    }

  enum struct ini_look_e
    {
    not_due,  ///< looked within the last second
    same,     ///< not changed since it was seen (also: still taken away, said before)
    changed,  ///< saved since it was seen, or asked (ask_soon): read it
    gone      ///< taken away since it was seen: nothing read, the settings stay; once until it is back
    };

  ///\brief a file looked at while the game runs: at most once a second, by its last write. On the publishing thread,
  /// but for ask_soon
  class ini_watch_t
    {
    std::wstring path_;
    std::uint64_t written_{};
    std::uint64_t looked_ms_{};
    bool looked_{};
    bool gone_said_{};
    bool unreadable_said_{};
    std::atomic<bool> asked_{};

  public:
    ini_watch_t() = default;
    ini_watch_t(ini_watch_t const &) = delete;
    auto operator=(ini_watch_t const &) -> ini_watch_t & = delete;

    auto set_path(std::wstring path) -> void { path_ = std::move(path); }

    [[nodiscard]]
    auto path() const noexcept -> std::wstring const &
      {
      return path_;
      }

    ///\brief the file's last write as it is now taken for seen (after the plugin completed and before it reads it)
    auto mark_seen() noexcept -> void;

    ///\brief from any thread (a test's export): the next look reads the file whatever its time
    auto ask_soon() noexcept -> void;

    ///\brief `now_ms` a tick count in milliseconds (GetTickCount64)
    [[nodiscard]]
    auto look(std::uint64_t now_ms) noexcept -> ini_look_e;

    ///\brief the file could not be read: its time forgotten (read again at the next look); true the first time until
    /// readable(), to say it once
    [[nodiscard]]
    auto unreadable() noexcept -> bool;

    ///\brief the file was read
    auto readable() noexcept -> void;
    };
  }  // namespace edplugin_common
