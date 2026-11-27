// fake_ed 1 - a program in the game's place for the plugins' tests (README.txt beside it): it imports
// D3D11CreateDevice from d3d11.dll as Elite Dangerous does, so Windows loads the d3d11.dll beside the exe (edloader)
// and edloader the plugins of its list. It makes the device as the game does in the logs known (D3D11CreateDevice twice
// with no adapter given, hardware, no flags: first without a device, then with one), a swap chain on a window shown on
// the screen, and runs phases of frames, each a few seconds, as stimuli for a plugin that watches the context
// (tools/probe_plugin): without the multithread protection, with it, after it is turned off, with ClearState and Flush
// in each frame, after a ResizeBuffers. A frame is IASetPrimitiveTopology, a Draw of no points (nothing is drawn),
// ClearState and Flush by the phase, and Present(0, 0). Each line goes at once to fake_ed-<date>-<time>.txt beside the
// exe (a file of its own for each run; the lines' times in UTC, as the plugins' logs have them) and to the console; a
// plugin that hangs or ends the process leaves the lines before it. A watch thread ends the process when a call does
// not come back in 20 s (said). At the end 2 s for a plugin's last report, then the process ends without the modules'
// detach, as context_probe. Nothing is sent anywhere; no user or computer name and no path in the output. The exit
// code: 0 when every phase ran, 1 otherwise, 2 after a hang.
#include <edplugin_common/memory/memory.h>
#include <edplugin_common/memory/pe.h>
#include <edplugin_common/paths/paths.h>

#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

