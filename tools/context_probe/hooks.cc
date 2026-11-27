// context_probe - the plugins' context hooks as the library puts them (hooks.h).
#include "hooks.h"

#include "follow.h"
#include "slots.h"

#include <edplugin_common/chain/system_d3d11.h>
#include <edplugin_common/hook/context.h>
#include <edplugin_common/hook/contract.h>
#include <edplugin_common/hook/multithread.h>
#include <edplugin_common/hook/watch.h>

#include <d3d11_4.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace
  {
  namespace chain = edplugin_common::chain;
  namespace hook = edplugin_common::hook;

  /// the slots each plugin hooks, in the order of its list
  inline constexpr std::array<std::size_t, 4> hooked{13, 24, 110, 111};
  static_assert(context_probe::method_name(context_probe::context_methods, 13) == "Draw");
  static_assert(context_probe::method_name(context_probe::context_methods, 24) == "IASetPrimitiveTopology");
  static_assert(context_probe::method_name(context_probe::context_methods, 110) == "ClearState");
  static_assert(context_probe::method_name(context_probe::context_methods, 111) == "Flush");

  /// the entries compared with the runtime's table as the context came with it
  inline constexpr int runtime_compared{static_cast<int>(context_probe::context_methods.size())};

  struct plugin_t
    {
    char const * name{};
    hook::context_ask_e ask{hook::context_ask_e::automatic};
    hook::context_hooks_t hooks;
    hook::context_watch_t watch;
    std::array<hook::slot_patch_t, hooked.size()> list{};
    bool watched{};
    // calls of each hook since the last round: the plugin worked on the context, or passed the call on
    std::array<std::atomic<std::uint32_t>, hooked.size()> worked{};
    std::array<std::atomic<std::uint32_t>, hooked.size()> passed{};
    };
  }  // namespace

struct context_probe::hooks_run_t
  {
  ID3D11DeviceContext * context{};
  void ** runtime{};      ///< the table the context came with
  void ** a_table{};      ///< the table the object pointed at after A's attach
  void ** top_table{};    ///< after C's
  std::vector<void *> runtime_at_start;
  std::array<plugin_t, 3> plugins;
  hook::multithread_t multithread;
  hook::multithread_start_e multithread_start{hook::multithread_start_e::no_object};
  };

