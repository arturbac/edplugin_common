// The checks of hook/context.h, hook/watch.h and hook/multithread.h under wine, without d3d11: a context object of the
// test's own whose method table lies on the heap (as Windows' d3d11 keeps it after the object), its entries functions
// of the test that count their calls (the "runtime"), and two plugins whose hooks count theirs and call their originals.
// Checked: the attach on a table of our own (again: already, one reference) and the runtime rewriting every entry
// afterwards (the calls follow it); a second copy of the library recognizing that table by its mark and hooking in it
// in place (the runtime's entry told by the mark's table); a plugin above pointing the object at a table of its own over
// ours (no recursion, no turning off; an entry of its table refreshed from the runtime's: re-pointed); on a table of our
// own, our stub or the runtime's entry back in it (taken, taken by the runtime); a slot of the list past a short table
// (nothing written); a list that cannot be written (a read-only view of a section: nothing written); in place, the
// runtime writing its entry back over a hook (taken, the plugin's way out), an entry into the system's d3d11 (taken by
// the runtime) and a hook over ours that calls no further (covered, its slot quiet in the census); the object pointed
// back at the runtime's table (re-pointed); the multithread protection turned on; a second device's context on the
// shared table (its calls pass); one try per plugin (a second attach with another context, a fresh list or another
// table, and the same context after a try that failed: refused, nothing changed).
#include <edplugin_common/chain/system_d3d11.h>
#include <edplugin_common/file/file.h>
#include <edplugin_common/hook/context.h>
#include <edplugin_common/hook/multithread.h>
#include <edplugin_common/hook/watch.h>
#include <edplugin_common/log/log.h>

#include <windows.h>
#include <d3d11_4.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

using namespace std::string_view_literals;