namespace
  {
  /// the result file and the console: one line at a time (the watch thread writes too)
  SRWLOCK say_lock = SRWLOCK_INIT;
  HANDLE result{INVALID_HANDLE_VALUE};
  bool result_failed{};

  ///\brief a line of the result with its UTC time, written to the result file at once and to the console
  [[gnu::format(printf, 1, 2)]]
  auto say(char const * format, ...) -> void
    {
    SYSTEMTIME t{};
    GetSystemTime(&t);
    char line[1024]{};
    int const at{std::snprintf(line, sizeof line, "%02u:%02u:%02u.%03u ", unsigned(t.wHour), unsigned(t.wMinute),
                               unsigned(t.wSecond), unsigned(t.wMilliseconds))};
    va_list args;
    va_start(args, format);
    // 2 bytes kept for the line end
    std::size_t const room{sizeof line - 2 - std::size_t(at)};
    int const n{std::vsnprintf(line + at, room, format, args)};
    va_end(args);
    if(n >= int(room))
      std::memcpy(line + sizeof line - 2 - 7, " [cut]", 7);  // never silently shorter
    std::size_t const text{std::strlen(line)};
    std::memcpy(line + text, "\r\n", 3);
    DWORD const length{DWORD(text + 2)};
    AcquireSRWLockExclusive(&say_lock);
    std::fwrite(line, 1, length - 2, stdout);
    std::fputs("\n", stdout);
    if(result != INVALID_HANDLE_VALUE and not result_failed)
      {
      DWORD done{};
      if(not WriteFile(result, line, length, &done, nullptr) or done != length)
        {
        // the console still gets the lines; the end says the file is not whole
        result_failed = true;
        std::printf("the result file could not be written (error %lu): the lines from here are on the console only\n",
                    static_cast<unsigned long>(GetLastError()));
        }
      }
    ReleaseSRWLockExclusive(&say_lock);
    }

  ///\brief the result file closed; a line said later goes to the console only
  auto close_result() -> void
    {
    AcquireSRWLockExclusive(&say_lock);
    if(result != INVALID_HANDLE_VALUE)
      CloseHandle(result);
    result = INVALID_HANDLE_VALUE;
    ReleaseSRWLockExclusive(&say_lock);
    }

  [[nodiscard]]
  auto result_name(SYSTEMTIME const & t, int n) -> std::string
    {
    char name[64]{};
    if(n > 1)
      std::snprintf(name, sizeof name, "fake_ed-%04u%02u%02u-%02u%02u%02u-%d.txt", unsigned(t.wYear),
                    unsigned(t.wMonth), unsigned(t.wDay), unsigned(t.wHour), unsigned(t.wMinute), unsigned(t.wSecond),
                    n);
    else
      std::snprintf(name, sizeof name, "fake_ed-%04u%02u%02u-%02u%02u%02u.txt", unsigned(t.wYear), unsigned(t.wMonth),
                    unsigned(t.wDay), unsigned(t.wHour), unsigned(t.wMinute), unsigned(t.wSecond));
    return name;
    }

  ///\brief the result file beside the exe, new (CREATE_NEW: an earlier run's result is never replaced, also by a run
  /// at the same time), named by the run's start in local time; its name, or empty when none could be made (said on
  /// the console)
  [[nodiscard]]
  auto open_result() -> std::string
    {
    SYSTEMTIME began{};
    GetLocalTime(&began);
    std::wstring const dir{edplugin_common::directory_of(edplugin_common::module_path(nullptr))};
    if(dir.empty())
      {
      std::printf("no result file: the exe's folder not told; the lines go to the console only\n");
      return {};
      }
    for(int n{1}; n != 10; ++n)
      {
      std::string const name{result_name(began, n)};
      std::wstring const path{dir + L"\\" + std::wstring{name.begin(), name.end()}};  // ASCII only
      result = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
      if(result != INVALID_HANDLE_VALUE)
        return name;
      if(GetLastError() != ERROR_FILE_EXISTS)
        break;
      }
    std::printf("no result file beside the exe (error %lu): the lines go to the console only\n",
                static_cast<unsigned long>(GetLastError()));
    return {};
    }

  /// what the main thread does now and since when (GetTickCount64), for the watch thread
  std::atomic<char const *> doing{"the start"};
  std::atomic<ULONGLONG> doing_since{};
  /// how long a call may take before the watch thread ends the process
  constexpr ULONGLONG hang_ms{20000};

  auto now_doing(char const * what) noexcept -> void
    {
    doing.store(what);
    doing_since.store(GetTickCount64());
    }

  ///\brief a thread that ends the process, said, when the main thread's call does not come back in hang_ms: a plugin
  /// (or an overlay) that hangs in a hook leaves a line saying where, and the run's script goes on
  auto start_watch() -> void
    {
    now_doing("the start");
    HANDLE const thread{CreateThread(nullptr, 0,
                                     [](void *) -> DWORD
                                       {
                                       for(;;)
                                         {
                                         Sleep(1000);
                                         ULONGLONG const spent{GetTickCount64() - doing_since.load()};
                                         if(spent < hang_ms)
                                           continue;
                                         say("hang: %s did not come back in %llu s; the process ends (exit code 2)",
                                             doing.load(), spent / 1000);
                                         close_result();
                                         std::fflush(stdout);
                                         TerminateProcess(GetCurrentProcess(), 2);
                                         }
                                       },
                                     nullptr, 0, nullptr)};
    if(thread)
      CloseHandle(thread);
    else
      say("watch: not started (error %lu); a hang is not ended", static_cast<unsigned long>(GetLastError()));
    }

  template<typename com_t>
  struct ref_t
    {
    com_t * p{};

    ref_t() = default;
    ref_t(ref_t const &) = delete;
    auto operator=(ref_t const &) -> ref_t & = delete;

    ~ref_t()
      {
      reset();
      }

    [[nodiscard]]
    auto put() noexcept -> void **
      {
      return reinterpret_cast<void **>(&p);
      }

    auto reset() noexcept -> void
      {
      if(p)
        p->Release();
      p = nullptr;
      }
    };

  ///\brief a module's file name without its folder ("-" for nullptr, "?" when it cannot be told)
  [[nodiscard]]
  auto module_name(HMODULE module) -> std::string
    {
    if(not module)
      return "-";
    std::wstring const path{edplugin_common::module_path(module)};
    std::size_t const slash{path.find_last_of(L"\\/")};
    std::wstring const name{slash == std::wstring::npos ? path : path.substr(slash + 1)};
    int const n{WideCharToMultiByte(CP_UTF8, 0, name.c_str(), int(name.size()), nullptr, 0, nullptr, nullptr)};
    if(name.empty() or n <= 0)
      return "?";
    std::string text(std::size_t(n), '\0');
    if(WideCharToMultiByte(CP_UTF8, 0, name.c_str(), int(name.size()), text.data(), n, nullptr, nullptr) != n)
      return "?";
    return text;
    }

  ///\brief where a module lies, without its path: beside the exe, in the system folder, elsewhere
  [[nodiscard]]
  auto module_place(HMODULE module) -> char const *
    {
    if(not module)
      return "no module";
    std::wstring const dir{edplugin_common::directory_of(edplugin_common::module_path(module))};
    if(dir.empty())
      return "folder not told";
    if(_wcsicmp(dir.c_str(), edplugin_common::directory_of(edplugin_common::module_path(nullptr)).c_str()) == 0)
      return "beside the exe";
    wchar_t system[MAX_PATH]{};
    UINT const length{GetSystemDirectoryW(system, MAX_PATH)};
    if(length != 0 and length < MAX_PATH and _wcsicmp(dir.c_str(), system) == 0)
      return "the system folder";
    return "another folder";
    }

  ///\brief which module the exe's import of D3D11CreateDevice from d3d11.dll leads to: edloader beside the exe, as
  /// with the game, or the system's d3d11 when no d3d11.dll lies beside it
  auto tell_import() -> void
    {
    std::optional<edplugin_common::pe_image_t> const image{edplugin_common::pe_image(GetModuleHandleW(nullptr))};
    if(not image)
      {
      say("import: the exe's image not read");
      return;
      }
    void * entry{};
    edplugin_common::import_scan_t const scan{
      edplugin_common::for_each_import(*image,
                                       [&entry](edplugin_common::import_t const & i) noexcept
                                         {
                                         if(i.slot and i.function == "D3D11CreateDevice" and i.dll.size() == 9
                                            and _strnicmp(i.dll.data(), "d3d11.dll", 9) == 0)
                                           entry = *i.slot;
                                         })};
    if(not entry)
      {
      say("import: D3D11CreateDevice from d3d11.dll not found in the exe's imports (%d descriptors, %s)",
          scan.descriptors, scan.malformed ? "the walk stopped at a malformed one" : "all read");
      return;
      }
    HMODULE const m{edplugin_common::module_of(entry)};
    say("import: D3D11CreateDevice leads to %s (%s)", module_name(m).c_str(), module_place(m));
    }

  [[nodiscard]]
  auto on_off(BOOL b) -> char const *
    {
    return b ? "on" : "off";
    }

  /// a window shown on the screen, for the swap chain; its client size, the swap chain's at first
  constexpr int window_width{640};
  constexpr int window_height{480};
  /// the size ResizeBuffers gives the swap chain's buffers (the window's stays)
  constexpr UINT resized_width{800};
  constexpr UINT resized_height{600};
  constexpr wchar_t window_class[]{L"fake_ed_window"};

  ///\brief the messages waiting for this thread's windows, handled without waiting for more
  auto pump() -> void
    {
    // a bound: a window that keeps posting to itself does not hold the frames here
    MSG m{};
    for(int i{}; i != 256 and PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE); ++i)
      {
      TranslateMessage(&m);
      DispatchMessageW(&m);
      }
    }

  ///\brief the window's messages: a close asked (its X) is said and refused, the window stays to the run's end (the
  /// swap chain is on it)
  auto CALLBACK window_procedure(HWND window, UINT message, WPARAM w, LPARAM l) -> LRESULT
    {
    if(message == WM_CLOSE)
      {
      say("window: a close asked, refused; the window stays to the end of the run");
      return 0;
      }
    return DefWindowProcW(window, message, w, l);
    }

  [[nodiscard]]
  auto make_window() -> HWND
    {
    WNDCLASSEXW c{};
    c.cbSize = sizeof c;
    c.lpfnWndProc = window_procedure;
    c.hInstance = GetModuleHandleW(nullptr);
    c.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    c.lpszClassName = window_class;
    if(not RegisterClassExW(&c))
      {
      say("window: class not registered (error %lu)", static_cast<unsigned long>(GetLastError()));
      return nullptr;
      }
    RECT r{0, 0, window_width, window_height};
    DWORD const style{WS_OVERLAPPEDWINDOW};
    if(not AdjustWindowRectEx(&r, style, FALSE, 0))
      r = {0, 0, window_width, window_height};
    HWND const window{CreateWindowExW(0, window_class, L"fake_ed", style, CW_USEDEFAULT, CW_USEDEFAULT,
                                      r.right - r.left, r.bottom - r.top, nullptr, nullptr, c.hInstance, nullptr)};
    if(not window)
      {
      say("window: not made (error %lu)", static_cast<unsigned long>(GetLastError()));
      UnregisterClassW(window_class, c.hInstance);
      return nullptr;
      }
    ShowWindow(window, SW_SHOWNORMAL);
    pump();
    return window;
    }

  enum struct protect_e
    {
    keep,
    off,
    on
    };

  struct phase_t
    {
    char const * name;
    protect_e protect;  ///< SetMultithreadProtected before the phase, or the protection left as it is
    bool clear_flush;   ///< ClearState and Flush in each frame
    bool resize;        ///< ResizeBuffers before the phase
    };

  constexpr phase_t phases[]{
    {"without the multithread protection", protect_e::off, false, false},
    {"with the multithread protection", protect_e::on, false, false},
    {"after the multithread protection turned off", protect_e::off, false, false},
    {"ClearState and Flush in each frame", protect_e::keep, true, false},
    {"after ResizeBuffers", protect_e::keep, false, true},
  };

  /// how long each phase draws frames
  constexpr ULONGLONG phase_ms{3000};

  struct counts_t
    {
    long frames;
    long topologies;
    long draws;
    long clear_states;
    long flushes;
    long presents;
    long presents_not_ok;  ///< Present's hr other than S_OK (a status such as occluded, or a failure)
    HRESULT first_present_not_ok;
    };

  ///\brief the phase's frames for phase_ms; false when a Present failed (the frames stop there, said)
  [[nodiscard]]
  auto frames(ID3D11DeviceContext * ctx, IDXGISwapChain1 * chain, phase_t const & phase, counts_t & c) -> bool
    {
    ULONGLONG const began{GetTickCount64()};
    while(GetTickCount64() - began < phase_ms)
      {
      now_doing("a frame's context calls");
      ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
      ++c.topologies;
      ctx->Draw(0, 0);  // no points: nothing is drawn
      ++c.draws;
      if(phase.clear_flush)
        {
        ctx->ClearState();
        ++c.clear_states;
        ctx->Flush();
        ++c.flushes;
        }
      now_doing("Present");
      HRESULT const hr{chain->Present(0, 0)};
      ++c.presents;
      ++c.frames;
      if(hr != S_OK)
        {
        if(c.presents_not_ok == 0)
          c.first_present_not_ok = hr;
        ++c.presents_not_ok;
        }
      if(FAILED(hr))
        {
        say("  Present failed: hr 0x%08lX; the frames stop", static_cast<unsigned long>(hr));
        return false;
        }
      now_doing("the window's messages");
      pump();
      // Sleep's granularity: about 60 frames a second under wine, about 30 on Windows' default timer
      Sleep(16);
      }
    return true;
    }

  ///\brief the phases one after another; false when one stopped early or could not start; `protection_changed`
  /// false when the context has no ID3D11Multithread (the protection never changed, the first three phases alike)
  [[nodiscard]]
  auto run_phases(ID3D11DeviceContext * ctx, IDXGISwapChain1 * chain, bool & protection_changed) -> bool
    {
    ref_t<ID3D11Multithread> multithread;
    now_doing("QueryInterface(ID3D11Multithread)");
    HRESULT const mt{ctx->QueryInterface(__uuidof(ID3D11Multithread), multithread.put())};
    protection_changed = SUCCEEDED(mt) and multithread.p;
    if(not protection_changed)
      say("ID3D11Multithread: none (hr 0x%08lX); the protection is not changed", static_cast<unsigned long>(mt));
    else
      say("ID3D11Multithread: the protection %s at the start", on_off(multithread.p->GetMultithreadProtected()));
    for(phase_t const & phase : phases)
      {
      say("");
      if(phase.protect != protect_e::keep and not multithread.p)
        say("SetMultithreadProtected: not called, no ID3D11Multithread");
      else if(phase.protect != protect_e::keep)
        {
        BOOL const on{phase.protect == protect_e::on ? TRUE : FALSE};
        now_doing("SetMultithreadProtected");
        BOOL const was{multithread.p->SetMultithreadProtected(on)};
        say("SetMultithreadProtected(%s): was %s, now %s", on ? "TRUE" : "FALSE", on_off(was),
            on_off(multithread.p->GetMultithreadProtected()));
        }
      if(phase.resize)
        {
        now_doing("ResizeBuffers");
        HRESULT const hr{chain->ResizeBuffers(0, resized_width, resized_height, DXGI_FORMAT_UNKNOWN, 0)};
        say("ResizeBuffers(0, %u, %u): hr 0x%08lX", resized_width, resized_height, static_cast<unsigned long>(hr));
        if(FAILED(hr))
          return false;
        }
      say("phase: %s, starts", phase.name);
      counts_t c{};
      bool const whole{frames(ctx, chain, phase, c)};
      say("phase: %s, ends: frames %ld, IASetPrimitiveTopology %ld, Draw %ld, ClearState %ld, Flush %ld, Present %ld "
          "(not S_OK %ld, the first 0x%08lX)",
          phase.name, c.frames, c.topologies, c.draws, c.clear_states, c.flushes, c.presents, c.presents_not_ok,
          static_cast<unsigned long>(c.first_present_not_ok));
      if(not whole)
        return false;
      }
    return true;
    }

  ///\brief a step's hr said; false when it failed or gave no object. `got` by reference: read here, after the call
  /// that fills it (the order of a call's arguments is not given)
  template<typename com_t>
  [[nodiscard]]
  auto said(char const * what, HRESULT hr, com_t * const & got) -> bool
    {
    say("%s: hr 0x%08lX%s", what, static_cast<unsigned long>(hr), SUCCEEDED(hr) and not got ? ", no object" : "");
    return SUCCEEDED(hr) and got;
    }

  ///\brief the device as the game makes it, its swap chain on `window` and the phases; false when a step failed
  [[nodiscard]]
  auto run(HWND window, bool & protection_changed) -> bool
    {
    // [W] the game's list of levels is not in its logs, only the level it got (12_0, no flags)
    constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    constexpr UINT level_count{UINT(sizeof levels / sizeof levels[0])};
    D3D_FEATURE_LEVEL level{};
    now_doing("D3D11CreateDevice");
    HRESULT hr{D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, level_count, D3D11_SDK_VERSION,
                                 nullptr, &level, nullptr)};
    say("D3D11CreateDevice without a device: hr 0x%08lX, feature level 0x%X", static_cast<unsigned long>(hr),
        unsigned(level));
    ref_t<ID3D11Device> device;
    ref_t<ID3D11DeviceContext> ctx;
    level = {};
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, level_count, D3D11_SDK_VERSION,
                           &device.p, &level, &ctx.p);
    say("D3D11CreateDevice with a device: hr 0x%08lX, feature level 0x%X", static_cast<unsigned long>(hr),
        unsigned(level));
    if(FAILED(hr) or not device.p or not ctx.p)
      return false;
    HMODULE const table_module{edplugin_common::module_of(*reinterpret_cast<void **>(device.p))};
    say("device: its method table in %s (%s)", module_name(table_module).c_str(), module_place(table_module));

    // the factory that made the device's adapter: a swap chain from another one is refused
    ref_t<IDXGIDevice> dxgi;
    ref_t<IDXGIAdapter> adapter;
    ref_t<IDXGIFactory2> factory;
    now_doing("the swap chain's factory");
    if(not said("QueryInterface(IDXGIDevice)", device.p->QueryInterface(__uuidof(IDXGIDevice), dxgi.put()), dxgi.p)
       or not said("IDXGIDevice::GetAdapter", dxgi.p->GetAdapter(&adapter.p), adapter.p)
       or not said("IDXGIAdapter::GetParent(IDXGIFactory2)",
                   adapter.p->GetParent(__uuidof(IDXGIFactory2), factory.put()), factory.p))
      {
      say("swap chain: not made");
      return false;
      }
    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = UINT(window_width);
    d.Height = UINT(window_height);
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.Scaling = DXGI_SCALING_STRETCH;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    d.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    ref_t<IDXGISwapChain1> chain;
    now_doing("CreateSwapChainForHwnd");
    hr = factory.p->CreateSwapChainForHwnd(device.p, window, &d, nullptr, nullptr, &chain.p);
    say("swap chain: flip discard, BGRA8, 2 buffers, %dx%d: hr 0x%08lX", window_width, window_height,
        static_cast<unsigned long>(hr));
    if(FAILED(hr) or not chain.p)
      {
      // Windows before 10 has no flip discard
      d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
      hr = factory.p->CreateSwapChainForHwnd(device.p, window, &d, nullptr, nullptr, &chain.p);
      say("swap chain: flip sequential: hr 0x%08lX", static_cast<unsigned long>(hr));
      if(FAILED(hr) or not chain.p)
        return false;
      }
    bool const whole{run_phases(ctx.p, chain.p, protection_changed)};
    if(not whole)
      {
      now_doing("GetDeviceRemovedReason");
      HRESULT const removed{device.p->GetDeviceRemovedReason()};
      say("the phases stopped early; GetDeviceRemovedReason: hr 0x%08lX", static_cast<unsigned long>(removed));
      }
    // a flip model swap chain goes only after the context's references to it are gone (ClearState, Flush), and the
    // plugins may keep the device and the context: so before its window
    say("");
    now_doing("ClearState and Flush before the swap chain's release");
    ctx.p->ClearState();
    ctx.p->Flush();
    chain.reset();
    say("end: ClearState and Flush (not a phase's), the swap chain released");
    return whole;
    }
  }  // namespace

