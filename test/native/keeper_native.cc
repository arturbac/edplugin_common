// The checks of ini/view.h, ini/keeper.h and ini/thread.h without Windows, built natively with sanitizers
// (tools/test-native.sh): a plugin's table and live settings seen through the view without the plugin's types - the
// rows, the values as the ini writes them, the defaults, a read of a text, the plugin's clamps, the start-only changes
// and the publication; the settings thread's steps - a look, a read and its lines, a file that cannot be read or is
// UTF-16, the freeze after a fault in a change or after the last attempt, a read that throws, and reads on one thread
// while another takes the settings (TSan); the settings thread's pass over a test's own files - the look's rhythm, a
// read, a file that cannot be read, is empty or is taken away, the extension's step and its wait, an extension set
// once, a frozen keeper, an extension set on another thread while passes run (TSan); and the start a plugin
// instantiates, through a start of this test's own in place of the library's Windows part.
#include <edplugin_common/ini/keeper.h>
#include <edplugin_common/ini/thread.h>
#include <edplugin_common/ini/view.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <new>
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
    check(notes.size() == 2 and notes[0].find("'folder'") != std::string::npos
            and notes[1].find("'count'") != std::string::npos,
          "ini_view_of: the start-only changes against the live object's start fields");

    v.publish(v.table, v.live, s);
    live3_settings_t<view_settings_t>::slot_t const & slot{l.acquire()};
    check(slot.generation == 1u and slot.settings.level == 2.f
            and slot.settings.list == std::vector<std::uint64_t>{0xA, 0xB}
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
    return std::any_of(lines.begin(), lines.end(),
                       [&](std::string const & l) { return l.find(part) != std::string::npos; });
    }

  [[noreturn]] auto read_throws(void const *, std::string_view, void *, ini_read_t &, ini_told_t &) -> void
    {
    throw std::bad_alloc{};
    }
  }  // namespace

namespace edplugin_common::detail
  {
  struct ini_keeper_peek_t
    {
    static auto set_changing(ini_keeper_t & k) noexcept -> void { k.changing_.store(true, std::memory_order_relaxed); }

    static auto changing(ini_keeper_t const & k) noexcept -> bool
      {
      return k.changing_.load(std::memory_order_relaxed);
      }

    static auto destroy(ini_keeper_t * k) noexcept -> void { delete k; }
    };

  struct ini_thread_peek_t
    {
    static auto destroy(ini_thread_t * t) noexcept -> void { delete t; }
    };
  }  // namespace edplugin_common::detail

