// event_report - the checks of report.h, one list for two runs: natively with sanitizers (test/native, an ASCII-only
// case-insensitive comparison) and under wine (event_report_test.exe, the program's own comparison, which knows the
// case of letters outside ASCII too).
#pragma once

#include "report.h"

#include <string>
#include <string_view>
#include <vector>

namespace event_report
  {
  using check_t = void (*)(bool ok, char const * what);

  ///\brief UTF-16 LE bytes with the byte order mark for an ASCII text
  [[nodiscard]]
  inline auto utf16_file(std::string_view ascii) -> std::string
    {
    std::string bytes{"\xFF\xFE"};
    for(char const c: ascii)
      (bytes += c) += '\0';
    return bytes;
    }

  [[nodiscard]]
  inline auto record(kind_e kind, std::string provider, unsigned id, std::vector<std::string> values, int day,
                     int hour = 12) -> record_t
    {
    record_t r;
    r.kind = kind;
    r.time = {2026, 11, day, hour, 0, 0, 60};
    r.provider = std::move(provider);
    r.id = id;
    r.values = std::move(values);
    return r;
    }

  ///\brief every check; `unicode_fold`: `equal` folds letters outside ASCII too
  inline auto report_checks(check_t check, fold_equal_t equal, bool unicode_fold) -> void
    {
    using namespace std::string_literals;
    // times and names
    local_time_t const t{2026, 11, 24, 9, 5, 7, 60};
    check(time_text(t) == "2026-11-24 09:05:07", "time_text");
    check(offset_text(60) == "+01:00" and offset_text(-210) == "-03:30" and offset_text(0) == "+00:00", "offset_text");
    check(result_name(t) == "event_report-20261124-090507.txt", "result_name: the run's local time");
    check(result_name(t, 3) == "event_report-20261124-090507-3.txt", "result_name: a third try in the same second");

    // UTF-16
    check(utf16le_to_utf8("A\0\x42\x01"s) == "A\xC5\x82", "utf16le_to_utf8: ASCII and a Polish letter");
    check(utf16le_to_utf8("\x3D\xD8\x00\xDE"s) == "\xF0\x9F\x98\x80", "utf16le_to_utf8: a surrogate pair");
    check(utf16le_to_utf8("\x3D\xD8" "A\0"s) == "\xEF\xBF\xBD" "A", "utf16le_to_utf8: an unpaired high surrogate");
    check(utf16le_to_utf8("\x00\xDE"s) == "\xEF\xBF\xBD", "utf16le_to_utf8: a lone low surrogate");
    check(utf16le_to_utf8("A\0B"s) == "A", "utf16le_to_utf8: an odd last byte dropped");

    // numbers and codes
    check(hex_value("c0000005") == 0xC0000005u and hex_value("0x40000015") == 0x40000015u,
          "hex_value: with and without 0x");
    check(not hex_value("") and not hex_value("0x") and not hex_value("12g") and not hex_value("11112222333344445"),
          "hex_value: refused");
    check(number_value("278") == 278u and number_value("0x116") == 0x116u and not number_value("-1"), "number_value");
    check(not exception_text(0xC0000005).empty() and exception_text(0x12345678).empty(), "exception_text");
    check(not bugcheck_text(0x141).empty() and not bugcheck_text(0).empty() and bugcheck_text(0x5555).empty(),
          "bugcheck_text");
    check(file_name_of("C:\\a\\b.dll") == "b.dll" and file_name_of("b.dll") == "b.dll" and file_name_of("x/") == "",
          "file_name_of");

    // which entries are the game's
    std::vector<std::string> const game{"ELITEDANGEROUS64.EXE"};
    std::vector<std::string> const launcher{"EDLAUNCH.EXE"};
    std::vector<std::string> const other{"notepad.exe"};
    check(kind_of("Application Error", 1000, game) == kind_e::game_crash, "kind_of: the game's crash, any case");
    check(kind_of("Application Error", 1000, launcher) == kind_e::launcher_crash, "kind_of: the launcher's crash");
    check(not kind_of("Application Error", 1000, other), "kind_of: another program's crash is not shown");
    check(not kind_of("Application Error", 1000, {}), "kind_of: no values");
    check(kind_of("Application Hang", 1002, game) == kind_e::game_hang and not kind_of("Application Hang", 1002, other),
          "kind_of: hangs");
    std::vector<std::string> const dotnet{"Application: EDLaunch.exe\nFramework Version: v4.0\nException Info: X"};
    check(kind_of(".NET Runtime", 1026, dotnet) == kind_e::launcher_crash, "kind_of: the launcher's .NET exception");
    std::vector<std::string> wer(15);
    wer[2] = "LiveKernelEvent";
    wer[5] = "141";
    check(kind_of("Windows Error Reporting", 1001, wer) == kind_e::wer_kernel, "kind_of: LiveKernelEvent");
    wer[2] = "APPCRASH";
    wer[5] = "EliteDangerous64.exe";
    check(not kind_of("Windows Error Reporting", 1001, wer), "kind_of: a crash's report shown with its crash only");
    wer[2] = "BEX64";
    check(kind_of("Windows Error Reporting", 1001, wer) == kind_e::wer_game, "kind_of: another report of the game");
    check(kind_of("Display", 4101, {}) == kind_e::display_reset and kind_of("igfxn", 5, {}) == kind_e::graphics_driver
            and kind_of("nvlddmkm", 153, {}) == kind_e::graphics_driver,
          "kind_of: graphics drivers, Intel's by its prefix");
    check(kind_of("Microsoft-Windows-Kernel-Power", 41, {}) == kind_e::restart
            and kind_of("Microsoft-Windows-WER-SystemErrorReporting", 1001, {}) == kind_e::blue_screen
            and kind_of("EventLog", 6008, {}) == kind_e::unexpected_shutdown and not kind_of("Kernel-Power", 41, {}),
          "kind_of: restarts by the providers' whole names");

    // Report.wer
    std::string const wer_text{"Version=1\r\nEventType=APPCRASH\r\nSig[0].Name=Application Name\r\n"
                               "Sig[0].Value=EliteDangerous64.exe\r\nSig[1].Name=Folder\r\n"
                               "Sig[1].Value=C:\\Users\\Sebastian\\a.exe\r\n"
                               "LoadedModule[0]=G:\\Games\\ED\\EliteDangerous64.exe\r\n"
                               "LoadedModule[1]=C:\\WINDOWS\\SYSTEM32\\ntdll.dll\r\n"
                               "LoadedModule[2]=C:\\WINDOWS\\System32\\DriverStore\\FileRepository\\nv.inf\\"
                               "nvwgf2umx.dll\r\n"
                               "LoadedModule[3]=C:\\Program Files\\Bitdefender\\atcuf64.dll\r\n"
                               "ReportIdentifier=e5318e14-2dd1\r\nAppName=EliteDangerous64.exe\r\n"
                               "AppPath=G:\\Games\\ED\\EliteDangerous64.exe\r\nNoEquals\r\n"};
    wer_t w;
    check(parse_wer(utf16_file(wer_text), w) == wer_error_e::none and w.event_type == "APPCRASH"
            and w.report_id == "e5318e14-2dd1" and w.modules.size() == 4 and w.signature.size() == 2
            and w.signature[0].second == "EliteDangerous64.exe",
          "parse_wer: the keys used");
    wer_t none;
    check(parse_wer("", none) == wer_error_e::not_utf16 and parse_wer("\xFF\xFE" "A"s, none) == wer_error_e::not_utf16
            and parse_wer(utf16_file("A=B\r\n"), none) == wer_error_e::no_event_type
            and parse_wer(std::string(wer_bytes_most + 2, '\0'), none) == wer_error_e::too_large,
          "parse_wer: refused");
    bool cut_ok{true};
    std::string const whole{utf16_file(wer_text)};
    for(std::size_t n{}; n <= whole.size(); ++n)
      {
      wer_t part;
      (void)parse_wer(std::string_view{whole}.substr(0, n), part);  // each length: no read past it (ASan)
      cut_ok = cut_ok and part.modules.size() <= 4;
      }
    check(cut_ok, "parse_wer: cut at every length");
    std::string many{"EventType=X\r\n"};
    for(int i{}; i != int(wer_modules_most) + 5; ++i)
      many += "LoadedModule[" + std::to_string(i) + "]=a.dll\r\n";
    wer_t lots;
    check(parse_wer(utf16_file(many), lots) == wer_error_e::none and lots.modules.size() == wer_modules_most
            and lots.modules_cut,
          "parse_wer: the modules' limit");

    // where loaded files lie
    check(place_of("C:\\WINDOWS\\System32\\DriverStore\\x\\a.dll", "G:\\Games\\ED\\", "C:\\Windows\\")
            == place_e::graphics_driver,
          "place_of: a driver store");
    check(place_of("g:\\games\\ed\\edloader.dll", "G:\\Games\\ED\\", "C:\\Windows\\") == place_e::game_folder,
          "place_of: the game's folder, any case");
    check(place_of("C:\\windows\\system32\\a.dll", "", "C:\\Windows\\") == place_e::windows, "place_of: Windows");
    check(place_of("C:\\Program Files\\x\\a.dll", "", "C:\\Windows\\") == place_e::elsewhere, "place_of: elsewhere");

    // cleaning
    std::vector<token_t> const tokens{{"Sebastian", "<user>"}, {"DESKTOP-J9", "<computer>"}, {"ed", "<user>"}};
    cleaned_t const c{clean("C:\\Users\\sebastian\\x Sebastianowski DESKTOP-J9\\a ed edloader", tokens, equal)};
    check(c.text == "C:\\Users\\<user>\\x Sebastianowski <computer>\\a ed edloader" and c.replaced == 2,
          "clean: names at word boundaries, any case; a short name not looked for");
    cleaned_t const ids{
      clean("User: S-1-5-21-2683841969-1805268516-718469333-1001 mail jan.kowalski+x@poczta.example.pl.", {}, equal)};
    check(ids.text == "User: <sid> mail <e-mail>." and ids.replaced == 2, "clean: a SID and an e-mail address");
    cleaned_t const drive{clean("C:\\Users\\a\\OneDrive - Firma Sp. z o.o\\Documents", {}, equal)};
    check(drive.text == "C:\\Users\\a\\OneDrive\\Documents", "clean: OneDrive's organisation");
    check(clean("a@b no@dot x@", {}, equal).text == "a@b no@dot x@", "clean: no address without a dotted domain");
    if(unicode_fold)
      {
      std::vector<token_t> const polish{{"\xC5\x81UKASZ", "<user>"}};  // ŁUKASZ
      check(clean("C:\\Users\\\xC5\x82ukasz\\x", polish, equal).text == "C:\\Users\\<user>\\x",
            "clean: a name outside ASCII in another case");
      }

    // escapes
    check(escape_controls("a\tb\r\nc\x1B[31m") == "a\tb\r\nc\\u001b[31m", "escape_controls: ESC");
    check(escape_controls("x\xE2\x80\xAEy") == "x\\u202ey", "escape_controls: a mark turning the direction");
    check(escape_controls("\xC5\x82\xFF\xC5") == "\xC5\x82\\xff\\xc5", "escape_controls: bytes that are not UTF-8");
    check(escape_controls("\xC0\x80") == "\\xc0\\x80", "escape_controls: an overlong form");

    // the report
    input_t in;
    in.now = t;
    in.windows_version = "10.0.26100";
    in.windows_dir = "C:\\Windows\\";
    in.sources.push_back({"the Application log", 7, false, {}});
    in.sources.push_back({"the System log", 0, false, "access denied (error 5)"});
    in.records.push_back(record(kind_e::game_crash, "Application Error", 1000,
                                {"EliteDangerous64.exe", "334.925.0.0", "6ada8048", "nvwgf2umx.dll", "32.0.15.6094",
                                 "x", "0xc0000005", "0x000000000031f29a", "1", "2",
                                 "G:\\Games\\ED\\EliteDangerous64.exe",
                                 "C:\\Users\\Sebastian\\m.dll", "e5318e14-2dd1"},
                                20));
    in.records.push_back(record(kind_e::restart, "Microsoft-Windows-Kernel-Power", 41, {"0"}, 21));
    in.records.push_back(record(kind_e::launcher_crash, ".NET Runtime", 1026, dotnet, 19));
    in.reports.push_back(w);
    in.dumps.push_back({"EliteDangerous64.exe.6388.dmp", 412u << 20, t});
    in.skipped.push_back("1 saved report not read: not UTF-16");
    std::string const text{report(in, tokens, equal)};
    auto const has = [&](std::string_view part) { return text.find(part) != std::string::npos; };
    check(has("event_report 1 - Elite Dangerous events from Windows' logs (2026-11-24 09:05:07 +01:00, Windows "
              "10.0.26100)"),
          "report: the head");
    check(has("game crashes: 1 (last 2026-11-20 12:00:00)"), "report: the summary");
    check(has("exception: 0xc0000005 (access violation"), "report: the exception's words");
    check(has("graphics drivers:   nvwgf2umx.dll") and has("the game's folder:  EliteDangerous64.exe")
            and has("from elsewhere:     atcuf64.dll") and has("and 1 from the Windows folder"),
          "report: the saved report's files by where they lie");
    check(has("stop code 0x0 (no stop code"), "report: Kernel-Power 41 with no stop code");
    check(has(".NET exception: X"), "report: the launcher's .NET exception");
    check(has("EliteDangerous64.exe.6388.dmp  412 MB"), "report: a crash dump");
    check(has("the System log: access denied (error 5)") and has("1 saved report not read: not UTF-16"),
          "report: what could not run and what was skipped");
    check(not has("Sebastian") and not has("G:\\") and not has("C:\\Users"), "report: no name and no path");
    check(has("SAVED REPORTS OF WINDOWS WITH NO GAME CRASH ABOVE\n  none"), "report: the saved report linked");
    input_t unlinked{in};
    unlinked.records.clear();
    std::string const unlinked_text{report(unlinked, tokens, equal)};
    check(unlinked_text.find("Folder: a.exe (its folder left out)") != std::string::npos
            and unlinked_text.find("Sebastian") == std::string::npos,
          "report: a saved report alone, a path in its signature as a file name");
    check(has("EliteDangerous64.exe.6388.dmp  412 MB"), "report: a dump's size");
    input_t many_in{in};
    for(int i{}; i != 25; ++i)
      many_in.records.push_back(record(kind_e::game_hang, "Application Hang", 1002, {"EliteDangerous64.exe", "1"}, 1));
    std::string const many_text{report(many_in, tokens, equal)};
    check(many_text.find("GAME HANGS (Application Hang 1002) (newest 20 of 25 shown)") != std::string::npos,
          "report: a section over its limit");
    }
  }  // namespace event_report
