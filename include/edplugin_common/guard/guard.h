// edplugin_common - a plugin's code run under a fault handler: a fault of the processor (an access violation, a stack
// overflow, a jump into nothing) or a C++ exception in it comes back as a fault_t instead of ending the game.
// The guard's __try is in the library's guard.cc, around a call through a pointer: clang-cl catches a fault under
// __try only from a call. Without /EHa a destructor in the frames a fault leaves runs or not, as the compiler made the
// call the fault came through; what those frames still held of the library's locks (core/lock.h) the guard lets go,
// anything else (flags, buffers, raw locks) is the caller's handler's. A fault in another module (the system, a
// driver) may leave that module's own locks held, so guard_t turns the part off at once then.
// Never put the original under a guard (the next element of a chain, d3d11, the game's code): a fault there is the
// game's to report. Not caught: __fastfail, /GS and CFG failures, a heap found broken.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <exception>
#include <string_view>
#include <type_traits>

namespace edplugin_common
  {
  struct fault_t
    {
    DWORD code;              ///< the exception's code; 0xE06D7363 for a C++ exception
    void const * address;    ///< where it happened (nullptr for a C++ exception)
    HMODULE module;          ///< the loaded module holding `address`, nullptr when none (told after the handler)
    ULONG_PTR access;        ///< an access violation's kind: 0 read, 1 write, 8 execute
    void const * accessed;   ///< an access violation's address
    int locks_let_go;        ///< the library's locks held at the fault and let go by the guard
    bool locks_untracked;    ///< a lock held past the thread's note was held: not let go, the state is not known
    bool cpp;                ///< a C++ exception, its what() in `what`
    bool stack_not_reset;    ///< after a stack overflow the stack's guard page could not be set again
    char what[160];
    };

  inline constexpr DWORD cpp_exception_code{0xE06D7363};

  ///\brief what a fault filter does with an exception's code: EXCEPTION_CONTINUE_SEARCH for those that are no fault (a
  /// thread's name 0x406D1388, DBG_PRINTEXCEPTION_C and _WIDE_C; a breakpoint and a single step only with a debugger
  /// attached) and, in a build with EDPLUGIN_COMMON_UBSAN_TRAP, for UBSan's trap (an illegal instruction: the test ends
  /// instead of logging a fault); EXCEPTION_EXECUTE_HANDLER for the rest. Every filter of the library uses it
  [[nodiscard]]
  auto fault_action(DWORD code) noexcept -> int;

  using guarded_body_t = void (*)(void * arg) noexcept;

  ///\brief `body(arg)` under the fault handler: true when it returned, false after a fault with `fault` filled (it is
  /// not touched otherwise). After a fault, in this order: the stack's guard page set again after an overflow, the
  /// library's locks the body took and did not let go let go (from the top), this thread's log batch ended (under a
  /// guard of its own; a batch the caller began ends too)
  auto run_guarded_call(guarded_body_t body, void * arg, fault_t & fault) noexcept -> bool;

  ///\brief a fault as one line: "<what> at <module>+0x<offset>" and the locks let go; cut with "..." at the end of
  /// `text`
  auto fault_line(fault_t const & fault, char (&text)[320]) noexcept -> std::string_view;

  namespace detail
    {
    template<typename f_t>
    struct guarded_call_t
      {
      f_t * f;
      fault_t * fault;
      bool threw;
      };

    ///\brief `fault` as a C++ exception with `what`
    auto set_cpp_fault(fault_t & fault, char const * what) noexcept -> void;

    ///\brief after a C++ exception out of a guarded body: this thread's log batch ended, as after a fault
    auto after_cpp_fault() noexcept -> void;

    ///\brief the call of `f` with its C++ exceptions caught, in a function of its own, called through a pointer
    template<typename f_t>
    [[gnu::noinline]]
    auto guarded_thunk(void * p) noexcept -> void
      {
      auto & c{*static_cast<guarded_call_t<f_t> *>(p)};
      try
        {
        (*c.f)();
        }
      catch(std::exception const & e)
        {
        c.threw = true;
        set_cpp_fault(*c.fault, e.what());
        }
      catch(...)
        {
        c.threw = true;
        set_cpp_fault(*c.fault, "an exception not derived from std::exception");
        }
      }
    }  // namespace detail

  ///\brief `f()` under the fault handler, its C++ exceptions caught too (the log batch ended after one as after a
  /// fault); false after either, `fault` filled
  template<typename function_t>
  auto run_guarded(function_t && f, fault_t & fault) noexcept -> bool
    {
    using f_t = std::remove_reference_t<function_t>;
    detail::guarded_call_t<f_t> c{&f, &fault, false};
    if(not run_guarded_call(&detail::guarded_thunk<f_t>, &c, fault))
      return false;
    if(c.threw)
      detail::after_cpp_fault();
    return not c.threw;
    }

  ///\brief a part of a plugin under the guard, with a limit of faults: each fault logged with its place (the first 16
  /// of a session), the part's own handler called, and after `limit` faults (0: no limit) the part off for the
  /// session, said once. A stack overflow, a fault while a lock past the thread's note was held, or a fault in a module
  /// other than the plugin's (the one the library is linked into: its locks may be held) turns it off at once. Constant-initialised: a
  /// global one does no work when the dll is loaded
  class guard_t
    {
    char const * name_;
    int limit_;
    std::atomic<int> faults_{};
    std::atomic<bool> broken_{};

    auto fault_seen(fault_t const & fault) noexcept -> void;
    auto handler_faulted(fault_t const & fault) noexcept -> void;

  public:
    constexpr guard_t(char const * name, int limit) noexcept : name_{name}, limit_{limit} {}

    guard_t(guard_t const &) = delete;
    auto operator=(guard_t const &) -> guard_t & = delete;

    ///\brief whether the part is off for this session
    [[nodiscard]]
    auto broken() const noexcept -> bool
      {
      return broken_.load(std::memory_order_relaxed);
      }

    ///\brief the faults caught so far
    [[nodiscard]]
    auto faults() const noexcept -> int
      {
      return faults_.load(std::memory_order_relaxed);
      }

    [[nodiscard]]
    auto name() const noexcept -> char const *
      {
      return name_;
      }

    ///\brief the part off for this session from now, said once with `why`
    auto turn_off(char const * why) noexcept -> void;

    ///\brief `f()` under the guard, unless the part is off: true when it returned. After a fault the line is logged,
    /// then `on_fault(fault)` is called (outside the handler, under a guard of its own: a fault in it turns the part
    /// off); its `fault.locks_let_go` says data under a lock may be left half changed
    template<typename function_t, typename handler_t>
    auto run(function_t && f, handler_t && on_fault) noexcept -> bool
      {
      if(broken())
        return false;
      fault_t fault;
      if(run_guarded(f, fault))
        return true;
      fault_seen(fault);
      fault_t again;
      if(not run_guarded([&] { on_fault(static_cast<fault_t const &>(fault)); }, again))
        handler_faulted(again);
      return false;
      }

    ///\brief `f()` under the guard, without a handler of its own
    template<typename function_t>
    auto run(function_t && f) noexcept -> bool
      {
      return run(f, [](fault_t const &) noexcept {});
      }
    };

  ///\brief a thread's body under the guard, started again after a fault, `attempts` times at most (a second apart); a
  /// stack overflow ends it at once. Each fault and the end logged with the thread's `name`. The body's result, or 0
  /// after the last fault. `attempts` below 1 is taken as 1
  auto run_guarded_thread(char const * name, DWORD (*body)(void *), void * arg, int attempts = 8) noexcept -> DWORD;
  }  // namespace edplugin_common
