// context_probe's following of entries, without d3d11 (tools/test.sh runs it under wine): trampolines of each form
// written into a page of its own lead to a function of kernel32 (or the kernelbase it forwards to), two jumps one after
// the other are followed, ordinary code and a page that cannot be read stop it, and memory outside every module is
// told by its kind; a read of a page that cannot be read comes back false (the fault caught, the process goes on), a
// table is read up to such a page, and a jump out of a module is told from one inside it. The window for the swap
// chain is made, never visible, of the client size the swap chain gets, and gone with its object (skipped, and said,
// under a wine with no display, where no window can be made).
#include "follow.h"
#include "jump.h"
#include "window.h"

#include <edplugin_common/memory/memory.h>

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
  {
  int failures{};

  auto check(bool ok, char const * what, std::string const & got) -> void
    {
    std::printf("%s: %s (%s)\n", ok ? "ok  " : "FAIL", what, got.c_str());
    failures += ok ? 0 : 1;
    }

  [[nodiscard]]
  auto starts_with(std::string const & text, std::string_view head) -> bool
    {
    return text.starts_with(head);
    }

  ///\brief whether `text` ends in a module's function: "<name>.dll+0x..."
  [[nodiscard]]
  auto ends_in_module(std::string const & text) -> bool
    {
    std::size_t const at{text.rfind("-> ")};
    return at != std::string::npos and text.find(".dll+0x", at) != std::string::npos;
    }

  auto put64(std::uint8_t * at, std::uint64_t value) -> void
    {
    std::memcpy(at, &value, sizeof value);
    }
  }  // namespace

