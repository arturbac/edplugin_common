// context_probe's following of entries, without d3d11 (tools/test.sh runs it under wine): trampolines of each form
// written into a page of its own lead to a function of kernel32 (or the kernelbase it forwards to), two jumps one after
// the other are followed, ordinary code and a page that cannot be read stop it, and memory outside every module is
// told by its kind.
#include "follow.h"
#include "jump.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

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

  // where: a module's function, private memory, the module's name
  std::string const in_module{context_probe::where(target)};
  check(in_module.find(".dll+0x") != std::string::npos, "where: a function of a module", in_module);
  std::string const in_page{context_probe::where(page)};
  check(in_page == "no module, private 0x40", "where: the page, private and PAGE_EXECUTE_READWRITE", in_page);
  std::string const kernel32{context_probe::module_name(GetModuleHandleW(L"kernel32.dll"))};
  check(_stricmp(kernel32.c_str(), "kernel32.dll") == 0, "module_name: the file name without its folder", kernel32);
  check(context_probe::module_name(nullptr) == "-", "module_name: none", context_probe::module_name(nullptr));

  VirtualFree(closed, 0, MEM_RELEASE);
  VirtualFree(page, 0, MEM_RELEASE);
  std::printf(failures ? "FAILED (%d failure(s))\n" : "PASSED (%d failure(s))\n", failures);
  return failures ? 1 : 0;
  }
