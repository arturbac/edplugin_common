// The library's test under wine (tools/test.sh): a plain program, without d3d11 or dxgi, linked with the library as
// a plugin links it. Checked: the library's interface definitions reach the program and stralgo comes with it.
#include <stralgo/stralgo.h>

#include <cstdio>
#include <string>
#include <string_view>

using namespace std::string_view_literals;

namespace
  {
  int failures{};

  auto check(bool ok, char const * what) -> void
    {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if(not ok)
      ++failures;
    }
  }  // namespace

auto main() -> int
  {
#if defined(UNICODE) and defined(_UNICODE) and defined(NOMINMAX) and defined(WIN32_LEAN_AND_MEAN)
  check(true, "the library's definitions: UNICODE, _UNICODE, NOMINMAX, WIN32_LEAN_AND_MEAN");
#else
  check(false, "the library's definitions: UNICODE, _UNICODE, NOMINMAX, WIN32_LEAN_AND_MEAN");
#endif
  std::wstring const path{stralgo::stl::merge(L"C:\\game"sv, L"\\plugin.log"sv)};
  check(path == L"C:\\game\\plugin.log"sv, "stralgo merge through the library");

  std::printf(failures ? "FAILED (%d failure(s))\n" : "PASSED (%d failure(s))\n", failures);
  return failures ? 1 : 0;
  }
