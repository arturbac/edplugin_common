// edplugin_common - a loaded module's PE image read as data from outside (include/edplugin_common/memory/pe.h).
#include <edplugin_common/memory/pe.h>

#include <edplugin_common/memory/memory.h>

#include <windows.h>

#include <algorithm>

namespace edplugin_common
  {
  namespace
    {
    ///\brief the bytes the allocation at `base` covers from `base` on, its regions one after another
    [[nodiscard]]
    auto allocation_extent(std::uint8_t const * base) noexcept -> std::uint64_t
      {
      std::uint64_t extent{};
      for(int region{}; region != 4096; ++region)
        {
        MEMORY_BASIC_INFORMATION m{};
        if(VirtualQuery(base + extent, &m, sizeof m) != sizeof m or m.AllocationBase != base or m.State == MEM_FREE
           or m.RegionSize == 0)
          break;
        extent += m.RegionSize;
        }
      return extent;
      }

    ///\brief a section's bytes in memory: VirtualSize, or the raw size for an image whose VirtualSize is 0
    [[nodiscard]]
    auto section_size(IMAGE_SECTION_HEADER const & s) noexcept -> std::uint64_t
      {
      return s.Misc.VirtualSize != 0 ? s.Misc.VirtualSize : s.SizeOfRawData;
      }

    ///\brief the length of the part of the image (the headers, or a readable section) holding `rva`, from `rva` to its
    /// end within the bound; 0 when no part holds it
    [[nodiscard]]
    auto part_left(pe_image_t const & image, std::uint64_t headers, std::uint64_t rva) noexcept -> std::uint64_t
      {
      if(rva < headers)
        return headers - rva;
      IMAGE_SECTION_HEADER const * const s{image.section_of(rva)};
      if(not s or (s->Characteristics & IMAGE_SCN_MEM_READ) == 0)
        return 0;
      std::uint64_t const end{std::min<std::uint64_t>(std::uint64_t{s->VirtualAddress} + section_size(*s), image.size())};
      return end > rva ? end - rva : 0;
      }
    }  // namespace

  auto pe_image(HMODULE module, std::size_t loader_size) noexcept -> std::optional<pe_image_t>
    {
    auto const * const base{reinterpret_cast<std::uint8_t const *>(module)};
    if(not module or not readable(base, sizeof(IMAGE_DOS_HEADER)))
      return std::nullopt;
    IMAGE_DOS_HEADER dos;
    std::memcpy(&dos, base, sizeof dos);
    std::uint64_t const extent{allocation_extent(base)};
    if(dos.e_magic != IMAGE_DOS_SIGNATURE or not lfanew_ok(dos.e_lfanew, sizeof(IMAGE_NT_HEADERS64), extent)
       or not readable(base + dos.e_lfanew, sizeof(IMAGE_NT_HEADERS64)))
      return std::nullopt;
    IMAGE_NT_HEADERS64 nt;
    std::memcpy(&nt, base + dos.e_lfanew, sizeof nt);
    if(nt.Signature != IMAGE_NT_SIGNATURE or nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
      return std::nullopt;
    pe_image_t image;
    image.base_ = base;
    std::uint64_t size{nt.OptionalHeader.SizeOfImage};
    if(loader_size != 0 and loader_size != size)
      {
      image.size_differs_ = true;
      size = std::min<std::uint64_t>(size, loader_size);
      }
    if(extent < size)
      {
      image.size_differs_ = true;
      size = extent;
      }
    image.size_ = static_cast<std::uint32_t>(size);
    image.headers_ = static_cast<std::uint32_t>(std::min<std::uint64_t>(nt.OptionalHeader.SizeOfHeaders, size));
    std::uint32_t const directories{
      std::min<std::uint32_t>(nt.OptionalHeader.NumberOfRvaAndSizes, IMAGE_NUMBEROF_DIRECTORY_ENTRIES)};
    // the directories read must lie in the optional header, not in the section table after a short one
    if(nt.FileHeader.SizeOfOptionalHeader
       < offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory) + std::uint64_t{directories} * sizeof(IMAGE_DATA_DIRECTORY))
      return std::nullopt;
    std::copy_n(nt.OptionalHeader.DataDirectory, directories, image.directories_.begin());
    // the section table right after the optional header, inside the headers
    std::uint64_t const table{static_cast<std::uint64_t>(dos.e_lfanew) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader)
                              + nt.FileHeader.SizeOfOptionalHeader};
    std::uint16_t const count{nt.FileHeader.NumberOfSections};
    if(count > image.sections_.size() or not array_in(table, count, sizeof(IMAGE_SECTION_HEADER), image.headers_)
       or (count != 0 and not readable(base + table, count * sizeof(IMAGE_SECTION_HEADER))))
      return std::nullopt;
    std::memcpy(image.sections_.data(), base + table, count * sizeof(IMAGE_SECTION_HEADER));
    image.section_count_ = count;
    // the headers' part ends where the first section starts, should SizeOfHeaders reach past it
    for(IMAGE_SECTION_HEADER const & s: image.sections())
      image.headers_ = std::min(image.headers_, static_cast<std::uint32_t>(s.VirtualAddress));
    return image;
    }

  auto pe_image_t::section_of(std::uint64_t rva) const noexcept -> IMAGE_SECTION_HEADER const *
    {
    for(IMAGE_SECTION_HEADER const & s: sections())
      if(rva_inside(rva, s.VirtualAddress, section_size(s)))
        return &s;
    return nullptr;
    }

  auto pe_image_t::span(std::uint64_t rva, std::size_t bytes) const noexcept -> std::optional<std::span<std::uint8_t const>>
    {
    if(bytes == 0 or not rva_in(rva, bytes, size_) or part_left(*this, headers_, rva) < bytes)
      return std::nullopt;
    return std::span<std::uint8_t const>{base_ + rva, bytes};
    }

  auto pe_image_t::name_at(std::uint64_t rva, std::size_t most) const noexcept -> std::optional<std::string_view>
    {
    std::uint64_t const left{part_left(*this, headers_, rva)};
    std::size_t const bytes{static_cast<std::size_t>(std::min<std::uint64_t>(left, most + 1))};
    std::optional<std::span<std::uint8_t const>> const at{span(rva, bytes)};
    if(not at)
      return std::nullopt;
    auto const * const text{reinterpret_cast<char const *>(at->data())};
    void const * const nul{std::memchr(text, '\0', at->size())};
    if(not nul)
      return std::nullopt;
    return std::string_view{text, static_cast<std::size_t>(static_cast<char const *>(nul) - text)};
    }

  auto export_address(pe_image_t const & image, std::string_view name, int depth) noexcept -> void const *
    {
    IMAGE_DATA_DIRECTORY const dir{image.directory(IMAGE_DIRECTORY_ENTRY_EXPORT)};
    std::optional<export_t> found;
    bool const whole{for_each_export(image,
                                     [&](export_t const & e) noexcept
                                     {
                                       if(e.name != name)
                                         return true;
                                       found = e;
                                       return false;
                                     })};
    if(not whole or not found)
      return nullptr;
    if(not found->forwarder)
      return rva_in(found->rva, 1, image.size()) and found->rva != 0 ? image.base() + found->rva : nullptr;
    // "<dll>.<function>", its NUL inside the export directory; by ordinal ("<dll>.#<n>") not followed
    std::optional<std::string_view> const text{image.name_at(found->rva)};
    if(depth <= 0 or not text or not rva_in(found->rva - dir.VirtualAddress, text->size() + 1, dir.Size))
      return nullptr;
    std::size_t const dot{text->find_last_of('.')};
    if(dot == std::string_view::npos or dot == 0 or dot + 1 == text->size() or (*text)[dot + 1] == '#')
      return nullptr;
    char dll[256]{};
    std::string_view const dll_name{text->substr(0, dot)};
    if(dll_name.size() >= sizeof dll)
      return nullptr;
    std::memcpy(dll, dll_name.data(), dll_name.size());
    // GetModuleHandleA adds ".dll" to a name without an extension, and resolves an API set's name as the loader does
    std::optional<pe_image_t> const next{pe_image(GetModuleHandleA(dll))};
    if(not next)
      return nullptr;
    return export_address(*next, text->substr(dot + 1), depth - 1);
    }
  }  // namespace edplugin_common
