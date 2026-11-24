// edplugin_common - a plugin's code under a fault handler (include/edplugin_common/guard/guard.h). Built without LTO
// (CMakeLists.txt): call_caught stays a function of its own around its call, whatever a plugin's build does.
#include <edplugin_common/guard/guard.h>

#include <edplugin_common/core/lock.h>
#include <edplugin_common/log/log.h>
#include <edplugin_common/memory/memory.h>
#include <edplugin_common/text/format.h>

#include <windows.h>
#include <malloc.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

namespace edplugin_common
  {
  namespace
    {
    constexpr DWORD thread_name_code{0x406D1388};
    constexpr DWORD debug_print_code{0x40010006};       // DBG_PRINTEXCEPTION_C
    constexpr DWORD debug_print_wide_code{0x4001000A};  // DBG_PRINTEXCEPTION_WIDE_C

    ///\brief codes that tell something to a debugger and are no fault: always passed on
    [[nodiscard]]
    constexpr auto never_a_fault(DWORD code) noexcept -> bool
      {
      return code == thread_name_code or code == debug_print_code or code == debug_print_wide_code;
      }

    ///\brief codes a debugger takes; without one they are faults (a jump into 0xCC through a broken pointer)
    [[nodiscard]]
    constexpr auto debugger_code(DWORD code) noexcept -> bool
      {
      return code == EXCEPTION_BREAKPOINT or code == EXCEPTION_SINGLE_STEP;
      }

    static_assert(never_a_fault(0x406D1388) and never_a_fault(0x40010006) and never_a_fault(0x4001000A)
                  and not never_a_fault(EXCEPTION_ACCESS_VIOLATION) and not never_a_fault(cpp_exception_code));
    static_assert(debugger_code(0x80000003) and debugger_code(0x80000004) and not debugger_code(EXCEPTION_ACCESS_VIOLATION));

    ///\brief the filter: the code, the place, and an access violation's kind and address only - no lock, no allocation,
    /// no module's name (the stack may be used up, the loader's lock held)
    auto filter(EXCEPTION_POINTERS const * e, fault_t & fault) noexcept -> int
      {
      EXCEPTION_RECORD const * const r{e ? e->ExceptionRecord : nullptr};
      DWORD const code{r ? r->ExceptionCode : 0};
      int const action{fault_action(code)};
      if(action != EXCEPTION_EXECUTE_HANDLER)
        return action;
      fault.code = code;
      fault.address = r ? r->ExceptionAddress : nullptr;
      fault.module = nullptr;
      fault.access = 0;
      fault.accessed = nullptr;
      if(r and code == EXCEPTION_ACCESS_VIOLATION and r->NumberParameters >= 2)
        {
        fault.access = r->ExceptionInformation[0];
        fault.accessed = reinterpret_cast<void const *>(r->ExceptionInformation[1]);
        }
      fault.locks_let_go = 0;
      fault.locks_untracked = false;
      fault.cpp = false;
      fault.stack_not_reset = false;
      fault.what[0] = '\0';
      return EXCEPTION_EXECUTE_HANDLER;
      }

    ///\brief the __try itself, around a call through a pointer; no object with a destructor here: the handler unwinds
    /// nothing but the frames above it
    [[gnu::noinline]]
    auto call_caught(guarded_body_t body, void * arg, fault_t & fault) noexcept -> bool
      {
      __try
        {
        body(arg);
        }
      __except(filter(GetExceptionInformation(), fault))
        {
        return false;
        }
      return true;
      }

    ///\brief the library's locks this thread noted above `depth` let go, from the top; how many
    auto let_go_from(int depth) noexcept -> int
      {
      detail::held_locks_t & h{detail::held_locks};
      int n{};
      while(h.depth > depth)
        {
        --h.depth;
        detail::held_lock_t const held{h.at[h.depth]};
        if(held.shared)
          ReleaseSRWLockShared(held.lock);
        else
          ReleaseSRWLockExclusive(held.lock);
        ++n;
        }
      return n;
      }

    auto end_batch(void *) noexcept -> void { log_batch_end(); }

    ///\brief this thread's log batch ended under a guard of its own: a fault in the log's own write turns the log off
    /// for the session, and the locks that write took are let go
    auto end_batch_guarded() noexcept -> void
      {
      int const before{detail::held_locks.depth};
      fault_t log_fault;
      if(not call_caught(&end_batch, nullptr, log_fault))
        {
        let_go_from(before);
        log_break();
        }
      }

    ///\brief what follows every fault, in this order: the stack (first, so the rest has its guard page again), the
    /// locks, the module, the thread's log batch
    auto after_fault(fault_t & fault, int depth, int untracked) noexcept -> void
      {
      if(fault.code == EXCEPTION_STACK_OVERFLOW)
        fault.stack_not_reset = _resetstkoflw() == 0;
      detail::held_locks_t & h{detail::held_locks};
      // locks taken past the note whose destructors did not run: they cannot be let go
      fault.locks_untracked = h.untracked > untracked;
      h.untracked = untracked;
      fault.locks_let_go = let_go_from(depth);
      fault.module = module_of(fault.address);
      end_batch_guarded();
      }

    ///\brief a log line written under a guard of its own: a fault in it turns the log off, not the process
    template<typename F>
    auto log_guarded(F && write) noexcept -> void
      {
      fault_t ignored;
      if(not run_guarded(write, ignored))
        log_break();
      }
    }  // namespace

  auto fault_action(DWORD code) noexcept -> int
    {
    if(never_a_fault(code))
      return EXCEPTION_CONTINUE_SEARCH;
    if(debugger_code(code) and IsDebuggerPresent())
      return EXCEPTION_CONTINUE_SEARCH;
#if defined(EDPLUGIN_COMMON_UBSAN_TRAP)
    if(code == EXCEPTION_ILLEGAL_INSTRUCTION)
      return EXCEPTION_CONTINUE_SEARCH;
#endif
    return EXCEPTION_EXECUTE_HANDLER;
    }

  auto run_guarded_call(guarded_body_t body, void * arg, fault_t & fault) noexcept -> bool
    {
    int const depth{detail::held_locks.depth};
    int const untracked{detail::held_locks.untracked};
    if(call_caught(body, arg, fault))
      return true;
    after_fault(fault, depth, untracked);
    return false;
    }

  auto fault_line(fault_t const & fault, char (&text)[320]) noexcept -> std::string_view
    {
    wchar_t path[MAX_PATH]{};
    std::wstring_view file;
    if(fault.module and GetModuleFileNameW(fault.module, path, MAX_PATH) != 0)
      {
      file = path;
      if(std::size_t const slash{file.find_last_of(L"\\/")}; slash != std::wstring_view::npos)
        file.remove_prefix(slash + 1);
      }
    char what[200]{};
    if(fault.cpp)
      (void)try_format_into(what, "a C++ exception ({})", std::string_view{fault.what});
    else if(fault.code == EXCEPTION_ACCESS_VIOLATION)
      (void)try_format_into(what, "an access violation {} 0x{:X}",
                            fault.access == 1 ? "writing" : fault.access == 8 ? "executing" : "reading",
                            reinterpret_cast<std::uintptr_t>(fault.accessed));
    else if(fault.code == EXCEPTION_STACK_OVERFLOW)
      (void)try_format_into(what, "a stack overflow");
    else
      (void)try_format_into(what, "exception 0x{:08X}", static_cast<unsigned long>(fault.code));
    char place[MAX_PATH + 48]{};
    auto const at{reinterpret_cast<std::uintptr_t>(fault.address)};
    if(fault.module)
      (void)try_format_into(place, " at {}+0x{:X}", wide(file.empty() ? std::wstring_view{L"?"} : file),
                            at - reinterpret_cast<std::uintptr_t>(fault.module));
    else if(not fault.cpp)
      (void)try_format_into(place, " at 0x{:X} (no module)", at);
    std::size_t size{try_format_into(text, "{}{}", std::string_view{what}, std::string_view{place}).value_or(0)};
    if(fault.locks_let_go != 0)
      size += try_format_into(text + size, sizeof text - size, "; {} lock(s) let go", fault.locks_let_go).value_or(0);
    if(fault.locks_untracked)
      size += try_format_into(text + size, sizeof text - size, "; a lock past the thread's note held, not let go").value_or(0);
    if(fault.stack_not_reset)
      size += try_format_into(text + size, sizeof text - size, "; the stack's guard page not set again").value_or(0);
    if(size == sizeof text - 1)
      std::memcpy(text + size - 3, "...", 3);
    return std::string_view{text, size};
    }

  auto detail::set_cpp_fault(fault_t & fault, char const * what) noexcept -> void
    {
    fault.code = cpp_exception_code;
    fault.address = nullptr;
    fault.module = nullptr;
    fault.access = 0;
    fault.accessed = nullptr;
    fault.locks_let_go = 0;
    fault.locks_untracked = false;
    fault.cpp = true;
    fault.stack_not_reset = false;
    std::size_t n{};
    while(what and n + 1 < sizeof fault.what and what[n] != '\0')
      {
      fault.what[n] = what[n];
      ++n;
      }
    fault.what[n] = '\0';
    }

  auto detail::after_cpp_fault() noexcept -> void { end_batch_guarded(); }

  auto guard_t::turn_off(char const * why) noexcept -> void
    {
    if(broken_.exchange(true, std::memory_order_relaxed))
      return;
    log_guarded([&] { log_line("{}: off for this session ({}, {} fault(s))", name_, why, faults()); });
    }

  auto guard_t::fault_seen(fault_t const & fault) noexcept -> void
    {
    int const n{faults_.fetch_add(1, std::memory_order_relaxed) + 1};
    if(n <= 16)
      log_guarded(
        [&]
        {
          char text[320];
          std::string_view const line{fault_line(fault, text)};
          if(limit_ != 0)
            log_line("{}: {} ({} of {})", name_, line, n, limit_);
          else
            log_line("{}: {} ({})", name_, line, n);
        });
    if(fault.code == EXCEPTION_STACK_OVERFLOW)
      turn_off("a stack overflow");
    else if(fault.locks_untracked)
      turn_off("a lock not let go");
    else if(fault.module != nullptr and fault.module != module_of(reinterpret_cast<void const *>(&fault_action)))
      turn_off("a fault in another module, whose locks may be held");
    else if(limit_ != 0 and n >= limit_)
      turn_off("the limit of faults");
    }

  auto guard_t::handler_faulted(fault_t const & fault) noexcept -> void
    {
    log_guarded(
      [&]
      {
        char text[320];
        log_line("{}: its fault handler faulted: {}", name_, fault_line(fault, text));
      });
    turn_off("its fault handler faulted");
    }

  auto run_guarded_thread(char const * name, DWORD (*body)(void *), void * arg, int attempts) noexcept -> DWORD
    {
    attempts = std::max(attempts, 1);
    for(int n{1}; n <= attempts; ++n)
      {
      DWORD result{};
      fault_t fault;
      if(run_guarded([&] { result = body(arg); }, fault))
        return result;
      bool const overflow{fault.code == EXCEPTION_STACK_OVERFLOW};
      log_guarded(
        [&]
        {
          char text[320];
          log_line("thread {}: {} ({} of {}){}", name, fault_line(fault, text), n, attempts,
                   overflow ? "; not started again" : "");
        });
      if(overflow)
        return 0;
      if(n < attempts)
        Sleep(1000);
      }
    log_guarded([&] { log_line("thread {}: stopped after {} fault(s)", name, attempts); });
    return 0;
    }
  }  // namespace edplugin_common
