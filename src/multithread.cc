// edplugin_common - the runtime's multithread protection, told (include/edplugin_common/hook/multithread.h).
#include <edplugin_common/hook/multithread.h>

#include <windows.h>
#include <d3d11_4.h>

namespace edplugin_common::hook
  {
  namespace
    {
    ///\brief the object's ID3D11Multithread, or nullptr (also on a failed call: its out pointer is not trusted then)
    template<typename object_t>
    [[nodiscard]]
    auto multithread_of(object_t * object) noexcept -> ID3D11Multithread *
      {
      if(not object)
        return nullptr;
      ID3D11Multithread * found{};
      if(FAILED(object->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void **>(&found))))
        return nullptr;
      return found;
      }
    }  // namespace

  auto multithread_t::start(ID3D11DeviceContext * context, ID3D11Device * device) noexcept -> multithread_start_e
    {
    if(not context or started_.exchange(true, std::memory_order_acq_rel))
      return multithread_start_e::no_object;
    ID3D11Multithread * found{multithread_of(context)};
    if(not found)
      found = multithread_of(device);
    if(not found)
      return multithread_start_e::no_interface;
    // kept to the end of the process, never released (contract c)
    bool const now{found->GetMultithreadProtected() != FALSE};
    on_at_start_.store(now, std::memory_order_release);
    on_.store(now, std::memory_order_release);
    interface_.store(found, std::memory_order_release);
    return now ? multithread_start_e::on : multithread_start_e::off;
    }

  auto multithread_t::refresh() noexcept -> multithread_state_t
    {
    ID3D11Multithread * const found{interface_.load(std::memory_order_acquire)};
    bool const now{found and found->GetMultithreadProtected() != FALSE};
    bool const was{on_.exchange(now, std::memory_order_acq_rel)};
    return multithread_state_t{now, now != was};
    }
  }  // namespace edplugin_common::hook
