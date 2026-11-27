// The checks of the module ini without Windows (ini/ini_text.h, ini/ini_table.h, ini/live.h), built natively with
// sanitizers: a line's parts and its comment by the value's kind, each reader, UTF-8 and UTF-16, a table's read with
// its notes, the start-only settings changed, the lines an ini lacks, the doc's rows, and live_settings_t with a
// thread that keeps a reference to the start-only fields through publications (ThreadSanitizer); live3_settings_t
// alone, with a producer, a consumer and a reader of the start copy on threads of their own, its global pointer
// before and after live3_start, and live_fields on the render thread and on two others (its control:
// live3_control.cc).
#include <edplugin_common/ini/ini_table.h>
#include <edplugin_common/ini/live.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::string_view_literals;

namespace
  {
  using check_t = void (*)(bool ok, char const * what);
  check_t check{};

  using namespace edplugin_common;

  enum struct mode_e
    {
    off,
    on,
    automatic
    };

  struct test_settings_t
    {
    float level{1.5f};
    std::uint32_t count{7};
    bool flag{true};
    std::wstring folder{L"C:\\x"};
    std::wstring names{L"a.dll"};
    std::uint64_t shader{0xABCull};
    std::uint32_t colour{0x102030u};
    std::vector<std::uint64_t> list{1, 2};
    mode_e mode{mode_e::on};
    std::uint32_t width{640};
    std::uint32_t height{480};
    int outside{};  ///< not in the ini
    };

  enum struct group_e : std::size_t
    {
    first,
    second
    };

  constexpr char const * mode_names[]{"off", "on", "auto"};

  constexpr ini_choice_t<test_settings_t> mode_field{
    [](test_settings_t const & s) -> std::size_t { return static_cast<std::size_t>(s.mode); },
    [](test_settings_t & s, std::size_t i) -> void { s.mode = static_cast<mode_e>(i); }, mode_names};

  constexpr ini_custom_t<test_settings_t> size_field{
    [](test_settings_t & s, std::string_view v) -> bool
      {
      std::size_t const x{v.find('x')};
      if(x == std::string_view::npos)
        return false;
      std::optional<std::uint32_t> const w{read_uint32(ini_trim(v.substr(0, x)))}, h{read_uint32(ini_trim(v.substr(x + 1)))};
      if(not w or not h)
        return false;
      s.width = *w;
      s.height = *h;
      return true;
      },
    [](test_settings_t const & s) -> std::string { return std::to_string(s.width) + "x" + std::to_string(s.height); },
    [](test_settings_t & to, test_settings_t const & from) -> void
      {
      to.width = from.width;
      to.height = from.height;
      }};

  using row_t = ini_setting_t<test_settings_t, group_e>;
  using setting_read_e::at_start;
  using setting_read_e::live;

  row_t const rows[]{
    {"level", &test_settings_t::level, group_e::first, live, "0..2 (clamped)", "how much", "more for the doc", "the level"},
    {"count", &test_settings_t::count, group_e::first, at_start, "", "how many\nsecond line"},
    {"flag", &test_settings_t::flag, group_e::first, live, "0, 1", "on or off"},
    {"folder", &test_settings_t::folder, group_e::second, at_start, "a folder", "where", {}, {}, value_e::raw},
    {"names", &test_settings_t::names, group_e::second, at_start, "dll names", "which dlls"},
    {"shader", ini_hex_t<test_settings_t>{&test_settings_t::shader}, group_e::second, live, "", "a shader"},
    {"colour", ini_colour_t<test_settings_t>{&test_settings_t::colour}, group_e::second, live, "RRGGBB", "a colour"},
    {"list", ini_list_t<test_settings_t>{&test_settings_t::list}, group_e::second, live, "", "shaders"},
    {"mode", mode_field, group_e::first, live, "off, on, auto", "the mode"},
    {"size", size_field, group_e::second, at_start, "WIDTHxHEIGHT", "a surface"},
  };
  constexpr ini_withdrawn_t withdrawn[]{{"old", "level"}, {"gone", ""}};

  ini_table_t<test_settings_t, group_e> const table{rows, withdrawn};
  ini_table_t<test_settings_t, group_e> const table_own{rows, withdrawn, ini_notes_e::own, bad_item_e::skip_item, hex_zero_e::taken};

  auto has(std::vector<std::string> const & notes, std::string_view note) -> bool
    {
    return std::find(notes.begin(), notes.end(), note) != notes.end();
    }

  auto lines() -> void
    {
    ini_line_t const a{parse_ini_line("  key = value ; c \r")};
    check(a.kind == ini_line_e::setting and a.key == "key" and a.value == " value ; c" and not a.adding,
          "parse_ini_line: key, the value before its comment is cut, the ends trimmed");
    ini_line_t const b{parse_ini_line("list += A,B")};
    check(b.kind == ini_line_e::setting and b.key == "list" and b.adding and b.value == " A,B", "parse_ini_line: +=");
    check(parse_ini_line(" \t\r").kind == ini_line_e::empty and parse_ini_line("; x = 1").kind == ini_line_e::comment
            and parse_ini_line("#x").kind == ini_line_e::comment and parse_ini_line("[section]").kind == ini_line_e::comment
            and parse_ini_line("words").kind == ini_line_e::no_equals,
          "parse_ini_line: empty, comments by ; # [, words without =");

    check(ini_value(" 1;on", comment_e::anywhere) == "1" and ini_value(" taau#x", comment_e::anywhere) == "taau",
          "ini_value: anywhere, the first ; or #");
    check(ini_value(" a.dll ;b", comment_e::after_space) == "a.dll" and ini_value(" a#b.dll", comment_e::after_space) == "a#b.dll"
            and ini_value(" #x", comment_e::after_space).empty() and ini_value("#x", comment_e::after_space) == "#x"
            and ini_value(" a\t# c", comment_e::after_space) == "a",
          "ini_value: after_space, also the space after =, not at the value's position 0");
    check(ini_value(" D:\\Games #2\\x;y ", comment_e::none) == "D:\\Games #2\\x;y", "ini_value: none keeps ; and #");

    check(ini_named_key(";k = 1") == "k" and ini_named_key("# k += A") == "k" and ini_named_key("  k=") == "k"
            and ini_named_key("; words").empty(),
          "ini_named_key: set, as a comment, with += , a line of words");
    check(ini_without_bom("\xEF\xBB\xBFk = 1") == "k = 1" and ini_is_utf16("\xFF\xFEk") and ini_is_utf16("\xFE\xFF")
            and not ini_is_utf16("k"),
          "the byte order marks");
    check(ini_line_end("a\r\nb\n") == "\r\n" and ini_line_end("a\nb") == "\n" and ini_line_end("a") == "\r\n"
            and ini_line_end("") == "\r\n",
          "ini_line_end: the file's own, \\r\\n without any");
    }

  auto readers() -> void
    {
    check(read_float("+10") == 10.f and read_float("1e3") == 1000.f and read_float("-0.5") == -0.5f, "read_float: as strtof");
    check(not read_float("0,45") and not read_float("") and not read_float("inf") and not read_float("nan") and not read_float("1x"),
          "read_float: not whole, empty, not finite");
    check(read_uint32("0") == 0u and read_uint32("4294967295") == 4294967295u and not read_uint32("4294967296")
            and not read_uint32("+1") and not read_uint32("-1") and not read_uint32(""),
          "read_uint32: digits only, 32 bits");
    check(read_bool("1") == true and read_bool("false") == false and not read_bool("yes"), "read_bool");
    check(read_hex64("0x00ABC", hex_zero_e::refused) == 0xABCu and read_hex64("00000000000000000001", hex_zero_e::refused) == 1u
            and read_hex64("FFFFFFFFFFFFFFFF", hex_zero_e::refused) == ~0ull and read_hex64("ffff", hex_zero_e::refused) == 0xFFFFu,
          "read_hex64: 0x, leading zeros, 16 digits, lower case");
    check(not read_hex64("10000000000000000", hex_zero_e::refused) and not read_hex64("-1", hex_zero_e::refused)
            and not read_hex64("0x", hex_zero_e::refused)
            and not read_hex64("12G", hex_zero_e::refused) and not read_hex64("", hex_zero_e::refused),
          "read_hex64: 17 significant digits, a minus, no digit, not hex");
    check(not read_hex64("0", hex_zero_e::refused) and read_hex64("000", hex_zero_e::taken) == 0u, "read_hex64: 0 by the flag");
    check(read_hex64("+ABC", hex_zero_e::refused) == 0xABCu and read_hex64("+0xABC", hex_zero_e::refused) == 0xABCu
            and not read_hex64("+", hex_zero_e::refused) and not read_hex64("++1", hex_zero_e::refused)
            and not read_hex64("+-1", hex_zero_e::refused),
          "read_hex64: one leading + as strtoull took it");
    check(read_hex24("FFFFFF") == 0xFFFFFFu and read_hex24("0") == 0u and not read_hex24("1000000"), "read_hex24: at most FFFFFF");
    hex_list_t const list{read_hex_list(" A, B\tC,,D ", hex_zero_e::refused)};
    check(list.names == std::vector<std::uint64_t>{0xA, 0xB, 0xC, 0xD} and list.bad.empty(),
          "read_hex_list: by commas, spaces, tabs; empty items passed over");
    hex_list_t const bad{read_hex_list("A,Q,0", hex_zero_e::refused)};
    check(bad.names == std::vector<std::uint64_t>{0xA} and bad.bad == std::vector<std::string_view>{"Q", "0"},
          "read_hex_list: the bad words (0 refused)");

    std::string const text{"za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87 \xF0\x9F\x9A\x80"};
    std::wstring const wide{ini_utf16(text)};
    check(wide.size() == 9 and static_cast<std::uint32_t>(wide[2]) == 0x17Cu and static_cast<std::uint32_t>(wide[7]) == 0xD83Du
            and ini_utf8(wide) == text,
          "ini_utf16, ini_utf8: Polish letters and a pair of surrogates, both ways");
    check(ini_utf16("a\xFF" "b") == std::wstring{L'a', static_cast<wchar_t>(0xFFFD), L'b'} and ini_utf16("\xC3").size() == 1,
          "ini_utf16: a byte that is not UTF-8, a sequence cut short");
    }

  auto reads() -> void
    {
    // a list line longer than 1023 characters (80 names)
    std::string long_list{"list = "};
    for(int i{1}; i <= 80; ++i)
      long_list += std::format("{:016X}, ", i);
    std::string const text{"\xEF\xBB\xBF"
                           "level = 2;on\n"                        // 1
                           "count = 9 # nine\r\n"                  // 2
                           "words\n"                               // 3
                           "nope = 1\n"                            // 4
                           "old = 3\n"                             // 5
                           "old = 4\n"                             // 6
                           "flag += 1\n"                           // 7
                           "folder = D:\\Games #2\\x;y\n"          // 8
                           "names = b.dll c.dll ;the dlls\n"       // 9
                           "shader = -1\n"                         // 10
                           "colour = 0x00FF00\n"                   // 11
                           "list += 2, 3\n"                        // 12
                           "mode = sometimes\n"                    // 13
                           "size = 800 x 600\n"                    // 14
                           "level = 1,5\n"                         // 15
                           + long_list + "\n"                      // 16
                           "list += 1, 1, 51\n"};                  // 17 (0x51 = 81)
    test_settings_t s{};
    ini_read_t r;
    ini_told_t told;
    read_ini_text(text, table, s, r, told);
    check(s.level == 2.f and s.count == 9u and s.folder == L"D:\\Games #2\\x;y" and s.names == L"b.dll c.dll"
            and s.colour == 0x00FF00u and s.width == 800u and s.height == 600u,
          "read_ini_text: the values by their comment rules, a BOM passed over");
    check(has(r.notes, "line 3 has no '=': ignored") and has(r.notes, "line 4: no setting 'nope' (a typo?): ignored")
            and has(r.notes, "line 5: 'old' is no longer a setting (now level); ignored")
            and std::count_if(r.notes.begin(), r.notes.end(), [](std::string const & n) { return n.find("'old'") != std::string::npos; }) == 1
            and has(r.notes, "line 7: 'flag += 1': += is for lists only; the line is passed over"),
          "read_ini_text: no '=', a typo, a withdrawn key once, += on a value");
    check(has(r.notes, "line 10: 'shader = -1' cannot be read (16 hex digits); its value stays 0000000000000ABC") and s.shader == 0xABCu,
          "read_ini_text: a value that cannot be read stays, the kind for a row without a range");
    check(has(r.notes, "line 13: 'mode = sometimes' is none of off, on, auto; on instead") and s.mode == mode_e::on,
          "read_ini_text: a choice's unknown name gives the default");
    check(has(r.notes, "line 15: 'level = 1,5' cannot be read (0..2 (clamped)); its value stays 2") and s.level == 2.f,
          "read_ini_text: 1,5 is no number");
    std::vector<std::uint64_t> expected;
    for(std::uint64_t i{1}; i <= 81; ++i)
      expected.push_back(i);
    check(s.list == expected, "read_ini_text: += from the default, a line over 1023 characters whole, += passes over names held");
    check(r.lines[0] == 1 and r.lines[1] == 2 and r.lines[7] == 17 and r.lines[2] == 0 and r.lines[9] == 14,
          "read_ini_text: the line that set each row last (0: none; a line that cannot be read sets nothing)");

    test_settings_t twice{};
    read_ini_text("nope = 1\n", table, twice, r, told);
    check(r.notes == std::vector<std::string>{"line 1: no setting 'nope' (a typo?): ignored"}, "read_ini_text: the notes of the last read only");

    test_settings_t comments{};
    ini_read_t rc;
    read_ini_text("mode = auto;x\nsize = 800x600 # a size\nmode += on\n", table, comments, rc, told);
    check(comments.mode == mode_e::automatic and comments.width == 800u and comments.height == 600u
            and rc.notes == std::vector<std::string>{"line 3: 'mode += on': += is for lists only; the line is passed over"},
          "read_ini_text: a choice and a size of the plugin's own by the comment rule of numbers");

    test_settings_t again{};
    ini_read_t r2;
    read_ini_text("old = 1\ngone = 2\nlist = 5, 5\n", table, again, r2, told);
    check(r2.notes == std::vector<std::string>{"line 2: 'gone' is no longer a setting; ignored"}
            and again.list == std::vector<std::uint64_t>{5, 5},
          "read_ini_text: a withdrawn key said once a session; = keeps a name twice");

    test_settings_t rejected{};
    ini_read_t r3;
    read_ini_text("list = A, Q\nshader = 0\n", table, rejected, r3, told);
    check(rejected.list == std::vector<std::uint64_t>{1, 2}
            and has(r3.notes, "line 1: 'list = A, Q' cannot be read (16 hex digits each, by commas); its value stays "
                              "0000000000000001, 0000000000000002")
            and rejected.shader == 0xABCu,
          "read_ini_text: reject_line - the list as it was; 0 refused");
    test_settings_t skipped{};
    ini_read_t r4;
    read_ini_text("list = A, Q\nshader = 0\n", table_own, skipped, r4, told);
    check(skipped.list == std::vector<std::uint64_t>{0xA}
            and has(r4.notes, "line 1: 'list = A, Q': Q cannot be read (16 hex digits each, by commas); the list is "
                              "000000000000000A")
            and skipped.shader == 0u,
          "read_ini_text: skip_item - the good names and a note of the rest; 0 taken");
    }

  auto start_only() -> void
    {
    test_settings_t running{};
    running.count = 3;
    running.width = 100;
    test_settings_t read{};
    read.count = 4;
    ini_read_t r;
    r.lines.assign(std::size(rows), 0);
    r.lines[1] = 6;
    ini_told_t told;
    std::vector<std::string> notes;
    at_start_changes(table, read, r, running, told, notes);
    check(notes == std::vector<std::string>{"line 6: 'count' is read at the game's start only; the change takes effect at the next start",
                                            "'size' is read at the game's start only; the change takes effect at the next start"},
          "at_start_changes: with its line, and without one for a key taken out");
    notes.clear();
    at_start_changes(table, read, r, running, told, notes);
    check(notes.empty(), "at_start_changes: once a session per key");
    }

  auto files() -> void
    {
    test_settings_t const d{};
    ini_texts_t const texts{"; test settings\n; a line without ';' sets a value\n", "; settings added by test\n"};
    ini_missing_t const fresh{ini_completion("", table, d, texts)};
    check(fresh.blank and fresh.count == std::size(rows)
            and fresh.text.starts_with("; test settings\r\n; a line without ';' sets a value\r\n\r\n; how much\r\n;level = 1.5\r\n")
            and fresh.text.find("\r\n; how many\r\n; second line\r\n;count = 7\r\n") != std::string::npos
            and fresh.text.find(";list = 0000000000000001, 0000000000000002\r\n") != std::string::npos
            and fresh.text.find(";colour = 102030\r\n") != std::string::npos and fresh.text.find(";mode = on\r\n") != std::string::npos
            and fresh.text.find(";size = 640x480\r\n") != std::string::npos and fresh.text.find('\n') == fresh.text.find("\r\n") + 1,
          "ini_completion: a new file, \\r\\n, every block with its comment");
    ini_missing_t const lf{ini_completion("level = 1\n;count = 2\nflag = 1\nfolder = x\nnames =\nshader = 1\ncolour = 0\nlist = 1\n"
                                          "mode = on",
                                          table, d, texts)};
    check(not lf.blank and lf.count == 1 and lf.text == "\n\n; settings added by test\n\n; a surface\n;size = 640x480\n",
          "ini_completion: the file's \\n, its last line ended first, a key as a comment named, only the missing");
    ini_missing_t const crlf{ini_completion("\xEF\xBB\xBFlevel = 1\r\n", table_own, d, texts)};
    check(crlf.count == std::size(rows) - 1 and crlf.text.starts_with("\r\n; settings added by test\r\n;count = 7\r\n")
            and crlf.text.find("\r\n; the level") == std::string::npos,
          "ini_completion: a BOM's first key named, \\r\\n, own notes (none but level's)");
    ini_missing_t const blank{ini_completion(" \n\t\n", table, d, texts)};
    check(blank.blank and blank.text.starts_with("; test settings\n"), "ini_completion: a blank file takes made, its own \\n");
    std::string all;
    for(row_t const & row: rows)
      all += std::string{row.name} + " =\n";
    check(ini_completion(all, table, d, texts).count == 0, "ini_completion: nothing missing, nothing");

    ini_doc_rows_t<test_settings_t, group_e> const doc{table, d};
    check(doc.rows().size() == std::size(rows) and doc.rows()[0].about == "how much; more for the doc" and doc.rows()[0].value == "1.5"
            and doc.rows()[3].group == 1 and doc.rows()[7].value == "0000000000000001, 0000000000000002",
          "ini_doc_rows_t: values, about with doc_more, the group as its index");
    }

  auto slots() -> void
    {
    live_settings_t<test_settings_t> settings;
    test_settings_t start{};
    start.outside = 5;
    settings.start(start);
    test_settings_t read{};
    read.level = 0.25f;
    read.count = 99;  // start-only: not published
    read.list = {7};
    read.mode = mode_e::off;
    read.width = 1;
    settings.publish(read, table);
    test_settings_t const & now{settings.get()};
    check(now.level == 0.25f and now.list == std::vector<std::uint64_t>{7} and now.mode == mode_e::off and now.count == 7u
            and now.width == 640u and now.outside == 5 and settings.slot().generation == 1u,
          "live_settings_t: publish copies the live rows only, the generation one more");

    // a thread that keeps one reference and takes new ones, reading the start-only fields, through 200 publications
    std::atomic<bool> stop{};
    std::atomic<bool> started{};
    std::atomic<int> wrong{};
    std::atomic<long> rounds{};
    test_settings_t const & kept{settings.get()};
    std::thread reader{[&]
                         {
                           started.store(true, std::memory_order_release);
                           while(not stop.load(std::memory_order_acquire))
                             {
                             rounds.fetch_add(1, std::memory_order_relaxed);
                             test_settings_t const & s{settings.get()};
                             if(s.count != 7u or kept.count != 7u or s.folder != L"C:\\x" or kept.width != 640u)
                               wrong.fetch_add(1);
                             }
                         }};
    while(not started.load(std::memory_order_acquire))
      std::this_thread::yield();
    for(int i{}; i != 200; ++i)
      {
      // the reader goes round between the publications
      for(long const at{rounds.load(std::memory_order_relaxed)}; rounds.load(std::memory_order_relaxed) == at;)
        std::this_thread::yield();
      read.level = static_cast<float>(i);
      settings.publish(read, table);
      auto const & slot{settings.slot()};
      if(slot.settings.level != static_cast<float>(i) or slot.generation != static_cast<std::uint32_t>(i + 2))
        wrong.fetch_add(1);
      }
    stop.store(true, std::memory_order_release);
    reader.join();
    check(wrong.load() == 0, "live_settings_t: start-only fields read on another thread through 200 publications, the pair "
                             "settings-generation on the publishing one");
    }

  // live3_settings_t: live fields a std::vector and a std::wstring, each publication's values tied to its number, so a
  // slot read half written shows
  struct live3_test_t
    {
    float level{-1.f};                      ///< live: the publication's number
    std::vector<std::uint64_t> list{9, 9};  ///< live: (number % 7) + 1 items, each the number
    std::wstring text{L"start"};            ///< live: the number in digits, 40 times (beyond a short string's buffer)
    std::uint32_t count{7};                 ///< start-only
    std::wstring folder{L"C:\\start"};      ///< start-only
    };

  using row3_t = ini_setting_t<live3_test_t, group_e>;
  row3_t const rows3[]{
    {"level", &live3_test_t::level, group_e::first, live, "", "the number"},
    {"list", ini_list_t<live3_test_t>{&live3_test_t::list}, group_e::first, live, "", "a list"},
    {"text", &live3_test_t::text, group_e::first, live, "", "a text"},
    {"count", &live3_test_t::count, group_e::second, at_start, "", "start-only"},
    {"folder", &live3_test_t::folder, group_e::second, at_start, "", "start-only", {}, {}, value_e::raw},
  };
  ini_table_t<live3_test_t, group_e> const table3{rows3, {}};

  auto live3_numbered(int n) -> live3_test_t
    {
    live3_test_t s{};
    s.level = static_cast<float>(n);
    s.list.assign(static_cast<std::size_t>(n % 7) + 1u, static_cast<std::uint64_t>(n));
    s.text.clear();
    for(int i{}; i != 40; ++i)
      s.text += std::to_wstring(n);
    s.count = 1000;          // start-only: never published
    s.folder = L"D:\\read";  // start-only: never published
    return s;
    }

  ///\brief the live fields whole, of one publication (`n` from its level) or of the start (-1)
  auto live3_whole(live3_test_t const & s) -> bool
    {
    int const n{static_cast<int>(s.level)};
    if(n == -1)
      return s.list == std::vector<std::uint64_t>{9, 9} and s.text == L"start";
    live3_test_t const expected{live3_numbered(n)};
    return s.list == expected.list and s.text == expected.text;
    }

  auto live3_start_only(live3_test_t const & s) -> bool
    {
    return s.count == 7u and s.folder == L"C:\\start";
    }

  std::atomic<int> live3_told_lines{};
  std::atomic<int> live3_acquire_lines{};

  auto live3_told(std::string_view line) noexcept -> void
    {
    if(line == live3_settings_t<live3_test_t>::other_thread_line)
      live3_told_lines.fetch_add(1);
    if(line == live3_settings_t<live3_test_t>::other_acquire_line)
      live3_acquire_lines.fetch_add(1);
    }
  }  // namespace

