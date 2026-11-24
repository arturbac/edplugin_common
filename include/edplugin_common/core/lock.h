// edplugin_common - SRW locks held for a scope, each noted on a short stack of the thread while it is held. A fault
// under the guard (guard/guard.h) unwinds the frames above it without /EHa: a destructor there runs or not, as the
// compiler made the call the fault came through (a call that may throw: it runs; one known not to: it does not).
// Each destructor takes its own lock off the note, wherever it stands, so after the fault the note holds exactly the
// locks whose destructors did not run, and the guard lets go of those.
#pragma once

#include <windows.h>

namespace edplugin_common
  {
  namespace detail
    {
    struct held_lock_t
      {
      SRWLOCK * lock;
      bool shared;
      };

    inline constexpr int held_room{8};

    ///\brief the locks this thread holds through exclusive_lock_t and shared_lock_t, the latest on top. Past the room
    /// a lock is only counted: it is let go before every noted one under it, but a fault cannot let it go
    struct held_locks_t
      {
      held_lock_t at[held_room];
      int depth;
      int untracked;
      };

    // constant-initialised and trivially destructible: no work at a thread's start or end
    inline thread_local constinit held_locks_t held_locks{};

    inline auto note_held(SRWLOCK & lock, bool shared) noexcept -> void
      {
      held_locks_t & h{held_locks};
      if(h.untracked != 0 or h.depth == held_room)
        {
        ++h.untracked;
        return;
        }
      h.at[h.depth] = {&lock, shared};
      ++h.depth;
      }

    ///\brief `lock` taken off the note: the latest entry of it, the ones above moved down (a lock let go out of order,
    /// or a destructor run in an unwind that left the ones above it). With locks past the room held, one of those is
    /// counted off instead: they are the latest, and they cannot be told apart
    inline auto note_let_go(SRWLOCK & lock, bool shared) noexcept -> void
      {
      held_locks_t & h{held_locks};
      if(h.untracked != 0)
        {
        --h.untracked;
        return;
        }
      for(int i{h.depth - 1}; i >= 0; --i)
        if(h.at[i].lock == &lock and h.at[i].shared == shared)
          {
          for(int j{i}; j + 1 < h.depth; ++j)
            h.at[j] = h.at[j + 1];
          --h.depth;
          return;
          }
      }
    }  // namespace detail

  ///\brief an SRW lock held exclusively for a scope: let go on every way out of it
  class exclusive_lock_t
    {
    SRWLOCK & lock_;

  public:
    explicit exclusive_lock_t(SRWLOCK & lock) noexcept : lock_{lock}
      {
      AcquireSRWLockExclusive(&lock_);
      detail::note_held(lock_, false);
      }

    ~exclusive_lock_t()
      {
      detail::note_let_go(lock_, false);
      ReleaseSRWLockExclusive(&lock_);
      }

    exclusive_lock_t(exclusive_lock_t const &) = delete;
    auto operator=(exclusive_lock_t const &) -> exclusive_lock_t & = delete;
    };

  ///\brief an SRW lock held shared for a scope
  class shared_lock_t
    {
    SRWLOCK & lock_;

  public:
    explicit shared_lock_t(SRWLOCK & lock) noexcept : lock_{lock}
      {
      AcquireSRWLockShared(&lock_);
      detail::note_held(lock_, true);
      }

    ~shared_lock_t()
      {
      detail::note_let_go(lock_, true);
      ReleaseSRWLockShared(&lock_);
      }

    shared_lock_t(shared_lock_t const &) = delete;
    auto operator=(shared_lock_t const &) -> shared_lock_t & = delete;
    };
  }  // namespace edplugin_common
