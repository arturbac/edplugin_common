// The checks of edplugin_common's headers without a Windows header, built natively with sanitizers
// (tools/test-native.sh): memory/pe_range.h's ranges, shader/shader.h's hashes and 3DMigoto's name, also from
// several threads at once, tools/context_probe's decoding of jumps (every form cut at every length: no read past the
// bytes given) and its text of slots, and tools/event_report's part without Windows (report_checks.h, with an
// ASCII-only comparison).
#include <edplugin_common/memory/pe_range.h>
#include <edplugin_common/shader/shader.h>

#include "context_probe/jump.h"
#include "context_probe/slots.h"
#include "event_report/report_checks.h"

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

  auto context_probe_jumps() -> void
    {
    using context_probe::decode_jump;
    using context_probe::jump_e;
    // each form whole, then cut to every shorter length in a buffer of exactly that size (ASan: a read past it fails)
    std::vector<std::vector<std::uint8_t>> const forms{
      {0xEB, 0x10},
      {0xE9, 0x00, 0x01, 0x00, 0x00},
      {0xFF, 0x25, 0x08, 0x00, 0x00, 0x00},
      {0x48, 0xFF, 0x25, 0x08, 0x00, 0x00, 0x00},
      {0x48, 0xB8, 1, 2, 3, 4, 5, 6, 7, 8, 0xFF, 0xE0},
      {0x48, 0xB8, 1, 2, 3, 4, 5, 6, 7, 8, 0x50, 0xC3},
      {0x49, 0xBA, 1, 2, 3, 4, 5, 6, 7, 8, 0x41, 0xFF, 0xE2},
      {0x49, 0xBB, 1, 2, 3, 4, 5, 6, 7, 8, 0x41, 0xFF, 0xE3},
      {0x68, 1, 2, 3, 4, 0xC7, 0x44, 0x24, 0x04, 5, 6, 7, 8, 0xC3}};
    bool whole{true}, cut{true};
    for(std::vector<std::uint8_t> const & form: forms)
      {
      whole = whole and decode_jump(form, 0x7FF000001000ull).kind != jump_e::none
              and decode_jump(form, 0x7FF000001000ull).length == form.size();
      for(std::size_t n{}; n != form.size(); ++n)
        {
        std::vector<std::uint8_t> const part(form.begin(), form.begin() + std::ptrdiff_t(n));
        cut = cut and decode_jump(part, 0).kind == jump_e::none;
        }
      }
    check(whole, "decode_jump: each form whole, its length");
    check(cut, "decode_jump: each form cut short is no jump, nothing read past it");
    std::vector<std::uint8_t> const near_top{0xE9, 0x10, 0x00, 0x00, 0x00};
    check(decode_jump(near_top, 0xFFFFFFFFFFFFFFF0ull).target == 0x5ull, "decode_jump: rel32 past the top wraps");

    bool const slots[]{false, true, true, true, false, true, false, true, true};
    check(context_probe::ranges_text(slots) == "1-3, 5, 7-8", "ranges_text: ranges and a single slot");
    bool const none[]{false, false};
    check(context_probe::ranges_text(none) == "none", "ranges_text: none");
    check(context_probe::method_name(context_probe::context_methods, -1) == "?"sv, "method_name: below 0");
    check(context_probe::method_name(context_probe::swap_chain_methods, 8) == "Present"sv,
          "method_name: the swap chain's Present");
    std::vector<bool> const vector_slots{true, true, false, true};
    check(context_probe::ranges_text(vector_slots) == "0-1, 3", "ranges_text: a vector of bool");

    int a{}, b{}, c{};
    std::vector<void *> const before{&a, &b, &c};
    std::vector<void *> const now{&a, &c, &c, &b};
    check(context_probe::ranges_text(context_probe::slots_differ(before, now)) == "1, 3",
          "slots_differ: a changed slot and one only the longer read reached");
    check(context_probe::ranges_text(context_probe::slots_differ(now, before)) == "1, 3", "slots_differ: either way");
    check(context_probe::slots_differ({}, {}).empty(), "slots_differ: two empty reads");

    std::vector<std::string> const was{"a.dll", "b.dll", "b.dll"};
    std::vector<std::string> const is{"b.dll", "c.dll", "b.dll", "b.dll", "a.dll"};
    check(context_probe::names_gained(was, is) == std::vector<std::string>{"c.dll", "b.dll"},
          "names_gained: a new name and a name once more, in the order of now");
    check(context_probe::names_gained(is, was).empty(), "names_gained: none");
    check(context_probe::result_name(2026, 11, 24, 9, 5, 7) == "context_probe-20261124-090507.txt",
          "result_name: the run's local time");
    check(context_probe::result_name(2026, 11, 24, 9, 5, 7, 2) == "context_probe-20261124-090507-2.txt",
          "result_name: a second try in the same second");
    }
  }  // namespace

namespace
  {
  ///\brief memory/pe_range.h with values the compiler does not know (the sanitizers watch the arithmetic)
  auto pe_ranges() -> void
    {
    using namespace edplugin_common;
    std::uint64_t volatile const top{~std::uint64_t{}};
    std::uint64_t volatile const size{0x2000};
    check(rva_in(0x1000, 0x1000, size) and not rva_in(8, top, size) and not rva_in(top, 1, size), "rva_in: within, no overflow");
    check(array_in(0x100, 4, 4, 0x110) and not array_in(0, top / 2, 8, size) and not array_in(0, 1, 0, size),
          "array_in: within, no overflow of count * item, no item of 0 bytes");
    check(rva_inside(0x1000, 0x1000, 1) and not rva_inside(top, 0x1000, 0x10), "rva_inside");
    std::int64_t volatile const negative{-8};
    check(lfanew_ok(0xF8, 264, 0x1000) and not lfanew_ok(negative, 264, size) and not lfanew_ok(4097, 264, size),
          "lfanew_ok: in [64, 4096] and the headers inside");
    }
  }  // namespace

auto main() -> int
  {
  pe_ranges();
  shader_names();
  shader_names_threads();
  context_probe_jumps();
  event_report::report_checks(check, event_report::equal_ascii_fold, false);
  std::printf(failures ? "FAILED (%d failure(s))\n" : "PASSED (%d failure(s))\n", failures);
  return failures ? 1 : 0;
  }
