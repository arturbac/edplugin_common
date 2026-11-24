// context_probe - where an entry of a method table leads: the module holding it, or, for one outside every module,
// the jumps its trampoline makes (jump.h) up to a module. Each byte looked at before it is read, and read under
// __try.
#pragma once

#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

namespace context_probe
  {
  ///\brief `bytes` bytes from `from` to `to`, for memory another module may change or free between the look and the
  /// read; false after a fault
  [[nodiscard]]
  auto read_caught(void * to, void const * from, std::size_t bytes) noexcept -> bool;

  ///\brief the entries of `table`, at most `most`, up to the first one that cannot be read (each looked at, then read
  /// by read_caught); the work goes on with this copy, not with the table
  [[nodiscard]]
  auto read_entries(void * const * table, int most) -> std::vector<void *>;

  ///\brief a module's file name without its folder ("-" for nullptr, "?" when it cannot be told)
  [[nodiscard]]
  auto module_name(HMODULE module) -> std::string;

  ///\brief `p` as "<module>+0x<offset>", or for memory outside every module its kind and protection
  /// ("no module, private 0x20")
  [[nodiscard]]
  auto where(void const * p) -> std::string;

  ///\brief the jumps from `entry` (at most `hops`), each "<form> -> <where>", until one leads into a module or no
  /// jump of a known form is there ("; stopped after <hops> jumps" when the limit ended it); "no jump read" when the
  /// first bytes are not one
  [[nodiscard]]
  auto follow_jumps(void const * entry, int hops = 4) -> std::string;

  ///\brief for an entry inside `module` whose first instruction is a jump out of `module` (an inline hook: the table
  /// is untouched, the function's code is not): where its jumps lead, as follow_jumps; empty for any other entry
  [[nodiscard]]
  auto jump_out_of(void const * entry, HMODULE module) -> std::string;
  }  // namespace context_probe
