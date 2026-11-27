// probe_plugin 1 - an edloader plugin for the plugins' tests that only watches (README.txt beside it): a d3d11.dll
// proxy on edworld's skeleton (src/proxy.cc there) that forwards every export to the next d3d11 in the chain and, at
// the first device made through it, puts context_probe's three plugins' hooks on the device's immediate context
// (tools/context_probe/hooks.h: hooks_attach) and starts a thread that reports every second what they counted
// (hooks_report). It draws nothing and sets no state of the context: of the device and the context it calls only
// GetImmediateContext and what the library asks, reads and references only (QueryInterface on both, AddRef, Release,
// GetMultithreadProtected at the attach and from the thread of reports every second).
//
// The chaining discipline is edworld's (and EDVR's): DllMain loads only the system copy (already mapped, no foreign
// DllMain under the loader lock); the next d3d11 is loaded on the first export call: under edloader "d3d11.dll" by
// name, edloader, which passes the call down its list; without edloader the system copy. A d3d11 after it that asks
// for "d3d11.dll" by name (a chained 3Dmigoto) reaches this proxy again: the second entry on the same thread goes to
// the system copy. The proxies before it on the stack at the first device are said, a 3Dmigoto among them told by its
// version resource; it is not refused. Which object got the hooks is said: the runtime's context (in the system
// d3d11) or a wrapper's (3Dmigoto's).
//
// The lines go to probe_plugin-<date>-<time>.txt in edloader's folder of logs (EDLOADER_LOG_DIR), or beside the dll
// without edloader (-2 and on for a name taken); each line with its UTC time, appended at once (a run that hangs keeps
// its lines). The library's own lines (the hook module's details) go to probe_plugin.log beside it; no result file is
// said by OutputDebugString.
#include "../context_probe/hooks.h"
#include "probe_proxy.h"

#include <edplugin_common/log/log.h>
#include <edplugin_common/memory/memory.h>
#include <edplugin_common/paths/paths.h>

