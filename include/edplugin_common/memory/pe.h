// edplugin_common - a loaded module's PE image read as data from outside the plugin: every offset checked against the
// image's bound before it is read (memory/pe_range.h), every value read as a copy to a local (no pointer to a header
// structure, no unaligned read), the headers and the section table read once. Texts (names) and span() are views of
// the module's memory, not copies: valid while the module stays loaded. The bound is the least of SizeOfImage,
// the size the loader gives (when the caller has it) and the memory the image's allocation covers. A part outside the
// headers and the readable sections is not read. The module is not pinned (FreeLibrary could run its
// DLL_PROCESS_DETACH under the caller's lock): another thread unloading it while it is read faults, so a caller reads
// under a guard (guard/guard.h) and skips the module then.
#pragma once

#include <edplugin_common/memory/pe_range.h>

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace edplugin_common
  {
  class pe_image_t;

  ///\brief the image of a loaded module (`loader_size`: the size the loader gives, 0 when not known), or none when its
  /// headers are not those of a PE32+ image within its memory: an MZ header, e_lfanew in [64, 4096], the NT headers'
  /// signature and magic, an optional header large enough for its data directories, at most 96 sections, the section
  /// table inside the headers. The headers' part ends at SizeOfHeaders or at the first section, whichever comes first
  [[nodiscard]]
  auto pe_image(HMODULE module, std::size_t loader_size = 0) noexcept -> std::optional<pe_image_t>;

  class pe_image_t
    {
    std::uint8_t const * base_{};
    std::uint32_t size_{};
    std::uint32_t headers_{};
    bool size_differs_{};
    std::array<IMAGE_DATA_DIRECTORY, IMAGE_NUMBEROF_DIRECTORY_ENTRIES> directories_{};
    std::uint16_t section_count_{};
    std::array<IMAGE_SECTION_HEADER, 96> sections_{};

    pe_image_t() noexcept = default;
    friend auto pe_image(HMODULE module, std::size_t loader_size) noexcept -> std::optional<pe_image_t>;

  public:
    [[nodiscard]]
    auto base() const noexcept -> std::uint8_t const *
      {
      return base_;
      }

    ///\brief the length of the headers' part, from RVA 0
    [[nodiscard]]
    auto headers() const noexcept -> std::uint32_t
      {
      return headers_;
      }

    ///\brief the bound every read keeps to
    [[nodiscard]]
    auto size() const noexcept -> std::uint32_t
      {
      return size_;
      }

    ///\brief whether SizeOfImage, the loader's size and the allocation's memory do not agree (the least is the bound)
    [[nodiscard]]
    auto size_differs() const noexcept -> bool
      {
      return size_differs_;
      }

    ///\brief the sections, read once
    [[nodiscard]]
    auto sections() const noexcept -> std::span<IMAGE_SECTION_HEADER const>
      {
      return {sections_.data(), section_count_};
      }

    ///\brief the section holding `rva`, nullptr when none
    [[nodiscard]]
    auto section_of(std::uint64_t rva) const noexcept -> IMAGE_SECTION_HEADER const *;

    ///\brief a data directory, {0, 0} when the image has none at `index`
    [[nodiscard]]
    auto directory(unsigned index) const noexcept -> IMAGE_DATA_DIRECTORY
      {
      return index < directories_.size() ? directories_[index] : IMAGE_DATA_DIRECTORY{};
      }

    ///\brief `bytes` bytes at `rva` when they lie within the bound and all in the headers or in one readable section;
    /// none otherwise (also for 0 bytes)
    [[nodiscard]]
    auto span(std::uint64_t rva, std::size_t bytes) const noexcept -> std::optional<std::span<std::uint8_t const>>;

    ///\brief a value of `value_t` at `rva`, copied out
    template<typename value_t>
    [[nodiscard]]
    auto read(std::uint64_t rva) const noexcept -> std::optional<value_t>
      {
      static_assert(std::is_trivially_copyable_v<value_t>);
      std::optional<std::span<std::uint8_t const>> const at{span(rva, sizeof(value_t))};
      if(not at)
        return std::nullopt;
      value_t value;
      std::memcpy(&value, at->data(), sizeof value);
      return value;
      }

    ///\brief the NUL-terminated text at `rva`, at most `most` characters, its NUL inside the same part of the image;
    /// none when there is no such NUL
    [[nodiscard]]
    auto name_at(std::uint64_t rva, std::size_t most = 256) const noexcept -> std::optional<std::string_view>;
    };

  ///\brief the address of an export by `name`, read from the export directory (no GetProcAddress, which may be
  /// hooked): the ordinal checked against the functions' count, a forwarder ("NTDLL.RtlAllocateHeap") followed to the
  /// module GetModuleHandleA gives for its dll name (an API set too), `depth` times at most; a forwarder by ordinal
  /// ("DLL.#12") not followed. nullptr when none
  [[nodiscard]]
  auto export_address(pe_image_t const & image, std::string_view name, int depth = 2) noexcept -> void const *;

  struct export_t
    {
    std::string_view name;
    std::uint32_t rva;  ///< of the code, or of the forwarder's text
    bool forwarder;
    };

  /// the longest name of an export or an import read (MSVC's decorated names are cut at 4096 characters)
  inline constexpr std::size_t pe_name_most{4096};

  ///\brief `f(export_t)` for each export by name, while it returns true; false when the directory is out of shape
  /// (its tables past the bound, an ordinal past the functions, a name without its NUL within pe_name_most)
  template<typename function_t>
  auto for_each_export(pe_image_t const & image, function_t && f) noexcept(noexcept(f(export_t{}))) -> bool
    {
    IMAGE_DATA_DIRECTORY const dir{image.directory(IMAGE_DIRECTORY_ENTRY_EXPORT)};
    if(dir.VirtualAddress == 0)
      return true;
    std::optional<IMAGE_EXPORT_DIRECTORY> const exports{image.read<IMAGE_EXPORT_DIRECTORY>(dir.VirtualAddress)};
    if(not exports or dir.Size < sizeof(IMAGE_EXPORT_DIRECTORY))
      return false;
    std::uint32_t const names{exports->NumberOfNames};
    std::uint32_t const functions{exports->NumberOfFunctions};
    if(not array_in(exports->AddressOfNames, names, 4, image.size())
       or not array_in(exports->AddressOfNameOrdinals, names, 2, image.size())
       or not array_in(exports->AddressOfFunctions, functions, 4, image.size()))
      return false;
    for(std::uint32_t i{}; i != names; ++i)
      {
      std::optional<std::uint32_t> const name_rva{image.read<std::uint32_t>(exports->AddressOfNames + 4ull * i)};
      std::optional<std::uint16_t> const ordinal{image.read<std::uint16_t>(exports->AddressOfNameOrdinals + 2ull * i)};
      if(not name_rva or not ordinal or *ordinal >= functions)
        return false;
      std::optional<std::uint32_t> const rva{image.read<std::uint32_t>(exports->AddressOfFunctions + 4ull * *ordinal)};
      std::optional<std::string_view> const name{image.name_at(*name_rva, pe_name_most)};
      if(not rva or not name)
        return false;
      if(not f(export_t{*name, *rva, rva_inside(*rva, dir.VirtualAddress, dir.Size)}))
        return true;
      }
    return true;
    }

  struct import_t
    {
    std::string_view dll;
    std::string_view function;  ///< empty for an import by ordinal
    std::uint16_t ordinal;      ///< for an import by ordinal
    void ** slot;               ///< the import's slot: in the image past its headers, 8-aligned and in the IAT
                                ///< directory (when there is one); nullptr otherwise
    };

  ///\brief what a walk of the imports met besides the imports themselves
  struct import_scan_t
    {
    int descriptors;      ///< walked
    int without_names;    ///< skipped: no OriginalFirstThunk, nothing to tell the functions by
    int slots_refused;    ///< imports given without a slot
    bool past_size;       ///< descriptors past the directory's Size before the zero one
    bool malformed;       ///< a descriptor or a thunk past the bound, a name without its NUL, a thunk by name with
                          ///< bits 31-62 set: the walk stopped there
    };

  ///\brief `f(import_t)` for each import of every descriptor up to the first with no name or no slots (where the
  /// loader stops), inside the bound; the names' and the slots' thunks each checked at every step
  template<typename function_t>
  auto for_each_import(pe_image_t const & image, function_t && f) noexcept(noexcept(f(import_t{}))) -> import_scan_t
    {
    import_scan_t scan{};
    IMAGE_DATA_DIRECTORY const dir{image.directory(IMAGE_DIRECTORY_ENTRY_IMPORT)};
    IMAGE_DATA_DIRECTORY const iat{image.directory(IMAGE_DIRECTORY_ENTRY_IAT)};
    if(dir.VirtualAddress == 0)
      return scan;
    constexpr std::uint64_t descriptor{sizeof(IMAGE_IMPORT_DESCRIPTOR)};
    for(std::uint64_t k{};; ++k)
      {
      std::optional<IMAGE_IMPORT_DESCRIPTOR> const d{image.read<IMAGE_IMPORT_DESCRIPTOR>(dir.VirtualAddress + k * descriptor)};
      if(not d or k == 65536)
        {
        scan.malformed = true;
        return scan;
        }
      if(d->Name == 0 or d->FirstThunk == 0)
        return scan;
      if((k + 1) * descriptor > dir.Size)
        scan.past_size = true;
      ++scan.descriptors;
      std::optional<std::string_view> const dll{image.name_at(d->Name)};
      if(not dll)
        {
        scan.malformed = true;
        return scan;
        }
      if(d->OriginalFirstThunk == 0)
        {
        ++scan.without_names;
        continue;
        }
      for(std::uint64_t j{};; ++j)
        {
        std::uint64_t const slot_rva{d->FirstThunk + 8 * j};
        std::optional<std::uint64_t> const thunk{image.read<std::uint64_t>(d->OriginalFirstThunk + 8 * j)};
        std::optional<std::uint64_t> const slot_value{image.read<std::uint64_t>(slot_rva)};
        if(not thunk or not slot_value or j == 65536)
          {
          scan.malformed = true;
          return scan;
          }
        if(*thunk == 0)
          break;
        bool const slot_ok{slot_rva % 8 == 0 and slot_rva >= image.headers()
                           and (iat.VirtualAddress == 0
                                or (rva_inside(slot_rva, iat.VirtualAddress, iat.Size)
                                    and rva_in(slot_rva - iat.VirtualAddress, 8, iat.Size)))};
        import_t import{*dll, {}, 0, slot_ok ? reinterpret_cast<void **>(const_cast<std::uint8_t *>(image.base() + slot_rva)) : nullptr};
        if(not slot_ok)
          ++scan.slots_refused;
        if(IMAGE_SNAP_BY_ORDINAL64(*thunk))
          import.ordinal = static_cast<std::uint16_t>(IMAGE_ORDINAL64(*thunk));
        else
          {
          // IMAGE_IMPORT_BY_NAME: a 2-byte hint, then the name; bits 31-62 of such a thunk are 0 in PE32+
          std::optional<std::string_view> const function{image.name_at(*thunk + 2, pe_name_most)};
          if((*thunk >> 31) != 0 or not function)
            {
            scan.malformed = true;
            return scan;
            }
          import.function = *function;
          }
        f(static_cast<import_t const &>(import));
        }
      }
    }
  }  // namespace edplugin_common
