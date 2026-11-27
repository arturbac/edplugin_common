// edplugin_common - the part of a table of the library's own (hook/own_table.h) that needs no Windows header: the
// stub's bytes, the mark at the start of the stubs' memory, and the checks by which a plugin above tells such a table
// of another copy of the library from a foreign one. Built natively with sanitizers too (test/native).
#pragma once

#include <edplugin_common/hook/context_rule.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace edplugin_common::hook
  {
  ///\brief the mark at the start of the stubs' memory; its layout is fixed by own_table_layout (addresses as 64-bit
  /// numbers, whatever the copy of the library that reads it)
  struct own_table_mark_t
    {
    std::uint64_t magic;      ///< own_table_magic
    std::uint32_t layout;     ///< own_table_layout: the layout of this mark and of the stubs
    std::uint32_t contract;   ///< hook_contract_version of the copy that made the table
    std::uint64_t runtime;    ///< the table the context came with, the stubs jump through its entries
    std::uint64_t entries;    ///< entry 0 of the table of its own: the context object points here
    std::uint64_t count;      ///< its entries
    std::uint64_t stubs;      ///< stub 0
    std::uint64_t stub_step;  ///< bytes from one stub to the next
    };

  static_assert(std::is_standard_layout_v<own_table_mark_t> and std::is_trivially_copyable_v<own_table_mark_t>);
  static_assert(sizeof(own_table_mark_t) == 56 and offsetof(own_table_mark_t, stub_step) == 48,
                "the mark's layout is read by other copies of the library: a change of it is a new own_table_layout");

  ///\brief "edpc_tbl" in the order of the bytes in memory
  inline constexpr std::uint64_t own_table_magic{0x6C62'745F'6370'6465ull};
  inline constexpr std::uint32_t own_table_layout{1};
  ///\brief the stubs start this far into their memory, after the mark
  inline constexpr std::size_t own_stub_offset{64};
  ///\brief one stub: mov rax, imm64 (the address of the runtime's entry); jmp qword ptr [rax]; four int3 to the step
  inline constexpr std::size_t own_stub_step{16};
  inline constexpr std::size_t own_stub_code{12};
  ///\brief the words before entry 0 (the C++ ABIs' type information), copied from the runtime's table
  inline constexpr std::size_t own_table_header{2};
  ///\brief the most different allocations of the first entries without a module looked at for a mark
  inline constexpr std::size_t own_mark_candidates{4};

  static_assert(sizeof(own_table_mark_t) <= own_stub_offset and own_stub_code <= own_stub_step);

  ///\brief the stub of the slot whose runtime entry lies at `entry_address`
  [[nodiscard]]
  constexpr auto own_stub_bytes(std::uint64_t entry_address) noexcept -> std::array<std::uint8_t, own_stub_step>
    {
    std::array<std::uint8_t, own_stub_step> b{0x48, 0xB8};
    for(std::size_t i{}; i != 8; ++i)
      b[2 + i] = static_cast<std::uint8_t>(entry_address >> (8 * i));
    b[10] = 0xFF;
    b[11] = 0x20;
    for(std::size_t i{own_stub_code}; i != own_stub_step; ++i)
      b[i] = 0xCC;
    return b;
    }

  ///\brief the address of the runtime's entry a stub of this layout jumps through; none when `code` is not such a stub
  [[nodiscard]]
  constexpr auto own_stub_target(std::span<std::uint8_t const> code) noexcept -> std::optional<std::uint64_t>
    {
    if(code.size() < own_stub_code or code[0] != 0x48 or code[1] != 0xB8 or code[10] != 0xFF or code[11] != 0x20)
      return std::nullopt;
    std::uint64_t at{};
    for(std::size_t i{}; i != 8; ++i)
      at |= std::uint64_t{code[2 + i]} << (8 * i);
    return at;
    }

  ///\brief the mark of a table made by this copy of the library
  [[nodiscard]]
  constexpr auto own_table_mark(std::uint64_t runtime, std::uint64_t entries, std::uint64_t count, std::uint64_t stubs,
                                std::uint32_t contract) noexcept -> own_table_mark_t
    {
    return {own_table_magic, own_table_layout, contract, runtime, entries, count, stubs, own_stub_step};
    }

  ///\brief what was read for one candidate: the mark at the start of an allocation an entry leads into, and the code
  /// that entry leads to
  struct mark_reading_t
    {
    bool read{};                 ///< the region was private, executable and large enough, and both reads went well
    std::uint32_t slot{};        ///< the entry's slot in the context's table
    std::uint64_t entry{};       ///< the entry: the address of the code read into `stub`
    own_table_mark_t mark{};
    std::array<std::uint8_t, own_stub_step> stub{};
    };

  ///\brief why a candidate was taken or not, said in the log
  enum struct mark_check_e : std::uint8_t
    {
    accepted,
    unread,        ///< not private executable memory of the mark's size, or a reading failed
    magic,         ///< another magic: no mark
    layout,        ///< another layout of the mark: a foreign table (contract e)
    entries,       ///< the mark's entries are not the table the context points at
    count,         ///< the slot out of the mark's entries, or more entries than a table may have
    stub_address,  ///< the entry is not the mark's stub of its slot
    stub_code,     ///< the code there is not a stub of this layout
    stub_target,   ///< the stub jumps through another entry than the runtime's of its slot
    };

  ///\brief the check's name in the log
  [[nodiscard]]
  constexpr auto check_name(mark_check_e c) noexcept -> char const *
    {
    switch(c)
      {
      case mark_check_e::accepted:     return "accepted";
      case mark_check_e::unread:       return "not read";
      case mark_check_e::magic:        return "another magic";
      case mark_check_e::layout:       return "another layout";
      case mark_check_e::entries:      return "other entries";
      case mark_check_e::count:        return "the slot out of its count";
      case mark_check_e::stub_address: return "not the slot's stub";
      case mark_check_e::stub_code:    return "not a stub";
      case mark_check_e::stub_target:  return "a stub through another entry";
      }
    return "?";
    }

  ///\brief whether `r` is a mark of a table of another copy of the library that the context's table `table` is
  [[nodiscard]]
  constexpr auto mark_check(mark_reading_t const & r, std::uint64_t table) noexcept -> mark_check_e
    {
    if(not r.read)
      return mark_check_e::unread;
    if(r.mark.magic != own_table_magic)
      return mark_check_e::magic;
    if(r.mark.layout != own_table_layout or r.mark.stub_step != own_stub_step)
      return mark_check_e::layout;
    if(r.mark.entries != table)
      return mark_check_e::entries;
    if(r.mark.count > context_table_most or r.slot >= r.mark.count)
      return mark_check_e::count;
    if(r.entry != r.mark.stubs + r.slot * own_stub_step)
      return mark_check_e::stub_address;
    std::optional<std::uint64_t> const target{own_stub_target(r.stub)};
    if(not target)
      return mark_check_e::stub_code;
    if(*target != r.mark.runtime + r.slot * std::uint64_t{8})
      return mark_check_e::stub_target;
    return mark_check_e::accepted;
    }

  ///\brief the candidates looked at for a mark, and what was found
  struct mark_search_t
    {
    std::optional<own_table_mark_t> mark;            ///< the first candidate accepted
    std::uint8_t candidates{};                       ///< looked at
    mark_check_e last{mark_check_e::unread};         ///< of the last candidate looked at (accepted when found)
    };

  ///\brief the first of `readings` accepted for the table `table`; the others' checks only counted
  [[nodiscard]]
  constexpr auto first_mark(std::span<mark_reading_t const> readings, std::uint64_t table) noexcept -> mark_search_t
    {
    mark_search_t s{};
    for(mark_reading_t const & r: readings)
      {
      ++s.candidates;
      s.last = mark_check(r, table);
      if(s.last == mark_check_e::accepted)
        {
        s.mark = r.mark;
        break;
        }
      }
    return s;
    }

  ///\brief the allocation bases of entries without a module, each once and at most own_mark_candidates: true when
  /// `base` was taken as a new candidate (its slot kept)
  struct mark_candidates_t
    {
    std::array<std::uint64_t, own_mark_candidates> base{};
    std::array<std::uint32_t, own_mark_candidates> slot{};
    std::size_t count{};

    constexpr auto add(std::uint64_t allocation_base, std::uint32_t entry_slot) noexcept -> bool
      {
      if(count == own_mark_candidates or allocation_base == 0)
        return false;
      for(std::size_t i{}; i != count; ++i)
        if(base[i] == allocation_base)
          return false;
      base[count] = allocation_base;
      slot[count] = entry_slot;
      ++count;
      return true;
      }
    };

  namespace mark_cases
    {
    inline constexpr std::uint64_t runtime{0x7FF0'0000'1000};
    inline constexpr std::uint64_t entries{0x0000'0200'0010};
    inline constexpr std::uint64_t stubs_base{0x0000'0300'0000};

    [[nodiscard]]
    constexpr auto reading(std::uint32_t slot) noexcept -> mark_reading_t
      {
      std::uint64_t const stub{stubs_base + own_stub_offset + slot * own_stub_step};
      return {true, slot, stub, own_table_mark(runtime, entries, 319, stubs_base + own_stub_offset, 1),
              own_stub_bytes(runtime + slot * std::uint64_t{8})};
      }

    [[nodiscard]]
    constexpr auto changed(mark_reading_t r, auto change) noexcept -> mark_reading_t
      {
      change(r);
      return r;
      }

    static_assert(own_stub_target(own_stub_bytes(0x1122'3344'5566'7788ull)) == 0x1122'3344'5566'7788ull);
    static_assert(not own_stub_target(std::span<std::uint8_t const>{own_stub_bytes(8)}.first(11)), "too few bytes");
    static_assert(mark_check(reading(0), entries) == mark_check_e::accepted);
    static_assert(mark_check(reading(318), entries) == mark_check_e::accepted);
    static_assert(mark_check(reading(12), entries + 8) == mark_check_e::entries, "the context points elsewhere");
    static_assert(mark_check(changed(reading(12), [](auto & r) { r.read = false; }), entries) == mark_check_e::unread);
    static_assert(mark_check(changed(reading(12), [](auto & r) { r.mark.magic ^= 1; }), entries)
                  == mark_check_e::magic);
    static_assert(mark_check(changed(reading(12), [](auto & r) { r.mark.layout = 2; }), entries)
                  == mark_check_e::layout);
    static_assert(mark_check(changed(reading(12), [](auto & r) { r.mark.contract = 7; }), entries)
                  == mark_check_e::accepted, "another contract with the same layout: only told (contract e)");
    static_assert(mark_check(changed(reading(12), [](auto & r) { r.mark.count = 12; }), entries)
                  == mark_check_e::count);
    static_assert(mark_check(changed(reading(12), [](auto & r) { r.mark.count = context_table_most + 1; }), entries)
                  == mark_check_e::count);
    static_assert(mark_check(changed(reading(12), [](auto & r) { r.entry += own_stub_step; }), entries)
                  == mark_check_e::stub_address);
    static_assert(mark_check(changed(reading(12), [](auto & r) { r.stub[11] = 0xE0; }), entries)
                  == mark_check_e::stub_code, "jmp rax instead of jmp [rax]");
    static_assert(mark_check(changed(reading(12), [](auto & r) { r.stub = own_stub_bytes(runtime + 13 * 8); }), entries)
                  == mark_check_e::stub_target, "the stub of the next slot");

    inline constexpr std::array<mark_reading_t, 2> foreign_then_ours{
      changed(reading(3), [](auto & r) { r.mark.magic = 0; }), reading(40)};
    static_assert(first_mark(foreign_then_ours, entries).mark.has_value()
                  and first_mark(foreign_then_ours, entries).candidates == 2,
                  "the first candidate foreign, the second ours: taken");
    static_assert(not first_mark(std::span{foreign_then_ours}.first(1), entries).mark
                  and first_mark(std::span{foreign_then_ours}.first(1), entries).last == mark_check_e::magic);

    [[nodiscard]]
    constexpr auto five_candidates() noexcept -> mark_candidates_t
      {
      mark_candidates_t c{};
      c.add(0x1000, 7);
      c.add(0x1000, 8);
      c.add(0, 9);
      c.add(0x2000, 10);
      c.add(0x3000, 11);
      c.add(0x4000, 12);
      c.add(0x5000, 13);
      return c;
      }

    static_assert(five_candidates().count == own_mark_candidates and five_candidates().slot[1] == 10
                    and five_candidates().base[3] == 0x4000,
                  "each base once, no null base, at most four");
    }  // namespace mark_cases
  }  // namespace edplugin_common::hook