#include <windows.h>
#include <d3d11.h>
#include <winver.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
  {
  using create_device_fn = HRESULT(WINAPI *)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                             D3D_FEATURE_LEVEL const *, UINT, UINT, ID3D11Device **,
                                             D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
  using create_device_swap_fn = HRESULT(WINAPI *)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                                  D3D_FEATURE_LEVEL const *, UINT, UINT, DXGI_SWAP_CHAIN_DESC const *,
                                                  IDXGISwapChain **, ID3D11Device **, D3D_FEATURE_LEVEL *,
                                                  ID3D11DeviceContext **);

  HMODULE self_module{};
  HMODULE system_module{};
  create_device_fn system_create{};
  create_device_swap_fn system_create_swap{};
  create_device_fn next_create{};
  create_device_swap_fn next_create_swap{};

  /// the result file, set once at the first export call (before any line), empty when none could be named
  std::wstring result_path;
  /// one line at a time: the game's thread (the device) and the thread of reports write
  SRWLOCK say_lock = SRWLOCK_INIT;

  ///\brief a line of the result, appended at once with its UTC time (edplugin_common::log_line_to)
  template<typename... args_t>
  auto say(std::format_string<args_t...> fmt, args_t &&... args) noexcept -> void
    {
    if(result_path.empty())
      return;
    AcquireSRWLockExclusive(&say_lock);
    edplugin_common::log_line_to(result_path, fmt, std::forward<args_t>(args)...);
    ReleaseSRWLockExclusive(&say_lock);
    }

  ///\brief context_probe's lines (hooks.h)
  auto sink(char const * line) -> void
    {
    say("{}", line);
    }

  ///\brief a module's file name without its folder (the process's exe for nullptr, "?" when it cannot be told)
  [[nodiscard]]
  auto module_name(HMODULE module) -> std::string
    {
    wchar_t path[MAX_PATH]{};
    DWORD const n{GetModuleFileNameW(module, path, MAX_PATH)};
    if(n == 0 or n >= MAX_PATH)
      return "?";
    std::wstring_view const p{path, n};
    std::size_t const slash{p.find_last_of(L"\\/")};
    std::wstring_view const name{slash == std::wstring_view::npos ? p : p.substr(slash + 1)};
    char text[MAX_PATH * 3]{};
    if(not WideCharToMultiByte(CP_UTF8, 0, name.data(), int(name.size()), text, int(sizeof text) - 1, nullptr, nullptr))
      return "?";
    return text;
    }

  auto resolve_from(HMODULE preferred, HMODULE fallback) noexcept -> std::size_t
    {
    std::size_t missing{};
    for(std::size_t i{}; i != probe_proxy_count; ++i)
      {
      void * p{preferred ? reinterpret_cast<void *>(GetProcAddress(preferred, probe_proxy_names[i])) : nullptr};
      if(not p and fallback)
        p = reinterpret_cast<void *>(GetProcAddress(fallback, probe_proxy_names[i]));
      if(not p)
        {
        ++missing;
        p = reinterpret_cast<void *>(&probe_proxy_unresolved);
        }
      probe_proxy_real_procs[i] = p;
      }
    return missing;
    }

  ///\brief in DllMain: the system copy only (already mapped), every export resolved to it until chain_next
  auto loader_phase() noexcept -> void
    {
    wchar_t system[MAX_PATH]{};
    UINT const length{GetSystemDirectoryW(system, MAX_PATH)};
    if(length == 0 or length + 11 >= MAX_PATH)
      return;
    std::wmemcpy(system + length, L"\\d3d11.dll", 11);
    system_module = LoadLibraryW(system);
    resolve_from(system_module, nullptr);
    if(system_module)
      {
      system_create = reinterpret_cast<create_device_fn>(GetProcAddress(system_module, "D3D11CreateDevice"));
      system_create_swap
        = reinterpret_cast<create_device_swap_fn>(GetProcAddress(system_module, "D3D11CreateDeviceAndSwapChain"));
      }
    next_create = system_create;
    next_create_swap = system_create_swap;
    }

  ///\brief the result file's path: edloader's folder of logs (the dll's own without it), a name by the local time of
  /// the start, -2 and on when a file of that name is there already; empty when the folder cannot be told
  [[nodiscard]]
  auto result_file() -> std::wstring
    {
    std::wstring dir{edplugin_common::environment(edplugin_common::edloader_log_dir_variable)};
    if(dir.empty())
      dir = edplugin_common::directory_of(edplugin_common::module_path(self_module));
    if(dir.empty())
      return {};
    SYSTEMTIME t{};
    GetLocalTime(&t);
    for(int n{1}; n != 10; ++n)
      {
      wchar_t name[64]{};
      if(n > 1)
        std::swprintf(name, 64, L"\\probe_plugin-%04u%02u%02u-%02u%02u%02u-%d.txt", unsigned(t.wYear),
                      unsigned(t.wMonth), unsigned(t.wDay), unsigned(t.wHour), unsigned(t.wMinute),
                      unsigned(t.wSecond), n);
      else
        std::swprintf(name, 64, L"\\probe_plugin-%04u%02u%02u-%02u%02u%02u.txt", unsigned(t.wYear), unsigned(t.wMonth),
                      unsigned(t.wDay), unsigned(t.wHour), unsigned(t.wMinute), unsigned(t.wSecond));
      std::wstring path{dir + name};
      if(GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        return path;
      }
    return {};
    }

  ///\brief the next d3d11: under edloader (its variable set) "d3d11.dll" by name, edloader; else the system copy
  auto chain_next() -> void
    {
    if(edplugin_common::environment(edplugin_common::edloader_config_dir_variable).empty())
      {
      say("chain: no edloader (EDLOADER_CONFIG_DIR not set): next is the system d3d11.dll");
      return;
      }
    HMODULE const chained{LoadLibraryW(L"d3d11.dll")};
    if(not chained)
      {
      say("chain: d3d11.dll by name not loaded (error {}); next is the system d3d11.dll", GetLastError());
      return;
      }
    if(chained == self_module)
      {
      say("chain: d3d11.dll by name is probe_plugin itself (no edloader before it); next is the system d3d11.dll");
      FreeLibrary(chained);
      return;
      }
    auto const create{reinterpret_cast<create_device_fn>(GetProcAddress(chained, "D3D11CreateDevice"))};
    if(not create)
      {
      say("chain: {} exports no D3D11CreateDevice; next is the system d3d11.dll", module_name(chained));
      FreeLibrary(chained);
      return;
      }
    std::size_t const missing{resolve_from(chained, system_module)};
    next_create = create;
    if(auto const swap{
         reinterpret_cast<create_device_swap_fn>(GetProcAddress(chained, "D3D11CreateDeviceAndSwapChain"))})
      next_create_swap = swap;
    say("chain: next is {} by name ({}), {} export(s) unresolved (taken from the system copy)", module_name(chained),
        chained == system_module ? "the system copy" : "not the system copy: edloader", missing);
    }

  INIT_ONCE init_once = INIT_ONCE_STATIC_INIT;

  BOOL CALLBACK init_callback(PINIT_ONCE, PVOID, PVOID *)
    {
    // the strings here may throw (out of memory): caught, the calls go on to the system copy
    try
      {
      result_path = result_file();
      if(result_path.empty())
        OutputDebugStringW(L"probe_plugin: no result file (no folder of logs, or 9 of this second's names taken)");
      // the library's own lines (the hook module's details) in probe_plugin.log beside the result
      std::wstring const dir{edplugin_common::directory_of(result_path)};
      if(not dir.empty())
        (void)edplugin_common::log_open(dir, L"probe_plugin");
      say("probe_plugin 1, commit {}, read-only observer of the immediate context", PROBE_PLUGIN_VERSION_COMMIT);
      say("process: {}", module_name(nullptr));
      chain_next();
      }
    catch(...)
      {
      OutputDebugStringW(L"probe_plugin: an exception at the start (out of memory); calls go to the system copy");
      say("start: an exception (out of memory); calls go to the system copy");
      }
    return TRUE;
    }

  auto ensure_initialised() noexcept -> void
    {
    InitOnceExecuteOnce(&init_once, init_callback, nullptr, nullptr);
    }

  thread_local int create_depth{};

  struct depth_guard_t
    {
    depth_guard_t() noexcept { ++create_depth; }

    ~depth_guard_t() { --create_depth; }

    depth_guard_t(depth_guard_t const &) = delete;
    auto operator=(depth_guard_t const &) -> depth_guard_t & = delete;

    [[nodiscard]]
    auto reentrant() const noexcept -> bool
      {
      return create_depth > 1;
      }
    };

  std::atomic<bool> loop_reported{};

  ///\brief a string of a module's version resource (`ProductName`, `FileDescription`), its first language's, as
  /// UTF-8; empty when the file has none
  [[nodiscard]]
  auto version_string(wchar_t const * path, wchar_t const * key) -> std::string
    {
    DWORD ignored{};
    DWORD const size{GetFileVersionInfoSizeW(path, &ignored)};
    if(size == 0 or size > (1u << 20))
      return {};
    std::vector<char> data(size);
    if(not GetFileVersionInfoW(path, 0, size, data.data()))
      return {};
    struct
      {
      WORD language, code_page;
      } * translation{};
    UINT n{};
    if(not VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void **>(&translation), &n)
       or n < sizeof *translation)
      return {};
    wchar_t query[96]{};
    std::swprintf(query, 96, L"\\StringFileInfo\\%04x%04x\\%ls", unsigned(translation->language),
                  unsigned(translation->code_page), key);
    wchar_t * value{};
    char text[256]{};
    if(VerQueryValueW(data.data(), query, reinterpret_cast<void **>(&value), &n) and value and n)
      WideCharToMultiByte(CP_UTF8, 0, value, -1, text, int(sizeof text) - 1, nullptr, nullptr);
    return text;
    }

  ///\brief the text says "3Dmigoto", in any case
  [[nodiscard]]
  auto names_3dmigoto(std::string text) -> bool
    {
    for(char & c : text)
      if(c >= 'A' and c <= 'Z')
        c = char(c - 'A' + 'a');
    return text.find("3dmigoto") != std::string::npos;
    }

  ///\brief the d3d11 proxies before probe_plugin on the stack (each called down the chain to it: edloader, mods), a
  /// line each with its product and description, a 3Dmigoto among them told; once, at the first device
  auto note_proxies_before() -> void
    {
    void * frames[48]{};
    USHORT const n{RtlCaptureStackBackTrace(1, DWORD(std::size(frames)), frames, nullptr)};
    HMODULE said[16]{};
    std::size_t said_count{};
    for(USHORT i{}; i != n and said_count != std::size(said); ++i)
      {
      HMODULE const m{edplugin_common::module_of(frames[i])};
      if(not m or m == self_module or m == system_module or not GetProcAddress(m, "D3D11CreateDevice")
         or std::find(said, said + said_count, m) != said + said_count)
        continue;
      said[said_count++] = m;
      wchar_t path[MAX_PATH]{};
      if(not GetModuleFileNameW(m, path, MAX_PATH))
        continue;
      std::string const product{version_string(path, L"ProductName")};
      std::string const description{version_string(path, L"FileDescription")};
      std::string const version{version_string(path, L"FileVersion")};
      bool const migoto{names_3dmigoto(product) or names_3dmigoto(description)};
      say("chain: before probe_plugin: {} (product \"{}\", \"{}\", version \"{}\"){}", module_name(m), product,
          description, version, migoto ? " - 3Dmigoto" : "");
      }
    if(said_count == 0)
      say("chain: no d3d11 proxy before probe_plugin on the stack");
    }

  ///\brief who asks for a device: the first module on the stack that is neither probe_plugin nor a d3d11 proxy (one
  /// that exports D3D11CreateDevice); "-" when none is found (code outside any module only)
  [[nodiscard]]
  auto asked_by() -> std::string
    {
    void * frames[48]{};
    USHORT const n{RtlCaptureStackBackTrace(2, DWORD(std::size(frames)), frames, nullptr)};
    for(USHORT i{}; i != n; ++i)
      {
      HMODULE const m{edplugin_common::module_of(frames[i])};
      if(m and m != self_module and not GetProcAddress(m, "D3D11CreateDevice"))
        return module_name(m);
      }
    return "-";
    }

  std::atomic<bool> attached{};

  ///\brief every second, until a fault: what the hooks counted since the last report (hooks_report)
  auto WINAPI report_main(LPVOID arg) noexcept -> DWORD
    {
    auto * const run{static_cast<context_probe::hooks_run_t *>(arg)};
    for(unsigned n{1};; ++n)
      {
      Sleep(1000);
      char step[32]{};
      std::snprintf(step, sizeof step, "report %u", n);
      DWORD fault{};
      if(not context_probe::hooks_report_caught(run, step, &sink, fault))
        {
        say("report: a fault 0x{:08X} in {}; the reports stop", static_cast<unsigned long>(fault), step);
        return 0;
        }
      }
    }

  ///\brief the first device made through probe_plugin: which object it is (the runtime's or a wrapper's), the hooks
  /// on its immediate context and the thread of reports; a later device is said and left alone
  auto attach(ID3D11Device * device) -> void
    {
    if(attached.exchange(true))
      {
      say("device: another one made, not watched (only the first)");
      return;
      }
    HMODULE const device_module{edplugin_common::module_of(*reinterpret_cast<void **>(device))};
    ID3D11DeviceContext * context{};
    device->GetImmediateContext(&context);  // the reference kept to the end of the process (the hooks' contract c)
    if(not context)
      {
      say("device: no immediate context; nothing watched");
      return;
      }
    HMODULE const context_module{edplugin_common::module_of(*reinterpret_cast<void **>(context))};
    say("device: its method table in {}, its immediate context's in {}: {}", module_name(device_module),
        module_name(context_module),
        context_module == system_module ? "the runtime's context (the system d3d11)" : "a wrapper's context");
    context_probe::hooks_run_t * run{};
    DWORD fault{};
    if(not context_probe::hooks_attach_caught(device, context, &sink, run, fault))
      {
      say("hooks: a fault 0x{:08X} in the attach; nothing watched", static_cast<unsigned long>(fault));
      return;
      }
    if(not run)
      {
      say("hooks: not attached (the context's table holds an earlier device's plugin hooks)");
      return;
      }
    HANDLE const thread{CreateThread(nullptr, 0, &report_main, run, 0, nullptr)};
    if(not thread)
      {
      say("report: the thread not made (error {}); no reports", GetLastError());
      return;
      }
    CloseHandle(thread);
    say("report: every second from now");
    }

  ///\brief after a device's creation: said (the first 8), the first made one attached
  auto after_create(char const * what, HRESULT hr, ID3D11Device ** device, bool nested) noexcept -> void
    {
    static std::atomic<int> told{};
    bool const made{SUCCEEDED(hr) and device and *device};
    try
      {
      if(told++ < 8)
        say("{}: hr 0x{:08X}, device {}{}, asked by {}", what, static_cast<unsigned long>(hr),
            made           ? "made"
            : device       ? "asked, none made"
                           : "not asked",
            nested ? " (nested: the system copy, not watched)" : "", asked_by());
      if(made and not nested)
        attach(*device);
      }
    catch(...)
      {
      say("device: an exception (out of memory); not watched");
      }
    }

  ///\brief before the first creation that is not nested: the proxies before probe_plugin
  auto before_create(bool nested) noexcept -> void
    {
    static std::atomic<bool> noted{};
    if(nested or noted.exchange(true))
      return;
    try
      {
      note_proxies_before();
      }
    catch(...)
      {
      say("chain: the proxies before not told (an exception)");
      }
    }
  }  // namespace

