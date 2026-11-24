// edplugin_common - a plugin's table of settings and what the library does by it, without Windows: the ini's text read
// into the plugin's settings with notes for its log, a setting's value as the ini writes it, the lines an ini lacks,
// the live settings copied, the start-only settings a player changed while the game runs, the rows of doc/settings.md.
// The plugin keeps one table, each setting once; it holds its values' meaning (ranges it clamps to after a read), the
// library their form. Nothing here logs: the notes go back to the plugin, which logs them ("settings: <note>").
//
// A read starts from the plugin's defaults (the caller's settings_t, default made, with the fields that are not in the
// ini set first), so a line taken out of the file, or made a comment, gives the default back. Notes, one form for every
// plugin ("line N" counts from 1):
//   line N has no '=': ignored
//   line N: no setting 'k' (a typo?): ignored
//   line N: 'k' is no longer a setting (now j); ignored         (a withdrawn key: once a session each)
//   line N: 'k += v': += is for lists only; the line is passed over
//   line N: 'k = v' cannot be read (<range, or the kind>); its value stays <value>
//   line N: 'k = v' is none of a, b, c; <default> instead       (a choice)
//   line N: 'k = v': X, Y cannot be read (<range>); the list is <list>   (bad_item_e::skip_item)
//   line N: 'k' is read at the game's start only; the change takes effect at the next start   (at_start_changes)
//   'k' is read at the game's start only; the change takes effect at the next start   (no line sets it now: taken out,
//                                                                       made a comment, or a line that cannot be read)
// Lists: `k = A, B` sets the list as written (a name twice stays twice); `k += C` adds the names it does not hold yet
// to the list as it stands so far in the file (the default, unless an earlier line set it).
#pragma once

