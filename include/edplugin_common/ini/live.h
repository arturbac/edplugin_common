// edplugin_common - a plugin's settings as the game's threads see them while the ini is read again: two slots and an
// atomic pointer to the one in use (edworld's model), without Windows.
//
// The contract:
//   - start(s) fills both slots with the same settings, before any other thread reads them;
//   - the start-only fields (setting_read_e::at_start) never change after it: any thread may read them, through get()
//     or a reference kept as long as it likes;
//   - the live fields, and the slot's generation, are read only on the thread that publishes (the render thread, where
//     the plugin looks at its ini): publish() writes the slot not in use, which a reference from two publications
//     before may still show;
//   - a field read on another thread while the game runs gets a copy of its own the publishing thread keeps (a value
//     taken at the start, or a std::atomic mirror set at publish, as edupscale's cpu_probe for its hooks on the game's
//     loading threads).
//
// live3_settings_t: the same settings with three slots, for a publishing thread other than the one that reads the live
// fields (one producer, one consumer), and a fourth copy of the start-only fields. Its contract:
//   - start(s, told) fills the three slots and the start copy, before any other thread reads them; a plugin keeps the
//     object behind a global atomic pointer (live3_start), so a reader that sees the pointer sees it started;
//   - publish() on the producer's thread only: the live rows into the slot of its own, then that slot swapped with the
//     middle one by one exchange; it may allocate (std::vector, std::wstring), the consumer never does;
//   - acquire() on one consumer thread only, the same one for the whole session (the render thread: the thread of the
//     first acquire): the newest slot when there is one, else the slot it had; the reference holds until its next
//     acquire. Publications in between are lost; both sides wait-free. An acquire on another thread breaks the contract
//     and is said once a session through `told`: when it came first, the render thread's live_fields stays the start
//     copy; when it comes after, the two threads race for the consumer's slot, and a reference the render thread holds
//     may show a slot the producer writes;
//   - start_fields() on any thread, as long as the game runs: the start copy, which nothing writes after start;
//   - live_fields() on any thread: the slot of the last acquire on the thread that did the first acquire (the render
//     thread), the start copy before that first acquire, and the start copy on every other thread, said once a
//     session through `told` (a thread other than the render one never reads a slot).
#pragma once

#include <edplugin_common/ini/ini_table.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>
#include <thread>