// the next d3d11 is another module's function: its type is not checked (UBSan's function check), as hooks.cc's
// originals
extern "C" [[clang::no_sanitize("function")]] HRESULT WINAPI probe_proxy_impl_D3D11CreateDevice(
  IDXGIAdapter * adapter, D3D_DRIVER_TYPE driver_type, HMODULE software, UINT flags, D3D_FEATURE_LEVEL const * levels,
  UINT level_count, UINT sdk, ID3D11Device ** device, D3D_FEATURE_LEVEL * level, ID3D11DeviceContext ** context)
  {
  ensure_initialised();
  depth_guard_t const depth;
  create_device_fn target{next_create};
  if(depth.reentrant())
    {
    if(not loop_reported.exchange(true))
      say("chain: the next proxy called d3d11.dll by name, routed to the system copy");
    target = system_create;
    }
  if(not target)
    return E_FAIL;
  before_create(depth.reentrant());
  HRESULT const hr{target(adapter, driver_type, software, flags, levels, level_count, sdk, device, level, context)};
  after_create("D3D11CreateDevice", hr, device, depth.reentrant());
  return hr;
  }

extern "C" [[clang::no_sanitize("function")]] HRESULT WINAPI
  probe_proxy_impl_D3D11CreateDeviceAndSwapChain(
  IDXGIAdapter * adapter, D3D_DRIVER_TYPE driver_type, HMODULE software, UINT flags, D3D_FEATURE_LEVEL const * levels,
  UINT level_count, UINT sdk, DXGI_SWAP_CHAIN_DESC const * swap_desc, IDXGISwapChain ** swap, ID3D11Device ** device,
  D3D_FEATURE_LEVEL * level, ID3D11DeviceContext ** context)
  {
  ensure_initialised();
  depth_guard_t const depth;
  create_device_swap_fn const target{depth.reentrant() ? system_create_swap : next_create_swap};
  if(not target)
    return E_FAIL;
  before_create(depth.reentrant());
  HRESULT const hr{
    target(adapter, driver_type, software, flags, levels, level_count, sdk, swap_desc, swap, device, level, context)};
  after_create("D3D11CreateDeviceAndSwapChain", hr, device, depth.reentrant());
  return hr;
  }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
  {
  if(reason == DLL_PROCESS_ATTACH)
    {
    self_module = instance;
    DisableThreadLibraryCalls(instance);
    loader_phase();
    }
  return TRUE;
  }
