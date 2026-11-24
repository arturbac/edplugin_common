// The checks of memory/pe.h: every module of the test's process (under wine) read as an image with its exports and
// imports, and the test exe's imports of kernel32 leading where export_address says; then an image made in memory
// (headers, a readable .rdata with the export and import directories, .data with the import slots, a page that cannot
// be read after it) read whole, and changed one way at a time out of shape.
#include <edplugin_common/memory/pe.h>

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace
  {
  using check_t = void (*)(bool ok, char const * what);
  check_t check{};

  auto process_modules() -> void
    {
    using namespace edplugin_common;
    std::vector<HMODULE> modules(1024);
    DWORD needed{};
    if(not K32EnumProcessModules(GetCurrentProcess(), modules.data(), DWORD(modules.size() * sizeof(HMODULE)), &needed))
      {
      check(false, "pe: the process's modules listed");
      return;
      }
    modules.resize(std::min<std::size_t>(modules.size(), needed / sizeof(HMODULE)));
    int read{}, imports_whole{}, exports_whole{}, differs{};
    for(HMODULE const m: modules)
      {
      MODULEINFO info{};
      std::size_t const loader_size{K32GetModuleInformation(GetCurrentProcess(), m, &info, sizeof info) ? info.SizeOfImage : 0};
      std::optional<pe_image_t> const image{pe_image(m, loader_size)};
      if(not image)
        {
        wchar_t name[MAX_PATH]{};
        GetModuleFileNameW(m, name, MAX_PATH);
        std::printf("pe: not read as an image: %ls\n", name);
        continue;
        }
      ++read;
      differs += image->size_differs() ? 1 : 0;
      if(for_each_export(*image, [](export_t const &) noexcept { return true; }))
        ++exports_whole;
      else
        {
        wchar_t name[MAX_PATH]{};
        GetModuleFileNameW(m, name, MAX_PATH);
        std::printf("pe: exports out of shape: %ls\n", name);
        }
      import_scan_t const scan{for_each_import(*image, [](import_t const &) noexcept {})};
      if(not scan.malformed)
        ++imports_whole;
      else
        {
        wchar_t name[MAX_PATH]{};
        GetModuleFileNameW(m, name, MAX_PATH);
        std::printf("pe: imports out of shape: %ls\n", name);
        }
      }
    std::printf("pe: %zu modules, %d read, %d with whole exports, %d with whole imports, %d with sizes that differ\n", modules.size(),
                read, exports_whole, imports_whole, differs);
    check(read == int(modules.size()) and imports_whole == read and exports_whole == read,
          "pe_image, for_each_export, for_each_import: every module of the process, none refused");

    std::optional<pe_image_t> const kernel32{pe_image(GetModuleHandleW(L"kernel32.dll"))};
    void const * const by_table{kernel32 ? export_address(*kernel32, "GetTickCount64") : nullptr};
    void const * const by_loader{reinterpret_cast<void const *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTickCount64"))};
    check(by_table and by_table == by_loader, "export_address: kernel32's GetTickCount64 where GetProcAddress says (forwarders followed)");
    check(kernel32 and export_address(*kernel32, "edpc_no_such_function") == nullptr, "export_address: no such export - nullptr");

    std::optional<pe_image_t> const exe{pe_image(GetModuleHandleW(nullptr))};
    int named{}, agreeing{};
    if(exe and kernel32)
      (void)for_each_import(*exe,
                            [&](import_t const & i) noexcept
                            {
                              if(_strnicmp(i.dll.data(), "kernel32.dll", i.dll.size()) != 0 or i.dll.size() != 12 or i.function.empty()
                                 or not i.slot)
                                return;
                              ++named;
                              char function[256]{};
                              std::memcpy(function, i.function.data(), std::min<std::size_t>(i.function.size(), 255));
                              agreeing += *i.slot == export_address(*kernel32, function) ? 1 : 0;
                            });
    std::printf("pe: the exe's imports of kernel32 by name %d, %d where export_address says\n", named, agreeing);
    check(named != 0 and agreeing == named, "for_each_import: the exe's kernel32 slots hold what export_address finds");
    }

  // the made image: 0x0000 headers, 0x1000 .rdata (exports, imports), 0x2000 .data (the import slots), 0x3000 a page
  // that cannot be read (in the allocation, past SizeOfImage)
  constexpr std::uint32_t lfanew{0x80};
  constexpr std::uint32_t table{lfanew + 4 + sizeof(IMAGE_FILE_HEADER) + sizeof(IMAGE_OPTIONAL_HEADER64)};

  template<typename value_t>
  auto put(std::uint8_t * image, std::uint32_t at, value_t const & value) -> void
    {
    std::memcpy(image + at, &value, sizeof value);
    }

  auto put_text(std::uint8_t * image, std::uint32_t at, std::string_view text) -> void
    {
    std::memcpy(image + at, text.data(), text.size());
    image[at + text.size()] = 0;
    }

  auto make(std::uint8_t * image) -> void
    {
    std::memset(image, 0, 0x3000);
    IMAGE_DOS_HEADER dos{};
    dos.e_magic = IMAGE_DOS_SIGNATURE;
    dos.e_lfanew = lfanew;
    put(image, 0, dos);
    IMAGE_NT_HEADERS64 nt{};
    nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.NumberOfSections = 2;
    nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.SizeOfImage = 0x3000;
    nt.OptionalHeader.SizeOfHeaders = 0x400;
    nt.OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT] = {0x1000, 0x400};
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = {0x1400, 2 * sizeof(IMAGE_IMPORT_DESCRIPTOR)};
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT] = {0x2000, 0x18};
    put(image, lfanew, nt);
    IMAGE_SECTION_HEADER rdata{};
    std::memcpy(rdata.Name, ".rdata", 6);
    rdata.VirtualAddress = 0x1000;
    rdata.Misc.VirtualSize = 0x1000;
    rdata.Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA;
    put(image, table, rdata);
    IMAGE_SECTION_HEADER data{};
    std::memcpy(data.Name, ".data", 5);
    data.VirtualAddress = 0x2000;
    data.Misc.VirtualSize = 0x1000;
    data.Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_CNT_INITIALIZED_DATA;
    put(image, table + sizeof(IMAGE_SECTION_HEADER), data);
    // exports: alpha (code at 0x2100), beta (a forwarder to kernel32's GetTickCount64)
    IMAGE_EXPORT_DIRECTORY exports{};
    exports.NumberOfFunctions = 2;
    exports.NumberOfNames = 2;
    exports.AddressOfFunctions = 0x1100;
    exports.AddressOfNames = 0x1120;
    exports.AddressOfNameOrdinals = 0x1140;
    put(image, 0x1000, exports);
    put(image, 0x1100, std::uint32_t{0x2100});
    put(image, 0x1104, std::uint32_t{0x1300});
    put(image, 0x1120, std::uint32_t{0x1200});
    put(image, 0x1124, std::uint32_t{0x1210});
    put(image, 0x1140, std::uint16_t{0});
    put(image, 0x1142, std::uint16_t{1});
    put_text(image, 0x1200, "alpha");
    put_text(image, 0x1210, "beta");
    put_text(image, 0x1300, "KERNEL32.GetTickCount64");
    // imports of KERNEL32.dll: GetTickCount64 by name, ordinal 5; slots at 0x2000
    IMAGE_IMPORT_DESCRIPTOR d{};
    d.OriginalFirstThunk = 0x1500;
    d.Name = 0x1600;
    d.FirstThunk = 0x2000;
    put(image, 0x1400, d);
    put(image, 0x1500, std::uint64_t{0x1700});
    put(image, 0x1508, IMAGE_ORDINAL_FLAG64 | 5);
    put(image, 0x1510, std::uint64_t{0});
    put_text(image, 0x1600, "KERNEL32.dll");
    put(image, 0x1700, std::uint16_t{0});
    put_text(image, 0x1702, "GetTickCount64");
    put(image, 0x2000, std::uint64_t{0x1700});
    put(image, 0x2008, IMAGE_ORDINAL_FLAG64 | 5);
    }

  struct imports_t
    {
    edplugin_common::import_scan_t scan;
    int by_name;
    int by_ordinal;
    int slots;
    };

  auto imports_of(edplugin_common::pe_image_t const & image) -> imports_t
    {
    imports_t r{};
    r.scan = edplugin_common::for_each_import(image,
                                              [&](edplugin_common::import_t const & i) noexcept
                                              {
                                                r.by_name += i.function == "GetTickCount64" and i.dll == "KERNEL32.dll" ? 1 : 0;
                                                r.by_ordinal += i.function.empty() and i.ordinal == 5 ? 1 : 0;
                                                r.slots += i.slot ? 1 : 0;
                                              });
    return r;
    }

  auto exports_whole(edplugin_common::pe_image_t const & image) -> bool
    {
    return edplugin_common::for_each_export(image, [](edplugin_common::export_t const &) noexcept { return true; });
    }

  auto made_image() -> void
    {
    using namespace edplugin_common;
    auto * const image{static_cast<std::uint8_t *>(VirtualAlloc(nullptr, 0x4000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))};
    DWORD old{};
    if(not image or not VirtualProtect(image + 0x3000, 0x1000, PAGE_NOACCESS, &old))
      {
      check(false, "pe: the made image's pages");
      return;
      }
    auto const module{reinterpret_cast<HMODULE>(image)};
    make(image);
    std::optional<pe_image_t> const whole{pe_image(module)};
    check(whole and whole->size() == 0x3000 and not whole->size_differs() and whole->sections().size() == 2,
          "pe_image: the made image, its size and sections");
    check(whole and whole->base() + 0x2100 == export_address(*whole, "alpha"), "export_address: an export's code");
    void const * const tick{reinterpret_cast<void const *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTickCount64"))};
    check(whole and export_address(*whole, "beta") == tick, "export_address: a forwarder followed to kernel32");
    check(whole and export_address(*whole, "beta", 0) == nullptr, "export_address: a forwarder past the depth - nullptr");
    if(whole)
      {
      imports_t const i{imports_of(*whole)};
      check(i.by_name == 1 and i.by_ordinal == 1 and i.slots == 2 and i.scan.descriptors == 1 and not i.scan.malformed
              and not i.scan.past_size and i.scan.slots_refused == 0,
            "for_each_import: by name and by ordinal, both slots");
      check(whole->name_at(0x1200) == "alpha"sv and whole->read<std::uint16_t>(0x1142) == std::uint16_t{1},
            "pe_image_t: a name and a value read");
      check(not whole->span(0x2FFC, 8) and not whole->span(0x3000, 1) and not whole->span(0x500, 4),
            "pe_image_t: past a section, past the bound, between the headers and a section - none");
      }

    put(image, offsetof(IMAGE_DOS_HEADER, e_lfanew), LONG{8});
    check(not pe_image(module), "pe_image: e_lfanew inside the DOS header - none");
    put(image, offsetof(IMAGE_DOS_HEADER, e_lfanew), LONG{0x2000});
    check(not pe_image(module), "pe_image: e_lfanew past the first page - none");
    make(image);
    put(image, lfanew + 4 + offsetof(IMAGE_FILE_HEADER, NumberOfSections), WORD{97});
    check(not pe_image(module), "pe_image: 97 sections - none");
    make(image);
    put(image, lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader.Magic), WORD{IMAGE_NT_OPTIONAL_HDR32_MAGIC});
    check(not pe_image(module), "pe_image: PE32 - none");
    make(image);
    put(image, lfanew + 4 + offsetof(IMAGE_FILE_HEADER, SizeOfOptionalHeader), WORD{0x70});
    check(not pe_image(module), "pe_image: an optional header too short for its directories - none");
    make(image);
    put(image, lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader.SizeOfHeaders), DWORD{0x1800});
    std::optional<pe_image_t> const long_headers{pe_image(module)};
    check(long_headers and long_headers->headers() == 0x1000, "pe_image: SizeOfHeaders past the first section - cut at it");

    make(image);
    put(image, lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader.SizeOfImage), DWORD{0x10000});
    std::optional<pe_image_t> const larger{pe_image(module)};
    check(larger and larger->size() == 0x4000 and larger->size_differs(), "pe_image: SizeOfImage past the allocation - the allocation's");
    make(image);
    std::optional<pe_image_t> const by_loader{pe_image(module, 0x2000)};
    check(by_loader and by_loader->size() == 0x2000 and by_loader->size_differs() and not by_loader->span(0x2000, 8),
          "pe_image: the loader's smaller size is the bound");

    make(image);
    put(image, 0x1142, std::uint16_t{7});
    std::optional<pe_image_t> const ordinal{pe_image(module)};
    check(ordinal and not exports_whole(*ordinal) and export_address(*ordinal, "beta") == nullptr,
          "for_each_export, export_address: an ordinal past the functions - out of shape");
    make(image);
    put_text(image, 0x1300, "KERNEL32.#5");
    std::optional<pe_image_t> const by_ordinal{pe_image(module)};
    check(by_ordinal and export_address(*by_ordinal, "beta") == nullptr, "export_address: a forwarder by ordinal - not followed");
    make(image);
    put(image, 0x1000 + offsetof(IMAGE_EXPORT_DIRECTORY, AddressOfNames), DWORD{0x2FFE});
    std::optional<pe_image_t> const names_past{pe_image(module)};
    check(names_past and not exports_whole(*names_past), "for_each_export: the names' table past the bound - out of shape");
    make(image);
    put(image, 0x1124, std::uint32_t{0x1FFB});
    std::memcpy(image + 0x1FFB, "gamma", 5);
    std::optional<pe_image_t> const no_nul{pe_image(module)};
    check(no_nul and not exports_whole(*no_nul) and no_nul->name_at(0x1FFB) == std::nullopt,
          "for_each_export: a name without its NUL in its section - out of shape");
    make(image);
    // a decorated name of 300 characters before the others (sorted: '?' before letters)
    put(image, 0x1120, std::uint32_t{0x1800});
    put_text(image, 0x1800, "?" + std::string(299, 'x'));
    std::optional<pe_image_t> const long_name{pe_image(module)};
    int long_seen{};
    check(long_name
            and for_each_export(*long_name,
                                [&](export_t const & e) noexcept
                                {
                                  long_seen += e.name.size() == 300 ? 1 : 0;
                                  return true;
                                })
            and long_seen == 1 and export_address(*long_name, "beta") == tick,
          "for_each_export: a name of 300 characters read, the walk goes on");
    make(image);
    put(image, table + offsetof(IMAGE_SECTION_HEADER, Characteristics), DWORD{IMAGE_SCN_CNT_INITIALIZED_DATA});
    std::optional<pe_image_t> const unreadable{pe_image(module)};
    check(unreadable and not exports_whole(*unreadable) and export_address(*unreadable, "alpha") == nullptr,
          "for_each_export: a section without the read right - not read");

    make(image);
    put(image, 0x1400 + offsetof(IMAGE_IMPORT_DESCRIPTOR, OriginalFirstThunk), DWORD{0});
    std::optional<pe_image_t> const no_names{pe_image(module)};
    if(no_names)
      {
      imports_t const i{imports_of(*no_names)};
      check(i.scan.without_names == 1 and i.by_name == 0 and not i.scan.malformed,
            "for_each_import: a descriptor without OriginalFirstThunk - skipped, counted");
      }
    make(image);
    put(image, lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader.DataDirectory) + IMAGE_DIRECTORY_ENTRY_IAT * sizeof(IMAGE_DATA_DIRECTORY),
        IMAGE_DATA_DIRECTORY{0x2010, 8});
    std::optional<pe_image_t> const outside_iat{pe_image(module)};
    if(outside_iat)
      {
      imports_t const i{imports_of(*outside_iat)};
      check(i.slots == 0 and i.scan.slots_refused == 2 and i.by_name == 1, "for_each_import: slots outside the IAT directory - not given");
      }
    make(image);
    put(image, lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader.DataDirectory) + IMAGE_DIRECTORY_ENTRY_IMPORT * sizeof(IMAGE_DATA_DIRECTORY),
        IMAGE_DATA_DIRECTORY{0x1400, 10});
    std::optional<pe_image_t> const short_size{pe_image(module)};
    if(short_size)
      {
      imports_t const i{imports_of(*short_size)};
      check(i.scan.past_size and i.by_name == 1, "for_each_import: descriptors past the directory's Size - walked, said");
      }
    make(image);
    put(image, 0x1508, std::uint64_t{0x2FFF});
    std::optional<pe_image_t> const name_past{pe_image(module)};
    if(name_past)
      check(imports_of(*name_past).scan.malformed, "for_each_import: an import's name past its section - out of shape");
    make(image);
    put(image, 0x1400 + offsetof(IMAGE_IMPORT_DESCRIPTOR, FirstThunk), DWORD{0x2FFC});
    std::optional<pe_image_t> const slot_past{pe_image(module)};
    if(slot_past)
      check(imports_of(*slot_past).scan.malformed, "for_each_import: the slots' thunks past their section - out of shape");

    make(image);
    put(image, 0x1400 + offsetof(IMAGE_IMPORT_DESCRIPTOR, Name), DWORD{0});
    std::optional<pe_image_t> const no_dll_name{pe_image(module)};
    if(no_dll_name)
      {
      imports_t const i{imports_of(*no_dll_name)};
      check(i.scan.descriptors == 0 and i.by_name == 0 and not i.scan.malformed, "for_each_import: a descriptor without a name ends the walk");
      }
    make(image);
    put(image, lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader.DataDirectory) + IMAGE_DIRECTORY_ENTRY_IAT * sizeof(IMAGE_DATA_DIRECTORY),
        IMAGE_DATA_DIRECTORY{0, 0});
    put(image, 0x1400 + offsetof(IMAGE_IMPORT_DESCRIPTOR, FirstThunk), DWORD{0x100});
    std::optional<pe_image_t> const slots_in_headers{pe_image(module)};
    if(slots_in_headers)
      {
      imports_t const i{imports_of(*slots_in_headers)};
      check(i.slots == 0 and i.scan.slots_refused == 2, "for_each_import: slots in the headers, no IAT directory - not given");
      }
    make(image);
    put(image, 0x1500, std::uint64_t{0x1700} | (std::uint64_t{1} << 40));
    std::optional<pe_image_t> const high_bits{pe_image(module)};
    if(high_bits)
      check(imports_of(*high_bits).scan.malformed, "for_each_import: a thunk by name with bit 40 set - out of shape");

    check(not pe_image(reinterpret_cast<HMODULE>(image + 0x1000)), "pe_image: data is no image");
    VirtualFree(image, 0, MEM_RELEASE);
    }
  }  // namespace

auto pe_tests(check_t checker) -> void
  {
  check = checker;
  process_modules();
  made_image();
  }
