// context_probe - the jump a hook's trampoline starts with, decoded from its first bytes: where an entry of a method
// table that lies outside every module leads. The forms x64 hooking libraries write: jmp rel8, jmp rel32, jmp
// [rip+disp32] (also with REX.W), mov rax/r10/r11 imm64 with jmp to it, mov rax imm64 with push rax and ret, push imm32
// with mov [rsp+4] imm32 and ret. No Windows header: built natively with sanitizers too (test/native).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace context_probe
  {
  enum struct jump_e : std::uint8_t
    {
    none,         ///< not a jump of these forms (or too few bytes)
    rel8,         ///< EB rel8
    rel32,        ///< E9 rel32
    rip_pointer,  ///< (48) FF 25 disp32: the target is the pointer stored at `target`
    mov_rax,      ///< 48 B8 imm64, FF E0
    mov_rax_ret,  ///< 48 B8 imm64, 50, C3
    mov_r10,      ///< 49 BA imm64, 41 FF E2
    mov_r11,      ///< 49 BB imm64, 41 FF E3
    push_ret      ///< 68 imm32, C7 44 24 04 imm32, C3
    };

  struct jump_t
    {
    jump_e kind{jump_e::none};
    std::uint64_t target{};  ///< where it leads; with rip_pointer, where the pointer to read lies
    std::uint8_t length{};   ///< the bytes of the jump
    };

  namespace detail
    {
    [[nodiscard]]
    constexpr auto u32(std::span<std::uint8_t const> code, std::size_t at) noexcept -> std::uint32_t
      {
      return std::uint32_t{code[at]} | std::uint32_t{code[at + 1]} << 8 | std::uint32_t{code[at + 2]} << 16
             | std::uint32_t{code[at + 3]} << 24;
      }

    [[nodiscard]]
    constexpr auto u64(std::span<std::uint8_t const> code, std::size_t at) noexcept -> std::uint64_t
      {
      return std::uint64_t{u32(code, at)} | std::uint64_t{u32(code, at + 4)} << 32;
      }

    ///\brief `at` moved by a signed displacement, modulo 2^64 as the processor adds it
    [[nodiscard]]
    constexpr auto moved(std::uint64_t at, std::int64_t by) noexcept -> std::uint64_t
      {
      return at + static_cast<std::uint64_t>(by);
      }
    }  // namespace detail

  ///\brief the jump `code` starts with, `code` lying at address `at`; kind none when it is not one of the forms above
  [[nodiscard]]
  constexpr auto decode_jump(std::span<std::uint8_t const> code, std::uint64_t at) noexcept -> jump_t
    {
    using detail::u32;
    using detail::u64;
    using detail::moved;
    std::size_t const n{code.size()};
    if(n >= 2 and code[0] == 0xEB)
      return {jump_e::rel8, moved(at + 2, static_cast<std::int8_t>(code[1])), 2};
    if(n >= 5 and code[0] == 0xE9)
      return {jump_e::rel32, moved(at + 5, static_cast<std::int32_t>(u32(code, 1))), 5};
    if(n >= 6 and code[0] == 0xFF and code[1] == 0x25)
      return {jump_e::rip_pointer, moved(at + 6, static_cast<std::int32_t>(u32(code, 2))), 6};
    if(n >= 7 and code[0] == 0x48 and code[1] == 0xFF and code[2] == 0x25)
      return {jump_e::rip_pointer, moved(at + 7, static_cast<std::int32_t>(u32(code, 3))), 7};
    if(n >= 12 and code[0] == 0x48 and code[1] == 0xB8 and code[10] == 0xFF and code[11] == 0xE0)
      return {jump_e::mov_rax, u64(code, 2), 12};
    if(n >= 12 and code[0] == 0x48 and code[1] == 0xB8 and code[10] == 0x50 and code[11] == 0xC3)
      return {jump_e::mov_rax_ret, u64(code, 2), 12};
    if(n >= 13 and code[0] == 0x49 and code[1] == 0xBA and code[10] == 0x41 and code[11] == 0xFF and code[12] == 0xE2)
      return {jump_e::mov_r10, u64(code, 2), 13};
    if(n >= 13 and code[0] == 0x49 and code[1] == 0xBB and code[10] == 0x41 and code[11] == 0xFF and code[12] == 0xE3)
      return {jump_e::mov_r11, u64(code, 2), 13};
    if(n >= 14 and code[0] == 0x68 and code[5] == 0xC7 and code[6] == 0x44 and code[7] == 0x24 and code[8] == 0x04
       and code[13] == 0xC3)
      return {jump_e::push_ret, std::uint64_t{u32(code, 1)} | std::uint64_t{u32(code, 9)} << 32, 14};
    return {};
    }

  ///\brief the jump's form as the probe's lines name it
  [[nodiscard]]
  constexpr auto jump_name(jump_e kind) noexcept -> char const *
    {
    switch(kind)
      {
      case jump_e::rel8:        return "jmp rel8";
      case jump_e::rel32:       return "jmp rel32";
      case jump_e::rip_pointer: return "jmp [rip]";
      case jump_e::mov_rax:     return "mov rax, jmp rax";
      case jump_e::mov_rax_ret: return "mov rax, push rax, ret";
      case jump_e::mov_r10:     return "mov r10, jmp r10";
      case jump_e::mov_r11:     return "mov r11, jmp r11";
      case jump_e::push_ret:    return "push, ret";
      case jump_e::none:        break;
      }
    return "no jump";
    }

  namespace detail
    {
    inline constexpr std::uint8_t rel8_back[]{0xEB, 0xFE};
    inline constexpr std::uint8_t rel32_forward[]{0xE9, 0x10, 0x00, 0x00, 0x00};
    inline constexpr std::uint8_t rel32_back[]{0xE9, 0xF0, 0xFF, 0xFF, 0xFF};
    inline constexpr std::uint8_t rip_pointer[]{0xFF, 0x25, 0x00, 0x00, 0x00, 0x00};
    inline constexpr std::uint8_t mov_rax[]{0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0xFF, 0xE0};
    inline constexpr std::uint8_t mov_r11[]{0x49, 0xBB, 0x88, 0x77, 0x66, 0x55, 0x44,
                                            0x33, 0x22, 0x11, 0x41, 0xFF, 0xE3};
    inline constexpr std::uint8_t push_ret[]{0x68, 0x44, 0x33, 0x22, 0x11, 0xC7, 0x44,
                                             0x24, 0x04, 0x88, 0x77, 0x66, 0x55, 0xC3};
    inline constexpr std::uint8_t rex_rip_pointer[]{0x48, 0xFF, 0x25, 0x10, 0x00, 0x00, 0x00};
    inline constexpr std::uint8_t mov_rax_ret[]{0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x50, 0xC3};
    inline constexpr std::uint8_t mov_r10[]{0x49, 0xBA, 0x88, 0x77, 0x66, 0x55, 0x44,
                                            0x33, 0x22, 0x11, 0x41, 0xFF, 0xE2};
    inline constexpr std::uint8_t not_jump[]{0x48, 0x89, 0x5C, 0x24, 0x08};
    }  // namespace detail

  static_assert(decode_jump(detail::rel8_back, 0x1000).target == 0x1000, "jmp rel8 -2: to itself");
  static_assert(decode_jump(detail::rel32_forward, 0x1000).target == 0x1015,
                "jmp rel32 +0x10: from the end of its 5 bytes");
  static_assert(decode_jump(detail::rel32_back, 0x1000).target == 0xFF5, "jmp rel32 -0x10");
  static_assert(decode_jump(detail::rel32_back, 0).target == 0xFFFFFFFFFFFFFFF5ull, "jmp rel32 below 0 wraps");
  static_assert(decode_jump(detail::rip_pointer, 0x1000).kind == jump_e::rip_pointer
                  and decode_jump(detail::rip_pointer, 0x1000).target == 0x1006,
                "jmp [rip+0]: the pointer right after the jump");
  static_assert(decode_jump(detail::mov_rax, 0).target == 0x1122334455667788ull, "mov rax imm64, jmp rax");
  static_assert(decode_jump(detail::mov_r11, 0).target == 0x1122334455667788ull, "mov r11 imm64, jmp r11");
  static_assert(decode_jump(detail::push_ret, 0).target == 0x5566778811223344ull, "push low, mov [rsp+4] high, ret");
  static_assert(decode_jump(detail::rex_rip_pointer, 0x1000).kind == jump_e::rip_pointer
                  and decode_jump(detail::rex_rip_pointer, 0x1000).target == 0x1017,
                "REX.W jmp [rip+0x10]: from the end of its 7 bytes");
  static_assert(decode_jump(detail::mov_rax_ret, 0).kind == jump_e::mov_rax_ret
                  and decode_jump(detail::mov_rax_ret, 0).target == 0x1122334455667788ull,
                "mov rax imm64, push rax, ret");
  static_assert(decode_jump(detail::mov_r10, 0).kind == jump_e::mov_r10
                  and decode_jump(detail::mov_r10, 0).target == 0x1122334455667788ull,
                "mov r10 imm64, jmp r10");
  static_assert(decode_jump(detail::not_jump, 0).kind == jump_e::none, "an ordinary first instruction");
  static_assert(decode_jump(std::span<std::uint8_t const>{detail::rel32_forward}.first(4), 0).kind == jump_e::none,
                "too few bytes for the form");
  }  // namespace context_probe
