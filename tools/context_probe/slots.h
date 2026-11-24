// context_probe - the names of the methods in the D3D11 runtime's tables by slot, as the Windows SDK (10.0.26100.0)
// declares them: ID3D11Device5 (with IUnknown) and ID3D11DeviceContext4 (with IUnknown and ID3D11DeviceChild). No
// Windows header: built natively too (test/native).
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace context_probe
  {
  using namespace std::string_view_literals;

  inline constexpr std::array<std::string_view, 69> device_methods{
    "QueryInterface"sv, "AddRef"sv, "Release"sv, "CreateBuffer"sv, "CreateTexture1D"sv, "CreateTexture2D"sv,
    "CreateTexture3D"sv, "CreateShaderResourceView"sv, "CreateUnorderedAccessView"sv, "CreateRenderTargetView"sv,
    "CreateDepthStencilView"sv, "CreateInputLayout"sv, "CreateVertexShader"sv, "CreateGeometryShader"sv,
    "CreateGeometryShaderWithStreamOutput"sv, "CreatePixelShader"sv, "CreateHullShader"sv, "CreateDomainShader"sv,
    "CreateComputeShader"sv, "CreateClassLinkage"sv, "CreateBlendState"sv, "CreateDepthStencilState"sv,
    "CreateRasterizerState"sv, "CreateSamplerState"sv, "CreateQuery"sv, "CreatePredicate"sv, "CreateCounter"sv,
    "CreateDeferredContext"sv, "OpenSharedResource"sv, "CheckFormatSupport"sv, "CheckMultisampleQualityLevels"sv,
    "CheckCounterInfo"sv, "CheckCounter"sv, "CheckFeatureSupport"sv, "GetPrivateData"sv, "SetPrivateData"sv,
    "SetPrivateDataInterface"sv, "GetFeatureLevel"sv, "GetCreationFlags"sv, "GetDeviceRemovedReason"sv,
    "GetImmediateContext"sv, "SetExceptionMode"sv, "GetExceptionMode"sv, "GetImmediateContext1"sv,
    "CreateDeferredContext1"sv, "CreateBlendState1"sv, "CreateRasterizerState1"sv, "CreateDeviceContextState"sv,
    "OpenSharedResource1"sv, "OpenSharedResourceByName"sv, "GetImmediateContext2"sv, "CreateDeferredContext2"sv,
    "GetResourceTiling"sv, "CheckMultisampleQualityLevels1"sv, "CreateTexture2D1"sv, "CreateTexture3D1"sv,
    "CreateRasterizerState2"sv, "CreateShaderResourceView1"sv, "CreateUnorderedAccessView1"sv,
    "CreateRenderTargetView1"sv, "CreateQuery1"sv, "GetImmediateContext3"sv, "CreateDeferredContext3"sv,
    "WriteToSubresource"sv, "ReadFromSubresource"sv, "RegisterDeviceRemovedEvent"sv, "UnregisterDeviceRemoved"sv,
    "OpenSharedFence"sv, "CreateFence"sv};

  inline constexpr std::array<std::string_view, 149> context_methods{
    "QueryInterface"sv, "AddRef"sv, "Release"sv, "GetDevice"sv, "GetPrivateData"sv, "SetPrivateData"sv,
    "SetPrivateDataInterface"sv, "VSSetConstantBuffers"sv, "PSSetShaderResources"sv, "PSSetShader"sv, "PSSetSamplers"sv,
    "VSSetShader"sv, "DrawIndexed"sv, "Draw"sv, "Map"sv, "Unmap"sv, "PSSetConstantBuffers"sv, "IASetInputLayout"sv,
    "IASetVertexBuffers"sv, "IASetIndexBuffer"sv, "DrawIndexedInstanced"sv, "DrawInstanced"sv, "GSSetConstantBuffers"sv,
    "GSSetShader"sv, "IASetPrimitiveTopology"sv, "VSSetShaderResources"sv, "VSSetSamplers"sv, "Begin"sv, "End"sv,
    "GetData"sv, "SetPredication"sv, "GSSetShaderResources"sv, "GSSetSamplers"sv, "OMSetRenderTargets"sv,
    "OMSetRenderTargetsAndUnorderedAccessViews"sv, "OMSetBlendState"sv, "OMSetDepthStencilState"sv, "SOSetTargets"sv,
    "DrawAuto"sv, "DrawIndexedInstancedIndirect"sv, "DrawInstancedIndirect"sv, "Dispatch"sv, "DispatchIndirect"sv,
    "RSSetState"sv, "RSSetViewports"sv, "RSSetScissorRects"sv, "CopySubresourceRegion"sv, "CopyResource"sv,
    "UpdateSubresource"sv, "CopyStructureCount"sv, "ClearRenderTargetView"sv, "ClearUnorderedAccessViewUint"sv,
    "ClearUnorderedAccessViewFloat"sv, "ClearDepthStencilView"sv, "GenerateMips"sv, "SetResourceMinLOD"sv,
    "GetResourceMinLOD"sv, "ResolveSubresource"sv, "ExecuteCommandList"sv, "HSSetShaderResources"sv, "HSSetShader"sv,
    "HSSetSamplers"sv, "HSSetConstantBuffers"sv, "DSSetShaderResources"sv, "DSSetShader"sv, "DSSetSamplers"sv,
    "DSSetConstantBuffers"sv, "CSSetShaderResources"sv, "CSSetUnorderedAccessViews"sv, "CSSetShader"sv,
    "CSSetSamplers"sv, "CSSetConstantBuffers"sv, "VSGetConstantBuffers"sv, "PSGetShaderResources"sv, "PSGetShader"sv,
    "PSGetSamplers"sv, "VSGetShader"sv, "PSGetConstantBuffers"sv, "IAGetInputLayout"sv, "IAGetVertexBuffers"sv,
    "IAGetIndexBuffer"sv, "GSGetConstantBuffers"sv, "GSGetShader"sv, "IAGetPrimitiveTopology"sv,
    "VSGetShaderResources"sv, "VSGetSamplers"sv, "GetPredication"sv, "GSGetShaderResources"sv, "GSGetSamplers"sv,
    "OMGetRenderTargets"sv, "OMGetRenderTargetsAndUnorderedAccessViews"sv, "OMGetBlendState"sv,
    "OMGetDepthStencilState"sv, "SOGetTargets"sv, "RSGetState"sv, "RSGetViewports"sv, "RSGetScissorRects"sv,
    "HSGetShaderResources"sv, "HSGetShader"sv, "HSGetSamplers"sv, "HSGetConstantBuffers"sv, "DSGetShaderResources"sv,
    "DSGetShader"sv, "DSGetSamplers"sv, "DSGetConstantBuffers"sv, "CSGetShaderResources"sv,
    "CSGetUnorderedAccessViews"sv, "CSGetShader"sv, "CSGetSamplers"sv, "CSGetConstantBuffers"sv, "ClearState"sv,
    "Flush"sv, "GetType"sv, "GetContextFlags"sv, "FinishCommandList"sv, "CopySubresourceRegion1"sv,
    "UpdateSubresource1"sv, "DiscardResource"sv, "DiscardView"sv, "VSSetConstantBuffers1"sv, "HSSetConstantBuffers1"sv,
    "DSSetConstantBuffers1"sv, "GSSetConstantBuffers1"sv, "PSSetConstantBuffers1"sv, "CSSetConstantBuffers1"sv,
    "VSGetConstantBuffers1"sv, "HSGetConstantBuffers1"sv, "DSGetConstantBuffers1"sv, "GSGetConstantBuffers1"sv,
    "PSGetConstantBuffers1"sv, "CSGetConstantBuffers1"sv, "SwapDeviceContextState"sv, "ClearView"sv, "DiscardView1"sv,
    "UpdateTileMappings"sv, "CopyTileMappings"sv, "CopyTiles"sv, "UpdateTiles"sv, "ResizeTilePool"sv,
    "TiledResourceBarrier"sv, "IsAnnotationEnabled"sv, "SetMarkerInt"sv, "BeginEventInt"sv, "EndEvent"sv, "Flush1"sv,
    "SetHardwareProtectionState"sv, "GetHardwareProtectionState"sv, "Signal"sv, "Wait"sv};

  ///\brief a slot's method name in `names`, "?" past its end
  template<std::size_t n>
  [[nodiscard]]
  constexpr auto method_name(std::array<std::string_view, n> const & names, int slot) noexcept -> std::string_view
    {
    return slot >= 0 and static_cast<std::size_t>(slot) < n ? names[static_cast<std::size_t>(slot)] : "?"sv;
    }

  static_assert(method_name(device_methods, 5) == "CreateTexture2D"sv, "ID3D11Device: CreateTexture2D in slot 5");
  static_assert(method_name(context_methods, 7) == "VSSetConstantBuffers"sv, "the context's own methods from slot 7");
  static_assert(method_name(context_methods, 12) == "DrawIndexed"sv, "ID3D11DeviceContext: DrawIndexed in slot 12");
  static_assert(method_name(context_methods, 148) == "Wait"sv, "ID3D11DeviceContext4: Wait the last");
  static_assert(method_name(context_methods, 149) == "?"sv, "past the end");
  }  // namespace context_probe
