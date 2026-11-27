// edplugin_common - hooks written into slots of a method table in place (the device's, DXGI's factory's, the swap
// chain's, the context's when it stays on its table): the order and the results in hook/slot_plan.h, Windows' memory
// calls here (VirtualQuery, VirtualProtect, FlushInstructionCache).
#pragma once

#include <edplugin_common/hook/slot_plan.h>

#include <span>

namespace edplugin_common::hook
  {
  ///\brief patch_slots_by() with Windows' memory calls: the hooks of `list` written into `table`, every page made
  /// writable_like() before the first write and given back after the last, nothing written when a page cannot be used.
  /// The caller has looked at the table as readable (memory/memory.h readable())
  [[nodiscard]]
  auto patch_slots(void ** table, std::span<slot_patch_t> list) noexcept -> slots_patched_t;

  ///\brief the memory calls patch_slots() goes through
  [[nodiscard]]
  auto windows_page_ops() noexcept -> page_ops_t const &;
  }  // namespace edplugin_common::hook
