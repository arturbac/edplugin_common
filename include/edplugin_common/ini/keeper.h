// edplugin_common - the settings thread's [u] state and steps, without threads and without Windows: what a look at the
// ini and a read of it do to a plugin's settings, through its view (ini/view.h), one code for every plugin. The thread,
// its guard and the file's reading are the caller's (a Windows overlay); nothing here logs: the lines go back to it.
//
// One read: the settings object of reads (made once) back to the plugin's defaults, the ini's text read into it, the
// plugin's clamps, the start-only changes against the live object's start fields (a note each, once a session), the
// live rows published. That change runs with the flag "changing" set; a fault in it can leave the object or the
// published slot half written, so a restart of the thread's body after it freezes the settings (no further read or
// publication to the end of the game: the render thread keeps reading the slot it took, which is whole). The settings
// freeze too when the caller's attempts are spent, or when a read throws (no memory).
//
// Made once with new by the caller and never deleted (nothing runs at the process's end); its destructor (for tests)
// frees the object of reads. All of it on the settings thread.
#pragma once

#include <edplugin_common/ini/ini.h>
#include <edplugin_common/ini/view.h>

#include <atomic>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace edplugin_common
  {
  namespace detail
    {
    ///\brief for the library's test only: a look at an ini_keeper_t's own members
    struct ini_keeper_peek_t;
    }  // namespace detail

  class ini_keeper_t
    {
    ini_view_t view_;
    std::string path_;  ///< UTF-8, for the lines
    void * read_{};     ///< the plugin's settings object of reads
    ini_read_t r_;
    ini_told_t told_;
    std::atomic<bool> changing_{};
    bool started_{};
    bool frozen_{};
    bool unreadable_said_{};
    bool utf16_said_{};

    friend struct detail::ini_keeper_peek_t;

    auto freeze(std::string_view why, std::vector<std::string> & lines) -> void
      {
      frozen_ = true;
      lines.push_back("settings: " + std::string{why} + "; the settings stay as they were published last, frozen to the "
                      "end of the game");
      }

    ///\brief the change under the flag, in a call of its own (a fault handler's restart sees the flag set)
    [[gnu::noinline]]
    auto change(std::string_view text) -> void
      {
      changing_.store(true, std::memory_order_relaxed);
      view_.defaults(read_);
      view_.read_text(view_.table, text, read_, r_, told_);
      view_.clamp_settings(view_.clamp, read_);
      view_.start_changes(view_.table, view_.live, read_, r_, told_, r_.notes);
      view_.publish(view_.table, view_.live, read_);
      changing_.store(false, std::memory_order_relaxed);
      }

  public:
    static constexpr std::string_view fault_line{"a fault while the settings were being changed"};
    static constexpr std::string_view spent_line{"the settings thread stopped after its last attempt"};
    static constexpr std::string_view memory_line{"no memory while the settings were being changed"};

    ///\brief over the plugin's `view`, its ini at `path` (UTF-8, for the lines), and what was said at the start (`told`,
    /// so a withdrawn key is not said again). Throws only what allocation throws
    ini_keeper_t(ini_view_t view, std::string path, ini_told_t told) :
        view_{view}, path_{std::move(path)}, told_{std::move(told)}
      {
      read_ = view_.make_settings();
      }

    ini_keeper_t(ini_keeper_t const &) = delete;
    auto operator=(ini_keeper_t const &) -> ini_keeper_t & = delete;

    ~ini_keeper_t() { view_.free_settings(read_); }

    [[nodiscard]]
    auto frozen() const noexcept -> bool
      {
      return frozen_;
      }

    ///\brief at each start of the thread's body: nothing the first time; after a fault in a change (the flag still set)
    /// the settings frozen, with a line
    auto body_started(std::vector<std::string> & lines) -> void
      {
      if(not started_)
        {
        started_ = true;
        return;
        }
      if(not frozen_ and changing_.load(std::memory_order_relaxed))
        freeze(fault_line, lines);
      }

    ///\brief the caller's attempts spent: the settings frozen, with a line
    auto attempts_spent(std::vector<std::string> & lines) -> void
      {
      if(not frozen_)
        freeze(spent_line, lines);
      }

    ///\brief a look at the ini (ini_watch_t::look): true to read it; a file taken away said
    [[nodiscard]]
    auto looked(ini_look_e look, std::vector<std::string> & lines) -> bool
      {
      if(frozen_)
        return false;
      if(look == ini_look_e::gone)
        lines.push_back("settings: " + path_ + " was taken away; the settings stay as they were");
      return look == ini_look_e::changed;
      }

    ///\brief a read of the ini: its `text` (nullopt: it could not be read), `there` false for no file. Read: the
    /// settings published and the read's notes as lines; could not be read and UTF-16 said once until a read; a read
    /// that throws (no memory) freezes the settings and gives unreadable. Frozen: none, nothing done
    auto read(std::optional<std::string_view> text, bool there, std::vector<std::string> & lines) -> ini_file_e
      {
      if(frozen_)
        return ini_file_e::none;
      if(not text)
        {
        if(not std::exchange(unreadable_said_, true))
          lines.push_back("settings: " + path_ + " could not be read; the settings stay as they were");
        return ini_file_e::unreadable;
        }
      if(text->empty() and not there)
        return ini_file_e::none;
      if(ini_is_utf16(*text))
        {
        if(not std::exchange(utf16_said_, true))
          lines.push_back("settings: " + path_ + " is UTF-16: not read (save it as UTF-8); the settings stay as they were");
        return ini_file_e::utf16;
        }
      unreadable_said_ = false;
      utf16_said_ = false;
      try
        {
        change(*text);
        }
      catch(...)
        {
        freeze(memory_line, lines);
        return ini_file_e::unreadable;
        }
      for(std::string const & note: r_.notes)
        lines.push_back("settings: " + note);
      return ini_file_e::read;
      }
    };
  }  // namespace edplugin_common