namespace
  {
  using edplugin_common::detail::ini_keeper_peek_t;

  auto keeper_steps() -> void
    {
    live3_settings_t<view_settings_t> l;
    l.start(view_settings_t{}, nullptr);
    ini_keeper_t * const k{ini_keeper_t::make(ini_view_of(table, l, &clamp), "edtest.ini", ini_told_t{})};
    check(k != nullptr, "ini_keeper_t::make");
    std::vector<std::string> lines;

    k->body_started(lines);
    check(lines.empty() and not k->frozen(), "ini_keeper_t: the body's first start says nothing");
    check(not k->looked(ini_look_e::not_due, lines) and not k->looked(ini_look_e::same, lines)
            and k->looked(ini_look_e::changed, lines) and lines.empty(),
          "ini_keeper_t: a look - read only when changed");
    check(not k->looked(ini_look_e::gone, lines) and lines.size() == 1 and has_line(lines, "edtest.ini was taken away"),
          "ini_keeper_t: a file taken away said, nothing read");

    lines.clear();
    check(k->read(std::string_view{"level = 5\nlist = A\ncount = 9\nnope = 1\n"}, true, false, lines)
            == ini_file_e::read,
          "ini_keeper_t: a read");
    live3_settings_t<view_settings_t>::slot_t const & one{l.acquire()};
    check(one.generation == 1u and one.settings.level == 2.f and one.settings.list == std::vector<std::uint64_t>{0xA}
            and one.settings.count == 7u,
          "ini_keeper_t: published, clamped, the start-only fields as at the start");
    check(has_line(lines, "settings: line 4: no setting 'nope'")
            and has_line(lines, "settings: line 3: 'count' is read at the game's start only"),
          "ini_keeper_t: the read's notes and the start-only change as lines");

    lines.clear();
    check(k->read(std::string_view{"level = 0.5\ncount = 9\n"}, true, false, lines) == ini_file_e::read
            and lines.empty(),
          "ini_keeper_t: a read again from the defaults; a start-only change said once a session");
    live3_settings_t<view_settings_t>::slot_t const & two{l.acquire()};
    check(two.generation == 2u and two.settings.level == 0.5f and two.settings.list == std::vector<std::uint64_t>{1},
          "ini_keeper_t: a value the ini no longer sets back to its default");

    lines.clear();
    check(k->read(std::string_view{}, false, false, lines) == ini_file_e::none and lines.empty(),
          "ini_keeper_t: no file - nothing read, nothing said");
    check(k->read(std::nullopt, true, true, lines) == ini_file_e::unreadable
            and k->read(std::nullopt, true, false, lines) == ini_file_e::unreadable
            and lines.size() == 1 and has_line(lines, "edtest.ini could not be read"),
          "ini_keeper_t: a file that cannot be read said when the watch says to (once)");
    lines.clear();
    check(k->read(std::string_view{"\xFF\xFE" "l\0", 4}, true, false, lines) == ini_file_e::utf16
            and k->read(std::string_view{"level = 1\n"}, true, false, lines) == ini_file_e::read
            and k->read(std::string_view{"\xFF\xFE", 2}, true, false, lines) == ini_file_e::utf16 and lines.size() == 1
            and has_line(lines, "is UTF-16"),
          "ini_keeper_t: UTF-16 not read, said once a session");
    check(l.acquire().generation == 3u, "ini_keeper_t: no file, unreadable and UTF-16 publish nothing");

    lines.clear();
    k->body_started(lines);
    check(lines.empty() and not k->frozen(), "ini_keeper_t: a start of the body without the flag: not frozen");
    ini_keeper_peek_t::set_changing(*k);
    k->body_started(lines);
    check(k->frozen() and lines.size() == 1 and has_line(lines, ini_keeper_t::fault_line),
          "ini_keeper_t: a start of the body with the flag set (a fault in a change) freezes the settings, said");
    check(not k->looked(ini_look_e::changed, lines)
            and k->read(std::string_view{"level = 0\n"}, true, false, lines) == ini_file_e::none
            and l.acquire().generation == 3u and lines.size() == 1,
          "ini_keeper_t: frozen - nothing read, nothing published, nothing more said");
    check(k->take_lines_lost() == 0, "ini_keeper_t: no line lost");
    ini_keeper_peek_t::destroy(k);

    ini_keeper_t * const spent{ini_keeper_t::make(ini_view_of(table, l, &clamp), "edtest.ini", ini_told_t{})};
    lines.clear();
    spent->attempts_spent(lines);
    spent->attempts_spent(lines);
    check(spent->frozen() and lines.size() == 1 and has_line(lines, ini_keeper_t::spent_line),
          "ini_keeper_t: the last attempt spent freezes the settings, said once");
    ini_keeper_peek_t::destroy(spent);
    }

  // a read that throws (no memory): nothing published, said, the flag down, not frozen; the next read goes on
  auto keeper_throws() -> void
    {
    live3_settings_t<view_settings_t> l;
    l.start(view_settings_t{}, nullptr);
    ini_view_t const good{ini_view_of(table, l, &clamp)};
    ini_view_t bad{good};
    bad.read_text = &read_throws;
    ini_keeper_t * const k{ini_keeper_t::make(bad, "edtest.ini", ini_told_t{})};
    std::vector<std::string> lines;
    check(k->read(std::string_view{"level = 0.5\n"}, true, false, lines) == ini_file_e::unreadable
            and lines.size() == 1 and has_line(lines, ini_keeper_t::memory_line) and not k->frozen()
            and not ini_keeper_peek_t::changing(*k) and l.acquire().generation == 0u,
          "ini_keeper_t: a read that throws - nothing published, said, the flag down, not frozen");
    k->body_started(lines);
    check(not k->frozen() and lines.size() == 1,
          "ini_keeper_t: after a read that threw, a start of the body freezes nothing");
    ini_keeper_peek_t::destroy(k);
    }

  // the settings thread reads the ini again and again while the render thread takes the settings
  auto keeper_threads() -> void
    {
    live3_settings_t<view_settings_t> l;
    l.start(view_settings_t{}, nullptr);
    ini_keeper_t * const k{ini_keeper_t::make(ini_view_of(table, l, &clamp), "edtest.ini", ini_told_t{})};
    constexpr int reads{2000};
    std::atomic<bool> done{};
    std::atomic<int> wrong{};
    std::uint32_t last_seen{};
    std::thread render{[&]
                         {
                           std::uint32_t last{};
                           for(bool end{};;)
                             {
                             end = done.load(std::memory_order_acquire);
                             live3_settings_t<view_settings_t>::slot_t const & s{l.acquire()};
                             // read n sets level n / 1000 (2 at most: not clamped) and n % 5 + 1 names, each n
                             std::uint64_t const n{static_cast<std::uint64_t>(s.settings.level * 1000.f + 0.5f)};
                             if(s.generation < last
                                or (s.generation != 0u
                                    and (n != s.generation or s.settings.list.size() != n % 5 + 1
                                         or std::any_of(s.settings.list.begin(), s.settings.list.end(),
                                                        [&](std::uint64_t x) { return x != n; }))))
                               wrong.fetch_add(1);
                             last = s.generation;
                             if(end)
                               break;
                             }
                           last_seen = last;
                         }};
    std::thread settings{[&]
                           {
                             std::vector<std::string> lines;
                             k->body_started(lines);
                             for(int n{1}; n <= reads; ++n)
                               {
                               std::string text{"level = " + std::to_string(n / 1000.) + "\nlist = "};
                               for(int i{}; i <= n % 5; ++i)
                                 text += (i ? ", " : "") + std::format("{:X}", n);
                               text += "\n";
                               if(k->looked(ini_look_e::changed, lines)
                                  and k->read(std::string_view{text}, true, false, lines) != ini_file_e::read)
                                 wrong.fetch_add(1);
                               }
                             if(not lines.empty())
                               wrong.fetch_add(1);
                           }};
    settings.join();
    done.store(true, std::memory_order_release);
    render.join();
    check(wrong.load() == 0 and last_seen == static_cast<std::uint32_t>(reads),
          "ini_keeper_t: reads on the settings thread, the render thread takes each slot whole, its read's number "
          "and in order, the last one at the end (TSan)");
    ini_keeper_peek_t::destroy(k);
    }
  }  // namespace

