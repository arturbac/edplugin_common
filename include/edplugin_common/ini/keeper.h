// edplugin_common - the settings thread's state and steps, without threads and without Windows: what a look at the
// ini and a read of it do to a plugin's settings, through its view (ini/view.h), one code for every plugin. The thread,
// its guard and the file's reading are the caller's (a Windows overlay); nothing here logs: the lines go back to it.
//
// One read: the settings object of reads (made once) back to the plugin's defaults, the ini's text read into it, the
// plugin's clamps, the start-only changes against the live object's start fields (a note each, once a session), the
// live rows published. That change runs with the flag "changing" set; a fault in it can leave the object or the
// published slot half written, so a start of the thread's body with the flag still set freezes the settings (no
// further read or publication to the end of the game: the render thread keeps reading the slot it took, which is
// whole). The caller's spent attempts freeze them too. A read that throws (no memory) freezes nothing: publish turns
// the slot only after the whole copy, so nothing half written is shown; the read is said, and the next one tries again.
//
// The caller's contract, on the settings thread: make() once (never deleted: nothing runs at the process's end, the
// destructor is the library test's); body_started() at every start of the thread's body, the first one too;
// attempts_spent() when it gives up; at each look, looked(ini_watch_t::look) and, when true, read() with the file's
// text and ini_watch_t::unreadable()'s answer (the one place a file that cannot be read is said once). Nothing here
// throws: a line that could not be made (no memory) is counted, and the caller logs lines_lost_line with
// take_lines_lost().
#pragma once

#include <edplugin_common/ini/ini.h>
#include <edplugin_common/ini/view.h>

