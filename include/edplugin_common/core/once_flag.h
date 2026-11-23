// edplugin_common - a flag for what is done or said once (a log line on the first time a state is seen).
#pragma once

#include <atomic>

namespace edplugin_common
  {
  ///\brief true from first() once only, also when threads ask at the same time; constant-initialised, so a global
  /// one does no work when the dll is loaded. Relaxed: it orders no other data, so it does not publish a result to
  /// another thread (for that, an atomic with release and acquire)
  class once_flag_t
    {
    std::atomic<bool> done_{};

  public:
    constexpr once_flag_t() noexcept = default;

    once_flag_t(once_flag_t const &) = delete;
    auto operator=(once_flag_t const &) -> once_flag_t & = delete;

    ///\brief true for the first caller only (until reset)
    [[nodiscard]]
    auto first() noexcept -> bool
      {
      return not done_.exchange(true, std::memory_order_relaxed);
      }

    ///\brief whether first() has said true already
    [[nodiscard]]
    auto done() const noexcept -> bool
      {
      return done_.load(std::memory_order_relaxed);
      }

    ///\brief the next first() says true again
    auto reset() noexcept -> void { done_.store(false, std::memory_order_relaxed); }
    };
  }  // namespace edplugin_common
