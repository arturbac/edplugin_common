// The checks of edplugin_common's headers without a Windows header, built natively with sanitizers
// (tools/test-native.sh): dxgi/device_removed.h's names of codes, memory/pe_range.h's ranges, memory/pass_modules.h's
// reading of a list, shader/shader.h's hashes and 3DMigoto's name, also from several threads at once, the module ini
// without Windows (ini_native.cc), tools/context_probe's decoding of jumps (every form cut at every length: no read
// past the bytes given) and its text of slots, and tools/event_report's part without Windows (report_checks.h, with an
// ASCII-only comparison), hook/context_rule.h's pick of the way to watch the context and hook/own_table_mark.h's
// stubs and checks of a mark, from numbers the compiler does not know, and hook/slot_plan.h's order of a write of
// slots (through memory calls of the test's own, also with a thread reading the slot meanwhile) and
// hook/watch_class.h's classes of a watched entry and of a table not ours.
#include <edplugin_common/dxgi/device_removed.h>
#include <edplugin_common/hook/context_rule.h>
#include <edplugin_common/hook/own_table_mark.h>
#include <edplugin_common/hook/slot_plan.h>
#include <edplugin_common/hook/watch_class.h>
#include <edplugin_common/memory/pass_modules.h>
#include <edplugin_common/memory/pe_range.h>
#include <edplugin_common/shader/shader.h>

#include "context_probe/jump.h"
#include "context_probe/slots.h"
#include "event_report/report_checks.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::string_view_literals;

// ini_native.cc
auto ini_native_checks(void (*checker)(bool ok, char const * what)) -> void;

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
    check(context_probe::slots_differ(std::vector<void *>{}, std::vector<void *>{}).empty(),
          "slots_differ: two empty reads");

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

  ///\brief the names of a list, as they come; an empty one stands for itself
  constexpr auto pass_names(std::wstring_view list) -> std::vector<std::wstring_view>
    {
    std::vector<std::wstring_view> names;
    while(not list.empty())
      names.push_back(edplugin_common::next_pass_name(list));
    return names;
    }

  static_assert(pass_names(edplugin_common::default_pass_modules) == std::vector{L"bdhkm64.dll"sv});

  static_assert(edplugin_common::device_code_name(0x887A0006u) == "DXGI_ERROR_DEVICE_HUNG"sv
                and edplugin_common::device_code_name(0u) == "S_OK"sv and edplugin_common::device_code_name(1u).empty());
  static_assert(edplugin_common::present_lost(0x887A0005u) and edplugin_common::present_lost(0x887A0007u)
                and not edplugin_common::present_lost(0u) and not edplugin_common::present_lost(0x887A0006u));
  static_assert(pass_names(L" a.dll ,\tb.dll\t") == std::vector{L"a.dll"sv, L"b.dll"sv});
  static_assert(pass_names(L"a.dll,,b.dll,") == std::vector{L"a.dll"sv, L""sv, L"b.dll"sv});
  static_assert(pass_names(L"").empty());

  ///\brief memory/pass_modules.h's reading of a list, from a text the compiler does not know
  auto pass_module_names() -> void
    {
    std::wstring const text{L"  bdhkm64.dll, ,x.dll  ,"};
    auto const names{pass_names(text)};
    check(names == std::vector{L"bdhkm64.dll"sv, L""sv, L"x.dll"sv}, "next_pass_name: names trimmed, an empty one kept");
    std::wstring_view rest{L"one"};
    check(edplugin_common::next_pass_name(rest) == L"one"sv and rest.empty(), "next_pass_name: the last name leaves none");
    }
  }  // namespace

