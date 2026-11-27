// edplugin_common - the part of watching the context's hooks (hook/watch.h) that needs no Windows header: what an
// entry of a watched slot is now (ours, taken back, taken by the runtime, covered by another hook), and what a table
// the context object points at instead of ours is (a plugin above on a table of its own, another hook's, or the
// runtime's: the object re-pointed). Built natively with sanitizers too (test/native).
#pragma once

#include <edplugin_common/hook/own_table_mark.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace edplugin_common::hook
  {
  ///\brief where an entry leads
  enum struct entry_place_e : std::uint8_t
    {
    system_d3d11,  ///< into the system's d3d11.dll
    other_module,  ///< into another module (a plugin, 3Dmigoto, an overlay)
    no_module,     ///< memory of no module (another hook's stubs)
    };

  ///\brief what the entry of a watched slot is
  enum struct slot_class_e : std::uint8_t
    {
    ours,              ///< our hook
    covered,           ///< another hook over ours (another module or memory of none): told once, the hooks still ours
    taken,             ///< our original back, or one of our stubs: the hook taken away
    taken_by_runtime,  ///< the runtime's entry: the runtime wrote over our hook
    };

  ///\brief what a table the object points at instead of ours is, from the watched slots' entries in it
  enum struct table_class_e : std::uint8_t
    {
    ours,             ///< the object points at our table
    covered,          ///< another hook's table (another module, memory of none): told once
    above_own_table,  ///< a plugin above on a table of its own: our hook in it, or a stub through our table's slot
    re_pointed,       ///< the runtime's entries: the object pointed away from our table
    };

  ///\brief whether a class turns the plugin's hooks off (the plugin's way out, D3)
  [[nodiscard]]
  constexpr auto turns_off(slot_class_e c) noexcept -> bool
    {
    return c == slot_class_e::taken or c == slot_class_e::taken_by_runtime;
    }

  [[nodiscard]]
  constexpr auto turns_off(table_class_e c) noexcept -> bool
    {
    return c == table_class_e::re_pointed;
    }

  ///\brief a watched slot as it was at the attach
  struct slot_watch_t
    {
    std::size_t slot{};
    std::uint64_t hook{};
    std::uint64_t original{};  ///< the stub of the slot on our own table, the entry replaced in place
    };

  ///\brief the addresses a watched entry is told by
  struct watch_ranges_t
    {
    std::uint64_t stubs_first{};  ///< our stubs' code (a table of our own), empty otherwise
    std::uint64_t stubs_end{};
    };

  ///\brief the class of `entry` (leading to `place`) in a watched slot. `runtime_entry` is the entry of the slot in the
  /// "runtime's table" of the way the context is watched: the table the context came with on a table of our own,
  /// the runtime's table of another copy of the library (its mark) when in place on its table; none in place on the
  /// runtime's table, on a wrapper's (3Dmigoto) or on a foreign one (each entry there is the table's own). An entry
  /// into the system's d3d11 other than the original is the runtime's too, wherever the original led (Windows' d3d11
  /// switching its variant of a method in place)
  [[nodiscard]]
  constexpr auto classify_entry(slot_watch_t const & w, std::uint64_t entry, entry_place_e place,
                                std::optional<std::uint64_t> runtime_entry, watch_ranges_t const & r) noexcept
    -> slot_class_e
    {
    if(entry == w.hook)
      return slot_class_e::ours;
    if(entry == w.original or (entry >= r.stubs_first and entry < r.stubs_end))
      return slot_class_e::taken;
    if((runtime_entry and entry == *runtime_entry) or place == entry_place_e::system_d3d11)
      return slot_class_e::taken_by_runtime;
    return slot_class_e::covered;
    }

  ///\brief the address `code` (lying at `at`) jumps through: the library's stub (mov rax, imm64; jmp [rax]) or jmp
  /// [rip+disp32] (also with REX.W); none for other code
  [[nodiscard]]
  constexpr auto jump_through(std::span<std::uint8_t const> code, std::uint64_t at) noexcept -> std::optional<std::uint64_t>
    {
    if(std::optional<std::uint64_t> const through{own_stub_target(code)})
      return through;
    std::size_t const skip{code.size() >= 1 and code[0] == 0x48 ? std::size_t{1} : std::size_t{0}};
    if(code.size() < skip + 6 or code[skip] != 0xFF or code[skip + 1] != 0x25)
      return std::nullopt;
    std::uint32_t const raw{std::uint32_t{code[skip + 2]} | std::uint32_t{code[skip + 3]} << 8
                            | std::uint32_t{code[skip + 4]} << 16 | std::uint32_t{code[skip + 5]} << 24};
    return at + skip + 6 + static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(raw)));
    }

  ///\brief one watched slot's entry in a table the object points at instead of ours
  struct foreign_entry_t
    {
    bool read{};                ///< the entry (and its first bytes) could be read
    std::uint64_t entry{};
    entry_place_e place{entry_place_e::no_module};
    std::optional<std::uint64_t> through{};  ///< what its code jumps through (jump_through)
    };

  ///\brief the class of one entry of a foreign table: `ours_at` the address of the slot in our table, `runtime_entry`
  /// the runtime's entry of the slot (as for classify_entry)
  [[nodiscard]]
  constexpr auto classify_foreign(foreign_entry_t const & e, slot_watch_t const & w, std::uint64_t ours_at,
                                  std::optional<std::uint64_t> runtime_entry) noexcept -> table_class_e
    {
    if(not e.read)
      return table_class_e::covered;
    if(e.entry == w.hook or (e.through and *e.through == ours_at))
      return table_class_e::above_own_table;
    if(e.place == entry_place_e::system_d3d11 or (runtime_entry and e.entry == *runtime_entry))
      return table_class_e::re_pointed;
    return table_class_e::covered;
    }

  ///\brief the classes of several slots taken together: the one that weighs most (re-pointed over a plugin above over
  /// covered)
  [[nodiscard]]
  constexpr auto worse(table_class_e a, table_class_e b) noexcept -> table_class_e
    {
    return static_cast<std::uint8_t>(a) > static_cast<std::uint8_t>(b) ? a : b;
    }

  [[nodiscard]]
  constexpr auto worse(slot_class_e a, slot_class_e b) noexcept -> slot_class_e
    {
    return static_cast<std::uint8_t>(a) > static_cast<std::uint8_t>(b) ? a : b;
    }

  [[nodiscard]]
  constexpr auto class_name(slot_class_e c) noexcept -> char const *
    {
    switch(c)
      {
      case slot_class_e::ours:             return "ours";
      case slot_class_e::covered:          return "covered by another hook";
      case slot_class_e::taken:            return "taken";
      case slot_class_e::taken_by_runtime: return "taken by the runtime";
      }
    return "?";
    }

  [[nodiscard]]
  constexpr auto class_name(table_class_e c) noexcept -> char const *
    {
    switch(c)
      {
      case table_class_e::ours:            return "ours";
      case table_class_e::covered:         return "another hook's table";
      case table_class_e::above_own_table: return "a plugin above with a table of its own";
      case table_class_e::re_pointed:      return "re-pointed";
      }
    return "?";
    }

  namespace detail
    {
    inline constexpr slot_watch_t watch_case{12, 0x1'0000, 0x2'0000};
    inline constexpr watch_ranges_t no_stubs{};
    }  // namespace detail

  static_assert(classify_entry(detail::watch_case, 0x1'0000, entry_place_e::other_module, std::nullopt, detail::no_stubs)
                == slot_class_e::ours);
  static_assert(classify_entry(detail::watch_case, 0x2'0000, entry_place_e::system_d3d11, std::nullopt, detail::no_stubs)
                == slot_class_e::taken);
  static_assert(classify_entry(detail::watch_case, 0x3'0010, entry_place_e::no_module, std::nullopt,
                               watch_ranges_t{0x3'0000, 0x3'1000})
                == slot_class_e::taken);
  // in place on 3Dmigoto's table: another plugin's hook over ours is no runtime's entry (no runtime's table)
  static_assert(classify_entry(slot_watch_t{12, 0x1'0000, 0x5'0000}, 0x6'0000, entry_place_e::other_module,
                               std::nullopt, detail::no_stubs)
                == slot_class_e::covered);
  // on a table of our own, the runtime's entry back in our table
  static_assert(classify_entry(detail::watch_case, 0x7'0000, entry_place_e::other_module, 0x7'0000, detail::no_stubs)
                == slot_class_e::taken_by_runtime);
  static_assert(jump_through(own_stub_bytes(0x1234'5678'9ABC), 0) == 0x1234'5678'9ABC);
  static_assert(jump_through(std::array<std::uint8_t, 6>{0xFF, 0x25, 0xFA, 0xFF, 0xFF, 0xFF}, 0x1000) == 0x1000);
  static_assert(jump_through(std::array<std::uint8_t, 7>{0x48, 0xFF, 0x25, 0x10, 0, 0, 0}, 0x1000) == 0x1017);
  static_assert(not jump_through(std::array<std::uint8_t, 5>{0xFF, 0x25, 0, 0, 0}, 0x1000));
  static_assert(worse(table_class_e::above_own_table, table_class_e::re_pointed) == table_class_e::re_pointed
                and worse(table_class_e::covered, table_class_e::above_own_table) == table_class_e::above_own_table);
  }  // namespace edplugin_common::hook
