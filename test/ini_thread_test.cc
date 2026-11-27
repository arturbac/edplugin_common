// The checks of the settings thread [u] on Windows (ini/thread.h, src/ini_thread.cc), under wine, with a table of the
// test's own: the thread started by ini_live_start reads a changed ini and publishes it, a wake makes a pass at once, a
// fault in a change starts the body again and freezes the settings, an extension that always faults spends the
// attempts and freezes them too, each said in the log. Each check has its own live object, watch and ini in the
// directory `ini_thread` under `dir`; all of them are never deleted, as in a plugin: the threads go on to the
// process's end, waiting.
#include <edplugin_common/file/file.h>
#include <edplugin_common/ini/thread.h>
#include <edplugin_common/log/log.h>

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace std::string_view_literals;

namespace
  {
  using check_t = void (*)(bool ok, char const * what);
  check_t check{};

  using namespace edplugin_common;

  struct thread_settings_t
    {
    float level{1.f};          ///< live, 0..4; 3 faults in the clamps (a fault in a change)
    std::uint32_t count{7};    ///< start-only
    };

  ini_setting_t<thread_settings_t> const rows[]{
    {"level", &thread_settings_t::level, 0, setting_read_e::live, "0..4", "the level"},
    {"count", &thread_settings_t::count, 0, setting_read_e::at_start, "", "how many"},
  };
  ini_table_t<thread_settings_t> const table{rows};
  // the threads read the table to the process's end: nothing of it destroyed at the exit
  static_assert(std::is_trivially_destructible_v<ini_setting_t<thread_settings_t>>
                and std::is_trivially_destructible_v<ini_table_t<thread_settings_t>>);

  std::uint8_t * closed{};  ///< a page that cannot be read or written

  [[gnu::noinline]]
  auto touch(void * p) -> void
    {
    *static_cast<int volatile *>(p) = 1;
    }

  auto clamp(thread_settings_t & s) noexcept -> void
    {
    if(s.level == 3.f)
      touch(closed);
    s.level = s.level < 0.f ? 0.f : s.level > 4.f ? 4.f : s.level;
    }

  auto told(std::string_view line) noexcept -> void { log_line("{}", line); }

  ///\brief the ini written beside it and moved over it: the thread never reads it cut short (an empty file would be
  /// read as every value's default and published)
  auto put(std::wstring const & path, std::string_view text) -> bool
    {
    std::wstring const part{path + L".part"};
    return write_whole_file(part, text.data(), text.size())
           and MoveFileExW(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
    }

  ///\brief the plugin's side of a start: its live object started, its watch marked seen, the thread started
  struct plugin_t
    {
    live3_settings_t<thread_settings_t> * live{};
    ini_watch_t * watch{};
    ini_thread_t * thread{};
    };

  auto start(std::wstring const & ini, std::string_view text, int attempts) -> plugin_t
    {
    plugin_t p{new live3_settings_t<thread_settings_t>{}, new ini_watch_t{}, nullptr};
    p.live->start(thread_settings_t{}, &told);
    put(ini, text);
    p.watch->set_path(ini);
    p.watch->mark_seen();
    p.thread = ini_live_start(table, *p.live, &clamp, *p.watch, ini_told_t{}, attempts);
    return p;
    }

  ///\brief the live object's generation past `generation` within `ms` (taken on this thread only, its consumer)
  auto published(plugin_t const & p, std::uint32_t generation, DWORD ms) -> bool
    {
    for(ULONGLONG const end{GetTickCount64() + ms}; GetTickCount64() < end; Sleep(20))
      if(p.live->acquire().generation > generation)
        return true;
    return p.live->acquire().generation > generation;
    }

  auto read_log(std::wstring const & path) -> std::string { return read_text(path).value_or(std::string{}); }

  ///\brief `part` in the log within `ms`
  auto logged(std::wstring const & path, std::string_view part, DWORD ms) -> bool
    {
    for(ULONGLONG const end{GetTickCount64() + ms}; GetTickCount64() < end; Sleep(50))
      if(read_log(path).find(part) != std::string::npos)
        return true;
    return read_log(path).find(part) != std::string::npos;
    }

  auto reads(std::wstring const & dir) -> void
    {
    std::wstring const ini{dir + L"\\reads.ini"};
    plugin_t const p{start(ini, "level = 0.5\n", 8)};
    check(p.thread != nullptr, "ini_live_start: the settings thread started");
    if(p.thread == nullptr)
      return;
    Sleep(1200);
    check(p.live->acquire().generation == 0u, "settings thread: an ini not changed since the start is not read");
    put(ini, "level = 1.5\ncount = 9\n");
    check(published(p, 0, 3000) and p.live->acquire().settings.level == 1.5f
            and p.live->acquire().settings.count == 7u,
          "settings thread: a changed ini read and published within its look's second, start-only fields as at the "
          "start");
    put(ini, "level = 9\n");
    check(published(p, 1, 3000) and p.live->acquire().settings.level == 4.f,
          "settings thread: the plugin's clamps on a read");
    // two wakes 100 ms apart, each asked by ask_soon: two reads within 400 ms. Without the wakes the asks wait for the
    // look's rhythm of a second: one read at most in less than a second (checked, as a slow Sleep can stretch it)
    std::uint32_t const before{p.live->acquire().generation};
    ULONGLONG const asked{GetTickCount64()};
    p.watch->ask_soon();
    ini_thread_wake(*p.thread);
    Sleep(100);
    p.watch->ask_soon();
    ini_thread_wake(*p.thread);
    bool const two{published(p, before + 1, 300)};
    ULONGLONG const took{GetTickCount64() - asked};
    check(two and took < 900u, "ini_thread_wake: a pass at once, two wakes two reads (asked by ask_soon)");
    }

  auto fault_in_change(std::wstring const & dir, std::wstring const & log) -> void
    {
    std::wstring const ini{dir + L"\\fault.ini"};
    plugin_t const p{start(ini, "level = 0.5\n", 8)};
    if(p.thread == nullptr)
      {
      check(false, "ini_live_start: the settings thread started (a fault in a change)");
      return;
      }
    put(ini, "level = 2\n");
    check(published(p, 0, 3000) and p.live->acquire().settings.level == 2.f,
          "settings thread: read before the fault");
    put(ini, "level = 3\n");
    check(logged(log, ini_keeper_t::fault_line, 5000),
          "settings thread: a fault in a change - the body started again, the settings frozen, said");
    std::uint32_t const frozen{p.live->acquire().generation};
    put(ini, "level = 0.25\n");
    p.watch->ask_soon();
    ini_thread_wake(*p.thread);
    Sleep(1500);
    check(p.live->acquire().generation == frozen and p.live->acquire().settings.level == 2.f,
          "settings thread: frozen - a changed ini not read, the slot published before the fault kept");
    }

  std::atomic<int> always_faults{};

  auto faulting_step(void *, ini_keeper_t &, std::vector<std::string> &) noexcept -> std::uint32_t
    {
    always_faults.fetch_add(1, std::memory_order_relaxed);
    touch(closed);
    return ini_thread_t::look_ms;
    }

  ini_extension_t const faulting{nullptr, &faulting_step};

  auto attempts_spent(std::wstring const & dir, std::wstring const & log) -> void
    {
    std::wstring const ini{dir + L"\\spent.ini"};
    plugin_t const p{start(ini, "level = 0.5\n", 2)};
    if(p.thread == nullptr)
      {
      check(false, "ini_live_start: the settings thread started (attempts spent)");
      return;
      }
    check(p.thread->extend(&faulting), "ini_thread_t::extend on the running thread");
    ini_thread_wake(*p.thread);
    check(logged(log, ini_keeper_t::spent_line, 6000) and logged(log, "thread settings: stopped after 2 fault(s)", 1000)
            and always_faults.load(std::memory_order_relaxed) == 2,
          "settings thread: an extension that always faults - the attempts spent, the settings frozen, said");
    put(ini, "level = 1\n");
    Sleep(1500);
    check(p.live->acquire().generation == 0u, "settings thread: after the last attempt nothing read");
    }
  }  // namespace

auto ini_thread_tests(void (*checker)(bool ok, char const * what), std::wstring const & here) -> void
  {
  check = checker;
  closed = static_cast<std::uint8_t *>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS));
  std::wstring const dir{here + L"\\ini_thread"};
  CreateDirectoryW(dir.c_str(), nullptr);
  std::wstring const log{dir + L"\\ini_thread.log"};
  log_open(dir, L"ini_thread");
  reads(dir);
  fault_in_change(dir, log);
  attempts_spent(dir, log);
  log_close();
  }
