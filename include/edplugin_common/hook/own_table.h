// edplugin_common - a table of the library's own for one immediate context: the context object pointed at it instead
// of the runtime's table, whose entries Windows' d3d11 rewrites when it switches its variants of the methods (and so
// takes away hooks written there). Every entry of the table of its own is a stub that jumps through the runtime's entry
// of its slot as it is at the call (a rewrite is followed), a plugin's hook takes the place of a stub, and the stub is
// the hook's original. The words before entry 0 are copied, the entries lie in private read-write memory, the stubs in
// private memory of their own, read and execute, with a mark at its start (hook/own_table_mark.h) by which a plugin
// above recognizes the table and hooks in it in place. Nothing of it is freed or restored: the context keeps pointing
// at it until the end of the process (contract c).
#pragma once

#include <edplugin_common/hook/own_table_mark.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace edplugin_common::hook
  {
  struct own_table_t
    {
    void ** runtime{};                    ///< the table the context came with: the stubs jump through its entries
    void ** entries{};                    ///< entry 0 of the table of its own
    std::size_t count{};                  ///< entries
    std::uint8_t const * stubs{};         ///< stub 0; the mark lies own_stub_offset bytes before it

    [[nodiscard]]
    auto made() const noexcept -> bool
      {
      return entries != nullptr;
      }

    ///\brief the stub of `slot` (the original of a hook in it); nullptr past the entries
    [[nodiscard]]
    auto stub(std::size_t slot) const noexcept -> void *
      {
      return slot < count ? const_cast<std::uint8_t *>(stubs + slot * own_stub_step) : nullptr;
      }

    ///\brief whether `p` lies in the stubs' code
    [[nodiscard]]
    auto holds_stub(void const * p) const noexcept -> bool
      {
      auto const at{reinterpret_cast<std::uintptr_t>(p)};
      auto const first{reinterpret_cast<std::uintptr_t>(stubs)};
      return stubs and at >= first and at - first < count * own_stub_step;
      }
    };

  enum struct own_table_make_e : std::uint8_t
    {
    made,
    bad_count,       ///< no entries, or more than context_table_most
    entries_memory,  ///< VirtualAlloc of the entries failed (`error`)
    stubs_memory,    ///< VirtualAlloc of the stubs failed (`error`)
    stubs_protect,   ///< the stubs' memory could not be made read and execute (`error`; ACG forbids it)
    };

  struct own_table_made_t
    {
    own_table_make_e result{own_table_make_e::made};
    DWORD error{};             ///< GetLastError of the failed call
    bool header_copied{};      ///< the words before entry 0 could be read and were copied (zero otherwise)
    };

  ///\brief a table of `count` stubs through the entries of `runtime`, the mark written; nothing changed but `out` (and
  /// nothing kept: the memory freed) when it fails. The context still points at `runtime`: own_table_point() after the
  /// hooks are put
  [[nodiscard]]
  auto make_own_table(void ** runtime, std::size_t count, own_table_t & out) noexcept -> own_table_made_t;

  ///\brief `hook` into `slot` of the table, the slot's stub returned as the hook's original (contract b); nullptr when
  /// the slot is past the entries (nothing written). A hook already there is kept, its stub returned again
  [[nodiscard]]
  auto own_table_put(own_table_t const & table, std::size_t slot, void * hook) noexcept -> void *;

  ///\brief the object pointed at the table of its own: after every hook was put
  auto own_table_point(own_table_t const & table, void * object) noexcept -> void;

  ///\brief the mark of a table of another copy of the library that `table` (the entries the context points at) is,
  /// looked for through the first entries read (`first`, at most context_first_entries; those without a module lead to
  /// the candidates): for each of at most own_mark_candidates allocations, private executable memory of the mark's
  /// size, the mark and the entry's code read under a fault handler, then mark_check()
  [[nodiscard]]
  auto find_mark(void * const * table, std::span<void * const> first) noexcept -> mark_search_t;
  }  // namespace edplugin_common::hook
