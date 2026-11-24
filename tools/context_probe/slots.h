// context_probe - the names of the methods in the D3D11 runtime's tables by slot, as the Windows SDK (10.0.26100.0)
// declares them: ID3D11Device5 (with IUnknown) and ID3D11DeviceContext4 (with IUnknown and ID3D11DeviceChild), and
// the text of a set of slots. No Windows header: built natively too (test/native).
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace context_probe
  {
  inline constexpr std::array<std::string_view, 69> device_methods{
    "QueryInterface", "AddRef", "Release", "CreateBuffer", "CreateTexture1D", "CreateTexture2D",
    "CreateTexture3D", "CreateShaderResourceView", "CreateUnorderedAccessView", "CreateRenderTargetView",
    "CreateDepthStencilView", "CreateInputLayout", "CreateVertexShader", "CreateGeometryShader",
    "CreateGeometryShaderWithStreamOutput", "CreatePixelShader", "CreateHullShader", "CreateDomainShader",
    "CreateComputeShader", "CreateClassLinkage", "CreateBlendState", "CreateDepthStencilState",
    "CreateRasterizerState", "CreateSamplerState", "CreateQuery", "CreatePredicate", "CreateCounter",
    "CreateDeferredContext", "OpenSharedResource", "CheckFormatSupport", "CheckMultisampleQualityLevels",
    "CheckCounterInfo", "CheckCounter", "CheckFeatureSupport", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "GetFeatureLevel", "GetCreationFlags", "GetDeviceRemovedReason",
    "GetImmediateContext", "SetExceptionMode", "GetExceptionMode", "GetImmediateContext1",
    "CreateDeferredContext1", "CreateBlendState1", "CreateRasterizerState1", "CreateDeviceContextState",
    "OpenSharedResource1", "OpenSharedResourceByName", "GetImmediateContext2", "CreateDeferredContext2",
    "GetResourceTiling", "CheckMultisampleQualityLevels1", "CreateTexture2D1", "CreateTexture3D1",
    "CreateRasterizerState2", "CreateShaderResourceView1", "CreateUnorderedAccessView1",
    "CreateRenderTargetView1", "CreateQuery1", "GetImmediateContext3", "CreateDeferredContext3",
    "WriteToSubresource", "ReadFromSubresource", "RegisterDeviceRemovedEvent", "UnregisterDeviceRemoved",
    "OpenSharedFence", "CreateFence"};

  inline constexpr std::array<std::string_view, 149> context_methods{
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "VSSetConstantBuffers", "PSSetShaderResources", "PSSetShader", "PSSetSamplers",
    "VSSetShader", "DrawIndexed", "Draw", "Map", "Unmap", "PSSetConstantBuffers", "IASetInputLayout",
    "IASetVertexBuffers", "IASetIndexBuffer", "DrawIndexedInstanced", "DrawInstanced", "GSSetConstantBuffers",
    "GSSetShader", "IASetPrimitiveTopology", "VSSetShaderResources", "VSSetSamplers", "Begin", "End",
    "GetData", "SetPredication", "GSSetShaderResources", "GSSetSamplers", "OMSetRenderTargets",
    "OMSetRenderTargetsAndUnorderedAccessViews", "OMSetBlendState", "OMSetDepthStencilState", "SOSetTargets",
    "DrawAuto", "DrawIndexedInstancedIndirect", "DrawInstancedIndirect", "Dispatch", "DispatchIndirect",
    "RSSetState", "RSSetViewports", "RSSetScissorRects", "CopySubresourceRegion", "CopyResource",
    "UpdateSubresource", "CopyStructureCount", "ClearRenderTargetView", "ClearUnorderedAccessViewUint",
    "ClearUnorderedAccessViewFloat", "ClearDepthStencilView", "GenerateMips", "SetResourceMinLOD",
    "GetResourceMinLOD", "ResolveSubresource", "ExecuteCommandList", "HSSetShaderResources", "HSSetShader",
    "HSSetSamplers", "HSSetConstantBuffers", "DSSetShaderResources", "DSSetShader", "DSSetSamplers",
    "DSSetConstantBuffers", "CSSetShaderResources", "CSSetUnorderedAccessViews", "CSSetShader",
    "CSSetSamplers", "CSSetConstantBuffers", "VSGetConstantBuffers", "PSGetShaderResources", "PSGetShader",
    "PSGetSamplers", "VSGetShader", "PSGetConstantBuffers", "IAGetInputLayout", "IAGetVertexBuffers",
    "IAGetIndexBuffer", "GSGetConstantBuffers", "GSGetShader", "IAGetPrimitiveTopology",
    "VSGetShaderResources", "VSGetSamplers", "GetPredication", "GSGetShaderResources", "GSGetSamplers",
    "OMGetRenderTargets", "OMGetRenderTargetsAndUnorderedAccessViews", "OMGetBlendState",
    "OMGetDepthStencilState", "SOGetTargets", "RSGetState", "RSGetViewports", "RSGetScissorRects",
    "HSGetShaderResources", "HSGetShader", "HSGetSamplers", "HSGetConstantBuffers", "DSGetShaderResources",
    "DSGetShader", "DSGetSamplers", "DSGetConstantBuffers", "CSGetShaderResources",
    "CSGetUnorderedAccessViews", "CSGetShader", "CSGetSamplers", "CSGetConstantBuffers", "ClearState",
    "Flush", "GetType", "GetContextFlags", "FinishCommandList", "CopySubresourceRegion1",
    "UpdateSubresource1", "DiscardResource", "DiscardView", "VSSetConstantBuffers1", "HSSetConstantBuffers1",
    "DSSetConstantBuffers1", "GSSetConstantBuffers1", "PSSetConstantBuffers1", "CSSetConstantBuffers1",
    "VSGetConstantBuffers1", "HSGetConstantBuffers1", "DSGetConstantBuffers1", "GSGetConstantBuffers1",
    "PSGetConstantBuffers1", "CSGetConstantBuffers1", "SwapDeviceContextState", "ClearView", "DiscardView1",
    "UpdateTileMappings", "CopyTileMappings", "CopyTiles", "UpdateTiles", "ResizeTilePool",
    "TiledResourceBarrier", "IsAnnotationEnabled", "SetMarkerInt", "BeginEventInt", "EndEvent", "Flush1",
    "SetHardwareProtectionState", "GetHardwareProtectionState", "Signal", "Wait"};

  ///\brief a slot's method name in `names`, "?" past its end
  template<std::size_t n>
  [[nodiscard]]
  constexpr auto method_name(std::array<std::string_view, n> const & names, int slot) noexcept -> std::string_view
    {
    return slot >= 0 and static_cast<std::size_t>(slot) < n ? names[static_cast<std::size_t>(slot)]
                                                            : std::string_view{"?"};
    }

  static_assert(method_name(device_methods, 5) == "CreateTexture2D", "ID3D11Device: CreateTexture2D in slot 5");
  static_assert(method_name(context_methods, 7) == "VSSetConstantBuffers", "the context's own methods from slot 7");
  static_assert(method_name(context_methods, 12) == "DrawIndexed", "ID3D11DeviceContext: DrawIndexed in slot 12");
  static_assert(method_name(context_methods, 148) == "Wait", "ID3D11DeviceContext4: Wait the last");
  static_assert(method_name(context_methods, 149) == "?", "past the end");

  ///\brief the slots set in `changed` as ranges, "7-9, 12, 14-35"; "none" for none
  [[nodiscard]]
  inline auto ranges_text(std::span<bool const> changed) -> std::string
    {
    std::string text;
    for(std::size_t i{}; i != changed.size();)
      {
      if(not changed[i])
        {
        ++i;
        continue;
        }
      std::size_t end{i};
      while(end + 1 != changed.size() and changed[end + 1])
        ++end;
      if(not text.empty())
        text += ", ";
      text += std::to_string(i);
      if(end != i)
        (text += '-') += std::to_string(end);
      i = end + 1;
      }
    return text.empty() ? std::string{"none"} : text;
    }
  }  // namespace context_probe
