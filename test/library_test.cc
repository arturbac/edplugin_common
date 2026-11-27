// The library's test under wine (tools/test.sh): a plain program, without d3d11 or dxgi, linked with the library as
// a plugin links it. Checked: the library's interface definitions reach the program, stralgo comes with it, and the
// modules core (locks, once_flag_t), text (wide, utf8, as_unsigned, try_format_into), log and state_log_t
// (test/log_test.cc), paths, file, memory, shader and dxgi (test/modules_test.cc), settings_doc
// (test/settings_doc_test.cc), ini with files and append_to_file's modes (test/ini_test.cc), memory/pe.h
// (test/pe_test.cc), the context's hooks with a context of the test's own (test/context_test.cc) and guard
// (test/guard_test.cc).
#include <edplugin_common/core/lock.h>
#include <edplugin_common/core/once_flag.h>
#include <edplugin_common/text/format.h>

#include <stralgo/stralgo.h>

#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace std::string_view_literals;

// test/log_test.cc
auto log_tests(void (*checker)(bool ok, char const * what)) -> void;
// test/modules_test.cc
auto modules_tests(void (*checker)(bool ok, char const * what)) -> void;
// test/settings_doc_test.cc
auto settings_doc_tests(void (*checker)(bool ok, char const * what)) -> void;
// test/ini_test.cc
auto ini_tests(void (*checker)(bool ok, char const * what)) -> void;
// test/guard_test.cc
auto guard_tests(void (*checker)(bool ok, char const * what), std::wstring const & dir) -> void;
auto guard_log_break_tests(void (*checker)(bool ok, char const * what), std::wstring const & dir) -> void;
// test/pe_test.cc
auto pe_tests(void (*checker)(bool ok, char const * what)) -> void;
// test/context_test.cc
auto context_tests(void (*checker)(bool ok, char const * what), std::wstring const & dir) -> void;

namespace
  {
  int failures{};

  auto check(bool ok, char const * what) -> void
    {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if(not ok)
      ++failures;
    }

  ///\brief a value whose formatting throws, for try_format_into's failure
  struct throws_t
    {
    };

  enum struct colour_e : int
    {
    red = -1
    };
  }  // namespace

template<>
struct std::formatter<throws_t, char> : std::formatter<std::string_view, char>
  {
  auto format(throws_t const &, std::format_context &) const -> std::format_context::iterator
    {
    throw std::runtime_error{"no text"};
    }
  };

// a plugin's own format_into over try_format_into, called unqualified with arguments of the library's types: compiled
// here so that argument-dependent lookup finding the library's function shows at build time
namespace plugin_like
  {
  using edplugin_common::utf8;
  using edplugin_common::wide;

  int failed_texts{};

  template<typename char_t, typename... args_t>
  auto format_into(char_t * buf, std::size_t size, std::type_identity_t<std::basic_format_string<char_t, args_t...>> fmt,
                   args_t &&... args) noexcept -> std::size_t
    {
    std::optional<std::size_t> const n{edplugin_common::try_format_into(buf, size, fmt, std::forward<args_t>(args)...)};
    if(not n)
      ++failed_texts;
    return n.value_or(0);
    }

  template<typename char_t, std::size_t size, typename... args_t>
  auto format_into(char_t (&buf)[size], std::type_identity_t<std::basic_format_string<char_t, args_t...>> fmt, args_t &&... args) noexcept
    -> std::size_t
    {
    return format_into(&buf[0], size, fmt, std::forward<args_t>(args)...);
    }

  auto texts() -> void
    {
    char narrow[64];
    std::size_t n{format_into(narrow, "a {} b", wide(L"za\u017C\u00F3\u0142\u0107"))};
    check(std::string_view{narrow, n} == "a za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87 b"sv, "a plugin's format_into with wide(), unqualified");
    wchar_t wide_text[64];
    n = format_into(wide_text, L"[{}]", utf8("\xC5\x82\xC3\xB3"));
    check(std::wstring_view{wide_text, n} == L"[\u0142\u00F3]"sv, "a plugin's format_into with utf8(), unqualified");
    check(format_into(narrow, sizeof narrow, "{}", throws_t{}) == 0 and failed_texts == 1, "a plugin's format_into: a failure counted once");
    }
  }  // namespace plugin_like

