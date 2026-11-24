// edplugin_common - a state whose changes are logged once each: `if(s.changed(v)) log_line(...)`.
#pragma once

#include <atomic>
#include <type_traits>

namespace edplugin_common
  {
  ///\brief the last value of a state seen (an integer or an enumeration), lock-free; changed() is true when a value
  /// differs from the last one, so a line is logged once per change of state, not once per frame. Relaxed: it orders
  /// no other data. Constant-initialised, so a global one does no work when the dll is loaded
  template<typename value_t>
  class state_log_t
    {
    static_assert(std::is_integral_v<value_t> or std::is_enum_v<value_t>, "state_log_t: an integer or an enumeration");
    static_assert(std::atomic<value_t>::is_always_lock_free, "state_log_t: a lock-free value");

    std::atomic<value_t> value_;

  public:
    ///\brief `first`: the value taken as seen before the first change (the caller's "unknown")
    constexpr explicit state_log_t(value_t first) noexcept : value_{first} {}

    state_log_t(state_log_t const &) = delete;
    auto operator=(state_log_t const &) -> state_log_t & = delete;

    ///\brief `now` kept as the last value; true when it differs from the one before
    [[nodiscard]]
    auto changed(value_t now) noexcept -> bool
      {
      return value_.exchange(now, std::memory_order_relaxed) != now;
      }

    ///\brief the last value kept
    [[nodiscard]]
    auto last() const noexcept -> value_t
      {
      return value_.load(std::memory_order_relaxed);
      }
    };
  }  // namespace edplugin_common
