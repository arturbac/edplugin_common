// context_probe - the plugins' context hooks as the library puts them (hook/context.h, hook/watch.h,
// hook/multithread.h), on a real immediate context, as plugins put them at the device's start: plugin A asks for the
// rule (on Windows a table of its own over the runtime's), plugin B too (A's table recognized by its mark, B's hooks in
// it in place) and plugin C asks for a table of its own (over A's, as a plugin with context_hook = 2, or one that does
// not read the mark). Their hooks on Draw, IASetPrimitiveTopology, ClearState and Flush count their calls and call
// their originals. Then calls through ID3D11DeviceContext4, the multithread protection on and off and ClearState with
// Flush, after each the watches' classes, the runtime's entries the stubs followed and the protection the library
// tells. Nothing of it is freed (contract c): the context keeps the tables and the references to the end of the
// process.
// The attach and the report draw nothing and set no state of the context (hooks_attach, hooks_report): no Draw,
// IASetPrimitiveTopology, ClearState or Flush and no change of the multithread protection; the attach puts the hooks
// and the library asks the context for its interfaces (QueryInterface, Release), the report asks the protection
// (GetMultithreadProtected) from its own thread. They are for a program that draws itself, with a plugin of this code
// in its chain; hooks_step and hooks_again add the probe's own calls, the protection on and off and ClearState with
// Flush.
#pragma once

#include <windows.h>
#include <d3d11.h>

namespace context_probe
  {
  ///\brief where a line of the result goes
  using sink_t = void (*)(char const * line);

  struct hooks_run_t;

  ///\brief the three plugins attached to `context` of `device` and the classes their watches see, told through `sink`;
  /// nothing drawn and no state set (the header's note). The run, kept to the end of the process, or nullptr when the
  /// context's table holds an earlier device's plugin hooks (a shared table: nothing attached). After it the watches
  /// are checked only by hooks_report, hooks_again or hooks_step, one thread at a time (hook/watch.h): a thread of
  /// reports gets the run after hooks_attach returned, by its start or through an atomic with release and acquire
  [[nodiscard]]
  auto hooks_attach(ID3D11Device * device, ID3D11DeviceContext * context, sink_t sink) -> hooks_run_t *;

  ///\brief at `step`: the calls the hooks counted since the last report (each count taken and set to 0 at once), the
  /// protection the library tells, the runtime's entries changed since the attach and the classes the watches see;
  /// nothing drawn and no state set
  auto hooks_report(hooks_run_t * run, char const * step, sink_t sink) -> void;

  ///\brief hooks_attach under a fault handler, as hooks_step_caught
  [[nodiscard]]
  auto hooks_attach_caught(ID3D11Device * device, ID3D11DeviceContext * context, sink_t sink, hooks_run_t *& run,
                           DWORD & fault) noexcept -> bool;

  ///\brief hooks_report under a fault handler, as hooks_step_caught
  [[nodiscard]]
  auto hooks_report_caught(hooks_run_t * run, char const * step, sink_t sink, DWORD & fault) noexcept -> bool;

  ///\brief the three plugins attached to `context` of `device`, the calls, the protection and ClearState, each told
  /// through `sink`; the run, kept to the end of the process, or nullptr when the context's table holds an earlier
  /// device's plugin hooks (a shared table: nothing attached)
  [[nodiscard]]
  auto hooks_step(ID3D11Device * device, ID3D11DeviceContext * context, sink_t sink) -> hooks_run_t *;

  ///\brief the calls counted since the last round, the classes and the calls again, after `step` ("the swap chain")
  auto hooks_again(hooks_run_t * run, char const * step, sink_t sink) -> void;

  ///\brief hooks_step under a fault handler: false, with the fault's code, when a fault (in a stub, a hook or the
  /// runtime under them) ended it; the result file is still written
  [[nodiscard]]
  auto hooks_step_caught(ID3D11Device * device, ID3D11DeviceContext * context, sink_t sink, hooks_run_t *& run,
                         DWORD & fault) noexcept -> bool;

  ///\brief hooks_again under a fault handler, as hooks_step_caught
  [[nodiscard]]
  auto hooks_again_caught(hooks_run_t * run, char const * step, sink_t sink, DWORD & fault) noexcept -> bool;

  ///\brief the table the context came with, before the plugins (the one the stubs of A's table jump through)
  [[nodiscard]]
  auto hooks_runtime_table(hooks_run_t const * run) noexcept -> void **;
  }  // namespace context_probe