namespace edplugin_common::detail
  {
  struct live3_settings_peek_t
    {
    template<typename settings_t>
    [[nodiscard]]
    static auto first_acquired(live3_settings_t<settings_t> const & l) noexcept -> bool
      {
      return l.first_acquired_.load(std::memory_order_acquire);
      }

    ///\brief back_, middle_ (with fresh) and front_ as the digits of one number
    template<typename settings_t>
    [[nodiscard]]
    static auto indices(live3_settings_t<settings_t> const & l) noexcept -> unsigned
      {
      return static_cast<unsigned>(l.back_) * 100u + static_cast<unsigned>(l.middle_.load()) * 10u + l.front_;
      }
    };
  }  // namespace edplugin_common::detail

namespace
  {
  using edplugin_common::detail::live3_settings_peek_t;

  auto slots3_sequence() -> void
    {
    live3_settings_t<live3_test_t> unstarted;
    check(live3_settings_peek_t::indices(unstarted) == 12u and unstarted.acquire().generation == 0u
            and live3_settings_peek_t::indices(unstarted) == 12u,
          "live3_settings_t: without start the indices 0, 1, 2 and nothing fresh");

    live3_settings_t<live3_test_t> l;
    l.start(live3_test_t{}, live3_told);
    check(l.acquire().generation == 0u and l.acquire().settings.level == -1.f and live3_whole(l.acquire().settings),
          "live3_settings_t: before a publication acquire gives the start");
    l.publish(live3_numbered(1), table3);
    live3_settings_t<live3_test_t>::slot_t const & one{l.acquire()};
    check(one.generation == 1u and one.settings.level == 1.f and live3_whole(one.settings) and live3_start_only(one.settings),
          "live3_settings_t: publish copies the live rows only, the generation one more");
    check(&l.acquire() == &one, "live3_settings_t: nothing new, the same slot");
    l.publish(live3_numbered(2), table3);
    l.publish(live3_numbered(3), table3);
    live3_settings_t<live3_test_t>::slot_t const & three{l.acquire()};
    check(three.generation == 3u and three.settings.level == 3.f and live3_whole(three.settings),
          "live3_settings_t: two publications, acquire takes the newest");
    bool mixed{true};
    for(int n{4}; n != 40; ++n)
      {
      l.publish(live3_numbered(n), table3);
      if(n % 3 == 0)
        {
        live3_settings_t<live3_test_t>::slot_t const & s{l.acquire()};
        mixed = mixed and s.generation == static_cast<std::uint32_t>(n) and live3_whole(s.settings);
        }
      }
    check(mixed, "live3_settings_t: publications and acquires mixed, each slot whole and the newest");
    check(l.start_fields().level == -1.f and live3_whole(l.start_fields()) and live3_start_only(l.start_fields()),
          "live3_settings_t: start_fields is the start, its live fields too");
    check(live3_told_lines.load() == 0, "live3_settings_t: one thread, no line");
    }

  // the producer and the consumer on threads of their own, a third reading start_fields
  auto slots3_threads() -> void
    {
    live3_settings_t<live3_test_t> l;
    l.start(live3_test_t{}, live3_told);
    constexpr int publications{3000};
    std::atomic<bool> done{};
    std::atomic<int> wrong{};
    std::atomic<long> taken{};
    std::thread consumer{[&]
                           {
                             std::uint32_t last{};
                             while(not done.load(std::memory_order_acquire))
                               {
                               live3_settings_t<live3_test_t>::slot_t const & s{l.acquire()};
                               if(s.generation < last or not live3_whole(s.settings)
                                  or (s.generation != 0u and s.settings.level != static_cast<float>(s.generation)))
                                 wrong.fetch_add(1);
                               if(s.generation != last)
                                 taken.fetch_add(1, std::memory_order_relaxed);
                               last = s.generation;
                               }
                             if(l.acquire().generation != static_cast<std::uint32_t>(publications))
                               wrong.fetch_add(1);
                           }};
    std::thread start_reader{[&]
                               {
                                 while(not done.load(std::memory_order_acquire))
                                   if(not live3_start_only(l.start_fields()) or l.start_fields().level != -1.f
                                      or not live3_whole(l.start_fields()))
                                     wrong.fetch_add(1);
                               }};
    for(int n{1}; n <= publications; ++n)
      l.publish(live3_numbered(n), table3);
    done.store(true, std::memory_order_release);
    consumer.join();
    start_reader.join();
    check(wrong.load() == 0 and taken.load() > 0,
          "live3_settings_t: a producer and a consumer on threads of their own, each slot whole, generations in order, "
          "the last one taken; start_fields on a third");
    }

  // the global pointer: nullptr before live3_start, the object started after
  auto slots3_pointer() -> void
    {
    static constinit std::atomic<live3_settings_t<live3_test_t> *> live3{nullptr};
    std::atomic<bool> reading{};
    std::atomic<int> wrong{};
    std::atomic<long> empty{};
    std::thread reader{[&]
                         {
                           reading.store(true, std::memory_order_release);
                           live3_settings_t<live3_test_t> * p{};
                           while((p = live3.load(std::memory_order_acquire)) == nullptr)
                             empty.fetch_add(1, std::memory_order_relaxed);
                           if(not live3_start_only(p->start_fields()) or not live3_whole(p->start_fields()))
                             wrong.fetch_add(1);
                         }};
    while(not reading.load(std::memory_order_acquire))
      std::this_thread::yield();
    for(long const at{empty.load(std::memory_order_relaxed)}; empty.load(std::memory_order_relaxed) == at;)
      std::this_thread::yield();
    live3_start(live3, live3_test_t{}, live3_told);
    reader.join();
    check(wrong.load() == 0 and empty.load() > 0, "live3_start: a reader before the pointer sees nullptr, after it the object started");
    delete live3.exchange(nullptr);  // a plugin never deletes it; here for LeakSanitizer
    }

  // live_fields (one render thread, reads on other threads): the render thread before its first acquire gets the
  // start; two other threads read until they see the first acquire, then 2000 more, and get the start only, while the
  // render thread takes two publications with a std::vector and a std::wstring; one line about them
  auto slots3_live_fields() -> void
    {
    live3_settings_t<live3_test_t> l;
    l.start(live3_test_t{}, live3_told);
    live3_told_lines.store(0);
    live3_acquire_lines.store(0);
    std::atomic<int> wrong{};
    std::atomic<int> others_reading{};
    std::atomic<int> step{};  // 1 published once, 2 acquired once, 3 published twice
    auto other{[&]
                 {
                   others_reading.fetch_add(1, std::memory_order_release);
                   bool seen{};
                   for(int after{}; after != 2000;)
                     {
                     live3_test_t const & s{l.live_fields()};
                     if(s.level != -1.f or not live3_whole(s) or not live3_start_only(s))
                       wrong.fetch_add(1);
                     if(seen)
                       ++after;
                     else
                       seen = live3_settings_peek_t::first_acquired(l);
                     }
                 }};
    std::thread other_1{other};
    std::thread other_2{other};
    std::thread render{[&]
                         {
                           while(others_reading.load(std::memory_order_acquire) != 2 or step.load(std::memory_order_acquire) != 1)
                             std::this_thread::yield();
                           live3_test_t const & before{l.live_fields()};
                           if(before.level != -1.f or not live3_whole(before))
                             wrong.fetch_add(100);
                           if(l.acquire().generation != 1u or l.live_fields().level != 1.f or not live3_whole(l.live_fields()))
                             wrong.fetch_add(100);
                           step.store(2, std::memory_order_release);
                           while(step.load(std::memory_order_acquire) != 3)
                             std::this_thread::yield();
                           if(l.acquire().generation != 2u or l.live_fields().level != 2.f or not live3_whole(l.live_fields()))
                             wrong.fetch_add(100);
                         }};
    l.publish(live3_numbered(1), table3);
    step.store(1, std::memory_order_release);
    while(step.load(std::memory_order_acquire) != 2)
      std::this_thread::yield();
    l.publish(live3_numbered(2), table3);
    step.store(3, std::memory_order_release);
    render.join();
    other_1.join();
    other_2.join();
    check(wrong.load() == 0, "live3_settings_t::live_fields: the start before the first acquire, the render thread's slot "
                             "after it, the start on other threads also while it takes publications");
    check(live3_told_lines.load() == 1, "live3_settings_t::live_fields: one line for the other threads");
    check(live3_acquire_lines.load() == 0, "live3_settings_t::acquire: one thread takes, no line");
    }

  // acquire on a thread other than the first one's (the contract broken, one after the other: no race): one line,
  // also after more; a null `told` says nothing
  auto slots3_other_acquire() -> void
    {
    live3_settings_t<live3_test_t> l;
    l.start(live3_test_t{}, live3_told);
    live3_acquire_lines.store(0);
    l.publish(live3_numbered(1), table3);
    (void)l.acquire();
    (void)l.acquire();
    check(live3_acquire_lines.load() == 0, "live3_settings_t::acquire: the first thread's acquires, no line");
    std::thread{[&]
                  {
                  (void)l.acquire();
                  (void)l.acquire();
                  }}.join();
    (void)l.acquire();
    check(live3_acquire_lines.load() == 1, "live3_settings_t::acquire: acquires on another thread, one line a session");
    live3_settings_t<live3_test_t> quiet;
    quiet.start(live3_test_t{}, nullptr);
    (void)quiet.acquire();
    std::thread{[&] { (void)quiet.acquire(); }}.join();
    check(live3_acquire_lines.load() == 1, "live3_settings_t::acquire: no told, no line");
    }
  }  // namespace

auto ini_native_checks(void (*checker)(bool ok, char const * what)) -> void
  {
  check = checker;
  lines();
  readers();
  reads();
  start_only();
  files();
  slots();
  slots3_sequence();
  slots3_threads();
  slots3_pointer();
  slots3_live_fields();
  slots3_other_acquire();
  }
