// edplugin_common - watching the context's hooks after the attach: every watched slot's entry and the table the
// context object points at, classed by hook/watch_class.h. Cheap enough for a frame: an entry or a table is classed
// again only when it changed since the last look (a table not ours: its address or a watched entry in it). The plugin turns itself off (D3) when check() says so, and tells the
// other classes once. The context's reference is the one context_hooks_t::attach() took (never released: contract c).
// For the census: how many runtime entries a table of our own followed, and the slots whose hooks were not called for a
// while.
#pragma once

#include <edplugin_common/hook/own_table.h>
#include <edplugin_common/hook/slot_plan.h>
#include <edplugin_common/hook/watch_class.h>

#include <windows.h>
#include <unknwn.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace edplugin_common::hook
  {
  ///\brief the most slots one context_watch_t watches
  inline constexpr std::size_t watch_slots_most{32};

  struct watch_setup_t
    {
    IUnknown * object{};                    ///< the context
    void ** table{};                        ///< the table the hooks are in: our own's entries, or the table in place
    std::span<slot_patch_t const> slots{};  ///< the slots as written: those patched or already ours are watched
    own_table_t const * own{};              ///< the table of our own; nullptr in place
    void * const * runtime{};               ///< the runtime's table of the way (watch_class.h classify_entry); may be null
    std::uint64_t system_first{};           ///< the system's d3d11.dll image
    std::uint64_t system_end{};
    };

  enum struct watch_start_e : std::uint8_t
    {
    started,
    no_object,       ///< no context or no table
    too_many_slots,  ///< more than watch_slots_most slots to watch
    no_slots,        ///< none of the slots patched or already ours
    };

  ///\brief what check() saw
  struct watch_state_t
    {
    slot_class_e slots{slot_class_e::ours};   ///< the watched slots' worst class
    std::size_t slot{};                       ///< a slot of that class
    std::uint64_t entry{};                    ///< its entry
    table_class_e table{table_class_e::ours}; ///< the class of the table the object points at
    std::uint64_t object_table{};             ///< the table the object points at
    bool changed{};                           ///< a class (or the slot of the worst) differs from the last check()

    [[nodiscard]]
    auto turns_off() const noexcept -> bool
      {
      return hook::turns_off(slots) or hook::turns_off(table);
      }
    };

  class context_watch_t
    {
  public:
    ///\brief the slots of `setup` watched from now on; a second start is refused
    /// (no_object) when one is already watched
    [[nodiscard]]
    auto start(watch_setup_t const & setup) noexcept -> watch_start_e;

    [[nodiscard]]
    auto started() const noexcept -> bool
      {
      return object_.load(std::memory_order_acquire) != nullptr;
      }

    ///\brief the context object watched (nullptr before start): a hook passes another context's calls through
    [[nodiscard]]
    auto object() const noexcept -> void *
      {
      return object_.load(std::memory_order_acquire);
      }

    ///\brief the entries and the object's table read and classed (each again only when it changed); one thread at a time
    [[nodiscard]]
    auto check() noexcept -> watch_state_t;

    ///\brief the index of `slot` among the watched (the order of the list), or watch_slots_most when not watched or
    /// before start(); from any thread
    [[nodiscard]]
    auto index_of(std::size_t slot) const noexcept -> std::size_t;

    ///\brief from a hook: one call of the watched slot at `index`
    auto called(std::size_t index) noexcept -> void
      {
      if(index < watch_slots_most)
        calls_[index].fetch_add(1, std::memory_order_relaxed);
      }

    ///\brief the slots watched; watched() and slot_at() only after started() was seen true
    [[nodiscard]]
    auto watched() const noexcept -> std::size_t
      {
      return count_;
      }

    ///\brief the watched slot at `index`
    [[nodiscard]]
    auto slot_at(std::size_t index) const noexcept -> std::size_t
      {
      return index < count_ ? slots_[index].slot : 0;
      }

    ///\brief on a table of our own: the watched slots whose runtime entry differs from the one at start() (the rewrites
    /// the stubs followed); 0 in place
    [[nodiscard]]
    auto runtime_moved() const noexcept -> std::size_t;

    ///\brief for the census, at `now_ms` (any steady clock in ms): the watched slots whose hooks had no call for
    /// `quiet_ms` or longer, as a bit each (bit i = index i)
    [[nodiscard]]
    auto quiet_slots(std::uint64_t now_ms, std::uint64_t quiet_ms) noexcept -> std::uint32_t;

  private:
    [[nodiscard]]
    auto place_of(std::uint64_t entry) const noexcept -> entry_place_e;
    [[nodiscard]]
    auto runtime_entry(std::size_t slot) const noexcept -> std::optional<std::uint64_t>;
    [[nodiscard]]
    auto class_table(void * const * table) const noexcept -> table_class_e;

    std::atomic<void *> object_{};
    void ** table_{};
    void * const * runtime_{};
    bool own_table_{};
    watch_ranges_t ranges_{};
    std::uint64_t system_first_{};
    std::uint64_t system_end_{};
    std::size_t count_{};
    std::array<slot_watch_t, watch_slots_most> slots_{};
    std::array<std::uint64_t, watch_slots_most> runtime_at_start_{};
    // the last look, each changed only by check() (one thread); atomics because the census may read them meanwhile
    std::array<std::atomic<std::uint64_t>, watch_slots_most> last_entry_{};
    std::array<std::atomic<slot_class_e>, watch_slots_most> last_class_{};
    std::atomic<std::uint64_t> last_table_{};
    std::atomic<table_class_e> last_table_class_{table_class_e::ours};
    std::array<std::uint64_t, watch_slots_most> foreign_entry_{};  ///< the watched entries of the last table not ours
    std::atomic<slot_class_e> said_slots_{slot_class_e::ours};
    std::atomic<std::size_t> said_slot_{};
    std::atomic<table_class_e> said_table_{table_class_e::ours};
    // calls of the hooks and the census' last look at them
    std::array<std::atomic<std::uint64_t>, watch_slots_most> calls_{};
    std::array<std::uint64_t, watch_slots_most> census_calls_{};
    std::array<std::uint64_t, watch_slots_most> census_change_ms_{};
    };
  }  // namespace edplugin_common::hook
