// context_probe - a window that is never shown (window.h).
#include "window.h"

namespace context_probe
  {
  namespace
    {
    constexpr wchar_t class_name[]{L"context_probe_hidden_window"};
    }  // namespace

  hidden_window_t::hidden_window_t() noexcept
    {
    WNDCLASSEXW c{};
    c.cbSize = sizeof c;
    c.lpfnWndProc = DefWindowProcW;
    c.hInstance = GetModuleHandleW(nullptr);
    c.lpszClassName = class_name;
    class_ = RegisterClassExW(&c);
    if(not class_)
      {
      error_ = GetLastError();
      return;
      }
    // the outer size for the client size the swap chain gets; no WS_VISIBLE and no ShowWindow: never on the screen
    RECT r{0, 0, width, height};
    DWORD const style{WS_OVERLAPPEDWINDOW};
    if(not AdjustWindowRectEx(&r, style, FALSE, 0))
      r = {0, 0, width, height};
    window_ = CreateWindowExW(0, class_name, L"context_probe", style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                              r.bottom - r.top, nullptr, nullptr, c.hInstance, nullptr);
    if(not window_)
      error_ = GetLastError();
    }

  hidden_window_t::~hidden_window_t()
    {
    if(window_)
      {
      DestroyWindow(window_);
      pump();  // the destroy's own messages
      }
    if(class_)
      UnregisterClassW(class_name, GetModuleHandleW(nullptr));
    }

  auto hidden_window_t::pump() const noexcept -> void
    {
    // a bound: a window that keeps posting to itself does not hold the probe here
    MSG m{};
    for(int i{}; i != 256 and PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE); ++i)
      {
      TranslateMessage(&m);
      DispatchMessageW(&m);
      }
    }
  }  // namespace context_probe
