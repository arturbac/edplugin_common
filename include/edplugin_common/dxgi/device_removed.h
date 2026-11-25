// edplugin_common - a lost device told once in the log: the code a Present returned and the device's own reason
// (GetDeviceRemovedReason), the line flushed to the disk at once (log_flush), as the whole system may stop soon after
// (a GPU hang or reset). Once for the module: each plugin has its own. The names of the codes have no Windows header
// (checked natively); the calls are in src/device_removed.cc.
#pragma once

#include <cstdint>
#include <string_view>

struct IDXGISwapChain;
struct ID3D11Device;

namespace edplugin_common
  {
  ///\brief the name of a code a lost device gives (`DXGI_ERROR_DEVICE_HUNG`), `S_OK` for 0; empty for another
  [[nodiscard]]
  constexpr auto device_code_name(std::uint32_t code) noexcept -> std::string_view
    {
    switch(code)
      {
      case 0x00000000u: return "S_OK";
      case 0x887A0001u: return "DXGI_ERROR_INVALID_CALL";
      case 0x887A0005u: return "DXGI_ERROR_DEVICE_REMOVED";
      case 0x887A0006u: return "DXGI_ERROR_DEVICE_HUNG";
      case 0x887A0007u: return "DXGI_ERROR_DEVICE_RESET";
      case 0x887A0020u: return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
      case 0x8007000Eu: return "E_OUTOFMEMORY";
      default:          return {};
      }
    }

  ///\brief whether a Present's result tells the device is lost: DXGI_ERROR_DEVICE_REMOVED or DXGI_ERROR_DEVICE_RESET
  [[nodiscard]]
  constexpr auto present_lost(std::uint32_t code) noexcept -> bool
    {
    return code == 0x887A0005u or code == 0x887A0007u;
    }

  ///\brief after a Present or Present1 (`hr` its result): a failure's code (FAILED) reads the swap chain's device's
  /// reason (GetDevice and GetDeviceRemovedReason, only then); a lost device's code (present_lost) or a reason other
  /// than S_OK is logged with both, once for the module, the line flushed. A success code: nothing, no call
  auto note_present_result(IDXGISwapChain * chain, long hr) noexcept -> void;

  ///\brief the device's GetDeviceRemovedReason (nothing for nullptr): not S_OK is logged, once for the module, the line
  /// flushed. For a plugin without a Present of its own; its caller calls it at most once a second
  auto check_device_removed(ID3D11Device * device) noexcept -> void;

  ///\brief whether a lost device was logged by this module
  [[nodiscard]]
  auto device_loss_logged() noexcept -> bool;
  }  // namespace edplugin_common
