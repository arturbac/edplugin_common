// edplugin_common - the part of writing hooks into slots of a method table (hook/slot.h) that needs no Windows header:
// the protection a page gets for a write (writable_like: write added, nothing else changed) and the order of the
// whole write: every page of the list made writable first, then the writes, then each page's protection back, then a
// read of every written slot. A page that cannot be made writable gives back the protection of the pages already
// changed and nothing is written. The memory calls come through page_ops_t, so the order is checked natively with
// sanitizers too (test/native).
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace edplugin_common::hook
  {
  ///\brief Windows' page protection values (winnt.h), checked against it in src/slot.cc
  namespace page
    {
    inline constexpr std::uint32_t no_access{0x01};
    inline constexpr std::uint32_t read_only{0x02};
    inline constexpr std::uint32_t read_write{0x04};
    inline constexpr std::uint32_t write_copy{0x08};
    inline constexpr std::uint32_t execute{0x10};
    inline constexpr std::uint32_t execute_read{0x20};
    inline constexpr std::uint32_t execute_read_write{0x40};
    inline constexpr std::uint32_t execute_write_copy{0x80};
    inline constexpr std::uint32_t guard{0x100};
    ///\brief the bits of the access itself; the rest are modifiers (no cache, write combine, guard)
    inline constexpr std::uint32_t access_bits{0xFF};
    }  // namespace page

  ///\brief the protection `protect` with write added and nothing else changed (execute stays as it was, the modifiers
  /// too; a page writable already keeps its protection); 0 when the page cannot be read now (no access, execute only, a
  /// guard page, no protection at all)
  [[nodiscard]]
  constexpr auto writable_like(std::uint32_t protect) noexcept -> std::uint32_t
    {
    if(protect & page::guard)
      return 0;
    std::uint32_t const modifiers{protect & ~page::access_bits};
    switch(protect & page::access_bits)
      {
      case page::read_write:
      case page::write_copy:
      case page::execute_read_write:
      case page::execute_write_copy:  return protect;
      case page::read_only:           return page::read_write | modifiers;
      case page::execute_read:        return page::execute_read_write | modifiers;
      default:                        return 0;
      }
    }

  ///\brief whether a page of protection `protect` can be written as it is (a write copy page gets a copy of its own)
  [[nodiscard]]
  constexpr auto writable(std::uint32_t protect) noexcept -> bool
    {
    std::uint32_t const access{protect & page::access_bits};
    return not(protect & page::guard)
           and (access == page::read_write or access == page::write_copy or access == page::execute_read_write
                or access == page::execute_write_copy);
    }

  static_assert(writable_like(page::read_only) == page::read_write);
  static_assert(writable_like(page::execute_read) == page::execute_read_write);
  static_assert(writable_like(page::execute_read | 0x200) == (page::execute_read_write | 0x200));
  static_assert(writable_like(page::write_copy) == page::write_copy);
  static_assert(writable_like(page::no_access) == 0 and writable_like(page::execute) == 0 and writable_like(0) == 0);
  static_assert(writable_like(page::read_only | page::guard) == 0);
  static_assert(writable(page::write_copy) and not writable(page::read_only) and not writable(page::read_write | page::guard));

  ///\brief what became of one slot of the list
  enum struct slot_result_e : std::uint8_t
    {
    not_tried,          ///< nothing written: another page of the list could not be used (slots_patched_t)
    patched,            ///< the hook written, read back; the slot's entry before it in `original`
    already_ours,       ///< the slot held the hook already (before the list, or from a slot listed twice): `original`
                        ///< untouched
    not_writable,       ///< its page could not be read or made writable: nothing of the list written
    read_back_differs,  ///< written, but read back as another entry: written over meanwhile (`original` is what the
                        ///< hook replaced)
    };

  ///\brief one slot of the list: the plugin fills `slot` and `hook`
  struct slot_patch_t
    {
    std::size_t slot{};
    void * hook{};
    void * original{};
    slot_result_e result{slot_result_e::not_tried};
    };

  ///\brief how the whole list went
  enum struct slots_patch_e : std::uint8_t
    {
    done,            ///< every slot patched, already ours or read back as another (each slot's result)
    too_many_pages,  ///< the slots lie on more than slot_pages_most pages: nothing written
    page_unusable,   ///< a page of the list not committed, or not readable: nothing written
    page_protect,    ///< a page could not be made writable (`error`): the pages changed before given back, nothing
                     ///< written
    };

  ///\brief the most different pages one list may lie on (a table of 2048 entries spans five)
  inline constexpr std::size_t slot_pages_most{8};

  struct slots_patched_t
    {
    slots_patch_e result{slots_patch_e::done};
    std::uint32_t error{};              ///< GetLastError of the failed VirtualProtect (page_protect)
    std::size_t failed_slot{};          ///< a slot on the page that failed (page_unusable, page_protect)
    std::uint32_t failed_protect{};     ///< that page's protection (page_unusable, page_protect)
    std::uint16_t patched{};
    std::uint16_t already_ours{};
    std::uint16_t read_back_differs{};
    std::uint8_t pages_changed{};       ///< pages made writable for the writes
    std::uint8_t restore_failed{};      ///< of them, pages whose protection could not be given back (left writable)

    ///\brief every slot of the list holds its hook (written now or before)
    [[nodiscard]]
    auto all_ours() const noexcept -> bool
      {
      return result == slots_patch_e::done and read_back_differs == 0;
      }
    };

  ///\brief the first slot of `list` at or past `entries` (the entries of the table it is to be written into); none when
  /// every slot lies within
  [[nodiscard]]
  constexpr auto slot_past(std::span<slot_patch_t const> list, std::size_t entries) noexcept -> std::optional<std::size_t>
    {
    for(slot_patch_t const & s : list)
      if(s.slot >= entries)
        return s.slot;
    return std::nullopt;
    }

  namespace detail
    {
    inline constexpr std::array<slot_patch_t, 3> past_case{slot_patch_t{12}, slot_patch_t{119}, slot_patch_t{20}};
    }  // namespace detail

  static_assert(not slot_past(detail::past_case, 120) and slot_past(detail::past_case, 119) == 119
                and slot_past(detail::past_case, 12) == 12 and not slot_past({}, 0));

  ///\brief the memory calls of patch_slots_by(): Windows' in src/slot.cc, a test's own natively
  struct page_ops_t
    {
    std::size_t page_size{};  ///< a power of two
    ///\brief the protection of the committed page at `page`; false when it is not committed
    auto (*protection)(std::uintptr_t page, std::uint32_t & protect) noexcept -> bool{};
    ///\brief the page at `page` given `protect`, its protection before in `old`; false (and `error`) when refused
    auto (*protect)(std::uintptr_t page, std::uint32_t protect, std::uint32_t & old, std::uint32_t & error) noexcept
      -> bool{};
    ///\brief after a write of `bytes` bytes at `p` (the instruction cache)
    auto (*written)(void * p, std::size_t bytes) noexcept -> void{};
    };

  namespace detail
    {
    struct slot_page_t
      {
      std::uintptr_t page{};
      std::uint32_t protect{};  ///< the protection found
      std::uint32_t old{};      ///< the protection to give back (made writable for the writes)
      std::size_t slot{};       ///< the first slot of the list on it
      bool write{};             ///< a slot on it is to be written
      bool changed{};           ///< made writable for the writes
      };

    [[nodiscard]]
    inline auto entry_page(void ** table, std::size_t slot, std::size_t page_size) noexcept -> std::uintptr_t
      {
      return reinterpret_cast<std::uintptr_t>(table + slot) & ~(std::uintptr_t{page_size} - 1);
      }

    ///\brief each page made writable before given back, in the order they were changed
    inline auto give_back(page_ops_t const & ops, std::span<slot_page_t> pages, slots_patched_t & out) noexcept -> void
      {
      for(slot_page_t & p : pages)
        {
        if(not p.changed)
          continue;
        std::uint32_t was{};
        std::uint32_t error{};
        if(not ops.protect(p.page, p.old, was, error))
          ++out.restore_failed;
        p.changed = false;
        }
      }
    }  // namespace detail

  ///\brief the hooks of `list` written into the slots of `table` through `ops`: every page of the list read first
  /// (committed and readable), every page with a slot to write made writable_like() next, then the writes (an atomic
  /// exchange each: `original` is exactly what the hook replaced), the protection of the pages given back, and every
  /// written slot read again. A slot already holding its hook is not written. A page that cannot be used leaves the
  /// table as it was: nothing written, the pages changed given back. The caller has looked at the table as readable
  /// and keeps `list` to the table's entries
  [[nodiscard]]
  inline auto patch_slots_by(page_ops_t const & ops, void ** table, std::span<slot_patch_t> list) noexcept -> slots_patched_t
    {
    slots_patched_t out{};
    std::array<detail::slot_page_t, slot_pages_most> pages{};
    std::size_t page_count{};
    for(slot_patch_t & s : list)
      {
      s.result = slot_result_e::not_tried;
      std::uintptr_t const at{detail::entry_page(table, s.slot, ops.page_size)};
      std::size_t i{};
      while(i != page_count and pages[i].page != at)
        ++i;
      if(i == page_count)
        {
        if(page_count == slot_pages_most)
          {
          out.result = slots_patch_e::too_many_pages;
          out.failed_slot = s.slot;
          return out;
          }
        pages[page_count++] = detail::slot_page_t{.page = at, .slot = s.slot};
        }
      }
    std::span<detail::slot_page_t> const used{pages.data(), page_count};
    auto const fail_all{[&](slots_patch_e why, detail::slot_page_t const & p) noexcept
                        {
                          out.result = why;
                          out.failed_slot = p.slot;
                          out.failed_protect = p.protect;
                          for(slot_patch_t & s : list)
                            s.result = slot_result_e::not_writable;
                        }};
    for(detail::slot_page_t & p : used)
      if(not ops.protection(p.page, p.protect) or writable_like(p.protect) == 0)
        {
        fail_all(slots_patch_e::page_unusable, p);
        return out;
        }
    for(slot_patch_t & s : list)
      {
      if(std::atomic_ref<void *>{table[s.slot]}.load(std::memory_order_acquire) == s.hook)
        {
        s.result = slot_result_e::already_ours;
        continue;
        }
      std::uintptr_t const at{detail::entry_page(table, s.slot, ops.page_size)};
      for(detail::slot_page_t & p : used)
        if(p.page == at)
          p.write = true;
      }
    for(detail::slot_page_t & p : used)
      {
      if(not p.write or writable(p.protect))
        continue;
      if(not ops.protect(p.page, writable_like(p.protect), p.old, out.error))
        {
        detail::give_back(ops, used, out);
        fail_all(slots_patch_e::page_protect, p);
        return out;
        }
      p.changed = true;
      ++out.pages_changed;
      }
    for(slot_patch_t & s : list)
      {
      if(s.result == slot_result_e::already_ours)
        {
        ++out.already_ours;
        continue;
        }
      void * const was{std::atomic_ref<void *>{table[s.slot]}.exchange(s.hook, std::memory_order_acq_rel)};
      ops.written(&table[s.slot], sizeof(void *));
      if(was == s.hook)
        {
        // the slot listed twice (or written by another thread with the same hook): the hook as its own original
        // would call itself
        s.result = slot_result_e::already_ours;
        ++out.already_ours;
        continue;
        }
      s.original = was;
      s.result = slot_result_e::patched;
      }
    detail::give_back(ops, used, out);
    for(slot_patch_t & s : list)
      {
      if(s.result != slot_result_e::patched)
        continue;
      if(std::atomic_ref<void *>{table[s.slot]}.load(std::memory_order_acquire) == s.hook)
        ++out.patched;
      else
        {
        s.result = slot_result_e::read_back_differs;
        ++out.read_back_differs;
        }
      }
    return out;
    }
  }  // namespace edplugin_common::hook
