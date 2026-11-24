// The checks of the module ini with files (ini/ini.h) and of append_to_file's modes, under wine: an ini made, completed
// by its own line ends, left alone when UTF-16 or when it cannot be read, read back; a write that fails part way (a new
// file taken away, one that was there cut back to its length); the watch's looks. The files in the directory `ini`
// under the current one (tools/test.sh runs the program in a new directory).
#include <edplugin_common/file/file.h>
#include <edplugin_common/ini/ini.h>

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace
  {
  using check_t = void (*)(bool ok, char const * what);
  check_t check{};

  using namespace edplugin_common;

  struct test_settings_t
    {
    float level{1.5f};
    std::uint32_t count{7};
    std::wstring folder{L"C:\\x"};
    std::vector<std::uint64_t> list{1, 2};
    };

  ini_setting_t<test_settings_t> const rows[]{
    {"level", &test_settings_t::level, 0, setting_read_e::live, "0..2 (clamped)", "how much"},
    {"count", &test_settings_t::count, 0, setting_read_e::at_start, "", "how many"},
    {"folder", &test_settings_t::folder, 0, setting_read_e::at_start, "a folder", "where", {}, {}, value_e::raw},
    {"list", ini_list_t<test_settings_t>{&test_settings_t::list}, 0, setting_read_e::live, "", "shaders"},
  };
  ini_table_t<test_settings_t> const table{rows};
  ini_texts_t const texts{"; test\n", "; added by test\n"};

  auto read_file(std::wstring const & path) -> std::string
    {
    std::optional<std::string> const text{read_text(path)};
    return text ? *text : std::string{"<unreadable>"};
    }

  auto put_file(std::wstring const & path, std::string_view text) -> bool { return write_whole_file(path, text.data(), text.size()); }

  auto appends(std::wstring const & dir) -> void
    {
    std::wstring const made{dir + L"\\made.txt"};
    check(append_to_file(made, "ab", 2, append_e::create_new) and read_file(made) == "ab", "append_to_file create_new: made");
    check(not append_to_file(made, "cd", 2, append_e::create_new) and GetLastError() == ERROR_FILE_EXISTS and read_file(made) == "ab",
          "append_to_file create_new: one that is there refused, ERROR_FILE_EXISTS, untouched");
    check(append_to_file(made, "cd", 2, append_e::append_existing) and read_file(made) == "abcd", "append_to_file append_existing: at its end");
    std::wstring const none{dir + L"\\none.txt"};
    check(not append_to_file(none, "x", 1, append_e::append_existing) and GetLastError() == ERROR_FILE_NOT_FOUND and write_time(none) == 0,
          "append_to_file append_existing: none made");
    std::wstring const cut_new{dir + L"\\cut-new.txt"};
    bool undone{};
    check(not append_to_file_cut(cut_new, "abcdef", 6, append_e::create_new, 3, &undone) and GetLastError() == ERROR_WRITE_FAULT
            and undone and write_time(cut_new) == 0,
          "append_to_file create_new: not written whole - taken away, the error kept");
    undone = false;
    check(not append_to_file_cut(made, "efgh", 4, append_e::append_existing, 2, &undone) and GetLastError() == ERROR_WRITE_FAULT
            and undone and read_file(made) == "abcd",
          "append_to_file append_existing: not written whole - cut back to its length");
    check(append_to_file_cut(made, "ef", 2, append_e::append_existing, 2) and read_file(made) == "abcdef",
          "append_to_file_cut: a cut at the end is a whole write");
    }

  auto completes(std::wstring const & dir) -> void
    {
    test_settings_t const d{};
    std::wstring const fresh{dir + L"\\fresh.ini"};
    ini_complete_t const made{complete_ini(fresh, table, d, texts)};
    std::string const text{read_file(fresh)};
    check(made.status == ini_complete_e::made and made.count == 4
            and text == "; test\r\n\r\n; how much\r\n;level = 1.5\r\n\r\n; how many\r\n;count = 7\r\n\r\n; where\r\n;folder = C:\\x\r\n"
                        "\r\n; shaders\r\n;list = 0000000000000001, 0000000000000002\r\n",
          "complete_ini: a new file, \\r\\n, every setting as a comment");
    check(complete_ini(fresh, table, d, texts).status == ini_complete_e::nothing, "complete_ini: again, nothing");
    test_settings_t s{};
    ini_read_t r;
    ini_told_t told;
    check(read_ini_file(fresh, table, s, r, told) == ini_file_e::read and r.notes.empty() and s.level == 1.5f,
          "read_ini_file: the new file, all comments: the defaults, no note");

    std::wstring const lf{dir + L"\\lf.ini"};
    put_file(lf, "level = 0.5\nfolder = D:\\Games #2;x");
    ini_complete_t const added{complete_ini(lf, table, d, texts)};
    check(added.status == ini_complete_e::added and added.count == 2
            and read_file(lf) == "level = 0.5\nfolder = D:\\Games #2;x\n\n; added by test\n\n; how many\n;count = 7\n\n; shaders\n"
                                 ";list = 0000000000000001, 0000000000000002\n",
          "complete_ini: added by the file's \\n, its last line ended first");
    test_settings_t t{};
    ini_read_t r2;
    check(read_ini_file(lf, table, t, r2, told) == ini_file_e::read and t.level == 0.5f and t.folder == L"D:\\Games #2;x"
            and r2.lines[0] == 1 and r2.lines[2] == 2,
          "read_ini_file: the values, a folder whole");

    std::wstring const spaces{dir + L"\\spaces.ini"};
    put_file(spaces, " \n\t\n");
    ini_complete_t const filled{complete_ini(spaces, table, d, texts)};
    check(filled.status == ini_complete_e::made and filled.count == 4 and read_file(spaces).starts_with(" \n\t\n; test\n\n; how much\n"),
          "complete_ini: a file of spaces filled after them, by its \\n");
    std::wstring const empty{dir + L"\\empty.ini"};
    put_file(empty, "");
    ini_complete_t const emptied{complete_ini(empty, table, d, texts)};
    check(emptied.status == ini_complete_e::made and read_file(empty).starts_with("; test\r\n\r\n; how much\r\n"),
          "complete_ini: an empty file there filled, \\r\\n");

    std::wstring const utf16{dir + L"\\utf16.ini"};
    put_file(utf16, "\xFF\xFEl\0=\0"sv);
    test_settings_t u{};
    ini_read_t r3;
    check(complete_ini(utf16, table, d, texts).status == ini_complete_e::utf16 and read_file(utf16) == "\xFF\xFEl\0=\0"sv
            and read_ini_file(utf16, table, u, r3, told) == ini_file_e::utf16,
          "complete_ini, read_ini_file: UTF-16 neither added to nor read");

    std::wstring const missing{dir + L"\\missing.ini"};
    test_settings_t m{};
    ini_read_t r4;
    check(read_ini_file(missing, table, m, r4, told) == ini_file_e::none and r4.lines.size() == std::size(rows), "read_ini_file: no file");

    // held unshared: neither read nor added to
    HANDLE const held{CreateFileW(fresh.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    test_settings_t h{};
    ini_read_t r5;
    check(held != INVALID_HANDLE_VALUE and complete_ini(fresh, table, d, texts).status == ini_complete_e::unreadable
            and read_ini_file(fresh, table, h, r5, told) == ini_file_e::unreadable,
          "complete_ini, read_ini_file: a file that cannot be read");
    if(held != INVALID_HANDLE_VALUE)
      CloseHandle(held);
    }

  auto set_time(std::wstring const & path, std::uint64_t time) -> void
    {
    HANDLE const f{CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr)};
    FILETIME const t{static_cast<DWORD>(time), static_cast<DWORD>(time >> 32)};
    SetFileTime(f, nullptr, nullptr, &t);
    CloseHandle(f);
    }

  auto watches(std::wstring const & dir) -> void
    {
    std::wstring const later{dir + L"\\later.ini"};
    ini_watch_t none;
    none.set_path(later);
    none.mark_seen();
    check(none.look(1'000) == ini_look_e::same, "ini_watch_t: no file at the start - not taken away");
    put_file(later, "level = 1\n");
    check(none.look(2'000) == ini_look_e::changed, "ini_watch_t: a file made later - changed");

    std::wstring const path{dir + L"\\watched.ini"};
    put_file(path, "level = 1\n");
    set_time(path, 133000000000000000ull);
    ini_watch_t watch;
    watch.set_path(path);
    watch.mark_seen();
    check(watch.look(10'000) == ini_look_e::same and watch.look(10'500) == ini_look_e::not_due, "ini_watch_t: seen, then not due within a second");
    set_time(path, 133000000010000000ull);
    check(watch.look(11'000) == ini_look_e::changed and watch.look(12'000) == ini_look_e::same, "ini_watch_t: saved since - changed, once");
    watch.ask_soon();
    check(watch.look(12'100) == ini_look_e::changed, "ini_watch_t: asked - read within the second, whatever its time");
    DeleteFileW(path.c_str());
    check(watch.look(13'200) == ini_look_e::gone and watch.look(14'300) == ini_look_e::same, "ini_watch_t: taken away - gone once");
    put_file(path, "level = 2\n");
    check(watch.look(15'400) == ini_look_e::changed, "ini_watch_t: back - changed");
    check(watch.unreadable() and not watch.unreadable() and watch.look(16'500) == ini_look_e::changed,
          "ini_watch_t: unreadable said once, its time forgotten - read again at the next look");
    watch.readable();
    check(watch.unreadable(), "ini_watch_t: after a read, unreadable said again");
    }
  }  // namespace

auto ini_tests(void (*checker)(bool ok, char const * what)) -> void
  {
  check = checker;
  CreateDirectoryW(L"ini", nullptr);
  wchar_t here[MAX_PATH]{};
  GetCurrentDirectoryW(MAX_PATH, here);
  std::wstring const dir{std::wstring{here} + L"\\ini"};
  appends(dir);
  completes(dir);
  watches(dir);
  }