namespace
  {
  using context_probe::hooks_run_t;
  using context_probe::sink_t;

  /// the runs, one for each device's context, in the order made: a hook takes the run of the context it is called for
  /// (the hooks' functions are the same for every run)
  inline constexpr std::size_t runs_most{32};
  std::array<std::atomic<hooks_run_t *>, runs_most> runs{};
  std::atomic<std::size_t> runs_made{};
  /// the last run, for a call of a context that is none of the runs'
  std::atomic<hooks_run_t *> current{};
  thread_local int depth{};
  std::atomic<bool> recursed{};

  [[gnu::format(printf, 2, 3)]]
  auto say(sink_t sink, char const * format, ...) -> void
    {
    char line[1024]{};
    va_list args;
    va_start(args, format);
    int const n{std::vsnprintf(line, sizeof line, format, args)};
    va_end(args);
    if(n >= int(sizeof line))
      std::memcpy(line + sizeof line - 7, " [cut]", 7);  // never silently shorter
    sink(line);
    }

  ///\brief a hook's depth in its thread: deeper than 8 is a hook that called itself again, its call not passed on
  struct depth_t
    {
    bool deep;

    depth_t() noexcept : deep{++depth > 8}
      {
      if(deep)
        recursed.store(true, std::memory_order_relaxed);
      }

    ~depth_t()
      {
      --depth;
      }

    depth_t(depth_t const &) = delete;
    auto operator=(depth_t const &) -> depth_t & = delete;
    };

  ///\brief plugin `p` of the run, and the call of the hook at `at` of its list counted; the hook's original, nullptr
  /// when it has none or the hook is too deep
  ///\brief the run of the context `self`; the last run when it is none of the runs'
  [[nodiscard]]
  auto run_of(void const * self) noexcept -> hooks_run_t &
    {
    std::size_t const made{std::min(runs_made.load(std::memory_order_acquire), runs_most)};
    for(std::size_t i{}; i != made; ++i)
      if(hooks_run_t * const r{runs[i].load(std::memory_order_acquire)}; r and r->context == self)
        return *r;
    return *current.load(std::memory_order_acquire);
    }

  template<int p>
  [[nodiscard]]
  auto note(std::size_t at, void const * self, depth_t const & d) noexcept -> void *
    {
    plugin_t & pl{run_of(self).plugins[p]};
    (pl.hooks.works(self) ? pl.worked : pl.passed)[at].fetch_add(1, std::memory_order_relaxed);
    if(pl.watched)
      pl.watch.called(pl.watch.index_of(hooked[at]));
    return d.deep ? nullptr : pl.list[at].original;
    }

  // the hooks, each of a method's own type; the originals are stubs or other plugins' hooks (no type check on them)
  template<int p>
  [[clang::no_sanitize("function")]]
  auto STDMETHODCALLTYPE draw(ID3D11DeviceContext * self, UINT count, UINT start) -> void
    {
    depth_t const d;
    using fn_t = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, UINT);
    if(auto const original{reinterpret_cast<fn_t>(note<p>(0, self, d))})
      original(self, count, start);
    }

  template<int p>
  [[clang::no_sanitize("function")]]
  auto STDMETHODCALLTYPE topology(ID3D11DeviceContext * self, D3D11_PRIMITIVE_TOPOLOGY t) -> void
    {
    depth_t const d;
    using fn_t = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, D3D11_PRIMITIVE_TOPOLOGY);
    if(auto const original{reinterpret_cast<fn_t>(note<p>(1, self, d))})
      original(self, t);
    }

  template<int p>
  [[clang::no_sanitize("function")]]
  auto STDMETHODCALLTYPE clear_state(ID3D11DeviceContext * self) -> void
    {
    depth_t const d;
    using fn_t = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *);
    if(auto const original{reinterpret_cast<fn_t>(note<p>(2, self, d))})
      original(self);
    }

  template<int p>
  [[clang::no_sanitize("function")]]
  auto STDMETHODCALLTYPE flush(ID3D11DeviceContext * self) -> void
    {
    depth_t const d;
    using fn_t = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *);
    if(auto const original{reinterpret_cast<fn_t>(note<p>(3, self, d))})
      original(self);
    }

  template<int p>
  [[nodiscard]]
  auto hooks_of() noexcept -> std::array<void *, hooked.size()>
    {
    return {reinterpret_cast<void *>(&draw<p>), reinterpret_cast<void *>(&topology<p>),
            reinterpret_cast<void *>(&clear_state<p>), reinterpret_cast<void *>(&flush<p>)};
    }

  [[nodiscard]]
  constexpr auto attach_name(hook::context_attach_e r) noexcept -> char const *
    {
    switch(r)
      {
      case hook::context_attach_e::attached: return "attached";
      case hook::context_attach_e::already: return "already";
      case hook::context_attach_e::not_read: return "NOT READ";
      case hook::context_attach_e::slot_out: return "A SLOT PAST THE TABLE";
      case hook::context_attach_e::slots_failed: return "SLOTS FAILED";
      case hook::context_attach_e::refused: return "REFUSED";
      }
    return "?";
    }

  [[nodiscard]]
  constexpr auto make_name(hook::own_table_make_e r) noexcept -> char const *
    {
    switch(r)
      {
      case hook::own_table_make_e::made: return "made";
      case hook::own_table_make_e::bad_count: return "BAD COUNT";
      case hook::own_table_make_e::entries_memory: return "NO MEMORY FOR THE ENTRIES";
      case hook::own_table_make_e::stubs_memory: return "NO MEMORY FOR THE STUBS";
      case hook::own_table_make_e::stubs_protect: return "THE STUBS NOT MADE EXECUTABLE";
      }
    return "?";
    }

  [[nodiscard]]
  constexpr auto patch_name(hook::slots_patch_e r) noexcept -> char const *
    {
    switch(r)
      {
      case hook::slots_patch_e::done: return "done";
      case hook::slots_patch_e::too_many_pages: return "TOO MANY PAGES";
      case hook::slots_patch_e::page_unusable: return "A PAGE UNUSABLE";
      case hook::slots_patch_e::page_protect: return "A PAGE NOT MADE WRITABLE";
      }
    return "?";
    }

  [[nodiscard]]
  constexpr auto slot_name(hook::slot_result_e r) noexcept -> char const *
    {
    switch(r)
      {
      case hook::slot_result_e::not_tried: return "not tried";
      case hook::slot_result_e::patched: return "patched";
      case hook::slot_result_e::already_ours: return "already ours";
      case hook::slot_result_e::not_writable: return "NOT WRITABLE";
      case hook::slot_result_e::read_back_differs: return "READ BACK AS ANOTHER";
      }
    return "?";
    }

  [[nodiscard]]
  constexpr auto watch_name(hook::watch_start_e r) noexcept -> char const *
    {
    switch(r)
      {
      case hook::watch_start_e::started: return "started";
      case hook::watch_start_e::no_object: return "NO OBJECT";
      case hook::watch_start_e::too_many_slots: return "TOO MANY SLOTS";
      case hook::watch_start_e::no_slots: return "NO SLOTS";
      }
    return "?";
    }

  [[nodiscard]]
  constexpr auto multithread_name(hook::multithread_start_e r) noexcept -> char const *
    {
    switch(r)
      {
      case hook::multithread_start_e::off: return "off";
      case hook::multithread_start_e::on: return "ON";
      case hook::multithread_start_e::no_interface: return "no ID3D11Multithread (taken as off)";
      case hook::multithread_start_e::no_object: return "NOT STARTED";
      }
    return "?";
    }

  [[nodiscard]]
  auto method(std::size_t slot) -> std::string
    {
    return std::string{context_probe::method_name(context_probe::context_methods, int(slot))};
    }

  ///\brief what the library reads of the table the object points at now, as plugin `pl` sees it before its attach
  auto tell_reading(sink_t sink, plugin_t const & pl, hook::context_reading_t const & r) -> void
    {
    if(not r.read)
      {
      say(sink, "  plugin %s: the table not read", pl.name);
      return;
      }
    hook::context_input_t const & in{r.input};
    hook::context_pick_t const pick{hook::context_pick(in)};
    std::size_t const least{in.qi_prefix < hook::context_least_length.size()
                              ? std::size_t{hook::context_least_length[in.qi_prefix]}
                              : 0};
    say(sink,
        "  plugin %s reads: table %lld B from the object, memory %s, module %s (%s), section %s; of the first %u "
        "entries %u in the system d3d11, %u in other modules, %u in none; answers ID3D11DeviceContext1..%u as itself, "
        "so at least %zu entries; %u entries%s; the rule: %s (%s)",
        pl.name, static_cast<long long>(r.table_from_object), hook::memory_name(in.memory),
        context_probe::module_name(r.module).c_str(), hook::module_name(in.module), r.section,
        hook::context_first_entries, in.system_entries, in.other_entries, in.no_module_entries, in.qi_prefix, least,
        in.length, in.length >= hook::context_table_most ? " (the most read)" : "", hook::way_name(pick.way),
        hook::point_name(pick.point));
    if(r.mark)
      say(sink, "  plugin %s reads: the mark of another copy of the library (contract %u, %llu entries)", pl.name,
          r.mark->contract, static_cast<unsigned long long>(r.mark->count));
    else
      say(sink, "  plugin %s reads: no mark (%u allocations looked at, the last: %s)", pl.name, r.mark_candidates,
          hook::check_name(r.mark_last));
    }

  auto tell_attach(sink_t sink, plugin_t const & pl, hook::context_attached_t const & a) -> void
    {
    std::string made{"-"};
    if(a.made)
      {
      char text[160]{};
      std::snprintf(text, sizeof text, "%s (error %lu, the words before entry 0 %s)", make_name(a.made->result),
                    static_cast<unsigned long>(a.made->error), a.made->header_copied ? "copied" : "NOT copied");
      made = text;
      }
    say(sink, "  plugin %s attach: %s, way %s (rule %s, %s); table of its own: %s", pl.name, attach_name(a.result),
        hook::way_name(a.way), hook::way_name(a.pick.way), hook::point_name(a.pick.point), made.c_str());
    if(a.way == hook::context_way_e::in_place)
      say(sink,
          "  plugin %s in place: %s, %u written, %u already ours, %u read back as another; %u pages made writable, %u "
          "not given back (error %lu)",
          pl.name, patch_name(a.slots.result), a.slots.patched, a.slots.already_ours, a.slots.read_back_differs,
          a.slots.pages_changed, a.slots.restore_failed, static_cast<unsigned long>(a.slots.error));
    for(hook::slot_patch_t const & s : pl.list)
      say(sink, "    plugin %s slot %zu %s: %s, its original %s", pl.name, s.slot, method(s.slot).c_str(),
          slot_name(s.result), s.original ? context_probe::where(s.original).c_str() : "none");
    }

  ///\brief the plugin `p` of `run` attached to its context with its list, and its watch started
  template<int p>
  auto attach(hooks_run_t & run, sink_t sink) -> void
    {
    plugin_t & pl{run.plugins[p]};
    chain::system_d3d11_t const & system{edplugin_common::chain::system_d3d11()};
    tell_reading(sink, pl, hook::read_context(run.context, system, pl.ask));
    std::array<void *, hooked.size()> const hooks{hooks_of<p>()};
    for(std::size_t i{}; i != hooked.size(); ++i)
      pl.list[i] = hook::slot_patch_t{hooked[i], hooks[i]};
    hook::context_attached_t const a{pl.hooks.attach(run.context, pl.ask, pl.list)};
    tell_attach(sink, pl, a);
    if(not a.attached())
      return;
    hook::watch_start_e const w{pl.watch.start(pl.hooks.watch_setup(pl.list))};
    pl.watched = w == hook::watch_start_e::started;
    say(sink, "  plugin %s watch: %s, %zu slots", pl.name, watch_name(w), pl.watched ? pl.watch.watched() : 0);
    }

  [[nodiscard]]
  auto table_now(hooks_run_t const & run) -> char const *
    {
    void ** const now{*reinterpret_cast<void ***>(run.context)};
    return now == run.top_table ? "C's (the top)"
         : now == run.a_table   ? "A's"
         : now == run.runtime   ? "the RUNTIME'S"
                                : "ANOTHER";
    }

  ///\brief the classes each watch sees, the protection the library tells, the runtime's entries changed since the
  /// start of the step
  auto checks(hooks_run_t & run, char const * step, sink_t sink) -> void
    {
    hook::multithread_state_t const m{run.multithread.refresh()};
    std::vector<void *> const runtime{context_probe::read_entries(run.runtime, runtime_compared)};
    std::vector<bool> const changed{context_probe::slots_differ(run.runtime_at_start, runtime)};
    say(sink,
        "  after %s: the object points at %s table; protection told %s%s; the runtime's table: slots changed since "
        "the attach %s",
        step, table_now(run), m.on ? "ON" : "off", m.turns_off() ? " (the plugins turn off)" : "",
        context_probe::ranges_text(changed).c_str());
    for(plugin_t & pl : run.plugins)
      {
      if(not pl.watched)
        {
        say(sink, "    plugin %s: not watched", pl.name);
        continue;
        }
      hook::watch_state_t const s{pl.watch.check()};
      say(sink,
          "    plugin %s: slots %s (slot %zu), table %s%s; entries of the table below (%s) the stubs followed %zu "
          "of %zu",
          pl.name, hook::class_name(s.slots), s.slot, hook::class_name(s.table), s.turns_off() ? ", TURNS OFF" : "",
          &pl == &run.plugins[2] ? "A's" : "the runtime's", pl.watch.runtime_moved(), pl.watch.watched());
      }
    }

  ///\brief the calls the hooks counted since the last round (on another thread too: the swap chain's, an overlay's)
  auto tell_counts(hooks_run_t const & run, char const * what, sink_t sink) -> void
    {
    say(sink, "  %s (as worked/passed by each plugin's hook; a hook called itself again: %s):", what,
        recursed.load(std::memory_order_relaxed) ? "YES" : "no");
    for(plugin_t const & pl : run.plugins)
      {
      std::string text;
      for(std::size_t i{}; i != hooked.size(); ++i)
        {
        char part[96]{};
        std::snprintf(part, sizeof part, "%s%s %u/%u", i ? ", " : "", method(hooked[i]).c_str(),
                      pl.worked[i].load(std::memory_order_relaxed), pl.passed[i].load(std::memory_order_relaxed));
        text += part;
        }
      say(sink, "    plugin %s: %s", pl.name, text.c_str());
      }
    }

  ///\brief IASetPrimitiveTopology, Draw of no vertices (nothing bound: nothing drawn), ClearState and Flush through
  /// ID3D11DeviceContext4 (the context itself when it has none), each hook's calls told
  auto calls(hooks_run_t & run, char const * step, sink_t sink) -> void
    {
    for(plugin_t & pl : run.plugins)
      for(std::size_t i{}; i != hooked.size(); ++i)
        {
        pl.worked[i].store(0, std::memory_order_relaxed);
        pl.passed[i].store(0, std::memory_order_relaxed);
        }
    recursed.store(false, std::memory_order_relaxed);
    ID3D11DeviceContext4 * c4{};
    if(FAILED(run.context->QueryInterface(__uuidof(ID3D11DeviceContext4), reinterpret_cast<void **>(&c4))))
      c4 = nullptr;
    ID3D11DeviceContext * const through{c4 ? c4 : run.context};
    through->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    through->Draw(0, 0);
    through->ClearState();
    through->Flush();
    char what[160]{};
    std::snprintf(what, sizeof what, "calls %s through %s", step,
                  not c4 ? "the context (no ID3D11DeviceContext4)"
                  : static_cast<void *>(c4) == static_cast<void *>(run.context)
                    ? "ID3D11DeviceContext4 (the same pointer)"
                    : "ID3D11DeviceContext4 (ANOTHER pointer)");
    if(c4)
      c4->Release();
    tell_counts(run, what, sink);
    }

  ///\brief whether a slot of `hooked` in the table the object points at holds a plugin's hook of this program: a
  /// table shared with an earlier device's context (in place there), where another attach would chain the hooks into
  /// themselves
  [[nodiscard]]
  auto holds_our_hooks(ID3D11DeviceContext * context) -> bool
    {
    std::vector<void *> const entries{context_probe::read_entries(*reinterpret_cast<void ***>(context),
                                                                  int(hooked.back()) + 1)};
    std::array<std::array<void *, hooked.size()>, 3> const ours{hooks_of<0>(), hooks_of<1>(), hooks_of<2>()};
    for(std::size_t const slot : hooked)
      for(auto const & plugin : ours)
        if(slot < entries.size() and std::ranges::find(plugin, entries[slot]) != plugin.end())
          return true;
    return false;
    }
  }  // namespace

