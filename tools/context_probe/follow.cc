// context_probe - where an entry of a method table leads (follow.h).
#include "follow.h"

#include "jump.h"

#include <edplugin_common/memory/memory.h>
#include <edplugin_common/paths/paths.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

namespace context_probe
  {
  namespace
    {
    ///\brief the copy itself, in a function of its own: clang-cl catches a fault under __try only from a call (not
    /// noexcept: nothing lets the compiler take the call for one that cannot fault)
    [[gnu::noinline]]
    auto copy_bytes(void * to, void const * from, std::size_t bytes) -> void
      {
      std::memcpy(to, from, bytes);
      }
    }  // namespace

  // no object with a destructor here: the handler unwinds nothing but the frame
  auto read_caught(void * to, void const * from, std::size_t bytes) noexcept -> bool
    {
    __try
      {
      copy_bytes(to, from, bytes);
      }
    __except(EXCEPTION_EXECUTE_HANDLER)
      {
      return false;
      }
    return true;
    }

  auto read_entries(void * const * table, int most) -> std::vector<void *>
    {
    std::vector<void *> entries;
    for(int i{}; i != most and edplugin_common::readable(&table[i], sizeof(void *)); ++i)
      {
      void * entry{};
      if(not read_caught(&entry, &table[i], sizeof entry))
        break;
      entries.push_back(entry);
      }
    return entries;
    }

  namespace
    {
    enum struct step_e
      {
      jumped,
      no_jump,
      not_readable,
      pointer_not_readable
      };

    struct step_t
      {
      step_e result;
      jump_e kind;
      void const * to;  ///< where the jump leads, for jumped
      };

    ///\brief the first instruction at `at`: whether it is a jump of a known form, and where it leads
    [[nodiscard]]
    auto step(void const * at) -> step_t
      {
      // up to 16 bytes, as many as can be read in the region
      std::uint8_t code[16]{};
      std::size_t n{sizeof code};
      while(n != 0 and not edplugin_common::readable(at, n))
        --n;
      if(n == 0 or not read_caught(code, at, n))
        return {step_e::not_readable, jump_e::none, nullptr};
      jump_t const jump{decode_jump(std::span<std::uint8_t const>{code, n}, reinterpret_cast<std::uintptr_t>(at))};
      if(jump.kind == jump_e::none)
        return {step_e::no_jump, jump_e::none, nullptr};
      std::uint64_t target{jump.target};
      if(jump.kind == jump_e::rip_pointer)
        {
        auto const * const pointer{reinterpret_cast<void const *>(static_cast<std::uintptr_t>(jump.target))};
        if(not edplugin_common::readable(pointer, sizeof target) or not read_caught(&target, pointer, sizeof target))
          return {step_e::pointer_not_readable, jump.kind, nullptr};
        }
      return {step_e::jumped, jump.kind, reinterpret_cast<void const *>(static_cast<std::uintptr_t>(target))};
      }
    }  // namespace

  auto module_name(HMODULE module) -> std::string
    {
    if(not module)
      return "-";
    std::wstring const path{edplugin_common::module_path(module)};
    if(path.empty())
      return "?";
    std::wstring const file{edplugin_common::file_name_of(path)};
    int const n{
      WideCharToMultiByte(CP_UTF8, 0, file.c_str(), static_cast<int>(file.size()), nullptr, 0, nullptr, nullptr)};
    std::string name(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if(n > 0)
      WideCharToMultiByte(CP_UTF8, 0, file.c_str(), static_cast<int>(file.size()), name.data(), n, nullptr, nullptr);
    return name.empty() ? std::string{"?"} : name;
    }

  auto where(void const * p) -> std::string
    {
    char text[160]{};
    if(HMODULE const module{edplugin_common::module_of(p)})
      {
      auto const offset{reinterpret_cast<std::uintptr_t>(p) - reinterpret_cast<std::uintptr_t>(module)};
      std::snprintf(text, sizeof text, "+0x%llX", static_cast<unsigned long long>(offset));
      return module_name(module) + text;
      }
    MEMORY_BASIC_INFORMATION m{};
    if(VirtualQuery(p, &m, sizeof m) != sizeof m)
      return "no module, not readable";
    char const * const kind{m.State == MEM_FREE      ? "free"
                            : m.State == MEM_RESERVE ? "reserve"
                            : m.Type == MEM_PRIVATE  ? "private"
                            : m.Type == MEM_MAPPED   ? "mapped"
                            : m.Type == MEM_IMAGE    ? "image"
                                                     : "other"};
    std::snprintf(text, sizeof text, "no module, %s 0x%lX", kind, static_cast<unsigned long>(m.Protect));
    return text;
    }

  auto follow_jumps(void const * entry, int hops) -> std::string
    {
    std::string text;
    void const * at{entry};
    int hop{};
    bool ended{};  // in a module, at code that is no jump, or at memory that cannot be read
    for(; hop != hops and not ended; ++hop)
      {
      step_t const s{step(at)};
      if(s.result == step_e::not_readable or s.result == step_e::pointer_not_readable)
        (text += text.empty() ? "" : "; ")
          += s.result == step_e::not_readable ? "not readable" : "jmp [rip] -> its pointer not readable";
      if(s.result != step_e::jumped)
        {
        ended = true;
        break;
        }
      at = s.to;
      ((text += text.empty() ? "" : "; ") += jump_name(s.kind)) += " -> ";
      text += where(at);
      ended = edplugin_common::module_of(at) != nullptr;
      }
    if(text.empty())
      return "no jump read";
    if(not ended)
      text += "; stopped after " + std::to_string(hop) + " jumps";
    return text;
    }

  auto jump_out_of(void const * entry, HMODULE module) -> std::string
    {
    step_t const s{step(entry)};
    if(s.result != step_e::jumped or edplugin_common::module_of(s.to) == module)
      return {};
    return follow_jumps(entry);
    }
  }  // namespace context_probe
