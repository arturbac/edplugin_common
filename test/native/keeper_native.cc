// The checks of ini/view.h and ini/keeper.h without Windows, built natively with sanitizers (tools/test-native.sh): a
// plugin's table and live settings seen through the view without the plugin's types - the rows, the values as the ini
// writes them, the defaults, a read of a text, the plugin's clamps, the start-only changes and the publication; the
// settings thread's steps - a look, a read and its lines, a file that cannot be read or is UTF-16, the freeze after a
// fault in a change or after the last attempt, and reads on one thread while another takes the settings (TSan).
#include <edplugin_common/ini/keeper.h>
#include <edplugin_common/ini/view.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
  {
  using check_t = void (*)(bool ok, char const * what);
  check_t check{};

  using namespace edplugin_common;

  struct view_settings_t
    {
    float level{1.f};                     ///< live, clamped to 0..2
    std::vector<std::uint64_t> list{1};   ///< live
    std::wstring folder{L"C:\\start"};    ///< start-only
    std::uint32_t count{7};               ///< start-only
    };

  enum struct group_e : std::size_t
    {
    first
    };

  using setting_read_e::at_start;
  using setting_read_e::live;

  ini_setting_t<view_settings_t, group_e> const rows[]{
    {"level", &view_settings_t::level, group_e::first, live, "0..2 (clamped)", "the level"},
    {"list", ini_list_t<view_settings_t>{&view_settings_t::list}, group_e::first, live, "", "a list"},
    {"folder", &view_settings_t::folder, group_e::first, at_start, "", "a folder", {}, {}, value_e::raw},
    {"count", &view_settings_t::count, group_e::first, at_start, "", "how many"},
  };
  ini_table_t<view_settings_t, group_e> const table{rows, {}};

  auto clamp(view_settings_t & s) noexcept -> void { s.level = std::clamp(s.level, 0.f, 2.f); }

  auto views() -> void
    {
    live3_settings_t<view_settings_t> l;
    l.start(view_settings_t{}, nullptr);
    ini_view_t const v{ini_view_of(table, l, &clamp)};

    check(v.rows(v.table) == 4 and v.name(v.table, 1) == "list" and v.about(v.table, 3) == "how many"
            and v.is_live(v.table, 0) and not v.is_live(v.table, 2),
          "ini_view_of: the rows, their names, about and live or start-only");

    void * const s{v.make_settings()};
    check(v.value_text(v.table, s, 0) == "1" and v.value_text(v.table, s, 1) == "0000000000000001"
            and v.value_text(v.table, s, 2) == "C:\\start",
          "ini_view_of: a fresh settings object has the defaults, the values as the ini writes them");

    ini_read_t r;
    ini_told_t told;
    v.read_text(v.table, "level = 5\nlist = A, B\nfolder = D:\\x\ncount = 9\n", s, r, told);
    check(static_cast<view_settings_t *>(s)->level == 5.f and r.lines[0] == 1 and r.lines[3] == 4,
          "ini_view_of: read_text reads by the table");
    v.clamp_settings(v.clamp, s);
    check(static_cast<view_settings_t *>(s)->level == 2.f, "ini_view_of: the plugin's clamp");

    std::vector<std::string> notes;
    v.start_changes(v.table, v.live, s, r, told, notes);
    check(notes.size() == 2 and notes[0].find("'folder'") != std::string::npos and notes[1].find("'count'") != std::string::npos,
          "ini_view_of: the start-only changes against the live object's start fields");

    v.publish(v.table, v.live, s);
    live3_settings_t<view_settings_t>::slot_t const & slot{l.acquire()};
    check(slot.generation == 1u and slot.settings.level == 2.f and slot.settings.list == std::vector<std::uint64_t>{0xA, 0xB}
            and slot.settings.folder == L"C:\\start" and slot.settings.count == 7u,
          "ini_view_of: publish copies the live rows only");

    v.defaults(s);
    check(static_cast<view_settings_t *>(s)->level == 1.f and static_cast<view_settings_t *>(s)->count == 7u,
          "ini_view_of: defaults");
    v.free_settings(s);

    ini_view_t const none{ini_view_of(table, l, nullptr)};
    view_settings_t far{};
    far.level = 9.f;
    none.clamp_settings(none.clamp, &far);
    check(far.level == 9.f, "ini_view_of: without clamps the values stay");
    }

  auto has_line(std::vector<std::string> const & lines, std::string_view part) -> bool
    {
    return std::any_of(lines.begin(), lines.end(), [&](std::string const & l) { return l.find(part) != std::string::npos; });
    }
  }  // namespace

