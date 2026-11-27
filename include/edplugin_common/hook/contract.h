// edplugin_common - the version of the contract the plugins' context hooks keep to (hook/own_table.h, its README
// part): said in the log of the pick of the way and written into the mark of a table of the library's own, so a plugin
// above one built from another version of the library tells it in its log. No Windows header.
#pragma once

#include <cstdint>

namespace edplugin_common::hook
  {
  ///\brief raised with a change of what a plugin may count on from another's hooks: the stubs, the originals, what is
  /// never freed or restored, the order of the attach
  inline constexpr std::uint32_t hook_contract_version{1};
  }  // namespace edplugin_common::hook
