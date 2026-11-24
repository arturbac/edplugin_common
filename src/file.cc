// edplugin_common - files (include/edplugin_common/file/file.h).
#include <edplugin_common/file/file.h>

#include <edplugin_common/log/log.h>
#include <edplugin_common/text/format.h>

#include <windows.h>

#include <algorithm>

namespace edplugin_common
  {
  auto read_text(std::wstring const & path) noexcept -> std::optional<std::string>
    {
    HANDLE const f{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr)};
    if(f == INVALID_HANDLE_VALUE)
      {
      DWORD const error{GetLastError()};
      if(error == ERROR_FILE_NOT_FOUND or error == ERROR_PATH_NOT_FOUND)
        return std::string{};
      return std::nullopt;
      }
    std::optional<std::string> text;
    try
      {
      text.emplace();
      char buf[4096];
      DWORD got{};
      bool ok{};
      while((ok = ReadFile(f, buf, sizeof buf, &got, nullptr) != 0) and got != 0)
        text->append(buf, got);
      if(not ok)
        text.reset();
      }
    catch(...)
      {
      text.reset();
      }
    CloseHandle(f);
    return text;
    }

  auto write_time(std::wstring const & path) noexcept -> std::uint64_t
    {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if(not GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a))
      return 0;
    return (std::uint64_t{a.ftLastWriteTime.dwHighDateTime} << 32) | a.ftLastWriteTime.dwLowDateTime;
    }

  auto write_whole_file(std::wstring const & path, void const * data, std::size_t bytes) noexcept -> bool
    {
    HANDLE const f{CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if(f == INVALID_HANDLE_VALUE)
      {
      log_line("file: {} could not be made (error {})", wide(path), GetLastError());
      return false;
      }
    auto const * at{static_cast<char const *>(data)};
    bool ok{true};
    for(std::size_t left{bytes}; ok and left != 0;)
      {
      DWORD const chunk{static_cast<DWORD>(std::min<std::size_t>(left, 1u << 30))};
      DWORD written{};
      ok = WriteFile(f, at, chunk, &written, nullptr) and written == chunk;
      at += written;
      left -= written;
      }
    DWORD const error{ok ? 0u : GetLastError()};
    ok = CloseHandle(f) and ok;
    if(not ok)
      {
      DeleteFileW(path.c_str());
      log_line("file: {} not written whole (error {}); removed", wide(path), error ? error : GetLastError());
      }
    return ok;
    }

  auto append_to_file(std::wstring const & path, void const * data, std::size_t bytes) noexcept -> bool
    {
    HANDLE const f{CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr)};
    if(f == INVALID_HANDLE_VALUE)
      return false;
    DWORD written{};
    bool const ok{bytes <= MAXDWORD and WriteFile(f, data, static_cast<DWORD>(bytes), &written, nullptr) and written == bytes};
    CloseHandle(f);
    return ok;
    }

  auto map_file_rw(std::wstring const & path, std::size_t bytes, std::string_view tag) noexcept -> void *
    {
    if(bytes == 0)
      {
      // a mapping's size 0 is the whole file to Windows: refused, not a mapping of whatever is there
      log_line("{}: cannot map {} (error {}); not publishing", tag, wide(path), DWORD{ERROR_INVALID_PARAMETER});
      return nullptr;
      }
    HANDLE const file{CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if(file == INVALID_HANDLE_VALUE)
      {
      log_line("{}: cannot open {} (error {}); not publishing", tag, wide(path), GetLastError());
      return nullptr;
      }
    // the mapping's size grows a smaller file to it
    HANDLE const mapping{CreateFileMappingW(file, nullptr, PAGE_READWRITE, static_cast<DWORD>(std::uint64_t{bytes} >> 32),
                                            static_cast<DWORD>(bytes), nullptr)};
    CloseHandle(file);
    if(not mapping)
      {
      log_line("{}: cannot map {} (error {}); not publishing", tag, wide(path), GetLastError());
      return nullptr;
      }
    // the view keeps the mapping alive; neither handle is needed past this point
    void * const view{MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes)};
    CloseHandle(mapping);
    if(not view)
      log_line("{}: cannot view {} (error {}); not publishing", tag, wide(path), GetLastError());
    return view;
    }

  auto map_file_ro(std::wstring const & path, std::size_t least, std::size_t most) noexcept -> mapped_t
    {
    if(most == 0)
      return {nullptr, 0};  // a view's size 0 is the whole file to Windows
    HANDLE const file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if(file == INVALID_HANDLE_VALUE)
      return {nullptr, 0};
    LARGE_INTEGER size{};
    if(not GetFileSizeEx(file, &size) or size.QuadPart <= 0 or static_cast<std::uint64_t>(size.QuadPart) < least)
      {
      CloseHandle(file);
      return {nullptr, 0};
      }
    std::size_t const bytes{static_cast<std::uint64_t>(size.QuadPart) < most ? static_cast<std::size_t>(size.QuadPart) : most};
    HANDLE const mapping{CreateFileMappingW(file, nullptr, PAGE_READONLY, static_cast<DWORD>(std::uint64_t{bytes} >> 32),
                                            static_cast<DWORD>(bytes), nullptr)};
    CloseHandle(file);
    if(not mapping)
      return {nullptr, 0};
    void const * const view{MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, bytes)};
    CloseHandle(mapping);
    return view ? mapped_t{view, bytes} : mapped_t{nullptr, 0};
    }
  }  // namespace edplugin_common
