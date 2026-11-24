// edplugin_common - a plugin's ini file (include/edplugin_common/ini/ini.h).
#include <edplugin_common/ini/ini.h>

#include <windows.h>

namespace edplugin_common
  {
  auto ini_append(std::wstring const & path, std::string const & text, bool there, ini_complete_e status,
                  std::size_t count) noexcept -> ini_complete_t
    {
    bool undone{};
    if(append_to_file(path, text.data(), text.size(), there ? append_e::append_existing : append_e::create_new, &undone))
      return {status, count, 0, true};
    DWORD const error{GetLastError()};
    // made meanwhile by another program (the player's editor): kept as it is, completed at the next start
    if(not there and (error == ERROR_FILE_EXISTS or error == ERROR_ALREADY_EXISTS))
      return {ini_complete_e::nothing, 0, 0, true};
    return {ini_complete_e::write_failed, 0, error, undone};
    }

  auto ini_watch_t::mark_seen() noexcept -> void
    {
    written_ = write_time(path_);
    // no file at the start: not "taken away" at the first look
    gone_said_ = written_ == 0;
    }

  auto ini_watch_t::ask_soon() noexcept -> void { asked_.store(true, std::memory_order_release); }

  auto ini_watch_t::look(std::uint64_t now_ms) noexcept -> ini_look_e
    {
    // the game never asks: one relaxed load a look; the exchange only when asked
    bool const asked{asked_.load(std::memory_order_relaxed) and asked_.exchange(false, std::memory_order_acquire)};
    if(not asked and looked_ and now_ms - looked_ms_ < 1000u)
      return ini_look_e::not_due;
    looked_ = true;
    looked_ms_ = now_ms;
    std::uint64_t const w{write_time(path_)};
    if(w == 0)
      {
      // taken away (an editor that saves by a new file and a rename, for a moment): the settings stay; read when back
      written_ = 0;
      if(gone_said_)
        return ini_look_e::same;
      gone_said_ = true;
      return ini_look_e::gone;
      }
    gone_said_ = false;
    if(not asked and w == written_)
      return ini_look_e::same;
    written_ = w;
    return ini_look_e::changed;
    }

  auto ini_watch_t::unreadable() noexcept -> bool
    {
    written_ = 0;
    if(unreadable_said_)
      return false;
    unreadable_said_ = true;
    return true;
    }

  auto ini_watch_t::readable() noexcept -> void { unreadable_said_ = false; }
  }  // namespace edplugin_common
