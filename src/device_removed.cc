// edplugin_common - a lost device told once in the log (include/edplugin_common/dxgi/device_removed.h).
#include <edplugin_common/dxgi/device_removed.h>
#include <edplugin_common/log/log.h>
#include <edplugin_common/text/format.h>

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <atomic>

namespace edplugin_common
  {
  namespace
    {
    constinit std::atomic<bool> logged{};

    ///\brief a code's name for the log, "unknown" for one without
    [[nodiscard]]
    auto name_of(std::uint32_t code) noexcept -> std::string_view
      {
      std::string_view const name{device_code_name(code)};
      return name.empty() ? std::string_view{"unknown"} : name;
      }

    ///\brief the line flushed; a failed flush said to a debugger (the log is where it would be said)
    auto flush() noexcept -> void
      {
      if(not log_flush())
        {
        wchar_t text[96];
        (void)try_format_into(text, L"edplugin_common: the lost device's line not flushed (error {})\n", GetLastError());
        OutputDebugStringW(text);
        }
      }

    ///\brief the first to log the loss in this module
    [[nodiscard]]
    auto first() noexcept -> bool
      {
      return not logged.load(std::memory_order_relaxed) and not logged.exchange(true, std::memory_order_relaxed);
      }
    }  // namespace

  auto note_present_result(IDXGISwapChain * chain, long hr) noexcept -> void
    {
    std::uint32_t const code{static_cast<std::uint32_t>(hr)};
    if(not FAILED(hr) or logged.load(std::memory_order_relaxed))
      return;
    ID3D11Device * device{};
    if(chain != nullptr and FAILED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void **>(&device))))
      device = nullptr;
    bool const known{device != nullptr};
    std::uint32_t reason{};
    if(known)
      {
      reason = static_cast<std::uint32_t>(device->GetDeviceRemovedReason());
      device->Release();
      }
    if((not present_lost(code) and reason == 0u) or not first())
      return;
    if(known)
      log_line("the device is lost: Present returned 0x{:08X} ({}), its reason 0x{:08X} ({})", code, name_of(code), reason,
               name_of(reason));
    else
      log_line("the device is lost: Present returned 0x{:08X} ({}); its reason unknown (no device from the swap chain)",
               code, name_of(code));
    flush();
    }

  auto check_device_removed(ID3D11Device * device) noexcept -> void
    {
    if(device == nullptr or logged.load(std::memory_order_relaxed))
      return;
    std::uint32_t const reason{static_cast<std::uint32_t>(device->GetDeviceRemovedReason())};
    if(reason == 0u or not first())
      return;
    log_line("the device is lost: its reason 0x{:08X} ({})", reason, name_of(reason));
    flush();
    }

  auto device_loss_logged() noexcept -> bool { return logged.load(std::memory_order_relaxed); }
  }  // namespace edplugin_common
