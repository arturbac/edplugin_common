// edplugin_common - a table of the library's own for one immediate context (include/edplugin_common/hook/own_table.h).
#include <edplugin_common/hook/own_table.h>

#include <edplugin_common/hook/contract.h>
#include <edplugin_common/memory/memory.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

namespace edplugin_common::hook
  {
  namespace
    {
    [[nodiscard]]
    auto address(void const * p) noexcept -> std::uint64_t
      {
      return reinterpret_cast<std::uintptr_t>(p);
      }

    ///\brief private executable memory at `base`, a whole mark long: where a table of the library's own keeps its mark
    [[nodiscard]]
    auto mark_region(void const * base) noexcept -> bool
      {
      MEMORY_BASIC_INFORMATION m{};
      DWORD constexpr execute{PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY};
      return VirtualQuery(base, &m, sizeof m) == sizeof m and m.State == MEM_COMMIT and m.Type == MEM_PRIVATE
             and (m.Protect & execute) and not(m.Protect & PAGE_GUARD) and m.BaseAddress == base
             and m.RegionSize >= sizeof(own_table_mark_t);
      }

    ///\brief the mark at `base` and the code at `r.entry`, both looked at before they are read
    [[nodiscard]]
    auto read_candidate(void const * base, mark_reading_t & r) noexcept -> bool
      {
      void const * const code{reinterpret_cast<void const *>(static_cast<std::uintptr_t>(r.entry))};
      return mark_region(base) and copy_readable(&r.mark, base, sizeof r.mark)
             and copy_readable(r.stub.data(), code, r.stub.size());
      }
    }  // namespace

  auto make_own_table(void ** runtime, std::size_t count, own_table_t & out) noexcept -> own_table_made_t
    {
    if(count == 0 or count > context_table_most)
      return {own_table_make_e::bad_count};
    auto * const words{
      static_cast<void **>(VirtualAlloc(nullptr, (own_table_header + count) * sizeof(void *), MEM_COMMIT | MEM_RESERVE,
                                        PAGE_READWRITE))};
    if(not words)
      return {own_table_make_e::entries_memory, GetLastError()};
    std::size_t const code_bytes{own_stub_offset + count * own_stub_step};
    auto * const code{
      static_cast<std::uint8_t *>(VirtualAlloc(nullptr, code_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))};
    if(not code)
      {
      DWORD const error{GetLastError()};
      VirtualFree(words, 0, MEM_RELEASE);
      return {own_table_make_e::stubs_memory, error};
      }
    own_table_made_t made{};
    // the words before entry 0 (the type information of the C++ ABIs) as they are; zero when they cannot be read
    void const * const header{
      reinterpret_cast<void const *>(reinterpret_cast<std::uintptr_t>(runtime) - own_table_header * sizeof(void *))};
    made.header_copied = copy_readable(words, header, own_table_header * sizeof(void *));
    if(not made.header_copied)
      std::fill_n(words, own_table_header, nullptr);
    void ** const entries{words + own_table_header};
    std::uint8_t * const stubs{code + own_stub_offset};
    for(std::size_t i{}; i != count; ++i)
      {
      std::array<std::uint8_t, own_stub_step> const stub{own_stub_bytes(address(runtime + i))};
      std::memcpy(stubs + i * own_stub_step, stub.data(), stub.size());
      entries[i] = stubs + i * own_stub_step;
      }
    own_table_mark_t const mark{
      own_table_mark(address(runtime), address(entries), count, address(stubs), hook_contract_version)};
    std::memcpy(code, &mark, sizeof mark);
    DWORD old{};
    if(not VirtualProtect(code, code_bytes, PAGE_EXECUTE_READ, &old))
      {
      DWORD const error{GetLastError()};
      VirtualFree(code, 0, MEM_RELEASE);
      VirtualFree(words, 0, MEM_RELEASE);
      return {own_table_make_e::stubs_protect, error};
      }
    FlushInstructionCache(GetCurrentProcess(), code, code_bytes);
    out = own_table_t{runtime, entries, count, stubs};
    return made;
    }

  auto own_table_put(own_table_t const & table, std::size_t slot, void * hook) noexcept -> void *
    {
    if(not table.made() or slot >= table.count)
      return nullptr;
    // the context may already point at the table (another hook put later): the entry is read by other threads' calls
    std::atomic_ref<void *> const entry{table.entries[slot]};
    if(entry.load(std::memory_order_relaxed) != hook)
      entry.store(hook, std::memory_order_release);
    return table.stub(slot);
    }

  auto own_table_point(own_table_t const & table, void * object) noexcept -> void
    {
    if(not table.made() or not object)
      return;
    std::atomic_ref<void **>{*static_cast<void ***>(object)}.store(table.entries, std::memory_order_release);
    }

  auto find_mark(void * const * table, std::span<void * const> first) noexcept -> mark_search_t
    {
    mark_candidates_t candidates{};
    std::size_t const looked{std::min<std::size_t>(first.size(), context_first_entries)};
    for(std::size_t i{}; i != looked and candidates.count != own_mark_candidates; ++i)
      {
      void * const entry{first[i]};
      MEMORY_BASIC_INFORMATION m{};
      if(not entry or module_of(entry) or VirtualQuery(entry, &m, sizeof m) != sizeof m)
        continue;
      candidates.add(address(m.AllocationBase), static_cast<std::uint32_t>(i));
      }
    std::array<mark_reading_t, own_mark_candidates> readings{};
    for(std::size_t k{}; k != candidates.count; ++k)
      {
      mark_reading_t & r{readings[k]};
      r.slot = candidates.slot[k];
      r.entry = address(first[r.slot]);
      r.read = read_candidate(reinterpret_cast<void const *>(static_cast<std::uintptr_t>(candidates.base[k])), r);
      }
    return first_mark(std::span<mark_reading_t const>{readings}.first(candidates.count), address(table));
    }
  }  // namespace edplugin_common::hook
