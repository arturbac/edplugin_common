// context_probe - where an entry of a method table leads: the module holding it, or, for one outside every module,
// the jumps its trampoline makes (jump.h) up to a module. Each byte looked at before it is read.
#pragma once

#include <windows.h>

#include <string>

namespace context_probe
  {
  ///\brief a module's file name without its folder ("-" for nullptr, "?" when it cannot be told)
  [[nodiscard]]
  auto module_name(HMODULE module) -> std::string;

  ///\brief `p` as "<module>+0x<offset>", or for memory outside every module its kind and protection
  /// ("no module, private 0x20")
  [[nodiscard]]
  auto where(void const * p) -> std::string;

  ///\brief the jumps from `entry` (at most `hops`), each "<form> -> <where>", until one leads into a module or no
  /// jump of a known form is there; "no jump read" when the first bytes are not one
  [[nodiscard]]
  auto follow_jumps(void const * entry, int hops = 4) -> std::string;
  }  // namespace context_probe
