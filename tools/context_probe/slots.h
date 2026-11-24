// context_probe - the names of the methods in the D3D11 runtime's and DXGI's tables by slot, as the Windows SDK
// (10.0.26100.0) declares them: ID3D11Device5 (with IUnknown), ID3D11DeviceContext4 (with IUnknown and
// ID3D11DeviceChild) and IDXGISwapChain4 (with IUnknown, IDXGIObject and IDXGIDeviceSubObject); the text of a set of
// slots, the slots two reads of a table differ in, the names a list gained, and the result file's name. No Windows
// header: built natively too (test/native).
#pragma once

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstddef>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

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

  inline constexpr std::array<std::string_view, 41> swap_chain_methods{
    "QueryInterface", "AddRef", "Release", "SetPrivateData", "SetPrivateDataInterface", "GetPrivateData", "GetParent",
    "GetDevice", "Present", "GetBuffer", "SetFullscreenState", "GetFullscreenState", "GetDesc", "ResizeBuffers",
    "ResizeTarget", "GetContainingOutput", "GetFrameStatistics", "GetLastPresentCount", "GetDesc1",
    "GetFullscreenDesc", "GetHwnd", "GetCoreWindow", "Present1", "IsTemporaryMonoSupported", "GetRestrictToOutput",
    "SetBackgroundColor", "GetBackgroundColor", "SetRotation", "GetRotation", "SetSourceSize", "GetSourceSize",
    "SetMaximumFrameLatency", "GetMaximumFrameLatency", "GetFrameLatencyWaitableObject", "SetMatrixTransform",
    "GetMatrixTransform", "GetCurrentBackBufferIndex", "CheckColorSpaceSupport", "SetColorSpace1", "ResizeBuffers1",
    "SetHDRMetaData"};

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
  static_assert(method_name(swap_chain_methods, 8) == "Present", "IDXGISwapChain: Present in slot 8");
  static_assert(method_name(swap_chain_methods, 13) == "ResizeBuffers", "IDXGISwapChain: ResizeBuffers in slot 13");
  static_assert(method_name(swap_chain_methods, 22) == "Present1", "IDXGISwapChain1: Present1 in slot 22");
  static_assert(method_name(swap_chain_methods, 40) == "SetHDRMetaData", "IDXGISwapChain4: SetHDRMetaData the last");

  ///\brief the slots set in `changed` (an array or a vector of bool) as ranges, "7-9, 12, 14-35"; "none" for none
  template<std::ranges::random_access_range range_t>
  [[nodiscard]]
  auto ranges_text(range_t const & changed) -> std::string
    {
    std::size_t const size{std::size_t(std::ranges::size(changed))};
    std::string text;
    for(std::size_t i{}; i != size;)
      {
      if(not changed[i])
        {
        ++i;
        continue;
        }
      std::size_t end{i};
      while(end + 1 != size and changed[end + 1])
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

  ///\brief the slots in which two reads of a table differ, as many as the longer read; a slot only one read reached
  /// counts as changed
  template<class entry_t>
  [[nodiscard]]
  constexpr auto slots_differ(std::vector<entry_t> const & before, std::vector<entry_t> const & now)
    -> std::vector<bool>
    {
    std::vector<bool> changed(std::max(before.size(), now.size()));
    for(std::size_t i{}; i != changed.size(); ++i)
      changed[i] = i >= before.size() or i >= now.size() or before[i] != now[i];
    return changed;
    }

  ///\brief the names in `now` that `before` does not hold, in `now`'s order (a name twice in `now` and once in
  /// `before` is gained once)
  [[nodiscard]]
  constexpr auto names_gained(std::vector<std::string> const & before, std::vector<std::string> const & now)
    -> std::vector<std::string>
    {
    std::vector<std::string> left{before.begin(), before.end()};
    std::vector<std::string> gained;
    for(std::string const & name: now)
      if(auto const at{std::ranges::find(left, name)}; at != left.end())
        left.erase(at);
      else
        gained.push_back(name);
    return gained;
    }

  static_assert(
    []
      {
      std::vector<bool> const changed{slots_differ(std::vector<int>{1, 2, 3}, std::vector<int>{1, 5, 3, 4})};
      return changed == std::vector<bool>{false, true, false, true};
      }(),
    "slots_differ: a changed slot and one only the longer read reached");
  static_assert(
    []
      {
      std::vector<std::string> const gained{names_gained({"a.dll", "b.dll"}, {"b.dll", "c.dll", "b.dll", "a.dll"})};
      return gained == std::vector<std::string>{"c.dll", "b.dll"};
      }(),
    "names_gained: a new name and a name once more");

  ///\brief the result file's name for a run started at the local time given, "context_probe-20261124-131500.txt"; the
  /// `n`-th try for that second (from 2) gets "-<n>" before ".txt", so no run overwrites another's result
  [[nodiscard]]
  inline auto result_name(int year, int month, int day, int hour, int minute, int second, int n = 1) -> std::string
    {
    char name[64]{};
    if(n > 1)
      std::snprintf(name, sizeof name, "context_probe-%04d%02d%02d-%02d%02d%02d-%d.txt", year, month, day, hour, minute,
                    second, n);
    else
      std::snprintf(name, sizeof name, "context_probe-%04d%02d%02d-%02d%02d%02d.txt", year, month, day, hour, minute,
                    second);
    return name;
    }
  }  // namespace context_probe
