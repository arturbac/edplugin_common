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
    ///\brief the copy itself, in a function of its own: clang-cl catches a fault under __try only from a call
    [[gnu::noinline]]
    auto copy_bytes(void * to, void const * from, std::size_t bytes) noexcept -> void
      {
      std::memcpy(to, from, bytes);
      }

    ///\brief `bytes` bytes of memory another module may change or free between the look and the read; false after a
    /// fault (no object with a destructor here: the handler unwinds nothing but the frame)
    [[nodiscard]]
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
      // up to 16 bytes, as many as can be read in the region
      std::uint8_t code[16]{};
      std::size_t n{sizeof code};
      while(n != 0 and not edplugin_common::readable(at, n))
        --n;
      if(n == 0 or not read_caught(code, at, n))
        {
        (text += text.empty() ? "" : "; ") += "not readable";
        ended = true;
        break;
        }
      jump_t const jump{decode_jump(std::span<std::uint8_t const>{code, n}, reinterpret_cast<std::uintptr_t>(at))};
      if(jump.kind == jump_e::none)
        {
        ended = true;
        break;
        }
      std::uint64_t target{jump.target};
      if(jump.kind == jump_e::rip_pointer)
        {
        auto const * const pointer{reinterpret_cast<void const *>(static_cast<std::uintptr_t>(jump.target))};
        if(not edplugin_common::readable(pointer, sizeof target) or not read_caught(&target, pointer, sizeof target))
          {
          (text += text.empty() ? "" : "; ") += "jmp [rip] -> its pointer not readable";
          ended = true;
          break;
          }
        }
      at = reinterpret_cast<void const *>(static_cast<std::uintptr_t>(target));
      ((text += text.empty() ? "" : "; ") += jump_name(jump.kind)) += " -> ";
      text += where(at);
      ended = edplugin_common::module_of(at) != nullptr;
      }
    if(text.empty())
      return "no jump read";
    if(not ended)
      text += "; stopped after " + std::to_string(hop) + " jumps";
    return text;
    }
  }  // namespace context_probe
