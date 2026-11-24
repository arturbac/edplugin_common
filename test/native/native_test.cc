// The checks of edplugin_common's headers without a Windows header, built natively with sanitizers
// (tools/test-native.sh): shader/shader.h's hashes and 3DMigoto's name, also from several threads at once.
#include <edplugin_common/shader/shader.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::string_view_literals;

namespace
  {
  int failures{};

  auto check(bool ok, char const * what) -> void
    {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    failures += ok ? 0 : 1;
    }

  auto shader_names() -> void
    {
    using namespace edplugin_common;
    std::uint8_t const ab[]{'a', 'b'};
    check(fnv1a64(ab, 1) == 0x44bd8ad473cd9906ull, "fnv1a64 of \"a\"");
    check(fnv1a64(ab + 1, 1, fnv1a64(ab, 1)) == fnv1a64(ab, 2), "fnv1a64 goes on from a hash so far");
    check(fnv1a64(static_cast<void const *>(ab), 2) == fnv1a64(ab, 2), "fnv1a64 of a void pointer");
    check(fnv_3dmigoto(static_cast<void const *>(ab), 2) == 0x61000000a4b1ull, "fnv_3dmigoto of \"ab\"");
    char text[24];
    check(migoto_name_text(0x61000000a4b1ull, text) == "3dmgt_000061000000a4b1"sv, "migoto_name_text");
    check(std::strlen(text) == 22, "migoto_name_text: the text ends with a NUL");
    check(migoto_name_text(~0ull, text) == "3dmgt_ffffffffffffffff"sv, "migoto_name_text: the largest name");
    }

  // the hashes are pure: 4 threads over the same bytes at once, nothing shared but the bytes they read
  auto shader_names_threads() -> void
    {
    std::vector<std::uint8_t> const data(1 << 16, 7);
    std::uint64_t const one{edplugin_common::fnv1a64(data.data(), data.size())};
    std::atomic<int> same{};
    std::vector<std::thread> threads;
    for(int t{}; t != 4; ++t)
      threads.emplace_back([&] { same += edplugin_common::fnv1a64(data.data(), data.size()) == one ? 1 : 0; });
    for(std::thread & t: threads)
      t.join();
    check(same == 4, "fnv1a64 from 4 threads at once");
    }
  }  // namespace

auto main() -> int
  {
  shader_names();
  shader_names_threads();
  std::printf(failures ? "FAILED (%d failure(s))\n" : "PASSED (%d failure(s))\n", failures);
  return failures ? 1 : 0;
  }
