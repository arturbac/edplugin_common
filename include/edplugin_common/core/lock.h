// edplugin_common - SRW locks held for a scope.
#pragma once

#include <windows.h>

namespace edplugin_common
  {
  ///\brief an SRW lock held exclusively for a scope: let go on every way out of it
  class exclusive_lock_t
    {
    SRWLOCK & lock_;

  public:
    explicit exclusive_lock_t(SRWLOCK & lock) noexcept : lock_{lock} { AcquireSRWLockExclusive(&lock_); }

    ~exclusive_lock_t() { ReleaseSRWLockExclusive(&lock_); }

    exclusive_lock_t(exclusive_lock_t const &) = delete;
    auto operator=(exclusive_lock_t const &) -> exclusive_lock_t & = delete;
    };

  ///\brief an SRW lock held shared for a scope
  class shared_lock_t
    {
    SRWLOCK & lock_;

  public:
    explicit shared_lock_t(SRWLOCK & lock) noexcept : lock_{lock} { AcquireSRWLockShared(&lock_); }

    ~shared_lock_t() { ReleaseSRWLockShared(&lock_); }

    shared_lock_t(shared_lock_t const &) = delete;
    auto operator=(shared_lock_t const &) -> shared_lock_t & = delete;
    };
  }  // namespace edplugin_common
