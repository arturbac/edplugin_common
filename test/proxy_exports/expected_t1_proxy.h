// t1 proxy exports, made by edplugin_common_proxy_exports from list.txt
#pragma once

#include <cstddef>

inline constexpr std::size_t t1_proxy_count{3};
// the names of t1_proxy_real_procs's slots, in its order
inline constexpr char const * t1_proxy_names[]{
  "Alpha",
  "Beta",
  "Gamma",
};
static_assert(sizeof(t1_proxy_names) / sizeof(t1_proxy_names[0]) == t1_proxy_count);

// t1_proxy_impl_<name> of each wrapped name is declared and defined by the plugin, with that function's signature
extern "C"
  {
  // t1_proxy_thunk_<i> jumps through slot i; every slot is filled before the first call
  extern void * t1_proxy_real_procs[t1_proxy_count];
  // returns 0: the slot of a function the next module does not have
  void t1_proxy_unresolved();
  }
