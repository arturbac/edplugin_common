// context_probe 5 - a diagnosis for the plugins on another Windows, without the game: where the D3D11 runtime keeps the
// method tables of a device and of its immediate context, whether it rewrites their entries, and what else in the
// process changed them. It loads d3d11.dll and dxgi.dll from the system folder only, makes devices on every hardware
// adapter (no flags, BGRA support, single-threaded) and one with no adapter given, reads their tables, takes a
// snapshot of the context's entries around a few calls of the runtime, follows the entries that lie outside the system
// d3d11 to the module they lead to, tells the entries in the system d3d11 whose code starts with a jump out of it (an
// inline hook), and lists the process's modules outside the Windows folder (what was injected into it). The lines go
// to context_probe.txt beside the exe and to the console. Nothing is drawn, nothing sent anywhere; no user or computer
// name and no path in the output. Without a window and a swap chain, and in a process that is not the game: an
// overlay that hooks only a game, or only at Present, is not seen. Every table is read as a copy, under __try.
#include "follow.h"
#include "jump.h"
#include "slots.h"

#include <edplugin_common/file/file.h>
#include <edplugin_common/memory/memory.h>
#include <edplugin_common/paths/paths.h>

#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
  {
  std::string out;

  ///\brief a line of the result, to `out` and to the console
  [[gnu::format(printf, 1, 2)]]
  auto say(char const * format, ...) -> void
    {
    char line[1024]{};
    va_list args;
    va_start(args, format);
    int const n{std::vsnprintf(line, sizeof line, format, args)};
    va_end(args);
    if(n >= int(sizeof line))
      std::memcpy(line + sizeof line - 7, " [cut]", 7);  // never silently shorter
    out += line;
    out += "\r\n";
    std::fputs(line, stdout);
    std::fputs("\n", stdout);
    }

  HMODULE system_d3d11{};
  /// how far a table is followed (the runtime's tables on Windows reach past the 256 a plugin's copy stops at)
  constexpr int table_scan{2048};
  /// entries outside the system d3d11 told one by one, at most
  constexpr int foreign_lines{16};
  PFN_D3D11_CREATE_DEVICE create_device{};

  ///\brief the file version from a loaded module's version resource ("?" when none)
  [[nodiscard]]
  auto module_version(HMODULE module) -> std::string
    {
    HRSRC const r{FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16))};
    HGLOBAL const g{r ? LoadResource(module, r) : nullptr};
    auto const * const data{g ? static_cast<std::uint8_t const *>(LockResource(g)) : nullptr};
    DWORD const size{r ? SizeofResource(module, r) : 0};
    if(not data or size < sizeof(VS_FIXEDFILEINFO))
      return "?";
    // VS_VERSIONINFO: the fixed part after its header and key, at a 4-byte boundary, by its signature
    for(DWORD at{}; at + sizeof(VS_FIXEDFILEINFO) <= size; at += 4)
      if(VS_FIXEDFILEINFO f{}; (std::memcpy(&f, data + at, sizeof f), f.dwSignature == 0xFEEF04BD))
        {
        char text[64]{};
        std::snprintf(text, sizeof text, "%u.%u.%u.%u", HIWORD(f.dwFileVersionMS), LOWORD(f.dwFileVersionMS),
                      HIWORD(f.dwFileVersionLS), LOWORD(f.dwFileVersionLS));
        return text;
        }
    return "?";
    }

  auto windows_version() -> void
    {
    using rtl_t = LONG(WINAPI *)(OSVERSIONINFOW *);
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof v;
    HMODULE const ntdll{GetModuleHandleW(L"ntdll.dll")};
    auto const rtl{ntdll ? reinterpret_cast<rtl_t>(reinterpret_cast<void *>(GetProcAddress(ntdll, "RtlGetVersion")))
                         : nullptr};
    if(rtl and rtl(&v) == 0)
      say("windows %lu.%lu.%lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    else
      say("windows ?");
    }

  [[nodiscard]]
  auto utf8(wchar_t const * text) -> std::string
    {
    int const n{WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr)};
    if(n <= 1)
      return "?";
    std::string name(std::size_t(n), '\0');
    if(WideCharToMultiByte(CP_UTF8, 0, text, -1, name.data(), n, nullptr, nullptr) != n)
      return "?";
    name.pop_back();  // the terminating null
    return name;
    }

  ///\brief the modules of the process outside the Windows folder (the system folder, the driver store a graphics
  /// driver loads from and the side-by-side folder are all in it), one a line by their file names: what the process
  /// loaded beside the exe, and what was injected into it (overlays, security software)
  auto modules_outside_windows() -> void
    {
    wchar_t windows[MAX_PATH]{};
    UINT const length{GetSystemWindowsDirectoryW(windows, MAX_PATH)};
    // the list, again with a larger buffer while the process holds more modules than it (a few tries: modules may
    // load meanwhile)
    std::vector<HMODULE> modules(1024);
    DWORD needed{};
    bool listed{length != 0 and length < MAX_PATH};
    for(int tries{}; listed and tries != 4; ++tries)
      {
      listed = K32EnumProcessModules(GetCurrentProcess(), modules.data(), DWORD(modules.size() * sizeof(HMODULE)),
                                     &needed);
      if(not listed or needed <= modules.size() * sizeof(HMODULE))
        break;
      modules.resize(needed / sizeof(HMODULE) + 64);
      }
    if(not listed)
      {
      say("modules outside the Windows folder: not read");
      return;
      }
    if(needed > modules.size() * sizeof(HMODULE))
      say("modules: the first %zu of %zu", modules.size(), std::size_t(needed / sizeof(HMODULE)));
    modules.resize(std::min<std::size_t>(modules.size(), needed / sizeof(HMODULE)));
    std::wstring windows_dir{windows, length};
    if(not windows_dir.ends_with(L'\\'))
      windows_dir += L'\\';
    HMODULE const exe{GetModuleHandleW(nullptr)};
    std::vector<std::string> names;
    for(HMODULE const module: modules)
      {
      std::wstring const path{edplugin_common::module_path(module)};
      bool const in_windows{path.size() > windows_dir.size()
                            and CompareStringOrdinal(path.c_str(), int(windows_dir.size()), windows_dir.c_str(),
                                                     int(windows_dir.size()), TRUE)
                                  == CSTR_EQUAL};
      if(module != exe and not in_windows)
        names.push_back(context_probe::module_name(module));
      }
    say("modules outside the Windows folder (besides the exe): %zu", names.size());
    for(std::string const & name: names)
      say("  module %s", name.c_str());
    }

  [[nodiscard]]
  auto same(ID3D11DeviceContext * ctx, REFIID id) -> char const *
    {
    void * other{};
    if(FAILED(ctx->QueryInterface(id, &other)) or not other)
      return "none";
    char const * const answer{other == static_cast<void *>(ctx) ? "same pointer" : "other pointer"};
    static_cast<IUnknown *>(other)->Release();
    return answer;
    }

  /// the entries of ID3D11DeviceContext4 (with IUnknown and ID3D11DeviceChild)
  constexpr int context4_entries{static_cast<int>(context_probe::context_methods.size())};
  static_assert(context4_entries == 149);

  struct snapshot_t
    {
    void ** table{};
    void * entry[context4_entries]{};
    int read{};
    };

  [[nodiscard]]
  auto take(ID3D11DeviceContext * ctx) -> snapshot_t
    {
    snapshot_t s;
    s.table = *reinterpret_cast<void ***>(ctx);
    std::vector<void *> const entries{context_probe::read_entries(s.table, context4_entries)};
    s.read = int(entries.size());
    std::ranges::copy(entries, s.entry);
    return s;
    }

  snapshot_t start;  ///< the snapshot at the device's start, for the changes since

  ///\brief the slots that differ between `a` and `b`
  [[nodiscard]]
  auto differ(snapshot_t const & a, snapshot_t const & b) -> std::array<bool, context4_entries>
    {
    std::array<bool, context4_entries> changed{};
    for(int i{}; i != std::min(a.read, b.read); ++i)
      changed[std::size_t(i)] = a.entry[i] != b.entry[i];
    return changed;
    }

  ///\brief what changed in the context's table since `before`, after `step` (the line of context_probe 4 as it was,
  /// then the slots as ranges); the snapshot now
  [[nodiscard]]
  auto compare(ID3D11DeviceContext * ctx, snapshot_t const & before, char const * step) -> snapshot_t
    {
    snapshot_t const now{take(ctx)};
    auto const changed{differ(before, now)};
    auto const since_start{differ(start, now)};
    auto const first{std::ranges::find(changed, true)};
    int const first_slot{first == changed.end() ? -1 : int(first - changed.begin())};
    say("  after %s: table pointer %s, entries changed %d of %d (first slot %d); %d differ from the start", step,
        now.table == before.table ? "same" : "MOVED", int(std::ranges::count(changed, true)),
        std::min(before.read, now.read), first_slot, int(std::ranges::count(since_start, true)));
    say("    slots changed %s; differ from the start %s", context_probe::ranges_text(changed).c_str(),
        context_probe::ranges_text(since_start).c_str());
    return now;
    }

  /// a copy of the context's table as plain data (no executable memory, no change of any page's protection): the object
  /// pointed at it for a few calls of the runtime, to see whether the runtime writes the object's table pointer back
  void * copy[2 + table_scan];

  auto copy_test(ID3D11DeviceContext * ctx, ID3D11Multithread * mt) -> void
    {
    void ** const table{*reinterpret_cast<void ***>(ctx)};
    std::vector<void *> const entries{context_probe::read_entries(table, table_scan)};
    int const n{int(std::ranges::find(entries, nullptr) - entries.begin())};
    // the two words before the table too: the C++ type information
    auto const * const before{
      reinterpret_cast<void * const *>(reinterpret_cast<std::uintptr_t>(table) - 2 * sizeof(void *))};
    if(n < context4_entries or not edplugin_common::readable(before, 2 * sizeof(void *))
       or not context_probe::read_caught(copy, before, 2 * sizeof(void *)))
      {
      say("  table pointer test: not done (table of %d entries)", n);
      return;
      }
    std::ranges::copy(entries.begin(), entries.begin() + n, copy + 2);
    void ** const mine{copy + 2};
    *reinterpret_cast<void ***>(ctx) = mine;
    // the slots compared: those of the snapshot at the start too
    int const compared{std::min({n, context4_entries, start.read})};
    auto look = [&](char const * step)
      {
      void ** const now{*reinterpret_cast<void ***>(ctx)};
      std::vector<void *> const runtime{context_probe::read_entries(table, compared)};
      int in_runtime{compared - int(runtime.size())}, copy_changed{};  // an entry no longer read counts as changed
      for(int i{}; i != compared; ++i)
        {
        in_runtime += i < int(runtime.size()) and runtime[std::size_t(i)] != start.entry[i] ? 1 : 0;
        copy_changed += mine[i] != start.entry[i] ? 1 : 0;
        }
      say("  table pointer test, after %s: the object points at %s; entries changed in the copy %d, in the runtime's "
          "table %d (of %d)",
          step, now == mine ? "the copy still" : now == table ? "the RUNTIME'S table again" : "ANOTHER table",
          copy_changed, in_runtime, compared);
      };
    if(mt)
      {
      mt->SetMultithreadProtected(TRUE);
      look("multithread protection on");
      mt->SetMultithreadProtected(FALSE);
      look("multithread protection off");
      }
    ctx->ClearState();
    ctx->Flush();
    look("ClearState and Flush");
    *reinterpret_cast<void ***>(ctx) = table;  // put back before anything else uses the context
    }

  [[nodiscard]]
  auto memory_type(void const * p, DWORD & protect, void *& allocation) -> char const *
    {
    MEMORY_BASIC_INFORMATION m{};
    protect = 0;
    allocation = nullptr;
    if(VirtualQuery(p, &m, sizeof m) != sizeof m)
      return "?";
    protect = m.Protect;
    allocation = m.AllocationBase;
    return m.State == MEM_FREE      ? "free"
         : m.State == MEM_RESERVE   ? "reserve"
         : m.Type == MEM_IMAGE      ? "image"
         : m.Type == MEM_PRIVATE    ? "private"
         : m.Type == MEM_MAPPED     ? "mapped"
                                    : "other";
    }

  ///\brief a table's place and length, how many of its entries lie in the system d3d11, each of its named entries
  /// that does not (up to foreign_lines) with where it leads, and each named entry in the system d3d11 whose code
  /// starts with a jump out of it (up to foreign_lines); the table read once, as a copy
  template<std::size_t n>
  auto table_facts(char const * what, void * object, void ** table, std::array<std::string_view, n> const & names)
    -> void
    {
    auto const offset{reinterpret_cast<std::intptr_t>(table) - reinterpret_cast<std::intptr_t>(object)};
    DWORD protect{}, object_protect{};
    void * allocation{};
    void * object_allocation{};
    char const * const type{memory_type(table, protect, allocation)};
    (void)memory_type(object, object_protect, object_allocation);
    HMODULE const module{edplugin_common::module_of(table)};
    char section[9]{"-"};
    if(module)
      edplugin_common::section_of(module, table, section);
    // the distance only within one allocation: between a module's image and the heap it is ASLR's, nothing measured
    if(allocation and allocation == object_allocation)
      say("%s: table - object %lld bytes, same allocation as the object: yes", what, static_cast<long long>(offset));
    else
      say("%s: table - object in another allocation, same allocation as the object: no", what);
    say("%s: table memory %s, protection 0x%lX, module %s, section %s", what, type, static_cast<unsigned long>(protect),
        context_probe::module_name(module).c_str(), section);
    std::vector<void *> const entries{context_probe::read_entries(table, table_scan)};
    auto const entry = [&](int i) { return entries[std::size_t(i)]; };
    // the length two ways: up to the first null entry, and (as the plugins) up to the first entry that is not code
    int const to_null{int(std::ranges::find(entries, nullptr) - entries.begin())};
    int const to_code{int(std::ranges::find_if(entries, [](void * e) { return not edplugin_common::executable(e); })
                          - entries.begin())};
    // the prefix whose entries are all the system d3d11's
    int d3d11_prefix{};
    while(d3d11_prefix != to_null and edplugin_common::module_of(entry(d3d11_prefix)) == system_d3d11)
      ++d3d11_prefix;
    int in_d3d11{}, in_other{}, in_none{};
    for(int i{}; i != std::min(to_null, 96); ++i)
      {
      HMODULE const m{edplugin_common::module_of(entry(i))};
      ++(m == nullptr ? in_none : m == system_d3d11 ? in_d3d11 : in_other);
      }
    say("%s: table entries %d to the first null, %d to the first not code, %d all in the system d3d11 (followed up to "
        "%d); first 96: system d3d11 %d, other module %d, no module %d",
        what, to_null, to_code, d3d11_prefix, table_scan, in_d3d11, in_other, in_none);
    // each named entry outside the system d3d11: its method, where it lies, and where its jumps lead; each one in it
    // whose first instruction jumps out of it
    int told{}, foreign{}, hooks_told{}, hooks{};
    for(int i{}; i != std::min(to_null, int(n)); ++i)
      {
      std::string const method{context_probe::method_name(names, i)};
      HMODULE const m{edplugin_common::module_of(entry(i))};
      if(m == system_d3d11)
        {
        std::string const out_of{context_probe::jump_out_of(entry(i), system_d3d11)};
        if(out_of.empty())
          continue;
        ++hooks;
        if(hooks_told == foreign_lines)
          continue;
        ++hooks_told;
        say("  %s slot %d %s: in the system d3d11, its code jumps out: %s", what, i, method.c_str(), out_of.c_str());
        continue;
        }
      ++foreign;
      if(told == foreign_lines)
        continue;
      ++told;
      std::string const lies{context_probe::where(entry(i))};
      std::string const leads{m ? std::string{} : "; " + context_probe::follow_jumps(entry(i))};
      say("  %s slot %d %s: %s%s", what, i, method.c_str(), lies.c_str(), leads.c_str());
      }
    if(foreign > told)
      say("  %s: %d more entries outside the system d3d11 not told", what, foreign - told);
    say("%s: named entries in the system d3d11 whose code starts with a jump out of it: %d%s", what, hooks,
        hooks > hooks_told ? " (the first told)" : "");
    }

  ///\brief whether the runtime's lock is taken again by the thread holding it (Enter twice, a method that takes it,
  /// Leave twice), on a thread of its own with a timeout: a lock that is not reentrant would hang there, not here
  [[nodiscard]]
  auto lock_reentrant(ID3D11DeviceContext * ctx, ID3D11Multithread * mt) -> char const *
    {
    struct work_t
      {
      ID3D11DeviceContext * ctx;
      ID3D11Multithread * mt;
      };
    // static: a thread still running after the wait reads it later, when this frame is gone
    static work_t work;
    work = {ctx, mt};
    HANDLE const thread{CreateThread(nullptr, 0,
                                     [](void * p) -> DWORD
                                       {
                                       auto const & w{*static_cast<work_t *>(p)};
                                       w.mt->Enter();
                                       w.mt->Enter();
                                       w.ctx->Flush();
                                       w.mt->Leave();
                                       w.mt->Leave();
                                       return 0;
                                       },
                                     &work, 0, nullptr)};
    if(not thread)
      return "not tried (no thread)";
    ULONGLONG const began{GetTickCount64()};
    DWORD const waited{WaitForSingleObject(thread, 10000)};
    ULONGLONG const took{GetTickCount64() - began};
    CloseHandle(thread);
    static char answer[64];
    if(waited != WAIT_OBJECT_0)
      return "NO (the thread did not finish in 10 s)";
    std::snprintf(answer, sizeof answer, took > 1000 ? "yes, slowly (%llu ms)" : "yes (%llu ms)",
                  static_cast<unsigned long long>(took));
    return answer;
    }

  bool lock_hung{};

  ///\brief a device on `adapter` (the default one for nullptr)
  [[nodiscard]]
  auto make(IDXGIAdapter * adapter, UINT flags, ID3D11Device *& device, ID3D11DeviceContext *& ctx,
            D3D_FEATURE_LEVEL & level) -> HRESULT
    {
    D3D_FEATURE_LEVEL const levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                     D3D_FEATURE_LEVEL_10_0};
    return create_device(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                         UINT(std::size(levels)), D3D11_SDK_VERSION, &device, &level, &ctx);
    }

  auto adapter_line(ID3D11Device * device) -> void
    {
    IDXGIDevice * dxgi{};
    IDXGIAdapter * a{};
    DXGI_ADAPTER_DESC d{};
    if(SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgi))) and dxgi
       and SUCCEEDED(dxgi->GetAdapter(&a)) and a and SUCCEEDED(a->GetDesc(&d)))
      say("adapter %s (vendor 0x%04X, device 0x%04X)", utf8(d.Description).c_str(), d.VendorId, d.DeviceId);
    if(a)
      a->Release();
    if(dxgi)
      dxgi->Release();
    }

  auto probe(IDXGIAdapter * adapter, UINT flags) -> void
    {
    ID3D11Device * device{};
    ID3D11DeviceContext * ctx{};
    D3D_FEATURE_LEVEL level{};
    HRESULT const hr{make(adapter, flags, device, ctx, level)};
    say("");
    say("device flags 0x%X: hr 0x%08lX, feature level 0x%X", flags, static_cast<unsigned long>(hr),
        static_cast<unsigned>(level));
    if(FAILED(hr) or not device or not ctx)
      {
      if(ctx)
        ctx->Release();
      if(device)
        device->Release();
      return;
      }
    adapter_line(device);
    table_facts("immediate context", ctx, *reinterpret_cast<void ***>(ctx), context_probe::context_methods);
    say("interfaces: ID3D11DeviceContext1 %s, 2 %s, 3 %s, 4 %s; ID3D11Multithread %s",
        same(ctx, __uuidof(ID3D11DeviceContext1)), same(ctx, __uuidof(ID3D11DeviceContext2)),
        same(ctx, __uuidof(ID3D11DeviceContext3)),
        same(ctx, __uuidof(ID3D11DeviceContext4)), same(ctx, __uuidof(ID3D11Multithread)));

    // the runtime's rewrites of the table: a snapshot around a few of its calls
    start = take(ctx);
    snapshot_t s{start};
    ID3D11Multithread * mt{};
    if(SUCCEEDED(ctx->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void **>(&mt))) and mt)
      {
      say("multithread protection %s at the start", mt->GetMultithreadProtected() ? "on" : "off");
      mt->SetMultithreadProtected(TRUE);
      s = compare(ctx, s, "multithread protection on");
      char const * const reentrant{lock_reentrant(ctx, mt)};
      lock_hung = reentrant[0] == 'N';
      say("  the runtime's lock taken twice by one thread, a method between: %s", reentrant);
      if(lock_hung)
        {
        mt->Release();
        return;  // the context is held by the hung thread: nothing more with it
        }
      mt->SetMultithreadProtected(FALSE);
      s = compare(ctx, s, "multithread protection off");
      mt->Release();
      }
    ctx->ClearState();
    ctx->Flush();
    s = compare(ctx, s, "ClearState and Flush");
    ID3D11DeviceContext * again{};
    device->GetImmediateContext(&again);
    say("  GetImmediateContext again: %s", again == ctx ? "the same object" : "ANOTHER object");
    if(again)
      again->Release();
    Sleep(2000);
    s = compare(ctx, s, "2 s");
    ID3D11DeviceContext * deferred{};
    if(SUCCEEDED(device->CreateDeferredContext(0, &deferred)) and deferred)
      {
      void ** const deferred_table{*reinterpret_cast<void ***>(deferred)};
      say("deferred context: its table %s the immediate context's",
          deferred_table == *reinterpret_cast<void ***>(ctx) ? "is" : "is not");
      table_facts("deferred context", deferred, deferred_table, context_probe::context_methods);
      deferred->Release();
      }
    (void)compare(ctx, s, "a deferred context made and released");
    ID3D11Multithread * mt2{};
    if(FAILED(ctx->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void **>(&mt2))))
      mt2 = nullptr;
    copy_test(ctx, mt2);
    if(mt2)
      mt2->Release();
    table_facts("device", device, *reinterpret_cast<void ***>(device), context_probe::device_methods);

    // a second device at once: its own table, at the same distance from its object?
    ID3D11Device * device2{};
    ID3D11DeviceContext * ctx2{};
    D3D_FEATURE_LEVEL level2{};
    if(SUCCEEDED(make(adapter, flags, device2, ctx2, level2)) and ctx2)
      {
      say("second device at once: its table %s the first's",
          *reinterpret_cast<void ***>(ctx2) == *reinterpret_cast<void ***>(ctx) ? "is" : "is not");
      table_facts("second immediate context", ctx2, *reinterpret_cast<void ***>(ctx2), context_probe::context_methods);
      }
    if(ctx2)
      ctx2->Release();
    if(device2)
      device2->Release();
    ctx->Release();
    device->Release();
    }

  struct hardware_t
    {
    UINT index;              ///< DXGI's
    IDXGIAdapter1 * adapter;  ///< a reference of its own
    };

  ///\brief the adapters DXGI lists, each told, the hardware ones kept for the probe; which one a program asking for
  /// high performance gets (a game on a laptop with two)
  [[nodiscard]]
  auto adapters() -> std::vector<hardware_t>
    {
    std::vector<hardware_t> hardware;
    using create_t = HRESULT(WINAPI *)(REFIID, void **);
    HMODULE const dxgi{LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
    auto const create{dxgi ? reinterpret_cast<create_t>(
                               reinterpret_cast<void *>(GetProcAddress(dxgi, "CreateDXGIFactory1")))
                           : nullptr};
    IDXGIFactory1 * factory{};
    if(not create or FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory))) or not factory)
      {
      say("adapters: dxgi.dll from the system folder not usable; the default adapter only");
      return hardware;
      }
    LUID fast{};
    bool fast_known{};
    IDXGIFactory6 * factory6{};
    if(SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory6), reinterpret_cast<void **>(&factory6))) and factory6)
      {
      IDXGIAdapter1 * a{};
      DXGI_ADAPTER_DESC1 d{};
      if(SUCCEEDED(factory6->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                        __uuidof(IDXGIAdapter1), reinterpret_cast<void **>(&a)))
         and a and SUCCEEDED(a->GetDesc1(&d)))
        {
        fast = d.AdapterLuid;
        fast_known = true;
        }
      if(a)
        a->Release();
      factory6->Release();
      }
    constexpr UINT most_adapters{64};
    for(UINT i{};; ++i)
      {
      if(i == most_adapters)
        {
        say("adapters: the first %u told, the rest not", most_adapters);
        break;
        }
      IDXGIAdapter1 * a{};
      HRESULT const listed{factory->EnumAdapters1(i, &a)};
      if(listed == DXGI_ERROR_NOT_FOUND)
        break;
      if(FAILED(listed))
        {
        say("adapters: listing stopped at %u (hr 0x%08lX)", i, static_cast<unsigned long>(listed));
        break;
        }
      DXGI_ADAPTER_DESC1 d{};
      if(not a or FAILED(a->GetDesc1(&d)))
        {
        if(a)
          a->Release();
        continue;
        }
      bool const software{(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0};
      bool const is_fast{fast_known and d.AdapterLuid.LowPart == fast.LowPart
                         and d.AdapterLuid.HighPart == fast.HighPart};
      say("adapter %u: %s (vendor 0x%04X, device 0x%04X), %llu MB own memory%s%s%s", i, utf8(d.Description).c_str(),
          d.VendorId, d.DeviceId, static_cast<unsigned long long>(d.DedicatedVideoMemory >> 20),
          i == 0 ? ", the default" : "",
          is_fast ? ", high performance" : "", software ? ", software (not probed)" : "");
      if(software)
        a->Release();
      else
        hardware.push_back({i, a});
      }
    if(not fast_known)
      say("adapters: which is the high performance one not told (no IDXGIFactory6)");
    factory->Release();
    return hardware;
    }

  [[nodiscard]]
  auto flag_sets() -> std::array<UINT, 3>
    {
    return {0u, UINT(D3D11_CREATE_DEVICE_BGRA_SUPPORT), UINT(D3D11_CREATE_DEVICE_SINGLETHREADED)};
    }

  ///\brief the probe itself, its lines in `out`
  auto run() -> void
    {
    say("context_probe 5");
    say("looked at in this program, without a window or a swap chain: an overlay that hooks only a game, or only at "
        "Present, is not seen here");
    windows_version();
    system_d3d11 = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    create_device = system_d3d11 ? reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(
                                     reinterpret_cast<void *>(GetProcAddress(system_d3d11, "D3D11CreateDevice")))
                                 : nullptr;
    if(not create_device)
      {
      say("d3d11.dll from the system folder: not loaded (error %lu)", GetLastError());
      return;
      }
    say("d3d11.dll from the system folder: yes, version %s", module_version(system_d3d11).c_str());
    std::vector<hardware_t> hardware{adapters()};
    if(hardware.empty())
      {
      say("adapters: no hardware one listed; the default adapter (D3D_DRIVER_TYPE_HARDWARE) probed");
      for(UINT const flags: flag_sets())
        if(not lock_hung)
          probe(nullptr, flags);
      return;
      }
    // each adapter's reference released even when a probe throws
    struct release_t
      {
      std::vector<hardware_t> & list;
      ~release_t()
        {
        for(hardware_t const & h: list)
          h.adapter->Release();
        }
      } const release{hardware};
    for(hardware_t const & h: hardware)
      {
      say("");
      if(lock_hung)
        say("=== adapter %u not probed: the runtime's lock hung before", h.index);
      else
        {
        say("=== adapter %u probed", h.index);
        for(UINT const flags: flag_sets())
          if(not lock_hung)
            probe(h.adapter, flags);
        }
      }
    // what a game gets that names no adapter: the card Windows and the driver's profile choose
    say("");
    if(lock_hung)
      say("=== the default device (no adapter given) not probed: the runtime's lock hung before");
    else
      {
      say("=== the default device (no adapter given) probed");
      probe(nullptr, 0);
      }
    }
  }  // namespace

auto main() -> int
  {
  // every dll loaded from here on from the system folder only (a d3d11.dll beside the exe would be a mod)
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
  // what was found until an exception is still written
  try
    {
    run();
    say("");
    modules_outside_windows();
    }
  catch(...)
    {
    say("");
    say("stopped: an exception (out of memory or the like); the lines above are what was found");
    }
  // beside the exe (a long path too)
  std::wstring const exe{edplugin_common::module_path(nullptr)};
  std::wstring const dir{edplugin_common::directory_of(exe)};
  if(not dir.empty() and edplugin_common::write_whole_file(dir + L"\\context_probe.txt", out.data(), out.size()))
    std::printf("\nwritten: context_probe.txt beside the exe\n");
  else
    std::printf("\ncould not write context_probe.txt beside the exe; the lines above are the result\n");
  std::printf("press Enter to close\n");
  (void)std::getchar();
  std::fflush(stdout);
  // a hung thread holds the runtime's lock: the process ends without the modules' detach, which could wait for it
  if(lock_hung)
    TerminateProcess(GetCurrentProcess(), 0);
  ExitProcess(0);
  }
