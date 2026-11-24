// The checks of the modules paths, file, memory (with pass_modules), shader and dxgi, in the directory `modules` under
// the current one (tools/test.sh runs the program in a new directory, with test_exports.dll beside it).
#include <edplugin_common/dxgi/format.h>
#include <edplugin_common/file/file.h>
#include <edplugin_common/log/log.h>
#include <edplugin_common/memory/memory.h>
#include <edplugin_common/memory/pass_modules.h>
#include <edplugin_common/memory/pe.h>
#include <edplugin_common/paths/paths.h>
#include <edplugin_common/shader/names_of.h>
#include <edplugin_common/shader/shader.h>

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>

using namespace std::string_view_literals;

namespace
  {
  using check_t = void (*)(bool ok, char const * what);
  check_t check{};
  std::wstring root;

  auto read_file(std::wstring const & path) -> std::string
    {
    std::optional<std::string> const text{edplugin_common::read_text(path)};
    return text.value_or(std::string{});
    }

  auto paths() -> void
    {
    std::wstring const long_value(300, L'x');
    SetEnvironmentVariableW(L"EDPC_TEST_LONG", long_value.c_str());
    check(edplugin_common::environment(L"EDPC_TEST_LONG") == long_value, "environment: a value of 300 characters whole");
    SetEnvironmentVariableW(L"EDPC_TEST_LONG", nullptr);
    check(edplugin_common::environment(L"EDPC_TEST_LONG").empty(), "environment: not set - empty");
    SetEnvironmentVariableW(L"EDPC_TEST_EMPTY", L"");
    check(edplugin_common::environment(L"EDPC_TEST_EMPTY").empty(), "environment: empty - empty");

    std::wstring const exe{edplugin_common::module_path(nullptr)};
    check(edplugin_common::file_name_of(exe) == L"edplugin_common_test.exe", "module_path(nullptr): the test's exe");
    check(edplugin_common::directory_of(L"C:\\a\\b.dll") == L"C:\\a" and edplugin_common::directory_of(L"C:/a/b.dll") == L"C:/a"
            and edplugin_common::directory_of(L"b.dll").empty(),
          "directory_of: before the last \\ or /, empty without one");
    check(edplugin_common::file_name_of(L"C:\\a\\b.dll") == L"b.dll" and edplugin_common::file_name_of(L"C:/a/b.dll") == L"b.dll"
            and edplugin_common::file_name_of(L"b.dll") == L"b.dll",
          "file_name_of: after the last \\ or /, the whole without one");

    SetEnvironmentVariableW(L"EDPC_TEST_DIR", L"C:\\given");
    check(edplugin_common::plugin_dir(L"EDPC_TEST_DIR", nullptr) == L"C:\\given", "plugin_dir: the variable's folder");
    SetEnvironmentVariableW(L"EDPC_TEST_DIR", nullptr);
    check(edplugin_common::plugin_dir(L"EDPC_TEST_DIR", nullptr) == edplugin_common::directory_of(exe),
          "plugin_dir: without the variable, the module's directory");
    check(edplugin_common::edloader_config_dir_variable == L"EDLOADER_CONFIG_DIR"sv
            and edplugin_common::edloader_log_dir_variable == L"EDLOADER_LOG_DIR"sv,
          "edloader's variables by name");

    // the game's build from two threads at once: one text, told once
    static std::string_view keys[2];
    HANDLE threads[2]{};
    for(int i{}; i != 2; ++i)
      threads[i] = CreateThread(
        nullptr, 0,
        [](void * at) -> DWORD
        {
          *static_cast<std::string_view *>(at) = edplugin_common::game_build_key();
          return 0;
        },
        &keys[i], 0, nullptr);
    if(not threads[0] or not threads[1])
      {
      check(false, "game_build_key: two threads could not start");
      for(HANDLE const t: threads)
        if(t)
          {
          WaitForSingleObject(t, 5000);
          CloseHandle(t);
          }
      return;
      }
    WaitForMultipleObjects(2, threads, TRUE, 5000);
    for(HANDLE const t: threads)
      CloseHandle(t);
    WIN32_FILE_ATTRIBUTE_DATA a{};
    GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &a);
    std::string const expected{std::format("{}-{:08x}{:08x}", a.nFileSizeLow, a.ftLastWriteTime.dwHighDateTime, a.ftLastWriteTime.dwLowDateTime)};
    check(keys[0] == expected and keys[1].data() == keys[0].data() and edplugin_common::game_build_key().data() == keys[0].data(),
          "game_build_key: the exe's size and last write, one text for two threads");
    }

  auto files() -> void
    {
    std::wstring const dir{root + L"\\files"};
    CreateDirectoryW(dir.c_str(), nullptr);
    std::optional<std::string> const missing{edplugin_common::read_text(dir + L"\\none.txt")};
    check(missing and missing->empty(), "read_text: no such file - empty");
    std::wstring const sub{dir + L"\\sub"};
    CreateDirectoryW(sub.c_str(), nullptr);
    check(not edplugin_common::read_text(sub), "read_text: a directory is not an empty file - none");
    check(edplugin_common::write_time(dir + L"\\none.txt") == 0, "write_time: no such file - 0");

    std::wstring const whole{dir + L"\\whole.bin"};
    check(edplugin_common::write_whole_file(whole, "abc", 3) and read_file(whole) == "abc", "write_whole_file: written whole");
    check(edplugin_common::write_time(whole) != 0, "write_time: a file's last write");
    check(edplugin_common::write_whole_file(whole, "xy", 2) and read_file(whole) == "xy", "write_whole_file: made anew");

    std::wstring const logs{dir + L"\\log"};
    CreateDirectoryW(logs.c_str(), nullptr);
    edplugin_common::log_open(logs, L"f");
    bool const refused{not edplugin_common::write_whole_file(dir + L"\\no-such-dir\\x.bin", "a", 1)};
    edplugin_common::log_close();
    std::string const log{read_file(logs + L"\\f.log")};
    check(refused and log.find("file: ") != std::string::npos and log.find("could not be made (error ") != std::string::npos
            and log.find("file: ", log.find("file: ") + 1) == std::string::npos,
          "write_whole_file: no directory - false, one line in the log");

    edplugin_common::log_open(logs, L"m");
    void * const no_dir{edplugin_common::map_file_rw(dir + L"\\no-such-dir\\shared", 64, "share")};
    void * const no_size{edplugin_common::map_file_rw(dir + L"\\zero", 0, "atmo")};
    edplugin_common::log_close();
    std::string const map_log{read_file(logs + L"\\m.log")};
    check(not no_dir and map_log.find("share: cannot open ") != std::string::npos and map_log.find("; not publishing") != std::string::npos,
          "map_file_rw: no directory - nullptr, the line with its tag");
    check(not no_size and map_log.find("atmo: cannot map ") != std::string::npos and map_log.find("(error 87)") != std::string::npos,
          "map_file_rw: 0 bytes refused, error 87");
    check(edplugin_common::map_file_ro(dir + L"\\whole.bin", 0, 0).view == nullptr, "map_file_ro: most 0 - none");

    std::wstring const appended{dir + L"\\appended.txt"};
    check(edplugin_common::append_to_file(appended, "a\n", 2) and edplugin_common::append_to_file(appended, "b\n", 2)
            and read_file(appended) == "a\nb\n",
          "append_to_file: twice, in order");

    // shared memory over a file: a new one, an existing smaller one grown, an existing larger one kept
    std::wstring const shared{dir + L"\\shared"};
    auto * const rw{static_cast<std::uint32_t *>(edplugin_common::map_file_rw(shared, 64, "test"))};
    check(rw != nullptr, "map_file_rw: a new file mapped");
    if(rw)
      {
      rw[0] = 0x12345678u;
      edplugin_common::mapped_t const ro{edplugin_common::map_file_ro(shared, 4, 1024)};
      check(ro.view and ro.bytes == 64 and *static_cast<std::uint32_t const *>(ro.view) == 0x12345678u,
            "map_file_ro: another view sees the write, at most the file's size");
      edplugin_common::mapped_t const cut{edplugin_common::map_file_ro(shared, 4, 16)};
      check(cut.view and cut.bytes == 16, "map_file_ro: a file larger than most - most");
      }
    check(edplugin_common::map_file_ro(shared, 65, 128).view == nullptr, "map_file_ro: smaller than least - none");
    check(edplugin_common::map_file_ro(dir + L"\\none", 1, 8).view == nullptr, "map_file_ro: no such file - none");
    std::wstring const small{dir + L"\\small"};
    edplugin_common::write_whole_file(small, "abcd", 4);
    void * const grown{edplugin_common::map_file_rw(small, 256, "test")};
    WIN32_FILE_ATTRIBUTE_DATA a{};
    GetFileAttributesExW(small.c_str(), GetFileExInfoStandard, &a);
    check(grown and a.nFileSizeLow == 256 and std::memcmp(grown, "abcd", 4) == 0, "map_file_rw: a smaller file grown, its bytes kept");
    std::wstring const large{dir + L"\\large"};
    std::string const big(1000, 'z');
    edplugin_common::write_whole_file(large, big.data(), big.size());
    void * const kept{edplugin_common::map_file_rw(large, 100, "test")};
    GetFileAttributesExW(large.c_str(), GetFileExInfoStandard, &a);
    check(kept and a.nFileSizeLow == 1000, "map_file_rw: a larger file keeps its size");
    }

  ///\brief functions and data the sections are told by
  [[gnu::noinline]] auto in_text() -> int { return 7; }

  constexpr char in_rdata[]{"constant"};

  auto names_checks(std::uint8_t * pages, std::size_t page) -> void;
  auto exports_checks(std::uint8_t * pages, std::size_t page) -> void;
  auto pass_modules_checks() -> void;

  auto memories() -> void
    {
    int on_stack{};
    check(edplugin_common::readable(&on_stack, sizeof on_stack), "readable: the stack");
    check(not edplugin_common::readable(nullptr, 1), "readable: nullptr");
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    std::size_t const page{si.dwPageSize};
    auto * const pages{static_cast<std::uint8_t *>(VirtualAlloc(nullptr, 3 * page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))};
    if(not pages)
      {
      check(false, "memory: three pages for the checks");
      return;
      }
    DWORD old{};
    VirtualProtect(pages + page, page, PAGE_NOACCESS, &old);
    VirtualProtect(pages + 2 * page, page, PAGE_READWRITE | PAGE_GUARD, &old);
    check(edplugin_common::readable(pages, page), "readable: a read-write page whole");
    check(not edplugin_common::readable(pages + page - 4, 8), "readable: a range into a no-access page - false");
    check(not edplugin_common::readable(pages + page, 1), "readable: a no-access page - false");
    check(not edplugin_common::readable(pages + 2 * page, 1), "readable: a guard page - false");
    check(edplugin_common::executable(reinterpret_cast<void const *>(&in_text)) and not edplugin_common::executable(pages),
          "executable: code yes, data no");
    HMODULE const exe{GetModuleHandleW(nullptr)};
    check(edplugin_common::module_of(reinterpret_cast<void const *>(&in_text)) == exe and edplugin_common::module_of(&on_stack) == nullptr,
          "module_of: a function in the exe, the stack in none");
    char section[9]{};
    edplugin_common::section_of(exe, reinterpret_cast<void const *>(&in_text), section);
    check(std::string_view{section} == ".text", "section_of: a function in .text");
    edplugin_common::section_of(exe, in_rdata, section);
    check(std::string_view{section} == ".rdata", "section_of: a constant in .rdata");
    check(not edplugin_common::pe_image(reinterpret_cast<HMODULE>(pages)) and edplugin_common::pe_image(exe),
          "pe_image: data is no module, the exe is");
    edplugin_common::section_of(reinterpret_cast<HMODULE>(pages), pages, section);
    check(std::string_view{section} == "-", "section_of: no module - \"-\"");

    names_checks(pages, page);
    exports_checks(pages, page);
    check(VirtualFree(pages, 0, MEM_RELEASE) != 0, "memory: the test's pages freed");
    pass_modules_checks();
    }

  ///\brief memory/pass_modules.h's comparison of a dll's name with a list (Windows' ordinal one, without case)
  auto pass_modules_checks() -> void
    {
    using edplugin_common::passed_over;
    check(passed_over(L"BDHKM64.DLL"sv, edplugin_common::default_pass_modules), "passed_over: the default list, case aside");
    check(passed_over(L"x.dll"sv, L" a.dll , x.dll "sv) and not passed_over(L"x.dl"sv, L"x.dll"sv),
          "passed_over: a name of the list trimmed, a shorter one not");
    check(not passed_over(L"y.dll"sv, L"x.dll, a.dll"sv), "passed_over: a name of the same length but another not");
    check(not passed_over(L""sv, L"a.dll,,"sv) and not passed_over(L"a.dll"sv, L""sv),
          "passed_over: an empty name and an empty list match none");
    check(passed_over(L"\u00c9.dll"sv, L"\u00e9.dll"sv), "passed_over: case aside beyond ASCII (E and e with an acute)");
    }

  auto names_checks(std::uint8_t * pages, std::size_t page) -> void
    {
    constexpr std::uint8_t ab[]{'a', 'b'};
    check(edplugin_common::fnv1a64(ab + 1, 1, edplugin_common::fnv1a64(ab, 1)) == edplugin_common::fnv1a64(ab, 2),
          "fnv1a64: going on from a hash so far");
    edplugin_common::names_t const named{edplugin_common::names_of(ab, 2)};
    check(named.read and named.name == edplugin_common::fnv1a64(ab, 2) and named.migoto == edplugin_common::fnv_3dmigoto(ab, 2),
          "names_of: both names of readable bytes");
    edplugin_common::names_t const faulted{edplugin_common::names_of(pages + page, 16)};
    check(not faulted.read and faulted.name == 0, "names_of: bytes that cannot be read - not read, no fault out");
    check(not edplugin_common::names_of(nullptr, 4).read, "names_of: no bytecode - not read");
    char text[24];
    check(edplugin_common::migoto_name_text(0x1234u, text) == "3dmgt_0000000000001234", "migoto_name_text: 3dmgt_ and 16 hex digits");
    check(edplugin_common::is_integer(DXGI_FORMAT_R16_UINT) and not edplugin_common::is_integer(DXGI_FORMAT_R16_FLOAT), "is_integer");
    }

  ///\brief headers of an image made in memory: an export directory at 0x1000 (a readable section on the no-access
  /// page) faults, one past the image's size is out of shape
  auto exports_checks(std::uint8_t * pages, std::size_t page) -> void
    {
    char name[64];
    DWORD fault{};
    HMODULE const fake{LoadLibraryW(L"test_exports.dll")};
    check(fake and edplugin_common::first_export_with_prefix(fake, "edpc_", name, fault) == edplugin_common::export_scan_e::found
            and std::string_view{name} == "edpc_test_export",
          "first_export_with_prefix: the test dll's export");
    check(edplugin_common::first_export_with_prefix(GetModuleHandleW(L"kernel32.dll"), "edpc_", name, fault)
            == edplugin_common::export_scan_e::none,
          "first_export_with_prefix: none in kernel32");
    check(fake and edplugin_common::first_export_with_prefix(fake, "", name, fault) == edplugin_common::export_scan_e::none,
          "first_export_with_prefix: an empty prefix finds none");
    if(fake)
      FreeLibrary(fake);

    if(page != 0x1000)
      {
      // the made image's directory at 0x1000 must be the second page
      std::printf("skip: first_export_with_prefix fault and malformed (page size %zu, not 4 KB)\n", page);
      return;
      }
    std::memset(pages, 0, page);
    auto * const dos{reinterpret_cast<IMAGE_DOS_HEADER *>(pages)};
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 64;
    auto * const nt{reinterpret_cast<IMAGE_NT_HEADERS64 *>(pages + 64)};
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.NumberOfSections = 1;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    nt->OptionalHeader.SizeOfImage = 0x2000;
    nt->OptionalHeader.SizeOfHeaders = 0x400;
    auto * const section{reinterpret_cast<IMAGE_SECTION_HEADER *>(pages + 64 + sizeof(IMAGE_NT_HEADERS64))};
    std::memcpy(section->Name, ".edata", 6);
    section->VirtualAddress = 0x1000;
    section->Misc.VirtualSize = 0x1000;
    section->Characteristics = IMAGE_SCN_MEM_READ;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT] = IMAGE_DATA_DIRECTORY{0x1000, sizeof(IMAGE_EXPORT_DIRECTORY)};
    auto const image{reinterpret_cast<HMODULE>(pages)};
    check(edplugin_common::first_export_with_prefix(image, "edpc_", name, fault) == edplugin_common::export_scan_e::fault
            and fault == EXCEPTION_ACCESS_VIOLATION,
          "first_export_with_prefix: a fault reading the directory - fault, its code");
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT] = IMAGE_DATA_DIRECTORY{0x3000, sizeof(IMAGE_EXPORT_DIRECTORY)};
    check(edplugin_common::first_export_with_prefix(image, "edpc_", name, fault) == edplugin_common::export_scan_e::malformed,
          "first_export_with_prefix: a directory past the image - malformed");
    }
  }  // namespace

auto modules_tests(check_t checker) -> void
  {
  check = checker;
  wchar_t here[MAX_PATH]{};
  GetCurrentDirectoryW(MAX_PATH, here);
  root = std::wstring{here} + L"\\modules";
  CreateDirectoryW(root.c_str(), nullptr);
  paths();
  files();
  memories();
  }