auto main() -> int
  {
  std::string name;
  bool whole{};
  bool protection_changed{};
  // what was found until an exception is still written
  try
    {
    name = open_result();
    start_watch();
    say("fake_ed 1");
    tell_import();
    HWND const window{make_window()};
    if(window)
      whole = run(window, protection_changed);
    // on every way: a plugin that writes a report every second writes its last one
    say("end: 2 s for a plugin's last report");
    now_doing("the 2 s for a plugin's last report");
    for(ULONGLONG const began{GetTickCount64()}; GetTickCount64() - began < 2000;)
      {
      pump();
      Sleep(50);
      }
    if(window)
      {
      now_doing("DestroyWindow");
      if(not DestroyWindow(window))
        say("window: not destroyed (error %lu)", static_cast<unsigned long>(GetLastError()));
      pump();  // the destroy's own messages
      UnregisterClassW(window_class, GetModuleHandleW(nullptr));
      }
    if(not whole)
      say("done: not every phase ran");
    else if(not protection_changed)
      say("done: every phase ran; the protection never changed (no ID3D11Multithread)");
    else
      say("done: every phase ran");
    }
  catch(...)
    {
    say("stopped: an exception (out of memory or the like); the lines above are what was found");
    }
  bool const file_whole{not name.empty() and not result_failed};
  close_result();
  if(file_whole)
    std::printf("written: %s beside the exe\n", name.c_str());
  std::fflush(stdout);
  // the process ends without the modules' detach (as context_probe): a plugin's thread may hold the runtime's lock,
  // and the device the plugins keep is alive, so a detach could wait on them and hold the run's script
  TerminateProcess(GetCurrentProcess(), whole ? 0 : 1);
  return whole ? 0 : 1;
  }
