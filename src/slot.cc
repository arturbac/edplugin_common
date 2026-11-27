// edplugin_common - hooks written into slots of a method table in place (include/edplugin_common/hook/slot.h).
#include <edplugin_common/hook/slot.h>

#include <windows.h>

namespace edplugin_common::hook
  {
  static_assert(page::no_access == PAGE_NOACCESS and page::read_only == PAGE_READONLY and page::read_write == PAGE_READWRITE
                and page::write_copy == PAGE_WRITECOPY and page::execute == PAGE_EXECUTE
                and page::execute_read == PAGE_EXECUTE_READ and page::execute_read_write == PAGE_EXECUTE_READWRITE
                and page::execute_write_copy == PAGE_EXECUTE_WRITECOPY and page::guard == PAGE_GUARD);

  namespace
    {
    auto protection(std::uintptr_t page, std::uint32_t & protect) noexcept -> bool
      {
      MEMORY_BASIC_INFORMATION m{};
      if(VirtualQuery(reinterpret_cast<void const *>(page), &m, sizeof m) != sizeof m or m.State != MEM_COMMIT)
        return false;
      protect = m.Protect;
      return true;
      }

    auto protect(std::uintptr_t page, std::uint32_t to, std::uint32_t & old, std::uint32_t & error) noexcept -> bool
      {
      DWORD was{};
      if(not VirtualProtect(reinterpret_cast<void *>(page), 1, to, &was))
        {
        error = GetLastError();
        return false;
        }
      old = was;
      return true;
      }

    auto written(void * p, std::size_t bytes) noexcept -> void
      {
      FlushInstructionCache(GetCurrentProcess(), p, bytes);
      }

    [[nodiscard]]
    auto page_size() noexcept -> std::size_t
      {
      SYSTEM_INFO info{};
      GetSystemInfo(&info);
      return info.dwPageSize;
      }
    }  // namespace

  auto windows_page_ops() noexcept -> page_ops_t const &
    {
    static page_ops_t const ops{page_size(), &protection, &protect, &written};
    return ops;
    }

  auto patch_slots(void ** table, std::span<slot_patch_t> list) noexcept -> slots_patched_t
    {
    return patch_slots_by(windows_page_ops(), table, list);
    }
  }  // namespace edplugin_common::hook
