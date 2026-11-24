// edplugin_common - files: a text read whole, a file's last write, a file written whole or appended to, a file mapped
// to memory shared with other processes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace edplugin_common
  {
  ///\brief the file's bytes as text; empty when there is no such file; none when it is there but cannot be read
  /// (opened, read, or the memory for it), so a caller does not take a file it could not read for an empty one
  [[nodiscard]]
  auto read_text(std::wstring const & path) noexcept -> std::optional<std::string>;

  ///\brief the file's last write (a FILETIME as a number); 0 when there is no such file
  [[nodiscard]]
  auto write_time(std::wstring const & path) noexcept -> std::uint64_t;

  ///\brief the file made anew with `bytes` bytes. A file not written whole is removed; a failure is a line in the
  /// module's log (`file: ...`) with the error's code
  auto write_whole_file(std::wstring const & path, void const * data, std::size_t bytes) noexcept -> bool;

  ///\brief `bytes` bytes appended to the file (made when missing); false when not all were written. Logs nothing
  auto append_to_file(std::wstring const & path, void const * data, std::size_t bytes) noexcept -> bool;

  ///\brief the file (made when missing, its directory not; grown to `bytes` when smaller) mapped for reading and
  /// writing, shared with other processes; the view stays for the process's life (nothing unmaps it). A failure is a
  /// line in the module's log, `<tag>: cannot open|map|view <path> (error N); not publishing`, and nullptr; `bytes` 0
  /// is refused (error 87: Windows would take it for the whole file)
  [[nodiscard]]
  auto map_file_rw(std::wstring const & path, std::size_t bytes, std::string_view tag) noexcept -> void *;

  struct mapped_t
    {
    void const * view;
    std::size_t bytes;
    };

  ///\brief another process's file mapped for reading: at most `most` bytes of it, none when there is no such file,
  /// it is smaller than `least`, or it cannot be mapped; none for `most` 0. Logs nothing (a caller tries again later);
  /// the view stays for the process's life
  [[nodiscard]]
  auto map_file_ro(std::wstring const & path, std::size_t least, std::size_t most) noexcept -> mapped_t;
  }  // namespace edplugin_common