namespace edplugin_common::detail
  {
  struct ini_keeper_peek_t
    {
    static auto set_changing(ini_keeper_t & k) noexcept -> void { k.changing_.store(true, std::memory_order_relaxed); }
    };
  }  // namespace edplugin_common::detail

namespace
  {
  using edplugin_common::detail::ini_keeper_peek_t;

  auto keeper_steps() -> void
    {
    live3_settings_t<view_settings_t> l;
    l.start(view_settings_t{}, nullptr);
    ini_keeper_t k{ini_view_of(table, l, &clamp), "edtest.ini", ini_told_t{}};
    std::vector<std::string> lines;

    k.body_started(lines);
    check(lines.empty() and not k.frozen(), "ini_keeper_t: the body's first start says nothing");
    check(not k.looked(ini_look_e::not_due, lines) and not k.looked(ini_look_e::same, lines)
            and k.looked(ini_look_e::changed, lines) and lines.empty(),
          "ini_keeper_t: a look - read only when changed");
    check(not k.looked(ini_look_e::gone, lines) and lines.size() == 1 and has_line(lines, "edtest.ini was taken away"),
          "ini_keeper_t: a file taken away said, nothing read");

    lines.clear();
    check(k.read(std::string_view{"level = 5\nlist = A\ncount = 9\nnope = 1\n"}, true, lines) == ini_file_e::read,
          "ini_keeper_t: a read");
    live3_settings_t<view_settings_t>::slot_t const & one{l.acquire()};
    check(one.generation == 1u and one.settings.level == 2.f and one.settings.list == std::vector<std::uint64_t>{0xA}
            and one.settings.count == 7u,
          "ini_keeper_t: published, clamped, the start-only fields as at the start");
    check(has_line(lines, "settings: line 4: no setting 'nope'") and has_line(lines, "settings: line 3: 'count' is read at the game's start only"),
          "ini_keeper_t: the read's notes and the start-only change as lines");

    lines.clear();
    check(k.read(std::string_view{"level = 0.5\ncount = 9\n"}, true, lines) == ini_file_e::read and lines.empty(),
          "ini_keeper_t: a read again from the defaults; a start-only change said once a session");
    check(l.acquire().generation == 2u and l.acquire().settings.level == 0.5f and l.acquire().settings.list == std::vector<std::uint64_t>{1},
          "ini_keeper_t: a value the ini no longer sets back to its default");

    lines.clear();
    check(k.read(std::string_view{}, false, lines) == ini_file_e::none and lines.empty() and l.acquire().generation == 2u,
          "ini_keeper_t: no file - nothing read, nothing said");
    check(k.read(std::nullopt, true, lines) == ini_file_e::unreadable and k.read(std::nullopt, true, lines) == ini_file_e::unreadable
            and lines.size() == 1 and has_line(lines, "edtest.ini could not be read"),
          "ini_keeper_t: a file that cannot be read said once");
    lines.clear();
    check(k.read(std::string_view{"\xFF\xFE" "l\0", 4}, true, lines) == ini_file_e::utf16
            and k.read(std::string_view{"\xFF\xFE", 2}, true, lines) == ini_file_e::utf16 and lines.size() == 1
            and has_line(lines, "is UTF-16"),
          "ini_keeper_t: UTF-16 said once, not read");
    lines.clear();
    check(k.read(std::string_view{"level = 1\n"}, true, lines) == ini_file_e::read and k.read(std::nullopt, true, lines) == ini_file_e::unreadable
            and has_line(lines, "could not be read"),
          "ini_keeper_t: after a read, a file that cannot be read said again");

    lines.clear();
    k.body_started(lines);
    check(lines.empty() and not k.frozen(), "ini_keeper_t: a restart after a fault outside a change: not frozen");
    std::uint32_t const before{l.acquire().generation};
    ini_keeper_peek_t::set_changing(k);
    k.body_started(lines);
    check(k.frozen() and lines.size() == 1 and has_line(lines, ini_keeper_t::fault_line),
          "ini_keeper_t: a restart after a fault in a change freezes the settings, said");
    check(not k.looked(ini_look_e::changed, lines) and k.read(std::string_view{"level = 0\n"}, true, lines) == ini_file_e::none
            and l.acquire().generation == before and lines.size() == 1,
          "ini_keeper_t: frozen - nothing read, nothing published, nothing more said");

    ini_keeper_t spent{ini_view_of(table, l, &clamp), "edtest.ini", ini_told_t{}};
    lines.clear();
    spent.attempts_spent(lines);
    spent.attempts_spent(lines);
    check(spent.frozen() and lines.size() == 1 and has_line(lines, ini_keeper_t::spent_line),
          "ini_keeper_t: the last attempt spent freezes the settings, said once");
    }

  // the settings thread reads the ini again and again while the render thread takes the settings
  auto keeper_threads() -> void
    {
    live3_settings_t<view_settings_t> l;
    l.start(view_settings_t{}, nullptr);
    ini_keeper_t k{ini_view_of(table, l, &clamp), "edtest.ini", ini_told_t{}};
    constexpr int reads{2000};
    std::atomic<bool> done{};
    std::atomic<int> wrong{};
    std::thread render{[&]
                         {
                           std::uint32_t last{};
                           while(not done.load(std::memory_order_acquire))
                             {
                             live3_settings_t<view_settings_t>::slot_t const & s{l.acquire()};
                             // each read n sets level n / 1000 (below 2: not clamped) and a list of n % 5 + 1 names, each n
                             std::uint64_t const n{static_cast<std::uint64_t>(s.settings.level * 1000.f + 0.5f)};
                             if(s.generation < last
                                or (s.generation != 0u
                                    and (s.settings.list.size() != n % 5 + 1
                                         or std::any_of(s.settings.list.begin(), s.settings.list.end(), [&](std::uint64_t x) { return x != n; }))))
                               wrong.fetch_add(1);
                             last = s.generation;
                             }
                         }};
    std::thread settings{[&]
                           {
                             std::vector<std::string> lines;
                             k.body_started(lines);
                             for(int n{1}; n <= reads; ++n)
                               {
                               std::string text{"level = " + std::to_string(n / 1000.) + "\nlist = "};
                               for(int i{}; i <= n % 5; ++i)
                                 text += (i ? ", " : "") + std::format("{:X}", n);
                               text += "\n";
                               if(k.looked(ini_look_e::changed, lines) and k.read(std::string_view{text}, true, lines) != ini_file_e::read)
                                 wrong.fetch_add(1);
                               }
                             if(not lines.empty())
                               wrong.fetch_add(1);
                           }};
    settings.join();
    done.store(true, std::memory_order_release);
    render.join();
    check(wrong.load() == 0 and l.acquire().generation == static_cast<std::uint32_t>(reads),
          "ini_keeper_t: reads on the settings thread, the render thread takes each slot whole and in order (TSan)");
    }
  }  // namespace

auto keeper_native_checks(void (*checker)(bool ok, char const * what)) -> void
  {
  check = checker;
  views();
  keeper_steps();
  keeper_threads();
  }
