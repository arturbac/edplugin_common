// edplugin_common - a plugin's doc/settings.md from its table of settings
// (include/edplugin_common/settings/settings_doc.h).
#include <edplugin_common/settings/settings_doc.h>

#include <windows.h>

#include <algorithm>
#include <iterator>
#include <new>

using namespace std::string_view_literals;

namespace edplugin_common
  {
  namespace
    {
    ///\brief a table cell's text: "|" escaped, "\r" dropped, "\n" as "; " (as " " after a ":" or a ";" of the text,
    /// which already part what comes before from what follows); with `markup`, "<" and ">" outside code spans as
    /// entities (whose own ";" is not the text's)
    auto append_cell(std::string & out, std::string_view text, bool markup) -> void
      {
      bool in_code{};
      char last{};  // the text's last character before this one, "\r" left out
      for(char const c: text)
        {
        if(c == '`')
          in_code = not in_code;
        if(c == '|')
          out += "\\|"sv;
        else if(c == '\r')
          continue;
        else if(c == '\n')
          out += last == ':' or last == ';' ? " "sv : "; "sv;
        else if(markup and not in_code and c == '<')
          out += "&lt;"sv;
        else if(markup and not in_code and c == '>')
          out += "&gt;"sv;
        else
          out += c;
        last = c;
        }
      }

    ///\brief `text` in a code span, a double-backtick one when it has a backtick of its own
    auto append_code(std::string & out, std::string_view text) -> void
      {
      bool const tick{text.find('`') != std::string_view::npos};
      out += tick ? "`` "sv : "`"sv;
      append_cell(out, text, false);
      out += tick ? " ``"sv : "`"sv;
      }

    [[nodiscard]]
    auto check_rows(settings_doc_t const & doc, std::span<setting_row_t const> rows) noexcept -> settings_doc_error_e
      {
      for(std::size_t i{}; i != rows.size(); ++i)
        {
        if(rows[i].name.empty())
          return settings_doc_error_e::empty_name;
        if(rows[i].group >= doc.groups.size())
          return settings_doc_error_e::group_out_of_range;
        for(std::size_t j{}; j != i; ++j)
          if(rows[j].name == rows[i].name)
            return settings_doc_error_e::duplicate_name;
        }
      return settings_doc_error_e::none;
      }

    [[nodiscard]]
    auto make_doc(settings_doc_t const & doc, std::span<setting_row_t const> rows) -> std::string
      {
      std::string out;
      ((out += "# "sv) += doc.title) += "\n\n"sv;
      ((out += "<!-- Made by "sv) += doc.made_by)
        += "; tools/test.sh or tools/doc.sh\n     checks it is up to date. Change the table, not this file. -->\n\n"sv;
      out += doc.intro;
      for(std::size_t group{}; group != doc.groups.size(); ++group)
        {
        ((out += "\n## "sv) += doc.groups[group]) += '\n';
        for(setting_read_e const read: {setting_read_e::live, setting_read_e::at_start})
          {
          out += read == setting_read_e::live ? "\n### Read while the game runs\n\n"sv
                                              : "\n### Read at the game's start (a change needs a restart)\n\n"sv;
          bool any{};
          for(setting_row_t const & row: rows)
            {
            if(row.group != group or row.read != read)
              continue;
            if(not any)
              out += "| setting | default | range | what |\n|---|---|---|---|\n"sv;
            any = true;
            out += "| "sv;
            append_code(out, row.name);
            out += " | "sv;
            if(row.value.empty())
              out += "empty"sv;
            else
              append_code(out, row.value);
            out += " | "sv;
            append_cell(out, row.range, true);
            out += " | "sv;
            append_cell(out, row.about, true);
            out += " |\n"sv;
            }
          if(not any)
            out += "None.\n"sv;
          }
        }
      return out;
      }
    }  // namespace

  auto settings_doc(settings_doc_t const & doc, std::span<setting_row_t const> rows) noexcept
    -> std::expected<std::string, settings_doc_error_e>
    {
    if(settings_doc_error_e const e{check_rows(doc, rows)}; e != settings_doc_error_e::none)
      return std::unexpected{e};
    try
      {
      return make_doc(doc, rows);
      }
    catch(std::bad_alloc const &)
      {
      return std::unexpected{settings_doc_error_e::no_memory};
      }
    catch(...)
      {
      return std::unexpected{settings_doc_error_e::failed};
      }
    }

  auto write_settings_doc(wchar_t const * variable, settings_doc_t const & doc,
                          std::span<setting_row_t const> rows) noexcept
    -> settings_doc_result_t
    {
    settings_doc_result_t result{settings_doc_status_e::not_asked, settings_doc_error_e::none, 0, {}};
    constexpr DWORD room{static_cast<DWORD>(std::size(result.path))};
    DWORD const n{GetEnvironmentVariableW(variable, result.path, room)};
    if(n == 0)
      return result;
    if(n >= room)
      {
      // the buffer was too small: GetEnvironmentVariableW left it as it was; the value's start for the log
      wchar_t * const whole{new(std::nothrow) wchar_t[n]};
      if(whole and GetEnvironmentVariableW(variable, whole, n) < n)
        std::copy_n(whole, room - 1, result.path);
      delete[] whole;
      result.path[room - 1] = L'\0';
      result.status = settings_doc_status_e::path_too_long;
      return result;
      }
    auto const text{settings_doc(doc, rows)};
    if(not text)
      {
      result.status = settings_doc_status_e::invalid;
      result.error = text.error();
      return result;
      }
    HANDLE const h{CreateFileW(result.path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if(h == INVALID_HANDLE_VALUE)
      {
      result.status = settings_doc_status_e::cannot_open;
      result.system_error = GetLastError();
      return result;
      }
    DWORD written{};
    bool const whole{WriteFile(h, text->data(), static_cast<DWORD>(text->size()), &written, nullptr)
                     and written == text->size()};
    DWORD const e{whole ? 0u : GetLastError()};
    bool const closed{CloseHandle(h) != 0};
    if(not whole or not closed)
      {
      result.status = settings_doc_status_e::write_failed;
      result.system_error = whole ? GetLastError() : e;
      return result;
      }
    result.status = settings_doc_status_e::written;
    return result;
    }
  }  // namespace edplugin_common
