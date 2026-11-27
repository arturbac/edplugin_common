// context_probe - the plugins' context hooks as the library puts them (hook/context.h, hook/watch.h,
// hook/multithread.h), on a real immediate context, as plugins put them at the device's start: plugin A asks for the
// rule (on Windows a table of its own over the runtime's), plugin B too (A's table recognized by its mark, B's hooks in
// it in place) and plugin C asks for a table of its own (over A's, as a plugin with context_hook = 2, or one that does
// not read the mark). Their hooks on Draw, IASetPrimitiveTopology, ClearState and Flush count their calls and call
// their originals. Then calls through ID3D11DeviceContext4, the multithread protection on and off and ClearState with
// Flush, after each the watches' classes, the runtime's entries the stubs followed and the protection the library
// tells. Nothing of it is freed (contract c): the context keeps the tables and the references to the end of the
// process.
#pragma once

#include <windows.h>
#include <d3d11.h>

namespace context_probe
  {
  ///\brief where a line of the result goes
  using sink_t = void (*)(char const * line);

  struct hooks_run_t;

  ///\brief the three plugins attached to `context` of `device`, the calls, the protection and ClearState, each told
  /// through `sink`; the run, kept to the end of the process, or nullptr when the context's table holds an earlier
  /// device's plugin hooks (a shared table: nothing attached)
  [[nodiscard]]
  auto hooks_step(ID3D11Device * device, ID3D11DeviceContext * context, sink_t sink) -> hooks_run_t *;

  ///\brief the calls counted since the last round, the classes and the calls again, after `step` (the swap chain's)
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
