// edplugin_common - the settings thread [u] on Windows (include/edplugin_common/ini/thread.h): its keeper, state and
// thread, the ini's watch and reads by the library's file calls, the wait on its event, its lines logged.
#include <edplugin_common/file/file.h>
#include <edplugin_common/guard/guard.h>
#include <edplugin_common/ini/thread.h>
#include <edplugin_common/log/log.h>
#include <edplugin_common/text/format.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace edplugin_common
  {
  namespace
    {
    auto watch_of(void * c) noexcept -> ini_watch_t & { return *static_cast<ini_watch_t *>(c); }

    auto look(void * c, std::uint64_t now_ms) noexcept -> ini_look_e { return watch_of(c).look(now_ms); }

    auto read(void * c) noexcept -> std::optional<std::string> { return read_text(watch_of(c).path()); }

    auto there(void * c) noexcept -> bool { return write_time(watch_of(c).path()) != 0; }

    auto unreadable(void * c) noexcept -> bool { return watch_of(c).unreadable(); }

    auto readable(void * c) noexcept -> void { watch_of(c).readable(); }

    auto log_lines(ini_thread_t & t, std::vector<std::string> & lines) noexcept -> void
      {
      for(std::string const & line: lines)
        log_line("{}", line);
      lines.clear();
      if(std::size_t const lost{t.keeper().take_lines_lost()}; lost != 0)
        log_line("{} ({})", ini_keeper_t::lines_lost_line, lost);
      }

    ///\brief the thread's body: never returns; after a fault run_guarded_thread starts it again. A wait on the event
    /// that fails is slept instead, said once when it starts failing and once when it waits again
    auto body(void * arg) -> DWORD
      {
      ini_thread_t & t{*static_cast<ini_thread_t *>(arg)};
      std::vector<std::string> lines;
      t.body_started(lines);
      log_lines(t, lines);
      bool wait_failed{};
      for(;;)
        {
        std::uint32_t const wait{t.pass(GetTickCount64(), lines)};
        log_lines(t, lines);
        if(WaitForSingleObject(static_cast<HANDLE>(t.wake()), wait) == WAIT_FAILED)
          {
          if(not wait_failed)
            log_line("settings: the settings thread's wait failed (error {}); it sleeps instead, a wake not heard",
                     GetLastError());
          wait_failed = true;
          Sleep(wait);
          }
        else if(wait_failed)
          {
          log_line("settings: the settings thread waits on its event again");
          wait_failed = false;
          }
        }
      }

    auto WINAPI thread_main(LPVOID arg) noexcept -> DWORD
      {
      ini_thread_t & t{*static_cast<ini_thread_t *>(arg)};
      DWORD const result{run_guarded_thread("settings", &body, arg, t.attempts())};
      // the body never returns: here only after its last attempt
      std::vector<std::string> lines;
      t.attempts_spent(lines);
      log_lines(t, lines);
      return result;
      }
    }  // namespace

  // what was made before a failure is never deleted, as when the thread runs (the keeper's destructor is not ours)
  auto ini_thread_start(ini_view_t const & view, ini_watch_t & watch, ini_told_t && told, int attempts) noexcept
    -> ini_thread_t *
    {
    std::string path;
    try
      {
      path = std::format("{}", wide(watch.path()));
      }
    catch(...)
      {
      log_line("settings: the settings thread not started (no memory); the ini is not read again while the game runs");
      return nullptr;
      }
    ini_keeper_t * const keeper{ini_keeper_t::make(view, path, std::move(told))};
    if(keeper == nullptr)
      {
      log_line("settings: the settings thread not started (no memory); the ini is not read again while the game runs");
      return nullptr;
      }
    HANDLE const wake{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    if(wake == nullptr)
      {
      log_line("settings: the settings thread not started: no event (error {}); the ini is not read again while the "
               "game runs",
               GetLastError());
      return nullptr;
      }
    ini_files_t const files{&watch, &look, &read, &there, &unreadable, &readable};
    ini_thread_t * const t{ini_thread_t::make(*keeper, files, wake, attempts)};
    if(t == nullptr)
      {
      CloseHandle(wake);
      log_line("settings: the settings thread not started (no memory); the ini is not read again while the game runs");
      return nullptr;
      }
    // the thread runs to the process's end: the module of its code pinned, never unloaded under it
    HMODULE module{};
    if(not GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&thread_main), &module))
      log_line("settings: the plugin's module not pinned (error {}); the settings thread runs while it stays loaded",
               GetLastError());
    HANDLE const thread{CreateThread(nullptr, 0, &thread_main, t, 0, nullptr)};
    if(thread == nullptr)
      {
      log_line("settings: the settings thread could not be made (error {}); the ini is not read again while the game "
               "runs",
               GetLastError());
      return nullptr;
      }
    CloseHandle(thread);
    return t;
    }

  auto ini_thread_wake(ini_thread_t & thread) noexcept -> void
    {
    if(not SetEvent(static_cast<HANDLE>(thread.wake())))
      log_line("settings: the settings thread not woken (error {}); it reads the ini at its next look",
               GetLastError());
    }
  }  // namespace edplugin_common