namespace edplugin_common
  {
  template<typename settings_t>
  class live_settings_t
    {
  public:
    struct slot_t
      {
      settings_t settings{};
      std::uint32_t generation{};  ///< one more at each publication; 0 from start()
      };

  private:
    slot_t slots_[2]{};
    std::atomic<slot_t const *> live_{&slots_[0]};

  public:
    live_settings_t() = default;
    live_settings_t(live_settings_t const &) = delete;
    auto operator=(live_settings_t const &) -> live_settings_t & = delete;

    ///\brief both slots `s`, generation 0; before any other thread reads them
    auto start(settings_t const & s) -> void
      {
      slots_[0].settings = s;
      slots_[1].settings = s;
      slots_[0].generation = 0;
      slots_[1].generation = 0;
      live_.store(&slots_[0], std::memory_order_release);
      }

    [[nodiscard]]
    auto get() const noexcept -> settings_t const &
      {
      return live_.load(std::memory_order_acquire)->settings;
      }

    ///\brief the settings and their generation through one pointer (on the publishing thread)
    [[nodiscard]]
    auto slot() const noexcept -> slot_t const &
      {
      return *live_.load(std::memory_order_acquire);
      }

    ///\brief the live rows of `read` into the slot not in use, field by field, its generation one more, then that slot
    /// in use. On the publishing thread only
    template<typename group_t>
    auto publish(settings_t const & read, ini_table_t<settings_t, group_t> const & table) -> void
      {
      slot_t const & now{*live_.load(std::memory_order_relaxed)};
      slot_t & spare{&now == &slots_[0] ? slots_[1] : slots_[0]};
      ini_copy_live(spare.settings, read, table);
      spare.generation = now.generation + 1u;
      live_.store(&spare, std::memory_order_release);
      }
    };

  ///\brief the line live3_settings_t hands the plugin for its log (the library logs nothing here)
  using live3_told_t = void (*)(std::string_view line) noexcept;

  namespace detail
    {
    ///\brief for the library's test only: a look at a live3_settings_t's own members
    struct live3_settings_peek_t;
    }  // namespace detail

  template<typename settings_t>
  class live3_settings_t
    {
  public:
    struct slot_t
      {
      settings_t settings{};
      std::uint32_t generation{};  ///< one more at each publication; 0 from start()
      };

    static constexpr std::string_view other_thread_line{
      "settings: a live setting read on a thread other than the render thread; the value from the game's start given "
      "there (said once a session)"};

    static constexpr std::string_view other_acquire_line{
      "settings: live settings taken on a thread other than the render thread (the first one to take them); one "
      "thread has to take them for the whole session (said once a session)"};

  private:
    static constexpr std::uint8_t index_bits{3};
    static constexpr std::uint8_t fresh{4};  ///< in middle_: its slot not yet taken by acquire
    static_assert(std::atomic<std::uint8_t>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);

    slot_t slots_[3]{};
    std::atomic<std::uint8_t> middle_{1};  ///< the slot between the two sides, and fresh
    std::uint8_t back_{0};                 ///< the producer's own slot
    std::uint8_t front_{2};                ///< the consumer's own slot
    std::uint32_t generation_{};           ///< the producer's count
    settings_t start_{};
    live3_told_t told_{};
    std::atomic<bool> first_acquired_{};
    std::thread::id render_thread_{};  ///< written once, before first_acquired_
    mutable std::atomic<bool> other_thread_told_{};
    std::atomic<bool> other_acquire_told_{};

    friend struct detail::live3_settings_peek_t;

  public:
    live3_settings_t() = default;
    live3_settings_t(live3_settings_t const &) = delete;
    auto operator=(live3_settings_t const &) -> live3_settings_t & = delete;

    ///\brief the start copy and the three slots `s`, generation 0; `told` logs the lines `other_thread_line` and
    /// `other_acquire_line`, on the thread that reads (any; nullptr: no line). Before any other thread reads the object
    auto start(settings_t const & s, live3_told_t told) -> void
      {
      start_ = s;
      for(slot_t & slot: slots_)
        {
        slot.settings = s;
        slot.generation = 0;
        }
      generation_ = 0;
      back_ = 0;
      middle_.store(1, std::memory_order_relaxed);
      front_ = 2;
      told_ = told;
      }

    ///\brief the live rows of `read` into the producer's slot, field by field, its generation one more, then that slot
    /// the middle one, marked fresh. On the producer's thread only
    template<typename group_t>
    auto publish(settings_t const & read, ini_table_t<settings_t, group_t> const & table) -> void
      {
      slot_t & own{slots_[back_]};
      ini_copy_live(own.settings, read, table);
      own.generation = ++generation_;
      back_ = middle_.exchange(static_cast<std::uint8_t>(back_ | fresh), std::memory_order_acq_rel) & index_bits;
      }

    ///\brief the newest published slot, or the one taken before when nothing is new; valid to the next acquire. On
    /// the consumer's thread only, the same for the whole session; the first acquire's thread is the render thread of
    /// live_fields(), an acquire on another thread is said once through `told` (other_acquire_line)
    [[nodiscard]]
    auto acquire() noexcept -> slot_t const &
      {
      if((middle_.load(std::memory_order_relaxed) & fresh) != 0)
        front_ = middle_.exchange(front_, std::memory_order_acq_rel) & index_bits;
      if(not first_acquired_.load(std::memory_order_acquire))
        {
        render_thread_ = std::this_thread::get_id();
        first_acquired_.store(true, std::memory_order_release);
        }
      else if(std::this_thread::get_id() != render_thread_ and not other_acquire_told_.load(std::memory_order_relaxed)
              and not other_acquire_told_.exchange(true, std::memory_order_relaxed) and told_ != nullptr)
        told_(other_acquire_line);
      return slots_[front_];
      }

    ///\brief the settings as they were at start(); on any thread. Its live fields keep their start values
    [[nodiscard]]
    auto start_fields() const noexcept -> settings_t const &
      {
      return start_;
      }

    ///\brief on the render thread after its first acquire the slot of its last acquire; else the start copy, and on a
    /// thread other than the render one the line `other_thread_line` through `told` once a session
    [[nodiscard]]
    auto live_fields() const noexcept -> settings_t const &
      {
      if(not first_acquired_.load(std::memory_order_acquire))
        return start_;
      if(std::this_thread::get_id() == render_thread_)
        return slots_[front_].settings;
      if(not other_thread_told_.exchange(true, std::memory_order_relaxed) and told_ != nullptr)
        told_(other_thread_line);
      return start_;
      }
    };

  ///\brief the object for a plugin's global `constinit std::atomic<live3_settings_t<settings_t> *> live{nullptr};`
  /// (trivially destructible): made once, started, then its address stored with release, so a thread that loads the
  /// pointer with acquire sees it whole, and nullptr before (settings not yet there: the reader's work that needs them
  /// passed over, without a wait). Never deleted: nothing runs at the process's end. An exception (the allocation, a
  /// copy of `s`) leaves the pointer nullptr and goes to the caller. Once per pointer
  template<typename settings_t>
  auto live3_start(std::atomic<live3_settings_t<settings_t> *> & to, settings_t const & s, live3_told_t told) -> void
    {
    std::unique_ptr<live3_settings_t<settings_t>> made{std::make_unique<live3_settings_t<settings_t>>()};
    made->start(s, told);
    to.store(made.release(), std::memory_order_release);
    }
  }  // namespace edplugin_common
