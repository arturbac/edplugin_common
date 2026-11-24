// edplugin_common - memory of the game's process (include/edplugin_common/memory/memory.h).
#include <edplugin_common/memory/memory.h>

#include <windows.h>

#include <algorithm>
#include <cstring>

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

  auto pe_headers(HMODULE module) noexcept -> IMAGE_NT_HEADERS64 const *
    {
    auto const * const base{reinterpret_cast<std::uint8_t const *>(module)};
    auto const * const dos{reinterpret_cast<IMAGE_DOS_HEADER const *>(base)};
    if(not module or not readable(dos, sizeof *dos) or dos->e_magic != IMAGE_DOS_SIGNATURE or dos->e_lfanew <= 0
       or dos->e_lfanew > 0x10000)
      return nullptr;
    auto const * const nt{reinterpret_cast<IMAGE_NT_HEADERS64 const *>(base + dos->e_lfanew)};
    if(not readable(nt, sizeof *nt) or nt->Signature != IMAGE_NT_SIGNATURE or nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
      return nullptr;
    return nt;
    }

  auto section_of(HMODULE module, void const * p, char (&name)[9]) noexcept -> void
    {
    name[0] = '-';
    name[1] = '\0';
    IMAGE_NT_HEADERS64 const * const nt{pe_headers(module)};
    if(not nt)
      return;
    std::uintptr_t const rva{reinterpret_cast<std::uintptr_t>(p) - reinterpret_cast<std::uintptr_t>(module)};
    IMAGE_SECTION_HEADER const * const sections{IMAGE_FIRST_SECTION(nt)};
    WORD const n{nt->FileHeader.NumberOfSections};
    if(n == 0 or not readable(sections, sizeof(IMAGE_SECTION_HEADER) * n))
      return;
    for(WORD i{}; i != n; ++i)
      {
      IMAGE_SECTION_HEADER const & s{sections[i]};
      DWORD const size{std::max(s.Misc.VirtualSize, s.SizeOfRawData)};
      if(rva >= s.VirtualAddress and rva < std::uintptr_t{s.VirtualAddress} + size)
        {
        std::memcpy(name, s.Name, 8);
        name[8] = '\0';
        return;
        }
      }
    }

  namespace
    {
    ///\brief the scan itself, its reads in a function of its own: clang-cl catches a fault under __try only from a
    /// call (without /EHa a fault of an instruction in the __try body itself is not caught)
    [[gnu::noinline]]
    auto scan_exports(std::uint8_t const * base, std::string_view prefix, char (&name)[64]) noexcept -> export_scan_e
      {
      std::size_t const need{prefix.size() + 1};
      auto const * const dos{reinterpret_cast<IMAGE_DOS_HEADER const *>(base)};
      if(dos->e_magic != IMAGE_DOS_SIGNATURE or dos->e_lfanew <= 0 or dos->e_lfanew > 4096)
        return export_scan_e::malformed;
      auto const * const nt{reinterpret_cast<IMAGE_NT_HEADERS64 const *>(base + dos->e_lfanew)};
      if(nt->Signature != IMAGE_NT_SIGNATURE or nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
         or nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT)
        return export_scan_e::malformed;
      std::uint32_t const size{nt->OptionalHeader.SizeOfImage};
      IMAGE_DATA_DIRECTORY const & dir{nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]};
      if(dir.VirtualAddress == 0)
        return export_scan_e::none;  // no exports
      if(dir.Size < sizeof(IMAGE_EXPORT_DIRECTORY) or dir.VirtualAddress > size
         or size - dir.VirtualAddress < sizeof(IMAGE_EXPORT_DIRECTORY))
        return export_scan_e::malformed;
      auto const * const exports{reinterpret_cast<IMAGE_EXPORT_DIRECTORY const *>(base + dir.VirtualAddress)};
      std::uint32_t const count{exports->NumberOfNames};
      if(exports->AddressOfNames > size or count > (size - exports->AddressOfNames) / sizeof(std::uint32_t))
        return export_scan_e::malformed;
      auto const * const names{reinterpret_cast<std::uint32_t const *>(base + exports->AddressOfNames)};
      for(std::uint32_t i{}; i != count; ++i)
        {
        std::uint32_t const at{names[i]};
        if(at > size or size - at < need)
          continue;
        char const * const text{reinterpret_cast<char const *>(base + at)};
        if(std::memcmp(text, prefix.data(), prefix.size()) != 0)
          continue;
        std::size_t n{};
        while(n + 1 < sizeof name and at + n < size and text[n] != '\0')
          {
          name[n] = text[n];
          ++n;
          }
        name[n] = '\0';
        return export_scan_e::found;
        }
      return export_scan_e::none;
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
      return scan_exports(reinterpret_cast<std::uint8_t const *>(module), prefix, name);
      }
    __except(fault = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
      {
      name[0] = '\0';
      return export_scan_e::fault;
      }
    }
  }  // namespace edplugin_common
