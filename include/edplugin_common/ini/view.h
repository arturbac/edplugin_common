// edplugin_common - a plugin's table of settings and its live settings seen without the plugin's types, for the
// settings thread (ini/keeper.h), which is the same code for every plugin: the table's rows (name, about, live or
// start-only, a value as the ini writes it), and the steps of a read while the game runs on a settings object of the
// plugin's own (its defaults, the ini's text read into it, the plugin's clamps, the start-only changes, the publication
// to live3_settings_t). Without Windows. The view points at the plugin's table and live object: both outlive it.
#pragma once

#include <edplugin_common/ini/ini_table.h>
#include <edplugin_common/ini/live.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace edplugin_common
  {
  ///\brief the plugin's settings_t behind `void *`, its table and live object behind `void const *` and `void *`;
  /// made by ini_view_of. The settings objects it takes are the ones its make_settings gave
  struct ini_view_t
    {
    ///\brief the plugin's `void (*)(settings_t &) noexcept`, kept as a function pointer of any type
    using clamp_t = void (*)();

    void const * table;
    void * live;
    clamp_t clamp;

    std::size_t (*rows)(void const * table) noexcept;
    // name, about, is_live and value_text take `row < rows(table)`
    std::string_view (*name)(void const * table, std::size_t row) noexcept;
    std::string_view (*about)(void const * table, std::size_t row) noexcept;
    bool (*is_live)(void const * table, std::size_t row) noexcept;
    ///\brief the row's value in `settings` as the ini writes it (ini_value_text); throws only what allocation throws
    std::string (*value_text)(void const * table, void const * settings, std::size_t row);

    ///\brief a settings object with the plugin's defaults; the caller owns it (free_settings)
    void * (*make_settings)();
    void (*free_settings)(void * settings) noexcept;
    ///\brief `settings` back to the plugin's defaults; throws only what allocation throws
    void (*defaults)(void * settings);
    ///\brief the ini's text into `settings` (read_ini_text); throws only what allocation throws
    void (*read_text)(void const * table, std::string_view text, void * settings, ini_read_t & r, ini_told_t & told);
    ///\brief the plugin's clamps on `settings`
    void (*clamp_settings)(clamp_t clamp, void * settings) noexcept;
    ///\brief at_start_changes of `read` against the live object's start fields; throws only what allocation throws
    void (*start_changes)(void const * table, void const * live, void const * read, ini_read_t const & r,
                          ini_told_t & told, std::vector<std::string> & notes);
    ///\brief the live rows of `read` published (live3_settings_t::publish); throws only what allocation throws
    void (*publish)(void const * table, void * live, void const * read);
    };
  // copied by value into a keeper and passed by reference: pointers only
  static_assert(std::is_trivially_copyable_v<ini_view_t>);

  namespace ini_view_detail
    {
    template<typename settings_t, typename group_t>
    struct ops_t
      {
      using table_t = ini_table_t<settings_t, group_t>;
      using live_t = live3_settings_t<settings_t>;
      using plugin_clamp_t = void (*)(settings_t &) noexcept;

      [[nodiscard]]
      static auto table(void const * t) noexcept -> table_t const &
        {
        return *static_cast<table_t const *>(t);
        }

      [[nodiscard]]
      static auto rows(void const * t) noexcept -> std::size_t
        {
        return table(t).rows.size();
        }

      [[nodiscard]]
      static auto name(void const * t, std::size_t row) noexcept -> std::string_view
        {
        return table(t).rows[row].name;
        }

      [[nodiscard]]
      static auto about(void const * t, std::size_t row) noexcept -> std::string_view
        {
        return table(t).rows[row].about;
        }

      [[nodiscard]]
      static auto is_live(void const * t, std::size_t row) noexcept -> bool
        {
        return table(t).rows[row].read == setting_read_e::live;
        }

      [[nodiscard]]
      static auto value_text(void const * t, void const * s, std::size_t row) -> std::string
        {
        return ini_value_text(*static_cast<settings_t const *>(s), table(t).rows[row].field);
        }

      [[nodiscard]]
      static auto make_settings() -> void *
        {
        return new settings_t{};
        }

      static auto free_settings(void * s) noexcept -> void { delete static_cast<settings_t *>(s); }

      static auto defaults(void * s) -> void { *static_cast<settings_t *>(s) = settings_t{}; }

      static auto read_text(void const * t, std::string_view text, void * s, ini_read_t & r, ini_told_t & told) -> void
        {
        read_ini_text(text, table(t), *static_cast<settings_t *>(s), r, told);
        }

      static auto clamp_settings(ini_view_t::clamp_t clamp, void * s) noexcept -> void
        {
        if(clamp != nullptr)
          reinterpret_cast<plugin_clamp_t>(clamp)(*static_cast<settings_t *>(s));
        }

      static auto start_changes(void const * t, void const * live, void const * read, ini_read_t const & r,
                                ini_told_t & told, std::vector<std::string> & notes) -> void
        {
        at_start_changes(table(t), *static_cast<settings_t const *>(read), r,
                         static_cast<live_t const *>(live)->start_fields(), told, notes);
        }

      static auto publish(void const * t, void * live, void const * read) -> void
        {
        static_cast<live_t *>(live)->publish(*static_cast<settings_t const *>(read), table(t));
        }
      };
    }  // namespace ini_view_detail

  ///\brief the view of the plugin's `table` and `live` (started), with its clamps after a read (`clamp`, nullptr: none)
  template<typename settings_t, typename group_t>
  [[nodiscard]]
  auto ini_view_of(ini_table_t<settings_t, group_t> const & table, live3_settings_t<settings_t> & live,
                   std::type_identity_t<void (*)(settings_t &) noexcept> clamp) noexcept -> ini_view_t
    {
    using ops_t = ini_view_detail::ops_t<settings_t, group_t>;
    return {&table,
            &live,
            reinterpret_cast<ini_view_t::clamp_t>(clamp),
            &ops_t::rows,
            &ops_t::name,
            &ops_t::about,
            &ops_t::is_live,
            &ops_t::value_text,
            &ops_t::make_settings,
            &ops_t::free_settings,
            &ops_t::defaults,
            &ops_t::read_text,
            &ops_t::clamp_settings,
            &ops_t::start_changes,
            &ops_t::publish};
    }
  }  // namespace edplugin_common