namespace
  {
  using check_t = void (*)(bool ok, char const * what);
  check_t check{};

  namespace hook = edplugin_common::hook;

  using call_t = void (*)(void * self);

  ///\brief entries of the test's runtime table: as long as the least table of a context answering only as
  /// ID3D11DeviceContext, and some more
  inline constexpr std::size_t entries{150};
  ///\brief the slots the plugins hook (DrawIndexed, Draw, DrawIndexedInstanced) and one they do not
  inline constexpr std::array<std::size_t, 3> hooked{12, 13, 20};
  inline constexpr std::size_t not_hooked{30};

  ///\brief calls of the runtime's entries: [0] those the table came with, [1] those it is rewritten with
  std::array<std::array<int, entries>, 2> runtime_calls{};
  std::array<int, entries> cover_calls{};

  template<int variant, std::size_t slot>
  auto runtime_fn(void *) -> void
    {
    ++runtime_calls[variant][slot];
    }

  ///\brief another hook over ours that calls no further
  template<std::size_t slot>
  auto cover_fn(void *) -> void
    {
    ++cover_calls[slot];
    }

  template<int variant, std::size_t... slot>
  constexpr auto runtime_entries(std::index_sequence<slot...>) noexcept -> std::array<call_t, entries>
    {
    return {&runtime_fn<variant, slot>...};
    }

  inline constexpr std::array<call_t, entries> runtime_a{runtime_entries<0>(std::make_index_sequence<entries>{})};
  inline constexpr std::array<call_t, entries> runtime_b{runtime_entries<1>(std::make_index_sequence<entries>{})};

  ///\brief ID3D11Multithread with a protection that only the test turns on
  struct fake_multithread_t final : ID3D11Multithread
    {
    BOOL protection{};

    auto STDMETHODCALLTYPE QueryInterface(REFIID, void ** out) -> HRESULT override
      {
      *out = nullptr;
      return E_NOINTERFACE;
      }

    auto STDMETHODCALLTYPE AddRef() -> ULONG override
      {
      return 2;
      }

    auto STDMETHODCALLTYPE Release() -> ULONG override
      {
      return 1;
      }

    auto STDMETHODCALLTYPE Enter() -> void override {}

    auto STDMETHODCALLTYPE Leave() -> void override {}

    auto STDMETHODCALLTYPE SetMultithreadProtected(BOOL on) -> BOOL override
      {
      BOOL const was{protection};
      protection = on;
      return was;
      }

    auto STDMETHODCALLTYPE GetMultithreadProtected() -> BOOL override
      {
      return protection;
      }
    };

  ///\brief the context object: its first word the table, as a COM object's
  struct fake_context_t
    {
    void ** table{};
    fake_multithread_t * multithread{};
    ULONG refs{1};
    };

  auto fake_query(void * self, IID const & id, void ** out) -> HRESULT
    {
    auto * const c{static_cast<fake_context_t *>(self)};
    if(id == __uuidof(ID3D11Multithread) and c->multithread)
      {
      c->multithread->AddRef();
      *out = static_cast<ID3D11Multithread *>(c->multithread);
      return S_OK;
      }
    *out = nullptr;
    return E_NOINTERFACE;
    }

  auto fake_add_ref(void * self) -> ULONG
    {
    return ++static_cast<fake_context_t *>(self)->refs;
    }

  auto fake_release(void * self) -> ULONG
    {
    return --static_cast<fake_context_t *>(self)->refs;
    }

  ///\brief the runtime's entries written from `table` on: IUnknown's three, then the counting functions
  auto fill_runtime(void ** table) -> void
    {
    table[0] = reinterpret_cast<void *>(&fake_query);
    table[1] = reinterpret_cast<void *>(&fake_add_ref);
    table[2] = reinterpret_cast<void *>(&fake_release);
    for(std::size_t i{3}; i != entries; ++i)
      table[i] = reinterpret_cast<void *>(runtime_a[i]);
    }

  ///\brief a context on a table of its own on the heap (two words before entry 0, a null after the last); never freed,
  /// as a context the hooks are attached to (contract c)
  auto fresh_context(fake_multithread_t * multithread = nullptr) -> fake_context_t *
    {
    auto * const words{new void *[hook::own_table_header + entries + 1]{}};
    void ** const table{words + hook::own_table_header};
    fill_runtime(table);
    return new fake_context_t{table, multithread};
    }

  ///\brief another context pointing at the same table (a second device's on a shared table)
  auto sharing_context(fake_context_t const * c) -> fake_context_t *
    {
    return new fake_context_t{c->table};
    }

  auto as_context(fake_context_t * c) -> ID3D11DeviceContext *
    {
    return reinterpret_cast<ID3D11DeviceContext *>(c);
    }

  ///\brief a call of `slot` through the table the object points at now (the stubs and hooks have no type check)
  [[clang::no_sanitize("function")]]
  auto call(fake_context_t * c, std::size_t slot) -> void
    {
    void * const * const table{c->table};
    reinterpret_cast<call_t>(table[slot])(c);
    }

  ///\brief one plugin: its context hooks, its watch, the list it attached, and its hooks' calls
  struct plugin_t
    {
    hook::context_hooks_t * hooks{};
    hook::context_watch_t * watch{};
    std::array<hook::slot_patch_t, hooked.size()> list{};
    std::size_t listed{};
    bool off{};  ///< turned off by its watch (the plugin's way out)
    std::array<int, entries> worked{};
    std::array<int, entries> passed{};

    auto span() -> std::span<hook::slot_patch_t>
      {
      return std::span{list}.first(listed);
      }

    auto original(std::size_t slot) const -> void *
      {
      for(std::size_t i{}; i != listed; ++i)
        if(list[i].slot == slot)
          return list[i].original;
      return nullptr;
      }
    };

  std::array<plugin_t, 2> plugins{};
  thread_local int depth{};
  bool recursed{};

  template<int p, std::size_t slot>
  [[clang::no_sanitize("function")]]
  auto hook_fn(void * self) -> void
    {
    plugin_t & pl{plugins[p]};
    if(++depth > 8)
      {
      recursed = true;
      --depth;
      return;
      }
    if(pl.hooks->works(self) and not pl.off)
      ++pl.worked[slot];
    else
      ++pl.passed[slot];
    if(pl.watch->started())
      pl.watch->called(pl.watch->index_of(slot));
    if(void * const original{pl.original(slot)})
      reinterpret_cast<call_t>(original)(self);
    --depth;
    }

  template<int p>
  auto hook_of(std::size_t slot) -> void *
    {
    switch(slot)
      {
      case 12: return reinterpret_cast<void *>(&hook_fn<p, 12>);
      case 13: return reinterpret_cast<void *>(&hook_fn<p, 13>);
      case 20: return reinterpret_cast<void *>(&hook_fn<p, 20>);
      }
    return nullptr;
    }

  ///\brief plugin `p` anew (the one before kept: its hooks may still be called), hooking `slots`
  template<int p>
  auto fresh_plugin(std::initializer_list<std::size_t> slots) -> plugin_t &
    {
    plugin_t & pl{plugins[p]};
    pl = plugin_t{};
    pl.hooks = new hook::context_hooks_t{};
    pl.watch = new hook::context_watch_t{};
    for(std::size_t const s : slots)
      pl.list[pl.listed++] = hook::slot_patch_t{s, hook_of<p>(s)};
    return pl;
    }

  auto reset_calls() -> void
    {
    runtime_calls = {};
    cover_calls = {};
    recursed = false;
    }

  auto call_hooked(fake_context_t * c) -> void
    {
    for(std::size_t const s : hooked)
      call(c, s);
    }

  auto each_hooked(auto && ok) -> bool
    {
    for(std::size_t const s : hooked)
      if(not ok(s))
        return false;
    return true;
    }

  auto start_watch(plugin_t & pl) -> bool
    {
    return pl.watch->start(pl.hooks->watch_setup(pl.span())) == hook::watch_start_e::started;
    }

  auto own_table_and_rewrite() -> void
    {
    reset_calls();
    fake_context_t * const c{fresh_context()};
    void ** const runtime{c->table};
    plugin_t & a{fresh_plugin<0>({12, 13, 20})};
    hook::context_attached_t const r{a.hooks->attach(as_context(c), hook::context_ask_e::automatic, a.span())};
    check(r.result == hook::context_attach_e::attached and r.way == hook::context_way_e::own_table
            and r.pick.point == hook::context_point_e::own_table,
          "context: a table on the heap with entries in a module - a table of our own");
    check(c->table != runtime and a.hooks->works(c), "context: the object points at the table of our own, attached");
    check(a.hooks->attach(as_context(c), hook::context_ask_e::automatic, a.span()).result
              == hook::context_attach_e::already
            and c->refs == 2,
          "context: the same context again - already attached, one reference taken");
    bool put{true};
    for(hook::slot_patch_t const & s : a.span())
      put = put and s.result == hook::slot_result_e::patched and s.original != nullptr;
    check(put, "context: every slot of the list put, its stub the original");
    call_hooked(c);
    call(c, not_hooked);
    check(each_hooked([&](std::size_t s) { return a.worked[s] == 1 and runtime_calls[0][s] == 1; }),
          "context: the calls through the hooks and their stubs to the runtime's entries");
    check(runtime_calls[0][not_hooked] == 1, "context: a slot without a hook through its stub");
    check(start_watch(a), "watch: started on the table of our own");
    hook::watch_state_t state{a.watch->check()};
    check(state.slots == hook::slot_class_e::ours and state.table == hook::table_class_e::ours and not state.turns_off(),
          "watch: every slot and the table ours");

    // the runtime rewrites every entry of its table (Windows' d3d11 switching its variants)
    for(std::size_t i{3}; i != entries; ++i)
      runtime[i] = reinterpret_cast<void *>(runtime_b[i]);
    call_hooked(c);
    call(c, not_hooked);
    check(each_hooked([&](std::size_t s) { return a.worked[s] == 2 and runtime_calls[1][s] == 1; }),
          "context: after the runtime rewrote its table the hooks call the new entries");
    check(runtime_calls[1][not_hooked] == 1, "context: a slot without a hook follows the rewrite");
    state = a.watch->check();
    check(state.slots == hook::slot_class_e::ours and state.table == hook::table_class_e::ours and not state.turns_off(),
          "watch: the rewrite of the runtime's table takes nothing away");
    check(a.watch->runtime_moved() == hooked.size(), "watch: runtime_moved counts the rewritten entries followed");

    // the object pointed back at the runtime's table
    c->table = runtime;
    state = a.watch->check();
    check(state.table == hook::table_class_e::re_pointed and state.turns_off() and state.changed,
          "watch: the object pointed back at the runtime's table - re-pointed, the plugin turns off");
    check(not recursed, "context: no hook called itself again");
    }

  auto second_copy_in_place() -> void
    {
    reset_calls();
    fake_context_t * const c{fresh_context()};
    void ** const runtime{c->table};
    plugin_t & a{fresh_plugin<0>({12, 13, 20})};
    hook::context_attached_t const ra{a.hooks->attach(as_context(c), hook::context_ask_e::automatic, a.span())};
    bool const watched{start_watch(a)};
    plugin_t & b{fresh_plugin<1>({12, 13, 20})};
    hook::context_attached_t const rb{b.hooks->attach(as_context(c), hook::context_ask_e::automatic, b.span())};
    check(ra.way == hook::context_way_e::own_table and rb.result == hook::context_attach_e::attached
            and rb.way == hook::context_way_e::in_place and rb.pick.point == hook::context_point_e::other_instance,
          "context: a second copy recognizes the table of our own by its mark and hooks in it in place");
    check(watched and start_watch(b), "watch: both copies watch");
    check(c->refs == 3, "context: each copy took a reference");
    call_hooked(c);
    check(each_hooked([&](std::size_t s) { return b.worked[s] == 1 and a.worked[s] == 1 and runtime_calls[0][s] == 1; })
            and not recursed,
          "context: the call through the second copy's hook, then the first's, then the runtime's entry");
    hook::watch_state_t const sa{a.watch->check()};
    check(sa.slots == hook::slot_class_e::covered and sa.table == hook::table_class_e::ours and not sa.turns_off(),
          "watch: the first copy sees its slots covered by the second's hooks, not turned off");
    for(std::size_t i{3}; i != entries; ++i)
      runtime[i] = reinterpret_cast<void *>(runtime_b[i]);
    call_hooked(c);
    check(each_hooked([&](std::size_t s) { return b.worked[s] == 2 and a.worked[s] == 2 and runtime_calls[1][s] == 1; }),
          "context: both copies' hooks follow the runtime's rewrite");
    hook::watch_state_t const sb{b.watch->check()};
    check(sb.slots == hook::slot_class_e::ours and sb.table == hook::table_class_e::ours and not sb.turns_off(),
          "watch: the second copy's slots ours after the runtime's rewrite");
    // the runtime's entry written into the first copy's table over the second's hook: told by the mark's runtime table
    c->table[20] = runtime[20];
    hook::watch_state_t const taken{b.watch->check()};
    check(taken.slots == hook::slot_class_e::taken_by_runtime and taken.slot == 20 and taken.turns_off(),
          "watch: the second copy tells the runtime's entry by the mark's table - taken by the runtime");
    check(not recursed, "context: no hook called itself again");
    }

  auto plugin_above_own_table() -> void
    {
    reset_calls();
    fake_context_t * const c{fresh_context()};
    void ** const runtime{c->table};
    plugin_t & a{fresh_plugin<0>({12, 13, 20})};
    hook::context_attached_t const ra{a.hooks->attach(as_context(c), hook::context_ask_e::automatic, a.span())};
    bool const watched{start_watch(a)};
    void ** const ours{c->table};
    plugin_t & b{fresh_plugin<1>({12})};
    hook::context_attached_t const rb{b.hooks->attach(as_context(c), hook::context_ask_e::own_table, b.span())};
    check(ra.way == hook::context_way_e::own_table and rb.result == hook::context_attach_e::attached
            and rb.way == hook::context_way_e::own_table and c->table != ours and watched,
          "context: a plugin above makes a table of its own over ours and points the object at it");
    call_hooked(c);
    check(b.worked[12] == 1 and each_hooked([&](std::size_t s) { return a.worked[s] == 1 and runtime_calls[0][s] == 1; })
            and not recursed,
          "context: through the plugin above's table to our hooks and the runtime, no recursion");
    hook::watch_state_t const s{a.watch->check()};
    check(s.table == hook::table_class_e::above_own_table and not s.turns_off(),
          "watch: a plugin above with a table of its own, not turned off");
    // the plugin above refreshes an entry of its table from the runtime's: the same table, one entry changed
    c->table[13] = runtime[13];
    hook::watch_state_t const refreshed{a.watch->check()};
    check(refreshed.table == hook::table_class_e::re_pointed and refreshed.turns_off(),
          "watch: an entry of the plugin above's table changed to the runtime's - re-pointed, the plugin turns off");
    }

  auto own_table_taken() -> void
    {
    reset_calls();
    fake_context_t * const c{fresh_context()};
    void ** const runtime{c->table};
    plugin_t & a{fresh_plugin<0>({12, 13, 20})};
    hook::context_attached_t const r{a.hooks->attach(as_context(c), hook::context_ask_e::automatic, a.span())};
    bool const watched{start_watch(a)};
    // our stub written back into our table's slot 12 (a hook taken away)
    c->table[12] = a.original(12);
    hook::watch_state_t state{a.watch->check()};
    check(r.way == hook::context_way_e::own_table and watched and state.slots == hook::slot_class_e::taken
            and state.slot == 12 and state.turns_off(),
          "watch: our stub back in our table - taken, the plugin turns off");
    // the runtime's entry written into our table's slot 13
    c->table[13] = runtime[13];
    state = a.watch->check();
    check(state.slots == hook::slot_class_e::taken_by_runtime and state.slot == 13 and state.turns_off(),
          "watch: the runtime's entry in our table - taken by the runtime");
    }

  auto short_table() -> void
    {
    reset_calls();
    // a table of 15 entries with context_hook 2: too short for a table of our own, so in place, and slot 20 of the
    // list past it
    fake_context_t * const c{fresh_context()};
    c->table[15] = nullptr;
    plugin_t & a{fresh_plugin<0>({12, 13, 20})};
    hook::context_attached_t const r{a.hooks->attach(as_context(c), hook::context_ask_e::own_table, a.span())};
    check(r.result == hook::context_attach_e::slot_out and r.pick.point == hook::context_point_e::too_short
            and c->table[12] == reinterpret_cast<void *>(runtime_a[12]) and not a.hooks->works(c) and c->refs == 1,
          "context: a slot of the list past a short table - nothing written, not attached");
    }

  auto list_not_written() -> void
    {
    reset_calls();
    // a section seen through a view that may be written and one that may only be read: the table in the second
    std::size_t const bytes{(hook::own_table_header + entries + 1) * sizeof(void *)};
    HANDLE const section{CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, DWORD(bytes), nullptr)};
    void * const rw{section ? MapViewOfFile(section, FILE_MAP_WRITE, 0, 0, 0) : nullptr};
    void * const ro{section ? MapViewOfFile(section, FILE_MAP_READ, 0, 0, 0) : nullptr};
    if(not rw or not ro)
      {
      check(false, "context: a section with a read-only view could not be made");
      return;
      }
    fill_runtime(static_cast<void **>(rw) + hook::own_table_header);
    void ** const table{static_cast<void **>(ro) + hook::own_table_header};
    auto * const c{new fake_context_t{table}};
    plugin_t & a{fresh_plugin<0>({12, 13, 20})};
    hook::context_attached_t const r{a.hooks->attach(as_context(c), hook::context_ask_e::automatic, a.span())};
    std::printf("context: the read-only view: attach %d, way %d, point %d, slots step %d, error %lu\n",
                static_cast<int>(r.result), static_cast<int>(r.way), static_cast<int>(r.pick.point),
                static_cast<int>(r.slots.result), static_cast<unsigned long>(r.slots.error));
    check(r.result == hook::context_attach_e::slots_failed and r.way == hook::context_way_e::in_place
            and r.pick.point == hook::context_point_e::memory_unknown,
          "context: a table in a mapped view - in place, the list not written");
    check(r.slots.result == hook::slots_patch_e::page_protect and r.slots.patched == 0
            and each_hooked([&](std::size_t s) { return table[s] == reinterpret_cast<void *>(runtime_a[s]); }),
          "context: a page that cannot be made writable - nothing written");
    check(not a.hooks->works(c), "context: a list not written - not attached");
    call_hooked(c);
    check(each_hooked([&](std::size_t s) { return runtime_calls[0][s] == 1 and a.worked[s] == 0 and a.passed[s] == 0; }),
          "context: the calls go to the runtime's entries");
    // a try that failed is the one try: the same context again is refused, nothing written
    check(a.hooks->attach(as_context(c), hook::context_ask_e::own_table, a.span()).result
            == hook::context_attach_e::refused
            and c->table == table
            and each_hooked([&](std::size_t s) { return table[s] == reinterpret_cast<void *>(runtime_a[s]); }),
          "context: the same context after a try that failed - refused, nothing changed");
    fake_context_t * const other{fresh_context()};
    void ** const other_table{other->table};
    check(a.hooks->attach(as_context(other), hook::context_ask_e::automatic, a.span()).result
            == hook::context_attach_e::refused
            and other->table == other_table and other->refs == 1 and c->refs == 1
            and each_hooked([&](std::size_t s) { return other_table[s] == reinterpret_cast<void *>(runtime_a[s]); }),
          "context: another context after a try that failed - refused, its table untouched, no reference");
    }

  auto in_place_taken() -> void
    {
    reset_calls();
    fake_context_t * const c{fresh_context()};
    plugin_t & a{fresh_plugin<0>({12, 13, 20})};
    hook::context_attached_t const r{a.hooks->attach(as_context(c), hook::context_ask_e::in_place, a.span())};
    check(r.result == hook::context_attach_e::attached and r.way == hook::context_way_e::in_place
            and r.pick.point == hook::context_point_e::asked_in_place and c->table[12] == hook_of<0>(12),
          "context: context_hook 1 - the hooks in place in the runtime's table");
    check(start_watch(a), "watch: started in place");
    call_hooked(c);
    check(each_hooked([&](std::size_t s) { return a.worked[s] == 1 and runtime_calls[0][s] == 1; }),
          "context: in place, through the hooks to the runtime's entries");

    // a hook over ours in slot 13 that calls no further: covered, its slot quiet
    c->table[13] = reinterpret_cast<void *>(&cover_fn<13>);
    hook::watch_state_t state{a.watch->check()};
    check(state.slots == hook::slot_class_e::covered and state.slot == 13 and not state.turns_off(),
          "watch: another hook over ours - covered, not turned off");
    std::uint32_t const at_start{a.watch->quiet_slots(1000, 500)};
    call(c, 12);
    call(c, 13);
    call(c, 20);
    std::uint32_t const later{a.watch->quiet_slots(2000, 500)};
    std::uint32_t const bit13{std::uint32_t{1} << a.watch->index_of(13)};
    check(at_start == 0 and later == bit13 and cover_calls[13] == 1 and a.worked[13] == 1,
          "watch: the census tells the covered slot's hook not called");

    // the runtime writes its own entry back over our hook in slot 12: taken, the plugin turns off
    c->table[12] = reinterpret_cast<void *>(runtime_a[12]);
    state = a.watch->check();
    check(state.slots == hook::slot_class_e::taken and state.slot == 12 and state.turns_off() and state.changed,
          "watch: the runtime's entry back in the slot - taken, the plugin turns off");
    a.off = state.turns_off();
    call(c, 12);
    call(c, 20);
    check(runtime_calls[0][12] == 3 and runtime_calls[0][20] == 3 and a.worked[20] == 2 and a.passed[20] == 1,
          "context: turned off, the game's calls reach the runtime as before");

    // an entry into the system's d3d11 (never called here)
    edplugin_common::chain::system_d3d11_t const & system{edplugin_common::chain::system_d3d11()};
    FARPROC const in_system{system.module ? GetProcAddress(system.module, "D3D11CreateDevice") : nullptr};
    if(not in_system)
      {
      check(false, "chain: the system's d3d11.dll and its D3D11CreateDevice");
      return;
      }
    c->table[20] = reinterpret_cast<void *>(in_system);
    state = a.watch->check();
    check(state.slots == hook::slot_class_e::taken_by_runtime and state.slot == 20,
          "watch: an entry into the system's d3d11 - taken by the runtime");
    c->table[20] = reinterpret_cast<void *>(runtime_a[20]);
    }

  auto protection() -> void
    {
    auto * const mt{new fake_multithread_t{}};
    fake_context_t * const c{fresh_context(mt)};
    hook::multithread_t m{};
    check(m.start(as_context(c), nullptr) == hook::multithread_start_e::off and not m.on() and not m.on_at_start(),
          "multithread: the context's ID3D11Multithread, the protection off");
    hook::multithread_state_t s{m.refresh()};
    check(not s.on and not s.changed and not s.turns_off(), "multithread: still off, no change");
    mt->protection = TRUE;
    s = m.refresh();
    check(s.on and s.changed and s.turns_off() and m.on(), "multithread: turned on - a change, the plugin turns off");
    s = m.refresh();
    check(s.on and not s.changed, "multithread: the change told once");
    check(m.start(as_context(c), nullptr) == hook::multithread_start_e::no_object, "multithread: a second start refused");
    hook::multithread_t none{};
    check(none.start(as_context(fresh_context()), nullptr) == hook::multithread_start_e::no_interface
            and not none.refresh().on,
          "multithread: no ID3D11Multithread - taken as off");
    }

  auto second_device() -> void
    {
    reset_calls();
    fake_context_t * const c1{fresh_context()};
    fake_context_t * const c2{sharing_context(c1)};
    plugin_t & a{fresh_plugin<0>({12, 13, 20})};
    hook::context_attached_t const r{a.hooks->attach(as_context(c1), hook::context_ask_e::in_place, a.span())};
    call(c2, 12);
    call(c1, 12);
    check(r.attached() and a.passed[12] == 1 and a.worked[12] == 1 and runtime_calls[0][12] == 2,
          "context: a second device's context on the shared table - its calls pass, the first's work");
    // one try per plugin: a second attach with the other context, and a fresh list, is refused before anything changes
    std::array<hook::slot_patch_t, 3> fresh{hook::slot_patch_t{12, hook_of<0>(12)},
                                            hook::slot_patch_t{13, hook_of<0>(13)},
                                            hook::slot_patch_t{20, hook_of<0>(20)}};
    void * const first_original{a.original(12)};
    hook::context_attached_t const again{a.hooks->attach(as_context(c2), hook::context_ask_e::in_place, fresh)};
    call(c1, 12);
    call(c2, 12);
    check(again.result == hook::context_attach_e::refused and a.hooks->works(c1) and not a.hooks->works(c2)
            and a.worked[12] == 2 and a.passed[12] == 2 and runtime_calls[0][12] == 4 and c1->refs == 2
            and c2->refs == 1,
          "context: a second attach with another context - refused, the first still the plugin's, the other's pass");
    check(a.original(12) == first_original
            and std::ranges::all_of(fresh, [](hook::slot_patch_t const & f)
                                      { return f.result == hook::slot_result_e::not_tried and f.original == nullptr; }),
          "context: a refused attach - the first's originals kept, the fresh list not written");
    check(a.hooks->attach(as_context(c2), hook::context_ask_e::in_place, fresh).result
            == hook::context_attach_e::refused,
          "context: a refusal again - refused, said in the log once");
    check(a.hooks->attach(as_context(c1), hook::context_ask_e::in_place, a.span()).result
            == hook::context_attach_e::already,
          "context: the first context again after a refusal - already");

    // on a table of our own the second device's context keeps the runtime's table
    reset_calls();
    fake_context_t * const c3{fresh_context()};
    fake_context_t * const c4{sharing_context(c3)};
    plugin_t & b{fresh_plugin<1>({12, 13, 20})};
    hook::context_attached_t const own{b.hooks->attach(as_context(c3), hook::context_ask_e::automatic, b.span())};
    call(c4, 12);
    check(own.way == hook::context_way_e::own_table and c4->table != c3->table and runtime_calls[0][12] == 1
            and b.worked[12] == 0 and b.passed[12] == 0,
          "context: a table of our own for one context - the other context's calls do not meet the hooks");
    // another context on a table of its own (another device's): refused, its table untouched, the first still works
    fake_context_t * const c5{fresh_context()};
    void ** const c5_table{c5->table};
    hook::context_attached_t const other{b.hooks->attach(as_context(c5), hook::context_ask_e::automatic, b.span())};
    call(c3, 12);
    call(c5, 12);
    check(other.result == hook::context_attach_e::refused and c5->table == c5_table
            and each_hooked([&](std::size_t s) { return c5_table[s] == reinterpret_cast<void *>(runtime_a[s]); })
            and b.hooks->works(c3) and b.worked[12] == 1 and runtime_calls[0][12] == 3 and c5->refs == 1,
          "context: a second attach on another table - refused, that table untouched, the first works");
    }
  }  // namespace

