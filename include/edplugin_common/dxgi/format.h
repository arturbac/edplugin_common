// edplugin_common - rules on DXGI formats.
#pragma once

#include <dxgiformat.h>

namespace edplugin_common
  {
  ///\brief a format whose values are integers (UINT, SINT): a shader reads and writes it as uint or int, not as float
  [[nodiscard]]
  constexpr auto is_integer(DXGI_FORMAT f) noexcept -> bool
    {
    switch(f)
      {
      case DXGI_FORMAT_R32G32B32A32_UINT: case DXGI_FORMAT_R32G32B32A32_SINT: case DXGI_FORMAT_R32G32B32_UINT:
      case DXGI_FORMAT_R32G32B32_SINT: case DXGI_FORMAT_R16G16B16A16_UINT: case DXGI_FORMAT_R16G16B16A16_SINT:
      case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R32G32_SINT: case DXGI_FORMAT_R10G10B10A2_UINT:
      case DXGI_FORMAT_R8G8B8A8_UINT: case DXGI_FORMAT_R8G8B8A8_SINT: case DXGI_FORMAT_R16G16_UINT:
      case DXGI_FORMAT_R16G16_SINT: case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R32_SINT: case DXGI_FORMAT_R8G8_UINT:
      case DXGI_FORMAT_R8G8_SINT: case DXGI_FORMAT_R16_UINT: case DXGI_FORMAT_R16_SINT: case DXGI_FORMAT_R8_UINT:
      case DXGI_FORMAT_R8_SINT: case DXGI_FORMAT_X24_TYPELESS_G8_UINT: case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        return true;
      default:
        return false;
      }
    }

  static_assert(is_integer(DXGI_FORMAT_R32_UINT) and is_integer(DXGI_FORMAT_R8G8_SINT), "UINT and SINT are integers");
  static_assert(not is_integer(DXGI_FORMAT_R8G8B8A8_UNORM) and not is_integer(DXGI_FORMAT_R16G16B16A16_FLOAT),
                "UNORM and FLOAT are not");
  }  // namespace edplugin_common
