// edplugin_common - the runtime's multithread protection, only told, never set and never taken: the context's hooks
// work only while it is off (contract g), and the plugin turns itself off (D3) when refresh() says it is on. The
// ID3D11Multithread comes from the context, else from the device; a device without one has the protection off. Its
// reference is kept to the end of the process (contract c). The state is an atomic flag: refresh() from the plugin's
// frame (or once a second) and from its census, on() from any thread.
#pragma once

#include <windows.h>
#include <d3d11_4.h>

#include <atomic>
#include <cstdint>

namespace edplugin_common::hook
  {
  enum struct multithread_start_e : std::uint8_t
    {
    off,           ///< asked: off
    on,            ///< asked: on (the plugin turns itself off)
    no_interface,  ///< neither the context nor the device gives an ID3D11Multithread: taken as off
    no_object,     ///< no context, or already started
    };

  ///\brief what refresh() saw
  struct multithread_state_t
    {
    bool on{};       ///< the protection is on now
    bool changed{};  ///< it differs from the last look (start() or refresh()); exactly one caller sees a change

    [[nodiscard]]
    auto turns_off() const noexcept -> bool
      {
      return on;
      }
    };

  class multithread_t
    {
  public:
    ///\brief the ID3D11Multithread of `context`, else of `device` (may be null), asked once and kept; a second start is
    /// refused (no_object)
    [[nodiscard]]
    auto start(ID3D11DeviceContext * context, ID3D11Device * device) noexcept -> multithread_start_e;

    ///\brief the protection asked again (off when there is no ID3D11Multithread or before start()); from any thread
    [[nodiscard]]
    auto refresh() noexcept -> multithread_state_t;

    ///\brief the state of the last look, not asked again
    [[nodiscard]]
    auto on() const noexcept -> bool
      {
      return on_.load(std::memory_order_acquire);
      }

    ///\brief the state start() saw
    [[nodiscard]]
    auto on_at_start() const noexcept -> bool
      {
      return on_at_start_.load(std::memory_order_acquire);
      }

  private:
    std::atomic<ID3D11Multithread *> interface_{};
    std::atomic<bool> started_{};
    std::atomic<bool> on_{};
    std::atomic<bool> on_at_start_{};
    };
  }  // namespace edplugin_common::hook
