// event_report - the checks of report.h, one list for two runs: natively with sanitizers (test/native, an ASCII-only
// case-insensitive comparison) and under wine (event_report_test.exe, the program's own comparison, which knows the
// case of letters outside ASCII too). The whole report of golden_input() is compared byte for byte with golden_text
// (report_golden.h).
#pragma once

#include "report.h"
#include "report_golden.h"

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

  ///\brief an Application Error 1000 entry of `exe` with its 13 values (the module, the code, the report id)
  [[nodiscard]]
  inline auto crash(kind_e kind, std::string exe, std::string module, std::string code, std::string id, int day,
                    int hour = 12) -> record_t
    {
    return record(kind, "Application Error", 1000,
                  {std::move(exe), "334.925.0.0", "6ada8048", std::move(module), "32.0.15.6094", "x", std::move(code),
                   "0x000000000031f29a", "1", "2", "G:\\Games\\ED\\EliteDangerous64.exe",
                   "C:\\Users\\Sebastian\\m.dll", std::move(id)},
                  day, hour);
    }

  ///\brief a Windows Error Reporting 1001 entry: its event name (values[2]) and P1-P3 (values[5..7])
  [[nodiscard]]
  inline auto wer_entry(kind_e kind, std::string event, std::string p1, std::string p2, std::string p3, int day)
    -> record_t
    {
    std::vector<std::string> v(15);
    v[2] = std::move(event);
    v[5] = std::move(p1);
    v[6] = std::move(p2);
    v[7] = std::move(p3);
    return record(kind, "Windows Error Reporting", 1001, std::move(v), day);
    }

  inline std::vector<token_t> const golden_tokens{
    {"Sebastian", "<user>"}, {"DESKTOP-J9", "<computer>"}, {"User", "<user>"}, {"ed", "<user>"}};

  ///\brief an input with every section and every kind of line: the report of it is golden_text
  [[nodiscard]]
  inline auto golden_input() -> input_t
    {
    input_t in;
    in.now = {2026, 11, 24, 9, 5, 7, 60};
    in.windows_version = "10.0.26100";
    in.windows_dir = "C:\\Windows\\";
    in.sources.push_back({"the Application log", 120, false, {}});
    in.sources.push_back({"the System log", 40, false, {}});
    in.sources.push_back({"the System log for Intel's graphics drivers", 5000, true, {}});
    in.sources.push_back({"Windows Error Reporting: the machine's ReportArchive", 3, false, {}});
    in.sources.push_back(
      {"Windows Error Reporting: the machine's ReportQueue", 0, false, "could not be read (error 5)"});
    in.sources.push_back({"Windows Error Reporting: the user's ReportArchive (no such folder)", 0, false, {}});
    // a crash linked by IntegratorReportIdentifier, another with no saved report
    in.records.push_back(crash(kind_e::game_crash, "EliteDangerous64.exe", "nvwgf2umx.dll", "0xc0000005",
                               "e5318e14-2dd1", 20, 21));
    in.records.push_back(crash(kind_e::game_crash, "EliteDangerous64.exe", "nvwgf2umx.dll", "c0000409", "", 18));
    in.records.push_back(
      record(kind_e::game_hang, "Application Hang", 1002, {"EliteDangerous64.exe", "334.925.0.0"}, 17));
    in.records.push_back(crash(kind_e::launcher_crash, "EDLaunch.exe", "KERNELBASE.dll", "e0434352", "", 16));
    in.records.push_back(record(kind_e::launcher_crash, ".NET Runtime", 1026,
                                {"Application: EDLaunch.exe\nFramework Version: v4.0.30319\nException Info: "
                                 "System.IO.IOException: The file 'C:\\Users\\Sebastian\\x.log' is in use.\n   at X"},
                                15));
    in.records.push_back(record(kind_e::restart, "Microsoft-Windows-Kernel-Power", 41, {"0", "0x0"}, 14));
    in.records.push_back(record(kind_e::restart, "Microsoft-Windows-Kernel-Power", 41, {"278"}, 13));
    in.records.push_back(record(kind_e::blue_screen, "Microsoft-Windows-WER-SystemErrorReporting", 1001,
                                {"0x0000009f (0x0000000000000003, 0xffffc00f, 0x0, 0x0)", "C:\\WINDOWS\\MEMORY.DMP"},
                                13, 11));
    in.records.push_back(record(kind_e::unexpected_shutdown, "EventLog", 6008, {"12:01:02", "2026-11-12"}, 12));
    in.records.push_back(record(kind_e::display_reset, "Display", 4101, {"nvlddmkm", ""}, 11));
    in.records.push_back(
      record(kind_e::graphics_driver, "nvlddmkm", 153, {"\\Device\\Video3", "Error occurred on GPUID: 100"}, 10));
    in.records.push_back(wer_entry(kind_e::wer_kernel, "LiveKernelEvent", "141", "ffffc00f", "0", 9));
    in.records.push_back(wer_entry(kind_e::wer_game, "BEX64", "EliteDangerous64.exe", "334.925.0.0", "6ada8048", 8));
    wer_t linked;
    linked.event_type = "APPCRASH";
    linked.report_id = "f00d";
    linked.integrator_report_id = "e5318e14-2dd1";
    linked.app_name = "Elite Dangerous";
    linked.app_path = "G:\\Games\\ED\\EliteDangerous64.exe";
    linked.modules = {"G:\\Games\\ED\\EliteDangerous64.exe", "G:\\Games\\ED\\edloader.dll",
                      "C:\\WINDOWS\\SYSTEM32\\ntdll.dll",
                      "C:\\WINDOWS\\System32\\DriverStore\\FileRepository\\nv.inf\\nvwgf2umx.dll",
                      "C:\\Users\\Sebastian\\AppData\\Local\\inject.dll"};
    linked.time = {2026, 11, 20, 21, 0, 1, 60};
    in.reports.push_back(linked);
    wer_t kernel;
    kernel.event_type = "LiveKernelEvent";
    kernel.report_id = "k1";
    kernel.signature = {
      {"Code", "141"}, {"Dump", "C:\\Windows\\LiveKernelReports\\WATCHDOG\\x.dmp"}, {"Seen by", "User"}};
    kernel.time = {2026, 11, 9, 12, 0, 2, 60};
    in.reports.push_back(kernel);
    in.dumps.push_back({"EliteDangerous64.exe.6388.dmp", 412ull << 20, {2026, 11, 20, 21, 0, 0, 60}});
    in.dumps.push_back({"EliteDangerous64.exe.10.dmp", std::nullopt, {2026, 11, 21, 8, 0, 0, 60}});
    in.dumps.push_back({"EDLaunch.exe.7.dmp", 100u, {2026, 11, 2, 8, 0, 0, 60}});
    in.skipped.push_back("1 saved report(s) not read: not UTF-16");
    in.notes.push_back("the user name is too short to look for in the text: check the file for it yourself");
    return in;
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
    // the hour put back in autumn: 02:30 in winter time is after 02:45 in summer time
    check(later({2026, 10, 25, 2, 30, 0, 60}, {2026, 10, 25, 2, 45, 0, 120})
            and not later({2026, 10, 25, 2, 45, 0, 120}, {2026, 10, 25, 2, 30, 0, 60}),
          "later: by UTC, not by the local time");
    check(later({2027, 1, 1, 0, 0, 0, 0}, {2026, 12, 31, 23, 59, 59, 0}) and not later(t, t), "later: a new year");

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
    check(intel_graphics("IGFX") and intel_graphics("igfxnd") and not intel_graphics("igf") and not intel_graphics("x"),
          "intel_graphics: by the prefix, any case");
    check(dump_of_game("elitedangerous64.exe.1.dmp") and dump_of_game("EDLaunch.exe.2.dmp")
            and not dump_of_game("notepad.exe.3.dmp"),
          "dump_of_game: any case");
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
            and w.signature[0].second == "EliteDangerous64.exe" and w.event_time == 0,
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
    wer_t newer;
    check(parse_wer(utf16_file("EventType=APPCRASH\r\nEventTime=134083404000000000\r\nReportIdentifier=aaaa\r\n"
                               "IntegratorReportIdentifier=bbbb\r\nAppName=Elite Dangerous\r\n"
                               "AppPath=G:\\ED\\EliteDangerous64.exe\r\n"),
                    newer)
              == wer_error_e::none
            and newer.event_time == 134083404000000000u and newer.integrator_report_id == "bbbb",
          "parse_wer: EventTime and IntegratorReportIdentifier");
    check(wer_has_id(newer, "AAAA") and wer_has_id(newer, "bbbb") and not wer_has_id(newer, "")
            and not wer_has_id(newer, "cccc"),
          "wer_has_id: either id, any case");
    check(wer_program(newer) == "EliteDangerous64.exe" and wer_wanted(newer, equal),
          "wer_program, wer_wanted: by AppPath's exe when AppName is a name for people");
    wer_t by_sig;
    by_sig.event_type = "AppHang";
    by_sig.app_name = "Elite Dangerous";
    by_sig.signature.push_back({"Application Name", "elitedangerous64.exe"});
    check(wer_program(by_sig) == "Elite Dangerous" and wer_wanted(by_sig, equal), "wer_wanted: by Sig[0]'s value");
    wer_t notepad;
    notepad.event_type = "APPCRASH";
    notepad.app_name = "notepad.exe";
    notepad.app_path = "C:\\Windows\\notepad.exe";
    wer_t kernel;
    kernel.event_type = "LiveKernelEvent";
    check(not wer_wanted(notepad, equal) and wer_wanted(kernel, equal), "wer_wanted: another program; LiveKernelEvent");
    check(wer_folder_wanted("AppCrash_EliteDangerous_1") and wer_folder_wanted("apphang_x")
            and wer_folder_wanted("Kernel_141_x") and not wer_folder_wanted("NonCritical_x")
            and not wer_folder_wanted("AppCras"),
          "wer_folder_wanted");

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
    check(clean("x S-1-12-1-3456789-1234567890-2345678901-3456789012 y", {}, equal).text == "x <sid> y",
          "clean: Azure AD's SID");
    cleaned_t const drive{clean("C:\\Users\\a\\OneDrive - Firma Sp. z o.o\\Documents", {}, equal)};
    check(drive.text == "C:\\Users\\a\\OneDrive\\Documents", "clean: OneDrive's organisation");
    check(clean("a@b no@dot x@", {}, equal).text == "a@b no@dot x@", "clean: no address without a dotted domain");
    std::vector<token_t> const full_name{{"Jan Kowalski", "<user>"}};
    check(clean("by jan kowalski, Jan Kowalskiego", full_name, equal).text == "by <user>, Jan Kowalskiego",
          "clean: a full name with a space");
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
    in.records.push_back(
      crash(kind_e::game_crash, "EliteDangerous64.exe", "nvwgf2umx.dll", "0xc0000005", "e5318e14-2dd1", 20));
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
    check(has(".NET exception: X\n"), "report: the launcher's .NET exception");
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
    input_t many_in{in};
    for(int i{}; i != 25; ++i)
      many_in.records.push_back(record(kind_e::game_hang, "Application Hang", 1002, {"EliteDangerous64.exe", "1"}, 1));
    std::string const many_text{report(many_in, tokens, equal)};
    check(many_text.find("GAME HANGS (Application Hang 1002) (newest 20 of 25 shown)") != std::string::npos,
          "report: a section over its limit");

    // a saved report of a crash not shown (the 21st, the oldest) is shown alone
    input_t over{in};
    over.records.clear();
    for(int i{}; i != 20; ++i)
      over.records.push_back(crash(kind_e::game_crash, "EliteDangerous64.exe", "a.dll", "c0000005", "", 22, i));
    over.records.push_back(crash(kind_e::game_crash, "EliteDangerous64.exe", "a.dll", "c0000005", "e5318e14-2dd1", 3));
    over.reports[0].time = {2026, 11, 3, 12, 0, 1, 60};
    std::string const over_text{report(over, tokens, equal)};
    check(over_text.find("SAVED REPORTS OF WINDOWS WITH NO GAME CRASH ABOVE\n  2026-") != std::string::npos,
          "report: the saved report of a crash over the limit shown alone");

    // a value cut at value_most bytes: a name across the cut is replaced first; a character is not split
    input_t long_in{in};
    long_in.records = {record(kind_e::display_reset, "Display", 4101, {std::string(190, 'a') + " Sebastian x"}, 20),
                       record(kind_e::display_reset, "Display", 4101, {std::string(199, 'b') + "\xC5\x82" "c"}, 19)};
    std::string const long_text{report(long_in, tokens, equal)};
    check(long_text.find(std::string(190, 'a') + " <user> x") != std::string::npos
            and long_text.find("Sebas") == std::string::npos,
          "report: a name across the cut replaced before it");
    check(long_text.find(std::string(199, 'b') + " ...") != std::string::npos
            and long_text.find("\\x") == std::string::npos,
          "report: a cut inside a character's bytes goes back to its start");

    // a user named like a word of the program's text: the program's text stays
    std::vector<token_t> const user_token{{"User", "<user>"}};
    input_t words{in};
    words.sources.push_back({"Windows Error Reporting: the user's ReportArchive", 1, false, {}});
    words.records.push_back(record(kind_e::display_reset, "Display", 4101, {"user"}, 22));
    std::string const words_text{report(words, user_token, equal)};
    check(words_text.find("the user's ReportArchive") != std::string::npos
            and words_text.find("the display driver <user> stopped") != std::string::npos,
          "report: the program's words left as they are, the values cleaned");

    // the whole report, byte for byte
    check(report(golden_input(), golden_tokens, equal) == golden_text, "report: golden_input() is golden_text");
    }
  }  // namespace event_report