namespace
  {
  ///\brief the refusals said in the log: once for each plugin refused (second_device's two, list_not_written's one)
  auto refusals(std::string_view text) -> int
    {
    int n{};
    for(std::size_t at{text.find("refused: one try per plugin")}; at != std::string_view::npos;
        at = text.find("refused: one try per plugin", at + 1))
      ++n;
    return n;
    }
  }  // namespace

auto context_tests(void (*checker)(bool ok, char const * what), std::wstring const & dir) -> void
  {
  check = checker;
  std::wstring const logs{dir + L"\\context"};
  CreateDirectoryW(logs.c_str(), nullptr);
  edplugin_common::log_open(logs, L"context");
  own_table_and_rewrite();
  second_copy_in_place();
  plugin_above_own_table();
  own_table_taken();
  short_table();
  list_not_written();
  in_place_taken();
  protection();
  second_device();
  edplugin_common::log_close();
  std::optional<std::string> const log{edplugin_common::read_text(logs + L"\\context.log")};
  std::string_view const text{log ? std::string_view{*log} : std::string_view{}};
  check(text.find("context: on a table of our own (") != std::string_view::npos
          and text.find("context: the table of another copy of the library (contract ") != std::string_view::npos
          and text.find("context: in place, ") != std::string_view::npos
          and text.find("context: not every hook in; the hooks pass their calls on") != std::string_view::npos
          and text.find("context: slot 20 of the list past the table's 15 entries") != std::string_view::npos
          and refusals(text) == 3,
        "context: the attach's lines in the log");
  }