namespace
  {
  ///\brief hook/context_rule.h from numbers the compiler does not know: the same cases as its static_asserts
  auto context_rule_picks() -> void
    {
    using namespace edplugin_common::hook;
    std::uint32_t volatile const length{319};
    std::uint16_t volatile const system{96};
    context_input_t in{table_memory_e::private_memory, table_module_e::none, system, 0, 0, false, 4, length};
    check(context_pick(in) == context_pick_t{context_way_e::own_table, context_point_e::own_table},
          "context_pick: Windows' table of 96/96 and 319 entries, own table");
    in.length = 148;
    check(context_pick(in).point == context_point_e::too_short, "context_pick: 148 entries for N = 4, too short");
    in.length = length;
    in.system_entries = 70;
    in.other_entries = 1;
    in.no_module_entries = 25;
    check(context_pick(in).point == context_point_e::few_modules, "context_pick: 71 of 96 in modules, in place");
    in.other_entries = 2;
    in.no_module_entries = 24;
    check(context_pick(in).way == context_way_e::own_table, "context_pick: 72 of 96 in modules, own table");
    in.no_module_entries = 25;
    check(context_pick(in).point == context_point_e::input_invalid, "context_pick: 97 of the first 96, in place");
    }

  ///\brief hook/own_table_mark.h from numbers the compiler does not know: a stub and its target, a mark accepted and
  /// each reason one is not, a foreign candidate before ours
  auto own_table_marks() -> void
    {
    using namespace edplugin_common::hook;
    std::uint64_t volatile const runtime{0x7FF0'0000'1000};
    std::uint64_t volatile const table{0x200'0010};
    std::uint64_t volatile const stubs{0x300'0040};
    std::uint32_t volatile const slot{12};
    auto const ours{[&](std::uint32_t s)
                    {
                      return mark_reading_t{true, s, stubs + s * own_stub_step, own_table_mark(runtime, table, 319, stubs, 1),
                                            own_stub_bytes(runtime + s * std::uint64_t{8})};
                    }};
    check(own_stub_target(own_stub_bytes(runtime)) == runtime, "own_stub_target: the entry's address back");
    mark_reading_t r{ours(slot)};
    check(mark_check(r, table) == mark_check_e::accepted, "mark_check: our mark, slot 12");
    check(mark_check(r, table + 8) == mark_check_e::entries, "mark_check: the context on another table");
    r.stub[1] = 0xB9;
    check(mark_check(r, table) == mark_check_e::stub_code, "mark_check: mov rcx instead of mov rax");
    r = ours(slot);
    r.stub = own_stub_bytes(runtime);
    check(mark_check(r, table) == mark_check_e::stub_target, "mark_check: slot 12's stub through entry 0");
    r = ours(slot);
    r.mark.count = slot;
    check(mark_check(r, table) == mark_check_e::count, "mark_check: the slot past the mark's entries");
    r = ours(slot);
    r.mark.layout = own_table_layout + 1;
    check(mark_check(r, table) == mark_check_e::layout, "mark_check: another layout");
    std::array<mark_reading_t, 3> readings{ours(3), ours(slot), ours(40)};
    readings[0].mark.magic = 0;
    readings[1].read = false;
    mark_search_t const found{first_mark(readings, table)};
    check(found.mark and found.mark->entries == table and found.candidates == 3,
          "first_mark: two candidates not ours, the third taken");
    mark_candidates_t candidates{};
    for(std::uint32_t i{}; i != 96; ++i)
      candidates.add(0x1000 * (i % 6), i);
    check(candidates.count == own_mark_candidates and candidates.slot[0] == 1 and candidates.base[3] == 0x4000,
          "mark_candidates_t: four bases, each once, none null");
    }
  }  // namespace

namespace
  {
  ///\brief memory calls of the test's own for hook/slot_plan.h: three pages of a table, their protection kept here, every
  /// call written down in `calls` (P made writable, W written, R given back)
  namespace fake_pages
    {
    std::size_t constexpr size{4096};
    std::size_t constexpr per_page{size / sizeof(void *)};
    alignas(size) std::array<void *, 3 * per_page> table{};
    std::array<std::uint32_t, 3> protect{};
    std::array<bool, 3> committed{};
    int refuse{-1};                    ///< the page whose VirtualProtect fails
    void ** overwrite_on_give_back{};  ///< a slot written over when a page's protection is given back
    std::string calls;

    auto index(std::uintptr_t page) -> std::size_t
      {
      return (page - reinterpret_cast<std::uintptr_t>(table.data())) / size;
      }

    auto reset(std::uint32_t all) -> void
      {
      for(std::size_t i{}; i != table.size(); ++i)
        table[i] = reinterpret_cast<void *>(0x1000 + i);
      protect.fill(all);
      committed.fill(true);
      refuse = -1;
      overwrite_on_give_back = nullptr;
      calls.clear();
      }

    auto protection(std::uintptr_t page, std::uint32_t & out) noexcept -> bool
      {
      out = protect[index(page)];
      return committed[index(page)];
      }

    auto change(std::uintptr_t page, std::uint32_t to, std::uint32_t & old, std::uint32_t & error) noexcept -> bool
      {
      std::size_t const i{index(page)};
      bool const give_back{edplugin_common::hook::writable(protect[i])};
      if(static_cast<int>(i) == refuse and not give_back)
        {
        error = 5;
        return false;
        }
      calls += (give_back ? "R" : "P") + std::to_string(i);
      if(give_back and overwrite_on_give_back)
        std::atomic_ref<void *>{*overwrite_on_give_back}.store(reinterpret_cast<void *>(0xBAD), std::memory_order_release);
      old = protect[i];
      protect[i] = to;
      return true;
      }

    auto written(void * p, std::size_t) noexcept -> void
      {
      calls += "W" + std::to_string(static_cast<void **>(p) - table.data());
      }

    edplugin_common::hook::page_ops_t constexpr ops{size, &protection, &change, &written};
    }  // namespace fake_pages

  ///\brief hook/slot_plan.h: every page made writable before the first write and given back after the last, a page
  /// refused leaving the table as it was, a slot already ours or listed twice not written over, a write over ours seen
  /// on the read back, a thread reading the slot meanwhile (TSan)
  auto slot_writes() -> void
    {
    using namespace edplugin_common::hook;
    namespace f = fake_pages;
    std::size_t const second{f::per_page + 3};
    void * const a{reinterpret_cast<void *>(0xA)};
    void * const b{reinterpret_cast<void *>(0xB)};

    f::reset(page::read_only);
    std::array<slot_patch_t, 2> list{slot_patch_t{12, a}, slot_patch_t{second, b}};
    slots_patched_t r{patch_slots_by(f::ops, f::table.data(), list)};
    check(r.all_ours() and r.patched == 2 and r.pages_changed == 2 and r.restore_failed == 0,
          "patch_slots_by: two slots on two read-only pages written");
    check(f::calls == "P0P1W12W" + std::to_string(second) + "R0R1", "patch_slots_by: both pages writable before the first write");
    check(list[0].original == reinterpret_cast<void *>(0x1000 + 12) and list[1].result == slot_result_e::patched
            and f::table[12] == a and f::protect[0] == page::read_only and f::protect[1] == page::read_only,
          "patch_slots_by: the originals, the hooks in place, the protection back");

    f::reset(page::execute_read);
    f::refuse = 1;
    list = {slot_patch_t{12, a}, slot_patch_t{second, b}};
    r = patch_slots_by(f::ops, f::table.data(), list);
    check(r.result == slots_patch_e::page_protect and r.error == 5 and r.failed_slot == second
            and f::calls == "P0R0" and f::protect[0] == page::execute_read,
          "patch_slots_by: the second page refused, the first page's protection back");
    check(f::table[12] == reinterpret_cast<void *>(0x1000 + 12) and list[0].result == slot_result_e::not_writable
            and list[0].original == nullptr,
          "patch_slots_by: nothing written when a page is refused");

    f::reset(page::read_write);
    f::table[12] = a;
    list = {slot_patch_t{12, a}, slot_patch_t{13, b}};
    r = patch_slots_by(f::ops, f::table.data(), list);
    check(r.all_ours() and r.already_ours == 1 and r.patched == 1 and r.pages_changed == 0 and f::calls == "W13"
            and list[0].result == slot_result_e::already_ours and list[0].original == nullptr,
          "patch_slots_by: a writable page untouched, a slot already ours not written");

    f::reset(page::read_write);
    list = {slot_patch_t{20, a}, slot_patch_t{20, a}};
    r = patch_slots_by(f::ops, f::table.data(), list);
    check(r.patched == 1 and r.already_ours == 1 and list[1].result == slot_result_e::already_ours
            and list[0].original == reinterpret_cast<void *>(0x1000 + 20),
          "patch_slots_by: a slot listed twice, the hook never its own original");

    f::reset(page::read_only);
    f::overwrite_on_give_back = &f::table[12];
    list = {slot_patch_t{12, a}, slot_patch_t{14, b}};
    r = patch_slots_by(f::ops, f::table.data(), list);
    check(not r.all_ours() and r.read_back_differs == 1 and r.patched == 1
            and list[0].result == slot_result_e::read_back_differs and list[0].original == reinterpret_cast<void *>(0x1000 + 12),
          "patch_slots_by: a slot written over before the read back");

    f::reset(page::read_only);
    f::committed[1] = false;
    list = {slot_patch_t{12, a}, slot_patch_t{second, b}};
    r = patch_slots_by(f::ops, f::table.data(), list);
    check(r.result == slots_patch_e::page_unusable and f::calls.empty() and list[0].result == slot_result_e::not_writable,
          "patch_slots_by: a page not committed, nothing changed");

    f::reset(page::read_only | page::guard);
    list = {slot_patch_t{12, a}, slot_patch_t{13, b}};
    r = patch_slots_by(f::ops, f::table.data(), list);
    check(r.result == slots_patch_e::page_unusable and r.failed_protect == (page::read_only | page::guard) and f::calls.empty(),
          "patch_slots_by: a guard page, nothing changed");

    f::reset(page::read_write);
    std::array<slot_patch_t, slot_pages_most + 1> wide{};
    for(std::size_t i{}; i != wide.size(); ++i)
      wide[i] = slot_patch_t{i * 8, a};
    // the pages are those of the table's entries (three): a list across more pages needs entries past them, so the
    // count is checked through a table seen as having one entry a page
    page_ops_t small{f::ops};
    small.page_size = 64;
    r = patch_slots_by(small, f::table.data(), wide);
    check(r.result == slots_patch_e::too_many_pages and r.failed_slot == slot_pages_most * 8 and f::calls.empty()
            and f::table[0] == reinterpret_cast<void *>(0x1000),
          "patch_slots_by: a list across nine pages, nothing written");

    f::reset(page::read_write);
    std::atomic<bool> seen{};
    // the reader gives up after 5 s: a write that never comes fails the check instead of hanging the test
    std::thread reader{[&]
                       {
                         auto const until{std::chrono::steady_clock::now() + std::chrono::seconds{5}};
                         while(std::atomic_ref<void *>{f::table[40]}.load(std::memory_order_acquire) != b)
                           {
                           if(std::chrono::steady_clock::now() > until)
                             return;
                           std::this_thread::yield();
                           }
                         seen = true;
                       }};
    std::array<slot_patch_t, 1> one{slot_patch_t{40, b}};
    r = patch_slots_by(f::ops, f::table.data(), one);
    reader.join();
    check(r.all_ours() and seen, "patch_slots_by: a thread reading the slot meanwhile sees the hook");
    }
  }  // namespace

namespace
  {
  ///\brief hook/watch_class.h from numbers the compiler does not know: each class of an entry per way of watching, and
  /// each class of a table not ours, with the stubs of a plugin above in both forms
  auto watch_classes() -> void
    {
    using namespace edplugin_common::hook;
    std::uint64_t volatile const hook{0x1'0000};
    std::uint64_t volatile const stub{0x3'0040};
    slot_watch_t const own{12, hook, stub};
    watch_ranges_t const stubs{0x3'0000, 0x3'1000};
    std::uint64_t const runtime_entry{0x7FF8'0000'1230};
    check(classify_entry(own, hook, entry_place_e::other_module, runtime_entry, stubs) == slot_class_e::ours,
          "classify_entry: our hook");
    check(classify_entry(own, stub + 16, entry_place_e::no_module, runtime_entry, stubs) == slot_class_e::taken,
          "classify_entry: another of our stubs in the slot");
    check(classify_entry(own, runtime_entry, entry_place_e::system_d3d11, runtime_entry, stubs)
            == slot_class_e::taken_by_runtime,
          "classify_entry: own table, the runtime's entry written into it");
    check(classify_entry(own, 0x9'0000, entry_place_e::no_module, runtime_entry, stubs) == slot_class_e::covered,
          "classify_entry: own table, another hook's stub over ours");
    // in place on 3Dmigoto's table (no runtime's table): another plugin over ours, then 3Dmigoto's own entry back
    slot_watch_t const migoto{12, hook, 0x5'0000};
    check(classify_entry(migoto, 0x6'0000, entry_place_e::other_module, std::nullopt, watch_ranges_t{})
            == slot_class_e::covered,
          "classify_entry: in place on 3Dmigoto, another plugin's hook over ours");
    check(classify_entry(migoto, 0x5'0000, entry_place_e::other_module, std::nullopt, watch_ranges_t{}) == slot_class_e::taken,
          "classify_entry: in place on 3Dmigoto, 3Dmigoto's entry back");
    // in place on Windows' table: the runtime switching its variant of the method
    slot_watch_t const runtime_in_place{12, hook, 0x7FF8'0000'1000};
    check(classify_entry(runtime_in_place, 0x7FF8'0000'2000, entry_place_e::system_d3d11, std::nullopt, watch_ranges_t{})
            == slot_class_e::taken_by_runtime,
          "classify_entry: in place on the runtime's table, another variant of the runtime's");

    std::uint64_t const ours_at{0x200'0010 + 12 * 8};
    foreign_entry_t e{true, 0x8'0000, entry_place_e::no_module,
                      jump_through(own_stub_bytes(ours_at), 0x8'0000)};
    check(classify_foreign(e, own, ours_at, runtime_entry) == table_class_e::above_own_table,
          "classify_foreign: a plugin above, the library's stub through our slot");
    std::array<std::uint8_t, 6> rip{0xFF, 0x25};
    std::uint64_t const at{ours_at - 0x100};
    std::uint32_t const disp{static_cast<std::uint32_t>(ours_at - (at + 6))};
    for(std::size_t i{}; i != 4; ++i)
      rip[2 + i] = static_cast<std::uint8_t>(disp >> (8 * i));
    e = foreign_entry_t{true, at, entry_place_e::no_module, jump_through(rip, at)};
    check(classify_foreign(e, own, ours_at, runtime_entry) == table_class_e::above_own_table,
          "classify_foreign: a plugin above, jmp [rip] through our slot");
    e = foreign_entry_t{true, hook, entry_place_e::other_module, std::nullopt};
    check(classify_foreign(e, own, ours_at, runtime_entry) == table_class_e::above_own_table,
          "classify_foreign: our hook copied into a table above");
    e = foreign_entry_t{true, runtime_entry, entry_place_e::system_d3d11, std::nullopt};
    check(classify_foreign(e, own, ours_at, runtime_entry) == table_class_e::re_pointed,
          "classify_foreign: the runtime's table, the object re-pointed");
    e = foreign_entry_t{true, 0x6'0000, entry_place_e::other_module, std::nullopt};
    check(classify_foreign(e, own, ours_at, runtime_entry) == table_class_e::covered,
          "classify_foreign: another module's table");
    e = foreign_entry_t{};
    check(classify_foreign(e, own, ours_at, runtime_entry) == table_class_e::covered,
          "classify_foreign: an entry that could not be read");
    check(worse(worse(table_class_e::covered, table_class_e::re_pointed), table_class_e::above_own_table)
            == table_class_e::re_pointed,
          "worse: re-pointed weighs most");
    }
  }  // namespace

auto main() -> int
  {
  context_rule_picks();
  own_table_marks();
  slot_writes();
  watch_classes();
  pe_ranges();
  pass_module_names();
  shader_names();
  shader_names_threads();
  context_probe_jumps();
  ini_native_checks(check);
  event_report::report_checks(check, event_report::equal_ascii_fold, false);
  std::printf(failures ? "FAILED (%d failure(s))\n" : "PASSED (%d failure(s))\n", failures);
  return failures ? 1 : 0;
  }