namespace
  {
  auto held_exclusively(SRWLOCK & lock) -> void
    {
    edplugin_common::exclusive_lock_t const held{lock};
    check(not TryAcquireSRWLockShared(&lock), "exclusive_lock_t: held exclusively in its scope");
    }

  auto held_shared(SRWLOCK & lock) -> void
    {
    edplugin_common::shared_lock_t const held{lock};
    bool const shared_too{TryAcquireSRWLockShared(&lock) != 0};
    check(shared_too, "shared_lock_t: another shared hold at the same time");
    if(shared_too)
      ReleaseSRWLockShared(&lock);
    check(not TryAcquireSRWLockExclusive(&lock), "shared_lock_t: no exclusive hold at the same time");
    }

  auto locks() -> void
    {
    SRWLOCK lock = SRWLOCK_INIT;
    held_exclusively(lock);
    bool const free_after{TryAcquireSRWLockExclusive(&lock) != 0};
    check(free_after, "exclusive_lock_t: let go at the end of its scope");
    if(free_after)
      ReleaseSRWLockExclusive(&lock);
    held_shared(lock);
    bool const free_again{TryAcquireSRWLockExclusive(&lock) != 0};
    check(free_again, "shared_lock_t: let go at the end of its scope");
    if(free_again)
      ReleaseSRWLockExclusive(&lock);
    }

  constinit edplugin_common::once_flag_t global_once;

  auto once() -> void
    {
    check(not global_once.done(), "once_flag_t: constant-initialised, not done");
    bool const first{global_once.first()};
    bool const second{global_once.first()};
    check(first and not second and global_once.done(), "once_flag_t: true once, then false");
    global_once.reset();
    check(global_once.first(), "once_flag_t: true again after reset");
    }

  auto unsigned_values() -> void
    {
    check(edplugin_common::as_unsigned(HRESULT(0x80004005)) == 0x80004005u, "as_unsigned: a negative HRESULT as its bits");
    check(edplugin_common::as_unsigned(colour_e::red) == 0xFFFFFFFFu, "as_unsigned: an enumeration as its value's bits");
    check(edplugin_common::as_unsigned(7u) == 7u, "as_unsigned: an unsigned value as it is");
    }

  auto wide_texts() -> void
    {
    check(std::format("{}", edplugin_common::wide(L"plugin.log")) == "plugin.log", "wide: ASCII");
    check(std::format("{}", edplugin_common::wide(static_cast<wchar_t const *>(nullptr))) == "(null)", "wide: nullptr as (null)");
    // a surrogate pair across the 120 characters of one piece
    std::wstring text(119, L'a');
    text += L"\U0001F600b";
    std::string expected(119, 'a');
    expected += "\xF0\x9F\x98\x80" "b";
    check(std::format("{}", edplugin_common::wide(text)) == expected, "wide: a surrogate pair at a piece's end stays whole");
    }

  auto utf8_texts() -> void
    {
    check(std::format(L"{}", edplugin_common::utf8(static_cast<char const *>(nullptr))) == L"(null)", "utf8: nullptr as (null)");
    // a two-byte character across the 240 bytes of one piece
    std::string text(239, 'a');
    text += "\xC5\x82z";
    std::wstring expected(239, L'a');
    expected += L"\u0142z";
    check(std::format(L"{}", edplugin_common::utf8(text)) == expected, "utf8: a character at a piece's end stays whole");
    // a four-byte character whose last three bytes fall past the piece's 240: back by all three
    text.assign(237, 'a');
    text += "\xF0\x9F\x98\x80z";
    expected.assign(237, L'a');
    expected += L"\U0001F600z";
    check(std::format(L"{}", edplugin_common::utf8(text)) == expected, "utf8: a four-byte character at a piece's end stays whole");
    }

  ///\brief text that is not UTF-8 (300 continuation bytes after one letter) in a thread: an endless loop would
  /// otherwise hold the test until wine's time limit
  auto utf8_not_utf8() -> void
    {
    static std::wstring made;
    HANDLE const thread{CreateThread(
      nullptr, 0,
      [](void *) -> DWORD
      {
        std::string const text{"A" + std::string(300, '\x80')};
        made = std::format(L"{}", edplugin_common::utf8(text));
        return 0;
      },
      nullptr, 0, nullptr)};
    if(not thread)
      {
      check(false, "utf8: the thread for bytes that are not UTF-8 could not start");
      return;
      }
    if(WaitForSingleObject(thread, 5000) != WAIT_OBJECT_0)
      {
      check(false, "utf8: bytes that are not UTF-8 end in time (5 s)");
      std::printf("FAILED (%d failure(s)), stopped at an endless loop\n", failures);
      ExitProcess(1);
      }
    CloseHandle(thread);
    check(not made.empty() and made.front() == L'A', "utf8: bytes that are not UTF-8 end, with text");
    }

  auto format_texts() -> void
    {
    char buf[8];
    std::optional<std::size_t> n{edplugin_common::try_format_into(buf, "{}", 1234567890)};
    check(n == 7u and std::string_view{buf} == "1234567", "try_format_into: cut at the buffer's end, with its NUL");
    n = edplugin_common::try_format_into(buf, 0, "{}", 1);
    check(n == 0u, "try_format_into: a buffer of size 0 left alone");
    std::memcpy(buf, "xxxxxxx", 8);
    n = edplugin_common::try_format_into(buf, "a{}", throws_t{});
    check(not n and buf[0] == '\0', "try_format_into: a text that cannot be made is none, the buffer empty");
    }
  }  // namespace

auto main() -> int
  {
  // every line out at once: a fault in a check leaves the lines before it in the output
  std::setvbuf(stdout, nullptr, _IONBF, 0);
#if defined(UNICODE) and defined(_UNICODE) and defined(NOMINMAX) and defined(WIN32_LEAN_AND_MEAN)
  check(true, "the library's definitions: UNICODE, _UNICODE, NOMINMAX, WIN32_LEAN_AND_MEAN");
#else
  check(false, "the library's definitions: UNICODE, _UNICODE, NOMINMAX, WIN32_LEAN_AND_MEAN");
#endif
  std::wstring const path{stralgo::stl::merge(L"C:\\game"sv, L"\\plugin.log"sv)};
  check(path == L"C:\\game\\plugin.log"sv, "stralgo merge through the library");

  locks();
  once();
  unsigned_values();
  wide_texts();
  utf8_texts();
  utf8_not_utf8();
  format_texts();
  plugin_like::texts();
  log_tests(check);
  modules_tests(check);
  settings_doc_tests(check);
  ini_tests(check);
  pe_tests(check);
  wchar_t here[MAX_PATH]{};
  GetCurrentDirectoryW(MAX_PATH, here);
  context_tests(check, here);
  guard_tests(check, here);
  // last: the log is off for the rest of the process after it
  guard_log_break_tests(check, here);

  std::printf(failures ? "FAILED (%d failure(s))\n" : "PASSED (%d failure(s))\n", failures);
  return failures ? 1 : 0;
  }
