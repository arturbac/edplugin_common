// edplugin_common - memory of the game's process (include/edplugin_common/memory/memory.h).
#include <edplugin_common/memory/memory.h>

#include <edplugin_common/guard/guard.h>
#include <edplugin_common/memory/pe.h>

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <optional>

namespace edplugin_common
  {
  auto readable(void const * p, std::size_t bytes) noexcept -> bool
    {
    MEMORY_BASIC_INFORMATION info{};
    if(not p or VirtualQuery(p, &info, sizeof info) != sizeof info or info.State != MEM_COMMIT)
      return false;
    DWORD const readable_access{PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
                                | PAGE_EXECUTE_WRITECOPY};
    if(not(info.Protect & readable_access) or (info.Protect & PAGE_GUARD))
      return false;
    auto const end{reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize};
    auto const at{reinterpret_cast<std::uintptr_t>(p)};
    return bytes <= end - at;
    }

  auto executable(void const * p) noexcept -> bool
    {
    MEMORY_BASIC_INFORMATION m{};
    return VirtualQuery(p, &m, sizeof m) == sizeof m and m.State == MEM_COMMIT
           and (m.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
    }

  auto module_of(void const * p) noexcept -> HMODULE
    {
    HMODULE m{};
    if(not p
       or not GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 static_cast<LPCWSTR>(p), &m))
      return nullptr;
    return m;
    }

  namespace
    {
    struct section_query_t
      {
      HMODULE module;
      void const * p;
      char (*name)[9];
      };

    ///\brief section_of's reads, run under the guard: the module may be unloaded meanwhile
    auto section_name(void * arg) noexcept -> void
      {
      auto const & q{*static_cast<section_query_t const *>(arg)};
      std::optional<pe_image_t> const image{pe_image(q.module)};
      if(not image)
        return;
      std::uintptr_t const rva{reinterpret_cast<std::uintptr_t>(q.p) - reinterpret_cast<std::uintptr_t>(q.module)};
      if(IMAGE_SECTION_HEADER const * const s{image->section_of(rva)})
        {
        std::memcpy(*q.name, s->Name, 8);
        (*q.name)[8] = '\0';
        }
      }
    }  // namespace

  auto section_of(HMODULE module, void const * p, char (&name)[9]) noexcept -> void
    {
    name[0] = '-';
    name[1] = '\0';
    section_query_t query{module, p, &name};
    fault_t fault;
    if(not run_guarded_call(&section_name, &query, fault))
      {
      name[0] = '-';
      name[1] = '\0';
      }
    }

  namespace
    {
    ///\brief the scan itself, its reads in a function of its own: clang-cl catches a fault under __try only from a
    /// call (without /EHa a fault of an instruction in the __try body itself is not caught)
    [[gnu::noinline]]
    auto scan_exports(HMODULE module, std::string_view prefix, char (&name)[64]) noexcept -> export_scan_e
      {
      std::optional<pe_image_t> const image{pe_image(module)};
      if(not image)
        return export_scan_e::malformed;
      export_scan_e result{export_scan_e::none};
      bool const whole{for_each_export(*image,
                                       [&](export_t const & e) noexcept
                                       {
                                         if(not e.name.starts_with(prefix))
                                           return true;
                                         std::size_t const n{std::min(e.name.size(), sizeof name - 1)};
                                         std::memcpy(name, e.name.data(), n);
                                         name[n] = '\0';
                                         result = export_scan_e::found;
                                         return false;
                                       })};
      return whole ? result : export_scan_e::malformed;
      }
    }  // namespace

  auto first_export_with_prefix(HMODULE module, std::string_view prefix, char (&name)[64], DWORD & fault) noexcept -> export_scan_e
    {
    // no object with a destructor in this function: a fault unwinds nothing but the frame. The filter always handles
    // the fault: one let through this noexcept function would end the process
    name[0] = '\0';
    fault = 0;
    if(prefix.empty())
      return export_scan_e::none;
    __try
      {
      return scan_exports(module, prefix, name);
      }
    __except(fault = GetExceptionCode(), fault_action(fault))
      {
      name[0] = '\0';
      return export_scan_e::fault;
      }
    }
  }  // namespace edplugin_common
