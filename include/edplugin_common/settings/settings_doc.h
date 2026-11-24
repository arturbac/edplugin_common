// edplugin_common - a plugin's doc/settings.md made from the plugin's own table of settings: the plugin describes
// each setting as text (it knows its types), the library lays the rows out and writes the file an environment
// variable names. One template for every plugin, so a plugin's tools/test.sh can check its doc/settings.md is up to
// date with a plain comparison.
//
// The doc, byte for byte ({...} the settings_doc_t's fields, `\n` a line end):
//
//   # {title}\n
//   \n
//   <!-- Made by {made_by}; tools/test.sh checks\n
//        it is up to date. Change the table, not this file. -->\n
//   \n
//   {intro}                                       (taken as it is: markdown ending in a line end)
//
// then for each group, in the order of `groups`:
//
//   \n
//   ## {group}\n
//   \n
//   ### Read while the game runs\n
//   \n
//   | setting | default | range | what |\n        (or "None.\n" when the group has no such row)
//   |---|---|---|---|\n
//   | `{name}` | `{value}` | {range} | {about} |\n  (a row each, in the order of `rows`; an empty value: empty)
//   \n
//   ### Read at the game's start (a change needs a restart)\n
//   \n
//   ... (the same for the rows read at the start)
//
// In the cells: "|" as "\|"; in range and about, "<" and ">" outside code spans as "&lt;" and "&gt;" (so a path
// such as logs\x_<n>.txt stays visible); "\r" dropped; "\n" as "; ", as " " after a ":" or a ";". A name or value
// with a backtick goes in a double-backtick code span.
#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace edplugin_common
  {
  ///\brief when the plugin reads a setting: at the game's start only, or again while the game runs
  enum struct setting_read_e
    {
    at_start,
    live
    };

  ///\brief one setting as the plugin describes it, all of it text the plugin made
  struct setting_row_t
    {
    std::string_view name;
    ///\brief the default as the ini takes it; empty: "empty" in the doc. A list with ", " between its items wraps in
    /// a browser
    std::string_view value;
    ///\brief an index into settings_doc_t::groups
    std::size_t group;
    setting_read_e read;
    ///\brief the values that make sense, "(clamped)" where the plugin holds the value to them; empty = any
    std::string_view range;
    ///\brief what the setting does; "\n" between the lines the ini's comment has, "; " in the doc (" " after a ":" or
    /// a ";" of the text)
    std::string_view about;
    };

  ///\brief the plugin's part of the doc; views and spans, so what they show stays alive until the call returns
  struct settings_doc_t
    {
    std::string_view title;    ///< "<plugin> settings"
    std::string_view made_by;  ///< "<plugin> from its table of settings (<source file>, `<variable>`)"
    std::string_view intro;    ///< markdown ending in a line end, taken as it is
    std::span<std::string_view const> groups;
    };

  enum struct settings_doc_error_e
    {
    none,
    group_out_of_range,  ///< a row's group not an index into groups
    duplicate_name,      ///< two rows of one name
    empty_name,          ///< a row without a name
    no_memory,           ///< the doc's text could not be made: no memory
    failed               ///< the doc's text could not be made: another exception
    };

  ///\brief the error's name, for a plugin's log
  [[nodiscard]]
  constexpr auto settings_doc_error_name(settings_doc_error_e error) noexcept -> char const *
    {
    switch(error)
      {
      case settings_doc_error_e::none:               return "none";
      case settings_doc_error_e::group_out_of_range: return "group_out_of_range";
      case settings_doc_error_e::duplicate_name:     return "duplicate_name";
      case settings_doc_error_e::empty_name:         return "empty_name";
      case settings_doc_error_e::no_memory:          return "no_memory";
      case settings_doc_error_e::failed:             return "failed";
      }
    return "?";
    }

  ///\brief the doc by the template above; an error for rows the template cannot take. Never throws
  [[nodiscard]]
  auto settings_doc(settings_doc_t const & doc, std::span<setting_row_t const> rows) noexcept
    -> std::expected<std::string, settings_doc_error_e>;

  enum struct settings_doc_status_e
    {
    not_asked,      ///< the environment variable is not set
    path_too_long,  ///< its value is 260 characters or longer
    invalid,        ///< settings_doc failed: error says why
    cannot_open,    ///< the file cannot be made: system_error says why
    write_failed,   ///< the file not written whole: system_error says why
    written
    };

  struct settings_doc_result_t
    {
    settings_doc_status_e status;
    settings_doc_error_e error;  ///< with invalid, else none
    unsigned long system_error;  ///< GetLastError() with cannot_open and write_failed, else 0
    ///\brief the variable's value (cut to its first 259 characters with path_too_long), for the plugin's log
    wchar_t path[260];
    };

  ///\brief the doc written to the file the environment variable names (made anew); nothing without the variable.
  /// The library logs nothing: the plugin logs the result. Never throws
  [[nodiscard]]
  auto write_settings_doc(wchar_t const * variable, settings_doc_t const & doc,
                          std::span<setting_row_t const> rows) noexcept
    -> settings_doc_result_t;
  }  // namespace edplugin_common