#include <edplugin_common/ini/ini_text.h>
#include <edplugin_common/settings/settings_doc.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace edplugin_common
  {
  ///\brief a hexadecimal name (a shader's), 16 digits in the ini
  template<typename settings_t>
  struct ini_hex_t
    {
    std::uint64_t settings_t::* field;
    };

  ///\brief a colour, RRGGBB in the ini
  template<typename settings_t>
  struct ini_colour_t
    {
    std::uint32_t settings_t::* field;
    };

  ///\brief a list of hexadecimal names, by ", " in the ini; `+=` adds to it
  template<typename settings_t>
  struct ini_list_t
    {
    std::vector<std::uint64_t> settings_t::* field;
    };

  ///\brief one of `names` (an enumeration's values in their order): the plugin's functions take the field's value to
  /// its index and back. An unknown name gives the default, said in the log
  template<typename settings_t>
  struct ini_choice_t
    {
    auto (*get)(settings_t const &) -> std::size_t;
    auto (*set)(settings_t &, std::size_t) -> void;
    std::span<char const * const> names;
    };

  ///\brief a form of the plugin's own (edworld's WIDTHxHEIGHT): read (false: the line cannot be read, the field as it
  /// was), its text, its copy; the comment rule of numbers. The row's range says the form
  template<typename settings_t>
  struct ini_custom_t
    {
    auto (*read)(settings_t &, std::string_view) -> bool;
    auto (*text)(settings_t const &) -> std::string;
    auto (*copy)(settings_t &, settings_t const &) -> void;
    };

  template<typename settings_t>
  using ini_field_t = std::variant<
    float settings_t::*,
    std::uint32_t settings_t::*,
    bool settings_t::*,
    std::wstring settings_t::*,
    ini_hex_t<settings_t>,
    ini_colour_t<settings_t>,
    ini_list_t<settings_t>,
    ini_choice_t<settings_t>,
    ini_custom_t<settings_t>>;

  ///\brief one setting of the plugin's table
  template<typename settings_t, typename group_t = std::size_t>
  struct ini_setting_t
    {
    std::string_view name;
    ini_field_t<settings_t> field;
    group_t group;  ///< doc/settings.md's group (an index into its groups, as a number or an enumeration)
    setting_read_e read;
    ///\brief the values that make sense, "(clamped)" where the plugin holds the value to them; empty = any. Also in the
    /// note of a line that cannot be read
    std::string_view range;
    ///\brief what it does: doc/settings.md, and the ini's comment with ini_notes_e::about ("\n" between its lines)
    std::string_view about;
    ///\brief doc/settings.md only, after `about` and "; "
    std::string_view doc_more{};
    ///\brief the ini's comment with ini_notes_e::own; empty = none
    std::string_view ini_note{};
    ///\brief a text's kind: plain (a comment after a space cut off) or raw (a path, a folder, a dll's name: no comment)
    value_e value{value_e::plain};
    };

  ///\brief a key a plugin no longer has: its line is passed over with a note of its own (once a session), not "a
  /// typo?". A key taken out goes here; a key whose type or meaning changes gets a new name, the old one goes here
  struct ini_withdrawn_t
    {
    std::string_view name;
    std::string_view now;  ///< the setting in its place; empty = none
    };

  ///\brief the comment over a key in the ini: its `about`, or its `ini_note` (empty: no comment)
  enum struct ini_notes_e
    {
    about,
    own
    };

  ///\brief a list's line with a word that is no name: the line refused (the list as it was), or the good names taken
  enum struct bad_item_e
    {
    reject_line,
    skip_item
    };

  template<typename settings_t, typename group_t = std::size_t>
  struct ini_table_t
    {
    std::span<ini_setting_t<settings_t, group_t> const> rows;
    std::span<ini_withdrawn_t const> withdrawn{};
    ini_notes_e notes{ini_notes_e::about};
    bad_item_e bad_item{bad_item_e::reject_line};
    hex_zero_e hex_zero{hex_zero_e::refused};
    };

  ///\brief what was said already this session (a withdrawn key, a start-only key changed), so it is said once
  class ini_told_t
    {
    std::vector<std::string> said_;

  public:
    ///\brief true the first time for `what`
    auto first(std::string_view what) -> bool
      {
      if(std::find(said_.begin(), said_.end(), what) != said_.end())
        return false;
      said_.emplace_back(what);
      return true;
      }
    };

  ///\brief a read's result beside the settings: the notes for the log, and the line that set each row (0: none)
  struct ini_read_t
    {
    std::vector<std::string> notes;
    std::vector<std::size_t> lines;  ///< by the table's rows
    };

  ///\brief the comment rule of a row's value (ini_text.h)
  template<typename settings_t, typename group_t>
  [[nodiscard]]
  constexpr auto ini_comment_rule(ini_setting_t<settings_t, group_t> const & row) noexcept -> comment_e
    {
    if(std::holds_alternative<std::wstring settings_t::*>(row.field))
      return row.value == value_e::raw ? comment_e::none : comment_e::after_space;
    return comment_e::anywhere;
    }

  ///\brief a field's value as the ini takes it: a number by {:g}, a name by 16 hex digits, a colour by 6, a list by
  /// `separator`, a choice by its name, a text as UTF-8
  template<typename settings_t>
  [[nodiscard]]
  auto ini_value_text(settings_t const & s, ini_field_t<settings_t> const & field, std::string_view separator = ", ")
    -> std::string
    {
    return std::visit(
      [&](auto const & f) -> std::string
        {
        using f_t = std::remove_cvref_t<decltype(f)>;
        if constexpr(std::is_same_v<f_t, float settings_t::*>)
          return std::format("{:g}", static_cast<double>(s.*f));
        else if constexpr(std::is_same_v<f_t, std::uint32_t settings_t::*>)
          return std::format("{}", s.*f);
        else if constexpr(std::is_same_v<f_t, bool settings_t::*>)
          return s.*f ? "1" : "0";
        else if constexpr(std::is_same_v<f_t, std::wstring settings_t::*>)
          return ini_utf8(s.*f);
        else if constexpr(std::is_same_v<f_t, ini_hex_t<settings_t>>)
          return std::format("{:016X}", s.*f.field);
        else if constexpr(std::is_same_v<f_t, ini_colour_t<settings_t>>)
          return std::format("{:06X}", s.*f.field);
        else if constexpr(std::is_same_v<f_t, ini_list_t<settings_t>>)
          {
          std::string text;
          for(std::uint64_t const name: s.*f.field)
            {
            if(not text.empty())
              text += separator;
            std::format_to(std::back_inserter(text), "{:016X}", name);
            }
          return text;
          }
        else if constexpr(std::is_same_v<f_t, ini_choice_t<settings_t>>)
          {
          std::size_t const i{f.get(s)};
          return i < f.names.size() ? f.names[i] : "?";
          }
        else
          return f.text(s);
        },
      field);
    }

  ///\brief one field's value from `from` into `to`
  template<typename settings_t>
  auto ini_copy_field(settings_t & to, settings_t const & from, ini_field_t<settings_t> const & field) -> void
    {
    std::visit(
      [&](auto const & f)
        {
        using f_t = std::remove_cvref_t<decltype(f)>;
        if constexpr(std::is_same_v<f_t, ini_hex_t<settings_t>> or std::is_same_v<f_t, ini_colour_t<settings_t>>
                     or std::is_same_v<f_t, ini_list_t<settings_t>>)
          to.*f.field = from.*f.field;
        else if constexpr(std::is_same_v<f_t, ini_choice_t<settings_t>>)
          f.set(to, f.get(from));
        else if constexpr(std::is_same_v<f_t, ini_custom_t<settings_t>>)
          f.copy(to, from);
        else
          to.*f = from.*f;
        },
      field);
    }

  ///\brief the rows read while the game runs, field by field, from `from` into `to` (never the whole structure: the
  /// start-only fields of `to` stay as they are)
  template<typename settings_t, typename group_t>
  auto ini_copy_live(settings_t & to, settings_t const & from, ini_table_t<settings_t, group_t> const & table) -> void
    {
    for(ini_setting_t<settings_t, group_t> const & row: table.rows)
      if(row.read == setting_read_e::live)
        ini_copy_field(to, from, row.field);
    }

  namespace ini_detail
    {
    ///\brief what a row's value takes, for a line that cannot be read when the row has no range
    template<typename settings_t, typename group_t>
    [[nodiscard]]
    auto kind_text(ini_setting_t<settings_t, group_t> const & row) -> std::string
      {
      if(not row.range.empty())
        return std::string{row.range};
      return std::visit(
        [&](auto const & f) -> std::string
          {
          using f_t = std::remove_cvref_t<decltype(f)>;
          if constexpr(std::is_same_v<f_t, float settings_t::*>)
            return "a number";
          else if constexpr(std::is_same_v<f_t, std::uint32_t settings_t::*>)
            return "a whole number, 0 or more";
          else if constexpr(std::is_same_v<f_t, bool settings_t::*>)
            return "0, 1, true or false";
          else if constexpr(std::is_same_v<f_t, ini_hex_t<settings_t>>)
            return "16 hex digits";
          else if constexpr(std::is_same_v<f_t, ini_colour_t<settings_t>>)
            return "RRGGBB";
          else if constexpr(std::is_same_v<f_t, ini_list_t<settings_t>>)
            return "16 hex digits each, by commas";
          else if constexpr(std::is_same_v<f_t, ini_choice_t<settings_t>>)
            {
            std::string known;
            for(char const * const name: f.names)
              (known += known.empty() ? "" : ", ") += name;
            return known;
            }
          else
            return "the form its range gives";
          },
        row.field);
      }

    ///\brief a value's text in a note: "empty" for none
    [[nodiscard]]
    inline auto shown(std::string text) -> std::string { return text.empty() ? std::string{"empty"} : text; }

    ///\brief `text` with each "\n" as `eol`
    [[nodiscard]]
    inline auto with_line_end(std::string_view text, std::string_view eol) -> std::string
      {
      std::string out;
      for(char const c: text)
        if(c == '\n')
          out += eol;
        else
          out += c;
      return out;
      }

    ///\brief one value into its field; false when it cannot be read (the field as it was). A choice's unknown name and
    /// a list's bad words make their own notes
    template<typename settings_t, typename group_t>
    auto read_value(ini_table_t<settings_t, group_t> const & table, ini_setting_t<settings_t, group_t> const & row,
                    settings_t & s, std::string_view value, bool adding, std::size_t number, std::vector<std::string> & notes)
      -> bool
      {
      return std::visit(
        [&](auto const & f) -> bool
          {
          using f_t = std::remove_cvref_t<decltype(f)>;
          if constexpr(std::is_same_v<f_t, float settings_t::*>)
            {
            std::optional<float> const v{read_float(value)};
            if(v)
              s.*f = *v;
            return v.has_value();
            }
          else if constexpr(std::is_same_v<f_t, std::uint32_t settings_t::*>)
            {
            std::optional<std::uint32_t> const v{read_uint32(value)};
            if(v)
              s.*f = *v;
            return v.has_value();
            }
          else if constexpr(std::is_same_v<f_t, bool settings_t::*>)
            {
            std::optional<bool> const v{read_bool(value)};
            if(v)
              s.*f = *v;
            return v.has_value();
            }
          else if constexpr(std::is_same_v<f_t, std::wstring settings_t::*>)
            {
            s.*f = ini_utf16(value);
            return true;
            }
          else if constexpr(std::is_same_v<f_t, ini_hex_t<settings_t>>)
            {
            std::optional<std::uint64_t> const v{read_hex64(value, table.hex_zero)};
            if(v)
              s.*f.field = *v;
            return v.has_value();
            }
          else if constexpr(std::is_same_v<f_t, ini_colour_t<settings_t>>)
            {
            std::optional<std::uint32_t> const v{read_hex24(value)};
            if(v)
              s.*f.field = *v;
            return v.has_value();
            }
          else if constexpr(std::is_same_v<f_t, ini_list_t<settings_t>>)
            {
            hex_list_t const read{read_hex_list(value, table.hex_zero)};
            if(not read.bad.empty() and table.bad_item == bad_item_e::reject_line)
              return false;
            std::vector<std::uint64_t> & to{s.*f.field};
            if(adding)
              {
              for(std::uint64_t const name: read.names)
                if(std::find(to.begin(), to.end(), name) == to.end())
                  to.push_back(name);
              }
            else
              to = read.names;
            if(not read.bad.empty())
              {
              std::string bad;
              for(std::string_view const word: read.bad)
                (bad += bad.empty() ? "" : ", ") += word;
              notes.push_back(std::format("line {}: '{} {} {}': {} cannot be read ({}); the list is {}", number, row.name,
                                          adding ? "+=" : "=", value, bad, kind_text(row), shown(ini_value_text(s, row.field))));
              }
            return true;
            }
          else if constexpr(std::is_same_v<f_t, ini_choice_t<settings_t>>)
            {
            for(std::size_t i{}; i != f.names.size(); ++i)
              if(value == f.names[i])
                {
                f.set(s, i);
                return true;
                }
            settings_t const defaults{};
            f.set(s, f.get(defaults));
            std::string known;
            for(char const * const name: f.names)
              (known += known.empty() ? "" : ", ") += name;
            notes.push_back(std::format("line {}: '{} = {}' is none of {}; {} instead", number, row.name, value, known,
                                        ini_value_text(s, row.field)));
            return true;
            }
          else
            return f.read(s, value);
          },
        row.field);
      }
    }  // namespace ini_detail

  ///\brief the ini's text into `s` (the plugin's defaults, the fields outside the ini set): every line by the table,
  /// the notes (`r.notes` emptied first) and the line that last set each row into `r`; a withdrawn key said once a
  /// session by `told`. A UTF-8 byte order mark at the start is passed over. Throws only what allocation throws
  template<typename settings_t, typename group_t>
  auto read_ini_text(std::string_view text, ini_table_t<settings_t, group_t> const & table, settings_t & s, ini_read_t & r,
                     ini_told_t & told) -> void
    {
    text = ini_without_bom(text);
    r.notes.clear();
    r.lines.assign(table.rows.size(), 0);
    std::size_t number{};
    for(std::size_t at{}; at < text.size();)
      {
      std::size_t const end{std::min(text.find('\n', at), text.size())};
      ini_line_t const line{parse_ini_line(text.substr(at, end - at))};
      at = end + 1;
      ++number;
      if(line.kind == ini_line_e::empty or line.kind == ini_line_e::comment)
        continue;
      if(line.kind == ini_line_e::no_equals)
        {
        r.notes.push_back(std::format("line {} has no '=': ignored", number));
        continue;
        }
      auto const row{std::find_if(table.rows.begin(), table.rows.end(),
                                  [&](ini_setting_t<settings_t, group_t> const & k) { return k.name == line.key; })};
      if(row == table.rows.end())
        {
        auto const gone{std::find_if(table.withdrawn.begin(), table.withdrawn.end(),
                                     [&](ini_withdrawn_t const & w) { return w.name == line.key; })};
        if(gone == table.withdrawn.end())
          r.notes.push_back(std::format("line {}: no setting '{}' (a typo?): ignored", number, line.key));
        else if(told.first(std::format("withdrawn {}", gone->name)))
          r.notes.push_back(gone->now.empty() ? std::format("line {}: '{}' is no longer a setting; ignored", number, gone->name)
                                              : std::format("line {}: '{}' is no longer a setting (now {}); ignored", number,
                                                            gone->name, gone->now));
        continue;
        }
      std::string_view const value{ini_value(line.value, ini_comment_rule(*row))};
      if(line.adding and not std::holds_alternative<ini_list_t<settings_t>>(row->field))
        {
        r.notes.push_back(std::format("line {}: '{} += {}': += is for lists only; the line is passed over", number, line.key, value));
        continue;
        }
      if(ini_detail::read_value(table, *row, s, value, line.adding, number, r.notes))
        r.lines[static_cast<std::size_t>(row - table.rows.begin())] = number;
      else
        r.notes.push_back(std::format("line {}: '{} {} {}' cannot be read ({}); its value stays {}", number, line.key,
                                      line.adding ? "+=" : "=", value, ini_detail::kind_text(*row),
                                      ini_detail::shown(ini_value_text(s, row->field))));
      }
    }

  ///\brief the start-only settings whose value in `read` (a read while the game runs) differs from `running`: a note
  /// each, once a session per key (`told`); with the line that sets it, or without one when no line does (taken out)
  template<typename settings_t, typename group_t>
  auto at_start_changes(ini_table_t<settings_t, group_t> const & table, settings_t const & read, ini_read_t const & r,
                        settings_t const & running, ini_told_t & told, std::vector<std::string> & notes) -> void
    {
    for(std::size_t i{}; i != table.rows.size(); ++i)
      {
      ini_setting_t<settings_t, group_t> const & row{table.rows[i]};
      if(row.read != setting_read_e::at_start or ini_value_text(read, row.field) == ini_value_text(running, row.field))
        continue;
      if(not told.first(std::format("at_start {}", row.name)))
        continue;
      std::size_t const line{i < r.lines.size() ? r.lines[i] : 0};
      notes.push_back(line ? std::format("line {}: '{}' is read at the game's start only; the change takes effect at the next start",
                                         line, row.name)
                           : std::format("'{}' is read at the game's start only; the change takes effect at the next start", row.name));
      }
    }

  ///\brief one setting's block in the ini: its comment (by table.notes; none when empty) after an empty line, a line
  /// "; <text>" each, then `;name = value` (`;name =` for an empty value), each line ended by `eol`
  template<typename settings_t, typename group_t>
  [[nodiscard]]
  auto ini_block(ini_table_t<settings_t, group_t> const & table, ini_setting_t<settings_t, group_t> const & row,
                 settings_t const & values, std::string_view eol) -> std::string
    {
    std::string out;
    std::string_view const comment{table.notes == ini_notes_e::about ? row.about : row.ini_note};
    if(not comment.empty())
      {
      out += eol;
      for(std::size_t at{}; at <= comment.size();)
        {
        std::size_t const end{std::min(comment.find('\n', at), comment.size())};
        ((out += "; ") += comment.substr(at, end - at)) += eol;
        at = end + 1;
        }
      }
    std::string const value{ini_value_text(values, row.field)};
    ((out += ';') += row.name) += value.empty() ? " =" : " = ";
    (out += value) += eol;
    return out;
    }

  ///\brief the header lines of the ini's text the library adds: `made` over a new (or blank) file's settings, `added`
  /// over the settings added to a file; "\n" between and after the lines, taken to the file's line end
  struct ini_texts_t
    {
    std::string_view made;
    std::string_view added;
    };

  struct ini_missing_t
    {
    std::string text;   ///< to append to the file; empty when nothing is missing
    std::size_t count;  ///< the settings in it
    bool blank;         ///< the file had nothing but spaces and line ends: `made` over it
    };

  ///\brief what to append to an ini whose text is `text` (empty: no file): the blocks of the settings it names nowhere
  /// (set, or as a comment `;k = ...`), with `values`, by its own line ends (ini_line_end); before them `made` for a
  /// blank file, else an empty line (two line ends when its last line has none) and `added`
  template<typename settings_t, typename group_t>
  [[nodiscard]]
  auto ini_completion(std::string_view text, ini_table_t<settings_t, group_t> const & table, settings_t const & values,
                      ini_texts_t const & texts) -> ini_missing_t
    {
    std::string_view const eol{ini_line_end(text)};
    std::string_view const body{ini_without_bom(text)};
    std::vector<std::string_view> named;
    for(std::size_t at{}; at < body.size();)
      {
      std::size_t const end{std::min(body.find('\n', at), body.size())};
      named.push_back(ini_named_key(body.substr(at, end - at)));
      at = end + 1;
      }
    ini_missing_t missing{{}, 0, body.find_first_not_of(" \t\r\n") == std::string_view::npos};
    std::string blocks;
    for(ini_setting_t<settings_t, group_t> const & row: table.rows)
      if(std::find(named.begin(), named.end(), row.name) == named.end())
        {
        blocks += ini_block(table, row, values, eol);
        ++missing.count;
        }
    if(missing.count == 0)
      return missing;
    if(missing.blank)
      missing.text = ini_detail::with_line_end(texts.made, eol);
    else
      {
      if(not text.ends_with('\n'))
        missing.text += eol;
      missing.text += eol;
      missing.text += ini_detail::with_line_end(texts.added, eol);
      }
    missing.text += blocks;
    return missing;
    }

  ///\brief the rows of doc/settings.md (settings_doc.h) from the table, the defaults' values (a list by ", ") and
  /// `about` with `doc_more` after "; "; the texts the rows show live in this object
  template<typename settings_t, typename group_t>
  class ini_doc_rows_t
    {
    std::vector<std::string> values_;
    std::vector<std::string> abouts_;
    std::vector<setting_row_t> rows_;

  public:
    ini_doc_rows_t(ini_table_t<settings_t, group_t> const & table, settings_t const & defaults)
      {
      values_.reserve(table.rows.size());
      abouts_.reserve(table.rows.size());
      rows_.reserve(table.rows.size());
      for(ini_setting_t<settings_t, group_t> const & row: table.rows)
        {
        values_.push_back(ini_value_text(defaults, row.field));
        std::string about{row.about};
        if(not row.doc_more.empty())
          (about += "; ") += row.doc_more;
        abouts_.push_back(std::move(about));
        rows_.push_back({row.name, values_.back(), static_cast<std::size_t>(row.group), row.read, row.range, abouts_.back()});
        }
      }

    ini_doc_rows_t(ini_doc_rows_t const &) = delete;
    auto operator=(ini_doc_rows_t const &) -> ini_doc_rows_t & = delete;

    [[nodiscard]]
    auto rows() const noexcept -> std::span<setting_row_t const>
      {
      return rows_;
      }
    };
  }  // namespace edplugin_common
