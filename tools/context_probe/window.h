// context_probe - a window that is never shown, for a swap chain: made without WS_VISIBLE and never given to
// ShowWindow, destroyed (and its class unregistered) with the object, on the thread that made it.
#pragma once

#include <windows.h>

namespace context_probe
  {
  class hidden_window_t
    {
  public:
    /// the window's client size, the swap chain's size too
    static constexpr int width{640};
    static constexpr int height{480};

    hidden_window_t() noexcept;
    ~hidden_window_t();
    hidden_window_t(hidden_window_t const &) = delete;
    auto operator=(hidden_window_t const &) -> hidden_window_t & = delete;

    ///\brief the window, nullptr when it could not be made (error() tells why)
    [[nodiscard]]
    auto handle() const noexcept -> HWND
      {
      return window_;
      }

    ///\brief GetLastError() of a window not made, 0 otherwise
    [[nodiscard]]
    auto error() const noexcept -> DWORD
      {
      return error_;
      }

    ///\brief the messages waiting for this thread's windows, handled without waiting for more
    auto pump() const noexcept -> void;

    ///\brief the window left as it is, neither destroyed nor its class unregistered (something may still use it)
    auto abandon() noexcept -> void
      {
      window_ = nullptr;
      class_ = 0;
      }

  private:
    ATOM class_{};
    HWND window_{};
    DWORD error_{};
    };
  }  // namespace context_probe