// the start a plugin instantiates calls this in place of the library's Windows part (src/ini_thread.cc), which the
// native build does not have: what it was given, kept for the check
namespace
  {
  struct started_t
    {
    int calls{};
    edplugin_common::ini_view_t view{};
    edplugin_common::ini_watch_t const * watch{};
    edplugin_common::ini_told_t told;
    int attempts{};
    };

  started_t started;
  }  // namespace

auto edplugin_common::ini_thread_start(ini_view_t const & view, ini_watch_t & watch, ini_told_t && told,
                                       int attempts) noexcept -> ini_thread_t *
  {
  ++started.calls;
  started.view = view;
  started.watch = &watch;
  started.told = std::move(told);
  started.attempts = attempts;
  return nullptr;
  }

namespace
  {
  using edplugin_common::detail::ini_thread_peek_t;

  ///\brief the ini as the settings thread's pass sees it, set by the test
  struct files_t
    {
    ini_look_e look{ini_look_e::same};
    std::optional<std::string> text;
    bool there{true};
    bool unreadable_said{};
    int looks{};
    int reads{};
    int readable{};

    static auto of(void * c) noexcept -> files_t & { return *static_cast<files_t *>(c); }

    static auto look_at(void * c, std::uint64_t) noexcept -> ini_look_e
      {
      ++of(c).looks;
      return of(c).look;
      }

    static auto read(void * c) noexcept -> std::optional<std::string>
      {
      ++of(c).reads;
      return of(c).text;
      }

    static auto is_there(void * c) noexcept -> bool { return of(c).there; }

    static auto unreadable(void * c) noexcept -> bool { return not std::exchange(of(c).unreadable_said, true); }

    static auto made_readable(void * c) noexcept -> void
      {
      of(c).unreadable_said = false;
      ++of(c).readable;
      }

    auto files() noexcept -> ini_files_t { return {this, &look_at, &read, &is_there, &unreadable, &made_readable}; }
    };

  ///\brief an extension of the test's own: its steps counted, the keeper it was given, the wait it asks
  struct step_t
    {
    int steps{};
    ini_keeper_t * keeper{};
    std::uint32_t wait{ini_thread_t::look_ms};
    int value{};       ///< set before the extension, read by its step (TSan)
    int seen{};        ///< the value its step saw
    std::atomic<bool> stepped{};

    static auto step(void * c, ini_keeper_t & k, std::vector<std::string> & lines) noexcept -> std::uint32_t
      {
      step_t & s{*static_cast<step_t *>(c)};
      ++s.steps;
      s.keeper = &k;
      s.seen = s.value;
      s.stepped.store(true, std::memory_order_relaxed);
      try
        {
        lines.emplace_back("step");
        }
      catch(...)
        {
        }
      return s.wait;
      }
    };

  auto thread_pass() -> void
    {
    live3_settings_t<view_settings_t> l;
    l.start(view_settings_t{}, nullptr);
    ini_keeper_t * const k{ini_keeper_t::make(ini_view_of(table, l, &clamp), "edtest.ini", ini_told_t{})};
    files_t f;
    ini_thread_t * const t{ini_thread_t::make(*k, f.files(), &f, 3)};
    check(t != nullptr and &t->keeper() == k and t->wake() == &f and t->attempts() == 3, "ini_thread_t::make");
    std::vector<std::string> lines;
    t->body_started(lines);

    f.look = ini_look_e::changed;
    f.text = "level = 0.5\nlist = B\n";
    check(t->pass(10'000, lines) == 1000u and f.reads == 1 and f.readable == 1 and lines.empty()
            and l.acquire().generation == 1u and l.acquire().settings.level == 0.5f,
          "ini_thread_t::pass: a changed ini read and published, the next pass a second on");
    f.look = ini_look_e::not_due;
    check(t->pass(10'400, lines) == 600u and f.reads == 1, "ini_thread_t::pass: a look not due - nothing read, the "
                                                           "wait to the second's end");
    check(t->pass(11'100, lines) == 0u and f.reads == 1,
          "ini_thread_t::pass: a look not due past the second (the watch's clock) - no wait");
    f.look = ini_look_e::same;
    check(t->pass(11'200, lines) == 1000u and f.reads == 1 and f.looks == 4,
          "ini_thread_t::pass: the same ini - nothing read, a second to the next look");

    f.look = ini_look_e::changed;
    f.text = std::nullopt;
    std::uint32_t const w1{t->pass(12'200, lines)};
    std::uint32_t const w2{t->pass(13'200, lines)};
    check(w1 == 1000u and w2 == 1000u and lines.size() == 1 and has_line(lines, "edtest.ini could not be read")
            and f.readable == 1 and l.acquire().generation == 1u,
          "ini_thread_t::pass: an ini that cannot be read said once, nothing published");
    lines.clear();
    f.text = "level = 0.25\n";
    check(t->pass(14'200, lines) == 1000u and f.readable == 2 and not f.unreadable_said
            and l.acquire().generation == 2u,
          "ini_thread_t::pass: readable again - read, the watch told");
    f.text = "";
    f.there = false;
    check(t->pass(15'200, lines) == 1000u and lines.empty() and l.acquire().generation == 2u,
          "ini_thread_t::pass: no file (an empty text, not there) - nothing read, nothing said");
    f.there = true;
    check(t->pass(16'200, lines) == 1000u and lines.empty() and l.acquire().generation == 3u
            and l.acquire().settings.level == 1.f,
          "ini_thread_t::pass: an empty file there - read, every value its default");
    f.look = ini_look_e::gone;
    check(t->pass(17'200, lines) == 1000u and lines.size() == 1 and has_line(lines, "edtest.ini was taken away")
            and f.reads == 6,
          "ini_thread_t::pass: an ini taken away said, nothing read");

    lines.clear();
    step_t s;
    ini_extension_t const e{&s, &step_t::step};
    ini_extension_t const other{&s, &step_t::step};
    check(not t->extend(nullptr) and t->extend(&e) and not t->extend(&other) and not t->extend(&e),
          "ini_thread_t::extend: once, never nullptr");
    f.look = ini_look_e::same;
    s.wait = 100;
    check(t->pass(18'200, lines) == 100u and s.steps == 1 and s.keeper == k and lines.size() == 1
            and lines[0] == "step",
          "ini_thread_t::pass: the extension's step with the keeper, its lines, its shorter wait");
    s.wait = 5000;
    check(t->pass(19'200, lines) == 1000u and s.steps == 2,
          "ini_thread_t::pass: an extension's longer wait - the look's rhythm");
    s.wait = 0;
    check(t->pass(19'300, lines) == ini_thread_t::step_wait_min_ms and s.steps == 3,
          "ini_thread_t::pass: an extension asking no wait - the shortest wait");
    s.wait = 5000;

    lines.clear();
    t->attempts_spent(lines);
    f.look = ini_look_e::changed;
    f.text = "level = 0\n";
    check(k->frozen() and lines.size() == 1 and has_line(lines, ini_keeper_t::spent_line)
            and t->pass(20'200, lines) == 1000u and s.steps == 4 and f.reads == 6 and l.acquire().generation == 3u,
          "ini_thread_t::pass: frozen after the last attempt - nothing read, the extension's step still runs");
    check(k->take_lines_lost() == 0, "ini_thread_t: no line lost");
    ini_thread_peek_t::destroy(t);
    ini_keeper_peek_t::destroy(k);
    }

  // passes on the settings thread while another thread sets the extension: the step sees what was written before it
  auto thread_extend() -> void
    {
    live3_settings_t<view_settings_t> l;
    l.start(view_settings_t{}, nullptr);
    ini_keeper_t * const k{ini_keeper_t::make(ini_view_of(table, l, &clamp), "edtest.ini", ini_told_t{})};
    files_t f;
    ini_thread_t * const t{ini_thread_t::make(*k, f.files(), nullptr, 8)};
    step_t s;
    ini_extension_t const e{&s, &step_t::step};
    std::thread settings{[&]
                           {
                             std::vector<std::string> lines;
                             for(std::uint64_t now{}; not s.stepped.load(std::memory_order_relaxed); now += 10)
                               {
                               (void)t->pass(now, lines);
                               lines.clear();
                               }
                           }};
    s.value = 42;
    bool const set{t->extend(&e)};
    settings.join();
    check(set and s.seen == 42 and s.keeper == k,
          "ini_thread_t::extend on another thread while passes run: the step sees what was written before (TSan)");
    ini_thread_peek_t::destroy(t);
    ini_keeper_peek_t::destroy(k);
    }

  auto thread_start() -> void
    {
    live3_settings_t<view_settings_t> l;
    l.start(view_settings_t{}, nullptr);
    ini_watch_t watch;
    ini_told_t told;
    (void)told.first("said at the start");
    check(ini_live_start(table, l, &clamp, watch, std::move(told)) == nullptr and started.calls == 1
            and started.view.table == &table and started.view.live == &l
            and started.view.rows(started.view.table) == 4 and started.view.name(started.view.table, 2) == "folder"
            and started.watch == &watch and started.attempts == 8 and not started.told.first("said at the start"),
          "ini_live_start: the view of the plugin's table and live object, its watch, what was said and 8 attempts "
          "to the library's start");
    view_settings_t far{};
    far.level = 9.f;
    started.view.clamp_settings(started.view.clamp, &far);
    check(far.level == 2.f, "ini_live_start: the plugin's clamps in the view");
    (void)ini_live_start(table, l, nullptr, watch, ini_told_t{}, 2);
    check(started.calls == 2 and started.attempts == 2 and started.view.clamp == nullptr,
          "ini_live_start: without clamps, with attempts of its own");
    }
  }  // namespace

auto keeper_native_checks(void (*checker)(bool ok, char const * what)) -> void
  {
  check = checker;
  views();
  keeper_steps();
  keeper_throws();
  keeper_threads();
  thread_pass();
  thread_extend();
  thread_start();
  }
