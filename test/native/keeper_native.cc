// The checks of ini/view.h without Windows, built natively with sanitizers (tools/test-native.sh): a plugin's table and
// live settings seen through the view without the plugin's types - the rows, the values as the ini writes them, the
// defaults, a read of a text, the plugin's clamps, the start-only changes and the publication.
#include <edplugin_common/ini/view.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
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
  }  // namespace

auto keeper_native_checks(void (*checker)(bool ok, char const * what)) -> void
  {
  check = checker;
  views();
  }