[[gnu::noinline]]
auto context_probe::hooks_step(ID3D11Device * device, ID3D11DeviceContext * context, sink_t sink) -> hooks_run_t *
  {
  say(sink, "");
  say(sink,
      "the plugins' hooks, the library's code (contract %u): A and B ask for the rule, C for a table of its own; each "
      "hooks Draw, IASetPrimitiveTopology, ClearState and Flush",
      hook::hook_contract_version);
  chain::system_d3d11_t const & system{edplugin_common::chain::system_d3d11()};
  if(not system.module or system.first == system.end)
    say(sink, "  the system's d3d11.dll for the library: %s (error %lu)",
        system.module ? "its image not read" : "NOT LOADED", static_cast<unsigned long>(system.error));
  if(holds_our_hooks(context))
    {
    say(sink, "  the plugins not attached on this device: its context's table holds an earlier device's plugin "
              "hooks (a table shared with that context)");
    return nullptr;
    }
  std::size_t const index{runs_made.load(std::memory_order_acquire)};
  if(index == runs_most)
    {
    say(sink, "  the plugins not attached on this device: %zu devices with them already", runs_most);
    return nullptr;
    }
  auto * const run{new hooks_run_t{}};  // to the end of the process, as the hooks in the context (contract c)
  run->context = context;
  run->runtime = *reinterpret_cast<void ***>(context);
  run->runtime_at_start = read_entries(run->runtime, runtime_compared);
  run->plugins[0].name = "A";
  run->plugins[1].name = "B";
  run->plugins[2].name = "C";
  run->plugins[2].ask = hook::context_ask_e::own_table;
  runs[index].store(run, std::memory_order_release);
  runs_made.store(index + 1, std::memory_order_release);
  current.store(run, std::memory_order_release);
  run->multithread_start = run->multithread.start(context, device);
  say(sink, "  the library's look at the multithread protection: %s", multithread_name(run->multithread_start));

  attach<0>(*run, sink);
  run->a_table = *reinterpret_cast<void ***>(context);
  attach<1>(*run, sink);
  attach<2>(*run, sink);
  run->top_table = *reinterpret_cast<void ***>(context);
  checks(*run, "the attach", sink);
  calls(*run, "after the attach", sink);

  ID3D11Multithread * mt{};
  if(FAILED(context->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void **>(&mt))))
    mt = nullptr;
  if(not mt and FAILED(device->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void **>(&mt))))
    mt = nullptr;
  if(mt)
    {
    mt->SetMultithreadProtected(TRUE);
    checks(*run, "the protection turned on", sink);
    calls(*run, "with the protection on", sink);
    mt->SetMultithreadProtected(FALSE);
    checks(*run, "the protection turned off", sink);
    calls(*run, "with the protection off", sink);
    mt->Release();
    }
  else
    say(sink, "  the protection not turned on: neither the context nor the device gives an ID3D11Multithread");
  context->ClearState();
  context->Flush();
  checks(*run, "ClearState and Flush", sink);
  calls(*run, "after ClearState and Flush", sink);
  return run;
  }

