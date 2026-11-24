// edplugin_common - a plugin's log (include/edplugin_common/log/log.h).
#include <edplugin_common/log/log.h>

#include <edplugin_common/core/lock.h>
#include <edplugin_common/core/once_flag.h>
#include <edplugin_common/text/format.h>

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <type_traits>

using namespace std::string_view_literals;

namespace edplugin_common
  {
  namespace
    {
    constexpr std::size_t line_room{2048};
    constexpr std::size_t batch_room{16384};

    // The module's log. Every global here is constant-initialised and trivially destructible: the library adds no
    // constructor or destructor to a plugin's dll.
    // The open log's handle, nullptr when there is none: stored with release, loaded with acquire. Read before the
    // lock only to decide whether a line is made at all; a write loads it again under `lock`, where log_open and
    // log_close change it, so a handle is never written to after it is closed.
    constinit std::atomic<HANDLE> file{nullptr};
    // no std::mutex: its lock may throw, and a line is logged where nothing may
    constinit SRWLOCK lock = SRWLOCK_INIT;
    // the thread whose batch it is, 0 for none. Taken by a CAS with acquire; given up with release only after what
    // the batch gathered is written and batch_used is 0, so the next owner starts on an empty batch it sees as such.
    // batch and batch_used belong to the owner alone
    constinit std::atomic<DWORD> batch_thread{};
    constinit char batch[batch_room]{};
    constinit std::size_t batch_used{};
    // a failed write is said once to a debugger: the log is where it would be said
    constinit once_flag_t write_failed;
    // off for the session after a fault in the log's own write (log_break)
    constinit std::atomic<bool> broken{};
    // the test's replacement of WriteFile, nullptr for none
    constinit std::atomic<detail::log_write_t> write_function{};

    static_assert(std::is_trivially_destructible_v<std::atomic<HANDLE>> and std::is_trivially_destructible_v<SRWLOCK>
                  and std::is_trivially_destructible_v<std::atomic<DWORD>> and std::is_trivially_destructible_v<once_flag_t>);

    ///\brief the room a line is made in, and an output iterator that stops at its end
    struct cut_buffer_t
      {
      char * at;
      char * end;
      };

    struct cut_out_t
      {
      using difference_type = std::ptrdiff_t;
      cut_buffer_t * b;

      auto operator*() noexcept -> cut_out_t & { return *this; }

      auto operator=(char c) noexcept -> cut_out_t &
        {
        if(b->at != b->end)
          *b->at++ = c;
        return *this;
        }

      auto operator++() noexcept -> cut_out_t & { return *this; }

      auto operator++(int) noexcept -> cut_out_t { return *this; }
      };

    ///\brief a line made in `buf`: its whole size with the line end, and where its text starts (after the stamp)
    struct line_t
      {
      std::size_t size;
      std::size_t text_at;
      };

    auto make_line(char (&buf)[line_room], std::string_view fmt, std::format_args args) noexcept -> line_t
      {
      SYSTEMTIME t;
      GetSystemTime(&t);
      cut_buffer_t room{buf, buf + line_room - 1};  // one byte kept for the line's end
      char const * text_at{buf};
      try
        {
        std::format_to(cut_out_t{&room}, "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                       t.wMilliseconds);
        text_at = room.at;
        std::vformat_to(cut_out_t{&room}, fmt, args);
        }
      catch(...)
        {
        for(char const c: " (log: the rest of this line could not be formatted)"sv)
          if(room.at != room.end)
            *room.at++ = c;
        }
      *room.at++ = '\n';
      return {static_cast<std::size_t>(room.at - buf), static_cast<std::size_t>(text_at - buf)};
      }

    ///\brief the last error's code, never 0 (a failed call that left none)
    auto last_error() noexcept -> DWORD
      {
      DWORD const e{GetLastError()};
      return e != 0 ? e : ERROR_GEN_FAILURE;
      }

    ///\brief bytes written to the open log under its lock; nothing when it is not open
    auto write_out(char const * data, std::size_t size) noexcept -> void
      {
      if(broken.load(std::memory_order_relaxed))
        return;
      exclusive_lock_t const hold{lock};
      HANDLE const h{file.load(std::memory_order_acquire)};
      if(h == nullptr)
        return;
      DWORD written{};
      detail::log_write_t const write{write_function.load(std::memory_order_relaxed)};
      BOOL const ok{write ? write(h, data, static_cast<DWORD>(size), &written, nullptr)
                          : WriteFile(h, data, static_cast<DWORD>(size), &written, nullptr)};
      if((not ok or written != size) and write_failed.first())
        {
        wchar_t text[96];
        if(try_format_into(text, L"edplugin_common log: a write failed (error {}); said once\n", last_error()))
          OutputDebugStringW(text);
        }
      }

    ///\brief what this thread's batch gathered written and the batch emptied; the batch stays this thread's
    auto write_batch() noexcept -> void
      {
      if(batch_used != 0)
        write_out(batch, batch_used);
      batch_used = 0;
      }

    ///\brief a text for a debugger (OutputDebugString) where the log itself cannot take it
    template<typename... args_t>
    auto say(std::wformat_string<args_t...> fmt, args_t &&... args) noexcept -> void
      {
      wchar_t text[1024];
      if(try_format_into(text, fmt, std::forward<args_t>(args)...))
        OutputDebugStringW(text);
      else
        OutputDebugStringW(L"edplugin_common log: a text could not be made\n");
      }

    ///\brief the file `path` moved to `target` in the directory `sub` (made when missing); 0, or the code of what
    /// failed, `not_dir` set when `sub` is there but is not a directory
    auto set_aside(std::wstring const & path, std::wstring const & sub, std::wstring const & target, bool & not_dir) noexcept -> DWORD
      {
      // CreateDirectoryW takes at most MAX_PATH - 12 characters with the NUL
      if(sub.size() > MAX_PATH - 13 or target.size() > MAX_PATH - 1)
        return ERROR_FILENAME_EXCED_RANGE;
      if(not CreateDirectoryW(sub.c_str(), nullptr))
        {
        DWORD const e{last_error()};
        if(e != ERROR_ALREADY_EXISTS)
          return e;
        DWORD const attributes{GetFileAttributesW(sub.c_str())};
        if(attributes == INVALID_FILE_ATTRIBUTES or (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
          {
          not_dir = true;
          return ERROR_ALREADY_EXISTS;
          }
        }
      if(not MoveFileExW(path.c_str(), target.c_str(), 0))
        return last_error();
      return 0;
      }
    }  // namespace

  auto log_open(std::wstring_view dir, std::wstring_view name) noexcept -> bool
    {
    if(log_is_open())
      return true;
    try
      {
      if(dir.empty() or name.empty())
        {
        say(L"edplugin_common log: no directory or no name for the log of {}\n", name);
        return false;
        }
      std::wstring const path{std::wstring{dir} + L'\\' + std::wstring{name} + L".log"};
      if(path.size() > MAX_PATH - 1)
        {
        say(L"{}: the log {} not opened: its path is longer than {} characters\n", name, path, MAX_PATH - 1);
        return false;
        }
      std::wstring target;
      DWORD aside_error{};
      bool not_dir{};
      DWORD open_error{};
        {
        exclusive_lock_t const hold{lock};
        if(file.load(std::memory_order_acquire) != nullptr)
          return true;
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if(GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a) and (a.nFileSizeHigh != 0 or a.nFileSizeLow != 0))
          {
          SYSTEMTIME t{};
          FileTimeToSystemTime(&a.ftLastWriteTime, &t);
          wchar_t stamp[32];
          (void)try_format_into(stamp, L".{:04}{:02}{:02}T{:02}{:02}{:02}Z.log", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
          std::wstring const sub{std::wstring{dir} + L'\\' + std::wstring{name}};
          target = sub + L'\\' + std::wstring{name} + stamp;
          aside_error = set_aside(path, sub, target, not_dir);
          }
        HANDLE const h{CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr)};
        if(h == INVALID_HANDLE_VALUE)
          open_error = last_error();
        else
          file.store(h, std::memory_order_release);
        }
      if(open_error != 0)
        {
        say(L"{}: the log {} could not be opened (error {})\n", name, path, open_error);
        return false;
        }
      if(aside_error != 0)
        log_line("log: the last session's log could not be set aside as {} (error {}{}); appending to it", wide(target), aside_error,
                 not_dir ? ", not a directory" : "");
      return true;
      }
    catch(...)
      {
      say(L"edplugin_common log: the log of {} not opened (no memory for its path)\n", name);
      return false;
      }
    }

  auto log_is_open() noexcept -> bool { return file.load(std::memory_order_acquire) != nullptr; }

  auto log_vline(std::string_view fmt, std::format_args args, line_tap_t tap) noexcept -> void
    {
    if(broken.load(std::memory_order_relaxed) or (tap == nullptr and file.load(std::memory_order_acquire) == nullptr))
      return;
    char buf[line_room];
    line_t const line{make_line(buf, fmt, args)};
    if(tap != nullptr)
      tap(std::string_view{buf + line.text_at, line.size - line.text_at - 1});
    if(file.load(std::memory_order_acquire) == nullptr)
      return;
    if(batch_thread.load(std::memory_order_relaxed) == GetCurrentThreadId())
      {
      if(batch_used + line.size > batch_room)
        write_batch();  // full: what is gathered goes out, the batch goes on
      std::memcpy(batch + batch_used, buf, line.size);
      batch_used += line.size;
      return;
      }
    write_out(buf, line.size);
    }

  auto log_batch_begin() noexcept -> void
    {
    DWORD expected{};
    // another thread's batch: this thread's lines are written at once; this thread's own: nothing (no nesting)
    batch_thread.compare_exchange_strong(expected, GetCurrentThreadId(), std::memory_order_acquire, std::memory_order_relaxed);
    }

  auto log_batch_end() noexcept -> void
    {
    // only this thread stores its own id, so a relaxed load tells whether the batch is this thread's
    if(batch_thread.load(std::memory_order_relaxed) != GetCurrentThreadId())
      return;
    write_batch();
    batch_thread.store(0, std::memory_order_release);
    }

  auto log_vline_to(std::wstring const & path, std::string_view fmt, std::format_args args) noexcept -> void
    {
    char buf[line_room];
    line_t const line{make_line(buf, fmt, args)};
    HANDLE const h{CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr)};
    if(h == INVALID_HANDLE_VALUE)
      {
      say(L"edplugin_common log: a line could not be appended to {} (error {})\n", path, last_error());
      return;
      }
    DWORD written{};
    if(not WriteFile(h, buf, static_cast<DWORD>(line.size), &written, nullptr) or written != line.size)
      say(L"edplugin_common log: a line not appended whole to {} (error {})\n", path, last_error());
    CloseHandle(h);
    }

  auto log_break() noexcept -> void
    {
    if(not broken.exchange(true, std::memory_order_relaxed))
      OutputDebugStringW(L"edplugin_common log: a fault in the log's own write; no line is written for the rest of the session\n");
    }

  auto log_is_broken() noexcept -> bool { return broken.load(std::memory_order_relaxed); }

  auto detail::log_write_function(log_write_t write) noexcept -> log_write_t
    {
    return write_function.exchange(write, std::memory_order_relaxed);
    }

  auto log_close() noexcept -> void
    {
    exclusive_lock_t const hold{lock};
    HANDLE const h{file.exchange(nullptr, std::memory_order_acq_rel)};
    if(h != nullptr)
      CloseHandle(h);
    }
  }  // namespace edplugin_common
