// edplugin_common - watching the context's hooks after the attach (include/edplugin_common/hook/watch.h).
#include <edplugin_common/hook/watch.h>

#include <edplugin_common/memory/memory.h>

#include <windows.h>

#include <array>
#include <atomic>

namespace edplugin_common::hook
  {
  static_assert(watch_slots_most <= 32, "quiet_slots() gives a bit a slot in 32 bits");

  namespace
    {
    [[nodiscard]]
    auto address(void const * p) noexcept -> std::uint64_t
      {
      return reinterpret_cast<std::uintptr_t>(p);
      }

    ///\brief a foreign table's entry that could not be read (no code lies at 1)
    inline constexpr std::uint64_t unread_entry{1};

    ///\brief an entry of a table that is never freed while the context lives (ours, the runtime's): read as it is now
    [[nodiscard]]
    auto load_entry(void * const * table, std::size_t slot) noexcept -> std::uint64_t
      {
      return address(std::atomic_ref<void *>{const_cast<void **>(table)[slot]}.load(std::memory_order_acquire));
      }
    }  // namespace

  auto context_watch_t::start(watch_setup_t const & setup) noexcept -> watch_start_e
    {
    if(started() or not setup.object or not setup.table)
      return watch_start_e::no_object;
    std::size_t n{};
    for(slot_patch_t const & s : setup.slots)
      if(s.result == slot_result_e::patched or s.result == slot_result_e::already_ours)
        ++n;
    if(n > watch_slots_most)
      return watch_start_e::too_many_slots;
    if(n == 0)
      return watch_start_e::no_slots;
    table_ = setup.table;
    runtime_ = setup.runtime;
    own_table_ = setup.own != nullptr;
    if(setup.own)
      ranges_ = watch_ranges_t{address(setup.own->stubs), address(setup.own->stubs) + setup.own->count * own_stub_step};
    system_first_ = setup.system_first;
    system_end_ = setup.system_end;
    count_ = 0;
    for(slot_patch_t const & s : setup.slots)
      {
      if(s.result != slot_result_e::patched and s.result != slot_result_e::already_ours)
        continue;
      std::size_t const i{count_++};
      slots_[i] = slot_watch_t{s.slot, address(s.hook), address(s.original)};
      runtime_at_start_[i] = runtime_ ? load_entry(runtime_, s.slot) : 0;
      last_entry_[i].store(address(s.hook), std::memory_order_relaxed);
      last_class_[i].store(slot_class_e::ours, std::memory_order_relaxed);
      }
    last_table_.store(address(table_), std::memory_order_relaxed);
    object_.store(setup.object, std::memory_order_release);
    return watch_start_e::started;
    }

  auto context_watch_t::place_of(std::uint64_t entry) const noexcept -> entry_place_e
    {
    if(entry >= system_first_ and entry < system_end_)
      return entry_place_e::system_d3d11;
    return module_of(reinterpret_cast<void const *>(static_cast<std::uintptr_t>(entry))) ? entry_place_e::other_module
                                                                                         : entry_place_e::no_module;
    }

  auto context_watch_t::runtime_entry(std::size_t slot) const noexcept -> std::optional<std::uint64_t>
    {
    if(not runtime_)
      return std::nullopt;
    return load_entry(runtime_, slot);
    }

  auto context_watch_t::class_table(void * const * table) const noexcept -> table_class_e
    {
    table_class_e worst{table_class_e::ours};
    for(std::size_t i{}; i != count_; ++i)
      {
      slot_watch_t const & w{slots_[i]};
      foreign_entry_t e{};
      void * entry{};
      e.read = copy_readable(&entry, table + w.slot, sizeof entry);
      if(e.read)
        {
        e.entry = address(entry);
        e.place = place_of(e.entry);
        std::array<std::uint8_t, own_stub_step> code{};
        if(copy_readable(code.data(), entry, code.size()))
          e.through = jump_through(code, e.entry);
        }
      worst = worse(worst, classify_foreign(e, w, address(table_ + w.slot), runtime_entry(w.slot)));
      }
    // the object off our table with every entry read as ours is still a table not ours
    return worst == table_class_e::ours ? table_class_e::covered : worst;
    }

  auto context_watch_t::check() noexcept -> watch_state_t
    {
    watch_state_t state{};
    void * const object{object_.load(std::memory_order_acquire)};
    if(not object)
      return state;
    for(std::size_t i{}; i != count_; ++i)
      {
      slot_watch_t const & w{slots_[i]};
      std::uint64_t const entry{load_entry(table_, w.slot)};
      slot_class_e c{last_class_[i].load(std::memory_order_relaxed)};
      if(entry != last_entry_[i].load(std::memory_order_relaxed))
        {
        c = classify_entry(w, entry, place_of(entry), runtime_entry(w.slot), ranges_);
        last_entry_[i].store(entry, std::memory_order_relaxed);
        last_class_[i].store(c, std::memory_order_relaxed);
        }
      if(i == 0 or worse(state.slots, c) != state.slots)
        {
        state.slots = c;
        state.slot = w.slot;
        state.entry = entry;
        }
      }
    void * const * const pointed{
      std::atomic_ref<void **>{*static_cast<void ***>(object)}.load(std::memory_order_acquire)};
    state.object_table = address(pointed);
    if(pointed == table_)
      state.table = table_class_e::ours;
    else
      {
      // a table not ours: classed again when its address or a watched entry in it changed (a plugin above refreshing
      // its copy, the runtime writing over it)
      bool same{address(pointed) == last_table_.load(std::memory_order_relaxed)};
      for(std::size_t i{}; i != count_; ++i)
        {
        void * entry{};
        std::uint64_t const now{copy_readable(&entry, pointed + slots_[i].slot, sizeof entry) ? address(entry)
                                                                                                : unread_entry};
        if(now != foreign_entry_[i])
          {
          foreign_entry_[i] = now;
          same = false;
          }
        }
      if(same)
        state.table = last_table_class_.load(std::memory_order_relaxed);
      else
        {
        state.table = class_table(pointed);
        last_table_.store(address(pointed), std::memory_order_relaxed);
        last_table_class_.store(state.table, std::memory_order_relaxed);
        }
      }
    state.changed = state.slots != said_slots_.load(std::memory_order_relaxed)
                    or (state.slots != slot_class_e::ours and state.slot != said_slot_.load(std::memory_order_relaxed))
                    or state.table != said_table_.load(std::memory_order_relaxed);
    said_slots_.store(state.slots, std::memory_order_relaxed);
    said_slot_.store(state.slot, std::memory_order_relaxed);
    said_table_.store(state.table, std::memory_order_relaxed);
    return state;
    }

  auto context_watch_t::index_of(std::size_t slot) const noexcept -> std::size_t
    {
    // the slots are published by start()'s store of the object: read only after it is seen
    if(not started())
      return watch_slots_most;
    for(std::size_t i{}; i != count_; ++i)
      if(slots_[i].slot == slot)
        return i;
    return watch_slots_most;
    }

  auto context_watch_t::runtime_moved() const noexcept -> std::size_t
    {
    if(not own_table_ or not runtime_)
      return 0;
    std::size_t moved{};
    for(std::size_t i{}; i != count_; ++i)
      if(load_entry(runtime_, slots_[i].slot) != runtime_at_start_[i])
        ++moved;
    return moved;
    }

  auto context_watch_t::quiet_slots(std::uint64_t now_ms, std::uint64_t quiet_ms) noexcept -> std::uint32_t
    {
    std::uint32_t quiet{};
    for(std::size_t i{}; i != count_; ++i)
      {
      std::uint64_t const calls{calls_[i].load(std::memory_order_relaxed)};
      if(calls != census_calls_[i] or census_change_ms_[i] == 0)
        {
        census_calls_[i] = calls;
        census_change_ms_[i] = now_ms;
        continue;
        }
      if(now_ms - census_change_ms_[i] >= quiet_ms)
        quiet |= std::uint32_t{1} << i;
      }
    return quiet;
    }
  }  // namespace edplugin_common::hook