#include <atomic>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace edplugin_common
  {
  namespace detail
    {
    ///\brief for the library's test only: a look at an ini_keeper_t's own members, and its destruction
    struct ini_keeper_peek_t;
    }  // namespace detail

  class ini_keeper_t
    {
  public:
    static constexpr std::string_view fault_line{"a fault while the settings were being changed"};
    static constexpr std::string_view spent_line{
      "the settings thread stopped after its last attempt or a stack overflow"};
    static constexpr std::string_view memory_line{
      "no memory while the ini was being read; nothing published, the settings stay as they were, read again at the "
      "next change"};
    static constexpr std::string_view lines_lost_line{"settings: lines of the settings thread lost (no memory)"};

  private:
    ini_view_t view_;
    std::string path_;  ///< UTF-8, for the lines
    void * read_{};     ///< the plugin's settings object of reads
    ini_read_t r_;
    ini_told_t told_;
    std::atomic<bool> changing_{};
    bool frozen_{};
    std::size_t lines_lost_{};

    friend struct detail::ini_keeper_peek_t;

    ini_keeper_t(ini_view_t const & view, std::string && path, ini_told_t && told, void * read) noexcept :
        view_{view}, path_{std::move(path)}, read_{read}, told_{std::move(told)}
      {
      }

    ~ini_keeper_t() { view_.free_settings(read_); }

    ///\brief "settings: " + the parts as one line into `lines`; counted when it cannot be made
    template<typename... parts_t>
    auto say(std::vector<std::string> & lines, parts_t const &... parts) noexcept -> void
      {
      try
        {
        std::string line{"settings: "};
        (line.append(parts), ...);
        lines.push_back(std::move(line));
        }
      catch(...)
        {
        ++lines_lost_;
        }
      }

    auto freeze(std::string_view why, std::vector<std::string> & lines) noexcept -> void
      {
      frozen_ = true;
      say(lines, why, "; the settings stay as they were published last, frozen to the end of the game");
      }

    ///\brief the change under the flag, in a call of its own (the body's next start sees the flag set after a fault)
    [[gnu::noinline]]
    auto change(std::string_view text) -> void
      {
      changing_.store(true, std::memory_order_relaxed);
      std::atomic_signal_fence(std::memory_order_seq_cst);
      view_.defaults(read_);
      view_.read_text(view_.table, text, read_, r_, told_);
      view_.clamp_settings(view_.clamp, read_);
      view_.start_changes(view_.table, view_.live, read_, r_, told_, r_.notes);
      view_.publish(view_.table, view_.live, read_);
      std::atomic_signal_fence(std::memory_order_seq_cst);
      changing_.store(false, std::memory_order_relaxed);
      }

  public:
    ///\brief the keeper over the plugin's `view`, its ini at `path` (UTF-8, for the lines), and what was said at the
    /// start (`told`, so a withdrawn key is not said again); nullptr when it cannot be made (no memory)
    [[nodiscard]]
    static auto make(ini_view_t const & view, std::string_view path, ini_told_t && told) noexcept -> ini_keeper_t *
      {
      void * read{};
      try
        {
        read = view.make_settings();
        return new ini_keeper_t{view, std::string{path}, std::move(told), read};
        }
      catch(...)
        {
        if(read != nullptr)
          view.free_settings(read);
        return nullptr;
        }
      }

    ini_keeper_t(ini_keeper_t const &) = delete;
    auto operator=(ini_keeper_t const &) -> ini_keeper_t & = delete;

    [[nodiscard]]
    auto frozen() const noexcept -> bool
      {
      return frozen_;
      }

    ///\brief the lines that could not be made since the last call (the caller logs lines_lost_line with the count)
    [[nodiscard]]
    auto take_lines_lost() noexcept -> std::size_t
      {
      return std::exchange(lines_lost_, 0);
      }

    ///\brief at every start of the thread's body, the first one too: the flag still set (a fault in a change) freezes
    /// the settings, with a line
    auto body_started(std::vector<std::string> & lines) noexcept -> void
      {
      if(not frozen_ and changing_.load(std::memory_order_relaxed))
        freeze(fault_line, lines);
      }

    ///\brief the caller's attempts spent: the settings frozen, with a line (once)
    auto attempts_spent(std::vector<std::string> & lines) noexcept -> void
      {
      if(not frozen_)
        freeze(spent_line, lines);
      }

    ///\brief a look at the ini (ini_watch_t::look): true to read it; a file taken away said; frozen: false
    [[nodiscard]]
    auto looked(ini_look_e look, std::vector<std::string> & lines) noexcept -> bool
      {
      if(frozen_)
        return false;
      if(look == ini_look_e::gone)
        say(lines, path_, " was taken away; the settings stay as they were");
      return look == ini_look_e::changed;
      }

    ///\brief a read of the ini: its `text` (nullopt: it could not be read; said when `say_unreadable`, the answer of
    /// ini_watch_t::unreadable()), `there` false for no file. read: the settings published, the read's notes as lines;
    /// unreadable also when the read throws (no memory: nothing published, said); utf16: said once a session; none:
    /// no file, or frozen (nothing done)
    auto read(std::optional<std::string_view> text, bool there, bool say_unreadable,
              std::vector<std::string> & lines) noexcept -> ini_file_e
      {
      if(frozen_)
        return ini_file_e::none;
      if(not text)
        {
        if(say_unreadable)
          say(lines, path_, " could not be read; the settings stay as they were");
        return ini_file_e::unreadable;
        }
      if(text->empty() and not there)
        return ini_file_e::none;
      if(ini_is_utf16(*text))
        {
        bool first{};
        try
          {
          first = told_.first("utf16");
          }
        catch(...)
          {
          ++lines_lost_;
          }
        if(first)
          say(lines, path_, " is UTF-16: not read (save it as UTF-8); the settings stay as they were");
        return ini_file_e::utf16;
        }
      try
        {
        change(*text);
        }
      catch(...)
        {
        changing_.store(false, std::memory_order_relaxed);
        say(lines, memory_line);
        return ini_file_e::unreadable;
        }
      for(std::string const & note: r_.notes)
        say(lines, note);
      return ini_file_e::read;
      }
    };
  }  // namespace edplugin_common