[[gnu::noinline]]
auto context_probe::hooks_again(hooks_run_t * run, char const * step, sink_t sink) -> void
  {
  if(not run)
    return;
  say(sink, "the plugins' hooks %s:", step);
  tell_counts(*run, "calls since the last round (the swap chain's thread, an overlay)", sink);
  checks(*run, step, sink);
  calls(*run, step, sink);
  }

auto context_probe::hooks_runtime_table(hooks_run_t const * run) noexcept -> void **
  {
  return run ? run->runtime : nullptr;
  }

// no object with a destructor in these two: the handler unwinds nothing but the frame (clang-cl catches a fault under
// __try only from a call: hooks_step and hooks_again are not inlined); what the faulting step had made is left as it is
auto context_probe::hooks_step_caught(ID3D11Device * device, ID3D11DeviceContext * context, sink_t sink,
                                      hooks_run_t *& run, DWORD & fault) noexcept -> bool
  {
  __try
    {
    run = hooks_step(device, context, sink);
    }
  __except(fault = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
    {
    depth = 0;
    return false;
    }
  return true;
  }

auto context_probe::hooks_again_caught(hooks_run_t * run, char const * step, sink_t sink, DWORD & fault) noexcept
  -> bool
  {
  __try
    {
    hooks_again(run, step, sink);
    }
  __except(fault = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
    {
    depth = 0;
    return false;
    }
  return true;
  }