auto main() -> int
  {
  void const * const target{
    reinterpret_cast<void const *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTickCount64"))};
  auto * const page{
    static_cast<std::uint8_t *>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))};
  void * const closed{VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS)};
  if(not target or not page or not closed)
    {
    std::printf("FAIL: no kernel32 function or no page\nFAILED (1 failure(s))\n");
    return 1;
    }
  auto const address{reinterpret_cast<std::uint64_t>(target)};

  // jmp [rip+0], the pointer right after it
  std::uint8_t * const rip_jump{page};
  std::memcpy(rip_jump, "\xFF\x25\x00\x00\x00\x00", 6);
  put64(rip_jump + 6, address);
  std::string const rip_text{context_probe::follow_jumps(rip_jump)};
  check(starts_with(rip_text, "jmp [rip] -> ") and ends_in_module(rip_text), "jmp [rip] leads to the module", rip_text);

  // mov rax, imm64; jmp rax
  std::uint8_t * const rax_jump{page + 64};
  std::memcpy(rax_jump, "\x48\xB8", 2);
  put64(rax_jump + 2, address);
  std::memcpy(rax_jump + 10, "\xFF\xE0", 2);
  std::string const rax_text{context_probe::follow_jumps(rax_jump)};
  check(starts_with(rax_text, "mov rax, jmp rax -> ") and ends_in_module(rax_text),
        "mov rax, jmp rax leads to the module", rax_text);

  // push low; mov [rsp+4], high; ret
  std::uint8_t * const push_jump{page + 128};
  push_jump[0] = 0x68;
  auto const low{static_cast<std::uint32_t>(address)};
  auto const high{static_cast<std::uint32_t>(address >> 32)};
  std::memcpy(push_jump + 1, &low, 4);
  std::memcpy(push_jump + 5, "\xC7\x44\x24\x04", 4);
  std::memcpy(push_jump + 9, &high, 4);
  push_jump[13] = 0xC3;
  std::string const push_text{context_probe::follow_jumps(push_jump)};
  check(starts_with(push_text, "push, ret -> ") and ends_in_module(push_text), "push, ret leads to the module",
        push_text);

  // jmp rel32 inside the page to the jmp [rip]: two jumps
  std::uint8_t * const two{page + 192};
  two[0] = 0xE9;
  auto const back{static_cast<std::int32_t>(rip_jump - (two + 5))};
  std::memcpy(two + 1, &back, 4);
  std::string const two_text{context_probe::follow_jumps(two)};
  check(starts_with(two_text, "jmp rel32 -> no module, private")
          and two_text.find("; jmp [rip] -> ") != std::string::npos and ends_in_module(two_text),
        "jmp rel32 to a trampoline, then on to the module", two_text);

  // jmp rel8 to itself: followed no further than the hops given
  std::uint8_t * const loop{page + 256};
  std::memcpy(loop, "\xEB\xFE", 2);
  std::string const loop_text{context_probe::follow_jumps(loop, 3)};
  check(loop_text.find("jmp rel8") == 0 and loop_text.find("; jmp rel8") != std::string::npos
          and not ends_in_module(loop_text) and loop_text.ends_with("; stopped after 3 jumps"),
        "a jump to itself stops at the hops given", loop_text);

  // mov rax, imm64; push rax; ret
  std::uint8_t * const rax_ret{page + 384};
  std::memcpy(rax_ret, "\x48\xB8", 2);
  put64(rax_ret + 2, address);
  std::memcpy(rax_ret + 10, "\x50\xC3", 2);
  std::string const rax_ret_text{context_probe::follow_jumps(rax_ret)};
  check(starts_with(rax_ret_text, "mov rax, push rax, ret -> ") and ends_in_module(rax_ret_text),
        "mov rax, push rax, ret leads to the module", rax_ret_text);

  // an ordinary first instruction (mov [rsp+8], rbx)
  std::uint8_t * const plain{page + 320};
  std::memcpy(plain, "\x48\x89\x5C\x24\x08", 5);
  std::string const plain_text{context_probe::follow_jumps(plain)};
  check(plain_text == "no jump read", "ordinary code: no jump", plain_text);

  // a page that cannot be read
  std::string const closed_text{context_probe::follow_jumps(closed)};
  check(closed_text == "not readable", "a page that cannot be read", closed_text);

  // the read under __try itself, on the page that cannot be read: false, and the process goes on
  std::uint8_t bytes[8]{};
  bool const fault_read{context_probe::read_caught(bytes, closed, sizeof bytes)};
  check(not fault_read, "read_caught: a page that cannot be read gives false", fault_read ? "true" : "false");
  bool const page_read{context_probe::read_caught(bytes, rip_jump, sizeof bytes)};
  check(page_read and bytes[0] == 0xFF and bytes[1] == 0x25, "read_caught: a readable page gives its bytes",
        page_read ? "true" : "false");

  // a table whose third entry would lie on a page that cannot be read: two entries
  auto * const pair{
    static_cast<std::uint8_t *>(VirtualAlloc(nullptr, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))};
  DWORD old_protect{};
  if(pair and VirtualProtect(pair + 4096, 4096, PAGE_NOACCESS, &old_protect))
    {
    auto * const table{reinterpret_cast<void **>(pair + 4096 - 2 * sizeof(void *))};
    table[0] = page;
    table[1] = nullptr;
    std::vector<void *> const entries{context_probe::read_entries(table, 5)};
    check(entries.size() == 2 and entries[0] == page and entries[1] == nullptr,
          "read_entries: up to the page that cannot be read", std::to_string(entries.size()) + " entries");
    }
  else
    check(false, "read_entries: no pair of pages", "");
  if(pair)
    VirtualFree(pair, 0, MEM_RELEASE);

  // jump_out_of: the trampoline's jump leads into the target's module (not out of it), out of another module
  HMODULE const target_module{edplugin_common::module_of(target)};
  HMODULE const ntdll{GetModuleHandleW(L"ntdll.dll")};
  std::string const into{context_probe::jump_out_of(rip_jump, target_module)};
  check(into.empty(), "jump_out_of: a jump into the module itself is not one out", into);
  std::string const out_of{target_module == ntdll ? std::string{} : context_probe::jump_out_of(rip_jump, ntdll)};
  check(starts_with(out_of, "jmp [rip] -> ") and ends_in_module(out_of), "jump_out_of: a jump out of the module",
        out_of);
  std::string const no_jump{context_probe::jump_out_of(plain, ntdll)};
  check(no_jump.empty(), "jump_out_of: ordinary code is no jump out", no_jump);
  std::string const unread{context_probe::jump_out_of(closed, ntdll)};
  check(unread.empty(), "jump_out_of: a page that cannot be read is no jump out", unread);

  // where: a module's function, private memory, the module's name
  std::string const in_module{context_probe::where(target)};
  check(in_module.find(".dll+0x") != std::string::npos, "where: a function of a module", in_module);
  std::string const in_page{context_probe::where(page)};
  check(in_page == "no module, private 0x40", "where: the page, private and PAGE_EXECUTE_READWRITE", in_page);
  std::string const kernel32{context_probe::module_name(GetModuleHandleW(L"kernel32.dll"))};
  check(_stricmp(kernel32.c_str(), "kernel32.dll") == 0, "module_name: the file name without its folder", kernel32);
  check(context_probe::module_name(nullptr) == "-", "module_name: none", context_probe::module_name(nullptr));

  // the hidden window: made, never visible, its client size, destroyed with the object. Under a wine without a
  // display (no X server, no Wayland, no null graphics driver set) no window can be made at all: told, not failed
  HWND made{};
  DWORD error{};
    {
    context_probe::hidden_window_t const window;
    made = window.handle();
    error = window.error();
    if(made)
      {
      window.pump();
      check(not IsWindowVisible(made), "hidden_window_t: never visible", "");
      RECT client{};
      bool const sized{GetClientRect(made, &client) != FALSE};
      std::string const size{std::to_string(client.right - client.left) + "x"
                             + std::to_string(client.bottom - client.top)};
      check(sized and client.right - client.left == context_probe::hidden_window_t::width
              and client.bottom - client.top == context_probe::hidden_window_t::height,
            "hidden_window_t: the client size", size);
      }
    }
  if(not made and error == ERROR_INVALID_WINDOW_HANDLE)
    std::printf("skip: hidden_window_t: no window can be made here (error %lu: no display for wine)\n",
                static_cast<unsigned long>(error));
  else
    {
    check(made != nullptr, "hidden_window_t: made", std::to_string(error));
    check(made and not IsWindow(made), "hidden_window_t: destroyed with the object", "");
    // a second one after the first: the class was unregistered and is registered again
    context_probe::hidden_window_t const again;
    check(again.handle() != nullptr, "hidden_window_t: a second one after the first", std::to_string(again.error()));
    }

  VirtualFree(closed, 0, MEM_RELEASE);
  VirtualFree(page, 0, MEM_RELEASE);
  std::printf(failures ? "FAILED (%d failure(s))\n" : "PASSED (%d failure(s))\n", failures);
  return failures ? 1 : 0;
  }
