// edplugin_common - a plugin's log: one file per game session, the last session's file set aside in a directory of
// its own, time-stamped lines of at most 2 KB written with Windows' own calls, and a batch of lines one thread
// gathers and writes at once.
// One log per module: the state is the module's own (every plugin's dll links its own copy of the library), and one
// name stands for one module in a process. The library logs nothing and closes nothing at DLL_PROCESS_DETACH (the
// game's threads are killed at its exit); nothing here may be called from DllMain.
#pragma once

#include <format>
#include <string>
#include <string_view>

namespace edplugin_common
  {
  ///\brief a copy of each line's text (without its time stamp and line end), handed over once per line before it is
  /// written, outside the log's lock: also in a batch, and when the log is not open
  using line_tap_t = void (*)(std::string_view text) noexcept;

  ///\brief the log `<dir>\<name>.log` opened for this session. A non-empty file there from the last session is set
  /// aside first as `<dir>\<name>\<name>.<UTC of its last write>.log` (the directory made when missing); when that
  /// cannot be done, this session appends to it and logs why, with the error's code. A second call while the log is
  /// open does nothing and is true, whatever it is given. Else false, said by OutputDebugString, for an empty `dir` or
  /// `name`, a path of the log longer than 259 characters or a file that cannot be opened
  auto log_open(std::wstring_view dir, std::wstring_view name) noexcept -> bool;

  ///\brief whether the log is open
  [[nodiscard]]
  auto log_is_open() noexcept -> bool;

  ///\brief one line: a UTC time stamp with milliseconds, the text, a line end, cut to 2 KB; a text that cannot be
  /// made is cut where it failed and says so. Written at once under the log's lock, or gathered in this thread's
  /// batch. Without a tap and without an open log nothing is formatted
  auto log_vline(std::string_view fmt, std::format_args args, line_tap_t tap = nullptr) noexcept -> void;

  ///\brief log_vline with the format checked against its arguments when compiled
  template<typename... args_t>
  auto log_line(std::format_string<args_t...> fmt, args_t &&... args) noexcept -> void
    {
    log_vline(fmt.get(), std::make_format_args(args...));
    }

  ///\brief from here this thread's lines are gathered (16 KB) and written at once by log_batch_end, or when the
  /// batch is full. One thread's batch at a time: another thread's lines are written at once meanwhile. Batches do
  /// not nest: a second begin in the same thread does nothing, the first end ends the batch. Without an open log
  /// nothing is gathered. The thread ends the batch on every way out, a fault too (a fault handler calls
  /// log_batch_end): a batch never ended keeps its lines and stays that thread's for good
  auto log_batch_begin() noexcept -> void;

  ///\brief what this thread's batch gathered written, and the batch given up; nothing in a thread without a batch
  auto log_batch_end() noexcept -> void;

  ///\brief one line as log_vline makes it appended to the file `path` (opened, written, closed), apart from the log
  auto log_vline_to(std::wstring const & path, std::string_view fmt, std::format_args args) noexcept -> void;

  ///\brief log_vline_to with the format checked against its arguments when compiled
  template<typename... args_t>
  auto log_line_to(std::wstring const & path, std::format_string<args_t...> fmt, args_t &&... args) noexcept -> void
    {
    log_vline_to(path, fmt.get(), std::make_format_args(args...));
    }

  ///\brief the log closed; for tests (a plugin leaves it open to the end of the process)
  auto log_close() noexcept -> void;

  ///\brief the log off for the rest of the session: no line made or written from now (a fault in the log's own write,
  /// guard/guard.h); said once by OutputDebugString
  auto log_break() noexcept -> void;

  ///\brief whether the log is off for the session (log_break)
  [[nodiscard]]
  auto log_is_broken() noexcept -> bool;

  namespace detail
    {
    using log_write_t = int (*)(void * file, void const * data, unsigned long size, unsigned long * written, void * overlapped);

    ///\brief for the library's test only: the function the log writes its file with (nullptr: WriteFile); the one before
    auto log_write_function(log_write_t write) noexcept -> log_write_t;
    }  // namespace detail
  }  // namespace edplugin_common
