// The checks of the module guard (guard/guard.h) with core/lock.h's note of held locks: faults of the processor in a
// lambda, under a frame with a destructor and under a noexcept frame, C++ exceptions, two stack overflows one after the
// other (on Windows only: wine ends the thread), the library's locks a fault leaves held let go (also through a call
// that may throw, let go out of order, and under a nested guard), a lock past the note, the codes that are no fault,
// guard_t's limit and handler, a fault in another module, a thread started again, the line in the log and a batch
// ended by a fault and by a C++ exception. The faults touch a page that cannot be read (not nullptr: UBSan's null check would end the
// test). The log's own write faulting is checked last (guard_log_break_tests): the log is off after it. And a measure
// of the guard's cost on the CPU.
#include <edplugin_common/core/lock.h>
#include <edplugin_common/file/file.h>
#include <edplugin_common/guard/guard.h>
#include <edplugin_common/log/log.h>

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <utility>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace std::string_view_literals;

namespace
  {
  using check_t = void (*)(bool ok, char const * what);
  check_t check{};
  std::uint8_t * closed{};  ///< a page that cannot be read or written

  [[gnu::noinline]]
  auto touch(void * p) -> void
    {
    *static_cast<int volatile *>(p) = 1;
    }

  int volatile endless{1};

  SRWLOCK inner_lock = SRWLOCK_INIT;

  ///\brief a lock taken, then a fault; called through a pointer the compiler cannot see through, so the call to it may
  /// throw and the caller's destructors are in the unwind
  auto lock_and_fault() -> void
    {
    edplugin_common::exclusive_lock_t const held{inner_lock};
    touch(closed);
    }

  void (*volatile lock_and_fault_pointer)() {&lock_and_fault};

  ///\brief whether `l` can be taken now (let go again at once)
  auto is_free(SRWLOCK & l) -> bool
    {
    if(TryAcquireSRWLockExclusive(&l) == 0)
      return false;
    ReleaseSRWLockExclusive(&l);
    return true;
    }

  ///\brief a recursion deep enough to use the stack up, in small frames: the guard page is met while there is still
  /// room below it to raise the exception
  [[gnu::noinline]]
  auto deep(int n) -> int
    {
    char volatile room[64];
    room[0] = static_cast<char>(n);
    return endless ? deep(n + 1) + room[0] : room[0];
    }

  auto raise_thread_name(void *) noexcept -> void { RaiseException(0x406D1388, 0, 0, nullptr); }

  ///\brief whether a thread's name raised under the guard reaches a handler outside it, which goes on with the
  /// execution as a debugger does (no unwinding through the guard's noexcept frames); no object with a destructor here
  [[gnu::noinline]]
  auto passes_guard() -> bool
    {
    edplugin_common::fault_t f;
    bool seen{};
    bool returned{};
    __try
      {
      returned = edplugin_common::run_guarded_call(&raise_thread_name, nullptr, f);
      }
    __except(seen = GetExceptionCode() == 0x406D1388, seen ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH)
      {
      }
    return seen and returned;
    }

  auto read_log(std::wstring const & path) -> std::string
    {
    return edplugin_common::read_text(path).value_or(std::string{});
    }

  ///\brief two stack overflows one after the other, then one under a guard_t, on a thread of its own that asks for
  /// room to raise the exception in (SetThreadStackGuarantee)
  auto overflows() -> void
    {
    // wine (Proton 11) ends a thread whose stack overflows instead of raising the exception (its log: "stack overflow
    // 3456 bytes"): checked on Windows only
    if(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version"))
      {
      std::printf("skip: run_guarded's stack overflows (wine ends the thread)\n");
      return;
      }
    struct result_t
      {
      edplugin_common::fault_t first, second;
      bool first_caught, second_caught, part_off;
      };

    static result_t r{};
    HANDLE const thread{CreateThread(
      nullptr, 256 * 1024,
      [](void *) -> DWORD
      {
        ULONG room{64 * 1024};
        SetThreadStackGuarantee(&room);
        r.first_caught = not edplugin_common::run_guarded([] { (void)deep(0); }, r.first);
        r.second_caught = not edplugin_common::run_guarded([] { (void)deep(0); }, r.second);
        static edplugin_common::guard_t part{"test overflow", 0};
        (void)part.run([] { (void)deep(0); });
        r.part_off = part.broken();
        return 0;
      },
      nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr)};
    if(not thread)
      {
      check(false, "run_guarded: a thread for the stack overflows");
      return;
      }
    DWORD const waited{WaitForSingleObject(thread, 60000)};
    DWORD code{};
    GetExitCodeThread(thread, &code);
    CloseHandle(thread);
    check(waited == WAIT_OBJECT_0 and code == 0 and r.first_caught and r.first.code == EXCEPTION_STACK_OVERFLOW and not r.first.stack_not_reset,
          "run_guarded: a stack overflow - caught, the guard page set again");
    check(r.second_caught and r.second.code == EXCEPTION_STACK_OVERFLOW, "run_guarded: a second stack overflow - caught");
    check(r.part_off, "guard_t: a stack overflow turns the part off at once");
    }

  auto faults() -> void
    {
    using edplugin_common::fault_t;
    using edplugin_common::run_guarded;
    fault_t f{};
    check(run_guarded([] {}, f), "run_guarded: a body that returns - true");
    check(not run_guarded([] { touch(closed); }, f) and f.code == EXCEPTION_ACCESS_VIOLATION and f.access == 1
            and f.accessed == closed and not f.cpp,
          "run_guarded: a fault in the lambda - false, an access violation writing the page");
    check(f.module == GetModuleHandleW(nullptr), "fault_t: the module holding the fault's place, the test's exe");
    char text[320];
    std::string_view const line{edplugin_common::fault_line(f, text)};
    check(line.starts_with("an access violation writing 0x") and line.find("edplugin_common_test.exe+0x") != std::string_view::npos,
          "fault_line: what and where");

    struct counted_t
      {
      int * n;

      ~counted_t() { ++*n; }
      };

    int destroyed{};
    check(not run_guarded(
            [&]
            {
              counted_t const c{&destroyed};
              touch(closed);
            },
            f)
            and destroyed <= 1,
          "run_guarded: a fault under a frame with a destructor - caught (the destructor run or not, as the call was made)");
    auto const quiet{[]() noexcept { touch(closed); }};
    check(not run_guarded(quiet, f) and f.code == EXCEPTION_ACCESS_VIOLATION, "run_guarded: a fault under a noexcept frame - caught");

    check(not run_guarded([] { throw std::runtime_error{"thrown here"}; }, f) and f.cpp and f.code == edplugin_common::cpp_exception_code
            and f.what == "thrown here"sv,
          "run_guarded: a C++ exception - false, its what()");
    check(not run_guarded([] { throw 5; }, f) and f.cpp and f.what == "an exception not derived from std::exception"sv,
          "run_guarded: a C++ exception of another type");

    overflows();

    // codes that are no fault go past the guard to the handler outside it
    check(passes_guard(), "run_guarded: a thread's name (0x406D1388) goes past the guard, the body goes on");
    check(edplugin_common::fault_action(0x40010006) == EXCEPTION_CONTINUE_SEARCH
            and edplugin_common::fault_action(0x4001000A) == EXCEPTION_CONTINUE_SEARCH,
          "fault_action: DBG_PRINTEXCEPTION_C and _WIDE_C passed on");
    check(IsDebuggerPresent() or edplugin_common::fault_action(EXCEPTION_BREAKPOINT) == EXCEPTION_EXECUTE_HANDLER,
          "fault_action: a breakpoint without a debugger is a fault");
#if defined(EDPLUGIN_COMMON_UBSAN_TRAP)
    check(edplugin_common::fault_action(EXCEPTION_ILLEGAL_INSTRUCTION) == EXCEPTION_CONTINUE_SEARCH,
          "fault_action: UBSan's trap passed on in a UBSan build");
#else
    check(edplugin_common::fault_action(EXCEPTION_ILLEGAL_INSTRUCTION) == EXCEPTION_EXECUTE_HANDLER,
          "fault_action: an illegal instruction is a fault");
#endif
    }

  auto locks() -> void
    {
    using namespace edplugin_common;
    fault_t f{};
    SRWLOCK a = SRWLOCK_INIT, b = SRWLOCK_INIT, outside = SRWLOCK_INIT;
    shared_lock_t const kept{outside};  // held before the guard: not the guard's to let go
    check(detail::held_locks.depth == 1, "lock: a lock held is noted");
    check(not run_guarded(
            [&]
            {
              exclusive_lock_t const x{a};
              shared_lock_t const s{b};
              touch(closed);
            },
            f)
            and f.locks_let_go == 2 and not f.locks_untracked,
          "run_guarded: the two locks the body held let go");
    bool const free_a{TryAcquireSRWLockExclusive(&a) != 0};
    bool const free_b{TryAcquireSRWLockExclusive(&b) != 0};
    if(free_a)
      ReleaseSRWLockExclusive(&a);
    if(free_b)
      ReleaseSRWLockExclusive(&b);
    check(free_a and free_b and detail::held_locks.depth == 1, "run_guarded: both free after the fault, the note back to the lock held before");
    check(not TryAcquireSRWLockExclusive(&outside), "run_guarded: the lock held before the guard still held");

    SRWLOCK many[detail::held_room + 1];
    for(SRWLOCK & l: many)
      InitializeSRWLock(&l);
    check(not run_guarded(
            [&]
            {
              // the room is full with the one held before: the last two past it
              exclusive_lock_t const l0{many[0]}, l1{many[1]}, l2{many[2]}, l3{many[3]}, l4{many[4]}, l5{many[5]}, l6{many[6]},
                l7{many[7]}, l8{many[8]};
              touch(closed);
            },
            f)
            and f.locks_untracked and f.locks_let_go == detail::held_room - 1,
          "run_guarded: locks past the note - not let go, said; the noted ones let go");
    check(detail::held_locks.depth == 1 and detail::held_locks.untracked == 0, "run_guarded: the note back as it was");
    // the two past the note stay held: let go here by hand
    ReleaseSRWLockExclusive(&many[7]);
    ReleaseSRWLockExclusive(&many[8]);
    bool all_free{true};
    for(SRWLOCK & l: many)
      {
      bool const free{TryAcquireSRWLockExclusive(&l) != 0};
      all_free = all_free and free;
      if(free)
        ReleaseSRWLockExclusive(&l);
      }
    check(all_free, "run_guarded: every lock free again");
    guard_t g{"test locks", 0};
    (void)g.run(
      [&]
      {
        exclusive_lock_t const l0{many[0]}, l1{many[1]}, l2{many[2]}, l3{many[3]}, l4{many[4]}, l5{many[5]}, l6{many[6]}, l7{many[7]},
          l8{many[8]};
        touch(closed);
      });
    check(g.broken(), "guard_t: a fault with a lock past the note turns the part off at once");
    ReleaseSRWLockExclusive(&many[7]);
    ReleaseSRWLockExclusive(&many[8]);

    // a fault through a call that may throw: the outer frame's destructor may run in the unwind, the inner's not
    check(not run_guarded(
            [&]
            {
              exclusive_lock_t const outer{a};
              lock_and_fault_pointer();
            },
            f),
          "run_guarded: a fault through a call by pointer - caught");
    check(is_free(a) and is_free(inner_lock) and detail::held_locks.depth == 1 and detail::held_locks.untracked == 0,
          "run_guarded: through a call by pointer - both locks free, the note as before");

    // let go out of order: the note takes off the lock let go, not the latest
    check(not run_guarded(
            [&]
            {
              std::optional<exclusive_lock_t> first{std::in_place, a};
              exclusive_lock_t const second{b};
              first.reset();
              touch(closed);
            },
            f)
            and f.locks_let_go == 1,
          "run_guarded: a lock let go out of order, then a fault - the other one let go");
    check(is_free(a) and is_free(b) and detail::held_locks.depth == 1, "run_guarded: out of order - both free, the note as before");

    // nested: the inner fault lets go only of what the inner body took
    bool outer_held_inside{};
    check(run_guarded(
            [&]
            {
              exclusive_lock_t const outer{a};
              fault_t inner{};
              bool const caught{not run_guarded(
                [&]
                {
                  exclusive_lock_t const in{b};
                  touch(closed);
                },
                inner)};
              outer_held_inside = caught and inner.locks_let_go == 1 and not is_free(a) and is_free(b);
            },
            f),
          "run_guarded: nested, the outer body returns");
    check(outer_held_inside and is_free(a) and detail::held_locks.depth == 1,
          "run_guarded: nested - the inner fault lets go of the inner lock only");
    }

  auto parts(std::wstring const & dir) -> void
    {
    using namespace edplugin_common;
    std::wstring const path{dir + L"\\guard.log"};
    DeleteFileW(path.c_str());
    log_open(dir, L"guard");

    static guard_t limited{"test part", 2};
    int handled{};
    int ran{};
    auto const body{[&]
                    {
                      ++ran;
                      touch(closed);
                    }};
    auto const handler{[&](fault_t const & f) noexcept { handled += f.code == EXCEPTION_ACCESS_VIOLATION ? 1 : 0; }};
    check(not limited.run(body, handler) and not limited.broken() and handled == 1, "guard_t: a fault - false, the handler called");
    check(not limited.run(body, handler) and limited.broken() and handled == 2, "guard_t: the limit reached - off");
    check(not limited.run(body, handler) and ran == 2 and limited.faults() == 2, "guard_t: off - the body not run");

    guard_t endless_part{"test endless", 0};
    for(int i{}; i != 20; ++i)
      (void)endless_part.run([] { touch(closed); });
    check(not endless_part.broken() and endless_part.faults() == 20, "guard_t: no limit - on after 20 faults");

    guard_t handled_badly{"test handler", 0};
    (void)handled_badly.run([] { touch(closed); }, [](fault_t const &) noexcept { touch(closed); });
    check(handled_badly.broken(), "guard_t: a fault in the handler turns the part off");

    // a fault in another module (ntdll's RtlFillMemory on the closed page)
    using fill_t = void(WINAPI *)(void *, SIZE_T, BYTE);
    auto const fill{reinterpret_cast<fill_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlFillMemory"))};
    guard_t elsewhere{"test elsewhere", 0};
    fault_t seen{};
    (void)elsewhere.run([&] { fill(closed, 16, 1); }, [&](fault_t const & f) noexcept { seen = f; });
    check(fill and seen.module != nullptr and seen.module != GetModuleHandleW(nullptr) and elsewhere.broken(),
          "guard_t: a fault in another module turns the part off at once");

    // a fault inside a batch: the batch written at the fault, before the guard's line
    log_batch_begin();
    log_line("guard test: a line of the batch");
    guard_t batched{"test batch", 0};
    (void)batched.run([] { touch(closed); });
    log_line("guard test: after the batch");

    // a C++ exception inside a batch: the batch ended as after a fault
    log_batch_begin();
    log_line("guard test: a line before the exception");
    fault_t thrown{};
    (void)run_guarded([] { throw std::runtime_error{"in a batch"}; }, thrown);
    check(read_log(path).find("guard test: a line before the exception") != std::string::npos,
          "run_guarded: a C++ exception ends the thread's batch");

    static int attempt{};
    DWORD const second{run_guarded_thread(
      "test thread",
      [](void *) -> DWORD
      {
        if(++attempt == 1)
          touch(closed);
        return 7;
      },
      nullptr, 2)};
    check(second == 7 and attempt == 2, "run_guarded_thread: started again after a fault, its result");
    DWORD const none{run_guarded_thread(
      "test thread always", [](void *) -> DWORD { throw std::runtime_error{"always"}; }, nullptr, 1)};
    check(none == 0, "run_guarded_thread: stopped after the faults - 0");
    static int zero_runs{};
    DWORD const once{run_guarded_thread(
      "test thread zero",
      [](void *) -> DWORD
      {
        ++zero_runs;
        return 3;
      },
      nullptr, 0)};
    check(once == 3 and zero_runs == 1, "run_guarded_thread: attempts 0 taken as 1");

    log_close();
    std::string const text{read_log(path)};
    check(text.find("test part: an access violation writing 0x") != std::string::npos and text.find("(1 of 2)") != std::string::npos,
          "log: the guard's line, its place and count");
    check(text.find("test part: off for this session (the limit of faults, 2 fault(s))") != std::string::npos, "log: the part off, once");
    check(text.find("test endless:") != std::string::npos and text.find("(16)") != std::string::npos and text.find("(17)") == std::string::npos,
          "log: the first 16 faults of a part told, no more");
    check(text.find("test handler: its fault handler faulted") != std::string::npos, "log: a handler's fault");
    check(text.find("test elsewhere: off for this session (a fault in another module") != std::string::npos,
          "log: the part off after a fault in another module");
    std::size_t const in_batch{text.find("guard test: a line of the batch")};
    std::size_t const batch_fault{text.find("test batch:")};
    check(in_batch != std::string::npos and batch_fault != std::string::npos and in_batch < batch_fault
            and text.find("guard test: after the batch") > batch_fault,
          "log: the batch written at the fault, before the guard's line");
    check(text.find("thread test thread: a C++ exception") == std::string::npos
            and text.find("thread test thread: an access violation") != std::string::npos
            and text.find("thread test thread always: a C++ exception (always) (1 of 1)") != std::string::npos
            and text.find("thread test thread always: stopped after 1 fault(s)") != std::string::npos,
          "log: the thread's faults and its end");
    }

  ///\brief the guard's cost on the CPU: an empty body under run_guarded, and a lock taken with its note
  auto bench() -> void
    {
    using namespace edplugin_common;
    constexpr int calls{1'000'000};
    int volatile sink{};
    fault_t f{};
    auto const t0{std::chrono::steady_clock::now()};
    for(int i{}; i != calls; ++i)
      (void)run_guarded([&] { sink = sink + 1; }, f);
    auto const t1{std::chrono::steady_clock::now()};
    SRWLOCK l = SRWLOCK_INIT;
    for(int i{}; i != calls; ++i)
      {
      exclusive_lock_t const hold{l};
      sink = sink + 1;
      }
    auto const t2{std::chrono::steady_clock::now()};
    for(int i{}; i != calls; ++i)
      {
      AcquireSRWLockExclusive(&l);
      sink = sink + 1;
      ReleaseSRWLockExclusive(&l);
      }
    auto const t3{std::chrono::steady_clock::now()};
    auto const ns{[](auto d) { return std::chrono::duration<double, std::nano>(d).count() / calls; }};
    std::printf("bench: run_guarded of an empty body %.1f ns; exclusive_lock_t %.1f ns, a raw SRW lock %.1f ns (a call each)\n",
                ns(t1 - t0), ns(t2 - t1), ns(t3 - t2));
    }

  ///\brief a write of the log that faults
  auto faulting_write(void *, void const *, unsigned long, unsigned long *, void *) -> int
    {
    touch(closed);
    return 1;
    }
  }  // namespace

auto guard_tests(check_t checker, std::wstring const & dir) -> void
  {
  check = checker;
  closed = static_cast<std::uint8_t *>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS));
  if(not closed)
    {
    check(false, "guard: a page that cannot be read");
    return;
    }
  faults();
  locks();
  parts(dir);
  bench();
  }

auto guard_log_break_tests(check_t checker, std::wstring const & dir) -> void
  {
  using namespace edplugin_common;
  check = checker;
  if(not closed)
    return;
  log_open(dir, L"guard-break");
  detail::log_write_t const before{detail::log_write_function(&faulting_write)};
  guard_t part{"test log", 0};
  (void)part.run([] { touch(closed); });
  check(log_is_broken(), "log: a fault in the log's own write - the log off, the process goes on");
  log_line("guard test: not written");
  check(log_is_broken(), "log: still off");
  (void)detail::log_write_function(before);
  log_close();
  }
