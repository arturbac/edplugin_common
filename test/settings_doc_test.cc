// The checks of the module settings: settings_doc against the expected text byte for byte, its escapes and errors,
// and write_settings_doc's results, its files in the directory `settings_doc` under the current one (tools/test.sh
// runs the program in a new directory).
#include <edplugin_common/settings/settings_doc.h>

#include <windows.h>

#include <string>
#include <string_view>

using namespace std::string_view_literals;

namespace
  {
  using check_t = void (*)(bool ok, char const * what);
  check_t check{};

  using edplugin_common::setting_read_e;
  using edplugin_common::setting_row_t;
  using edplugin_common::settings_doc_error_e;
  using edplugin_common::settings_doc_status_e;
  using edplugin_common::settings_doc_t;

  constexpr std::string_view groups[]{"Clouds"sv, "Nothing here"sv, "Other"sv};

  constexpr settings_doc_t doc{
    "test settings"sv,
    "test from its table of settings (test/settings_doc_test.cc, `TEST_SETTINGS_DOC`)"sv,
    "In `test.ini`.\n"sv,
    groups};

  setting_row_t const rows[]{
    {"clouds"sv, "1"sv, 0, setting_read_e::at_start, "0 or 1"sv, "the clouds"sv},
    {"clouds_cover"sv, "0.5"sv, 0, setting_read_e::live, "0..1 (clamped)"sv, "how much sky they cover"sv},
    {"name"sv, ""sv, 2, setting_read_e::live, ""sv, "a | in a cell"sv},
    {"log"sv, "logs\\x.txt"sv, 2, setting_read_e::live, ""sv, "logs\\x_<n>.txt, `a <b>` kept in code\r\nsecond line"sv},
    {"tick"sv, "a`b"sv, 2, setting_read_e::at_start, ""sv, "a value with a backtick"sv},
    {"choice"sv, "b"sv, 2, setting_read_e::at_start, "a, b"sv, "the choice:\na = one\nb = two"sv},
    {"pipes"sv, "a|b"sv, 2, setting_read_e::at_start, "x | y, <1>"sv, "lines;\nafter a semicolon, `odd tick <kept>"sv},
  };

  constexpr std::string_view expected{
    "# test settings\n"
    "\n"
    "<!-- Made by test from its table of settings (test/settings_doc_test.cc, `TEST_SETTINGS_DOC`); tools/test.sh checks\n"
    "     it is up to date. Change the table, not this file. -->\n"
    "\n"
    "In `test.ini`.\n"
    "\n"
    "## Clouds\n"
    "\n"
    "### Read while the game runs\n"
    "\n"
    "| setting | default | range | what |\n"
    "|---|---|---|---|\n"
    "| `clouds_cover` | `0.5` | 0..1 (clamped) | how much sky they cover |\n"
    "\n"
    "### Read at the game's start (a change needs a restart)\n"
    "\n"
    "| setting | default | range | what |\n"
    "|---|---|---|---|\n"
    "| `clouds` | `1` | 0 or 1 | the clouds |\n"
    "\n"
    "## Nothing here\n"
    "\n"
    "### Read while the game runs\n"
    "\n"
    "None.\n"
    "\n"
    "### Read at the game's start (a change needs a restart)\n"
    "\n"
    "None.\n"
    "\n"
    "## Other\n"
    "\n"
    "### Read while the game runs\n"
    "\n"
    "| setting | default | range | what |\n"
    "|---|---|---|---|\n"
    "| `name` | empty |  | a \\| in a cell |\n"
    "| `log` | `logs\\x.txt` |  | logs\\x_&lt;n&gt;.txt, `a <b>` kept in code; second line |\n"
    "\n"
    "### Read at the game's start (a change needs a restart)\n"
    "\n"
    "| setting | default | range | what |\n"
    "|---|---|---|---|\n"
    "| `tick` | `` a`b `` |  | a value with a backtick |\n"
    "| `choice` | `b` | a, b | the choice: a = one; b = two |\n"
    "| `pipes` | `a\\|b` | x \\| y, &lt;1&gt; | lines; after a semicolon, `odd tick <kept> |\n"};

  auto read_file(wchar_t const * path) -> std::string
    {
    HANDLE const f{CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if(f == INVALID_HANDLE_VALUE)
      return {};
    std::string text;
    char buf[4096];
    DWORD got{};
    while(ReadFile(f, buf, sizeof buf, &got, nullptr) and got != 0)
      text.append(buf, got);
    CloseHandle(f);
    return text;
    }

  auto texts() -> void
    {
    auto const made{edplugin_common::settings_doc(doc, rows)};
    check(made.has_value() and *made == expected, "settings_doc: the template byte for byte (groups, live first, None., empty, escapes, lines after : and ;)");

    constexpr settings_doc_t no_groups{"t"sv, "t"sv, "i\n"sv, {}};
    auto const bare{edplugin_common::settings_doc(no_groups, {})};
    check(bare.has_value() and *bare == "# t\n\n<!-- Made by t; tools/test.sh checks\n     it is up to date. Change the table, not this file. -->\n\ni\n",
          "settings_doc: no groups, the head and the intro only");

    // the ";" of an entity is not the text's: a line end after "<" or ">" is "; " as after any other character
    setting_row_t const entity[]{{"e"sv, ""sv, 0, setting_read_e::live, "<a>\nb"sv, "x <\ny"sv}};
    auto const made_entity{edplugin_common::settings_doc(doc, entity)};
    check(made_entity.has_value()
            and made_entity->find("| `e` | empty | &lt;a&gt;; b | x &lt;; y |\n"sv) != std::string::npos,
          "settings_doc: a line end after < or > (an entity) is \"; \"");
    }

  auto errors() -> void
    {
    setting_row_t const out_of_range[]{{"a"sv, ""sv, 3, setting_read_e::live, ""sv, ""sv}};
    auto const r1{edplugin_common::settings_doc(doc, out_of_range)};
    check(not r1 and r1.error() == settings_doc_error_e::group_out_of_range, "settings_doc: a group out of range is an error");

    setting_row_t const twice[]{{"a"sv, ""sv, 0, setting_read_e::live, ""sv, ""sv}, {"a"sv, ""sv, 2, setting_read_e::at_start, ""sv, ""sv}};
    auto const r2{edplugin_common::settings_doc(doc, twice)};
    check(not r2 and r2.error() == settings_doc_error_e::duplicate_name, "settings_doc: a name twice is an error");

    setting_row_t const nameless[]{{""sv, ""sv, 0, setting_read_e::live, ""sv, ""sv}};
    auto const r3{edplugin_common::settings_doc(doc, nameless)};
    check(not r3 and r3.error() == settings_doc_error_e::empty_name, "settings_doc: an empty name is an error");
    }

  auto files() -> void
    {
    constexpr wchar_t const * variable{L"EDPLUGIN_COMMON_TEST_SETTINGS_DOC"};
    CreateDirectoryW(L"settings_doc", nullptr);

    SetEnvironmentVariableW(variable, nullptr);
    auto const none{edplugin_common::write_settings_doc(variable, doc, rows)};
    check(none.status == settings_doc_status_e::not_asked and none.path[0] == L'\0', "write_settings_doc: without the variable nothing");

    SetEnvironmentVariableW(variable, L"settings_doc\\settings.md");
    auto const written{edplugin_common::write_settings_doc(variable, doc, rows)};
    check(written.status == settings_doc_status_e::written and std::wstring_view{written.path} == L"settings_doc\\settings.md"
            and read_file(L"settings_doc\\settings.md") == expected,
          "write_settings_doc: the file is settings_doc's text, the path given back");

    SetEnvironmentVariableW(variable, L"settings_doc\\no such directory\\settings.md");
    auto const closed{edplugin_common::write_settings_doc(variable, doc, rows)};
    check(closed.status == settings_doc_status_e::cannot_open and closed.system_error != 0, "write_settings_doc: a file that cannot be made, its error");

    std::wstring const longest(259, L'x');
    SetEnvironmentVariableW(variable, longest.c_str());
    auto const at_most{edplugin_common::write_settings_doc(variable, doc, rows)};
    check(at_most.status != settings_doc_status_e::path_too_long and std::wstring_view{at_most.path} == longest,
          "write_settings_doc: a path of 259 characters taken whole");

    std::wstring const long_path(260, L'x');
    SetEnvironmentVariableW(variable, long_path.c_str());
    auto const too_long{edplugin_common::write_settings_doc(variable, doc, rows)};
    check(too_long.status == settings_doc_status_e::path_too_long and std::wstring_view{too_long.path} == std::wstring_view{long_path}.substr(0, 259),
          "write_settings_doc: a path of 260 characters refused, its start given back");

    setting_row_t const twice[]{{"a"sv, ""sv, 0, setting_read_e::live, ""sv, ""sv}, {"a"sv, ""sv, 0, setting_read_e::live, ""sv, ""sv}};
    SetEnvironmentVariableW(variable, L"settings_doc\\invalid.md");
    auto const invalid{edplugin_common::write_settings_doc(variable, doc, twice)};
    check(invalid.status == settings_doc_status_e::invalid and invalid.error == settings_doc_error_e::duplicate_name
            and GetFileAttributesW(L"settings_doc\\invalid.md") == INVALID_FILE_ATTRIBUTES,
          "write_settings_doc: invalid rows, no file, the error given back");

    SetEnvironmentVariableW(variable, nullptr);
    }
  }  // namespace

auto settings_doc_tests(void (*checker)(bool ok, char const * what)) -> void
  {
  check = checker;
  texts();
  errors();
  files();
  }
