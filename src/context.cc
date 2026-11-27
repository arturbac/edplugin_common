// edplugin_common - the attach of a plugin's hooks to the game's immediate context
// (include/edplugin_common/hook/context.h).
#include <edplugin_common/hook/context.h>

#include <edplugin_common/hook/contract.h>
#include <edplugin_common/hook/slot.h>
#include <edplugin_common/log/log.h>
#include <edplugin_common/memory/memory.h>
#include <edplugin_common/paths/paths.h>
#include <edplugin_common/text/format.h>

#include <windows.h>
#include <d3d11_4.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <optional>
#include <string>

namespace edplugin_common::hook
  {
  namespace
    {
    [[nodiscard]]
    auto address(void const * p) noexcept -> std::uint64_t
      {
      return reinterpret_cast<std::uintptr_t>(p);
      }

    [[nodiscard]]
    auto memory_of(void const * table) noexcept -> table_memory_e
      {
      MEMORY_BASIC_INFORMATION m{};
      if(VirtualQuery(table, &m, sizeof m) != sizeof m or m.State != MEM_COMMIT)
        return table_memory_e::unknown;
      if(m.Type == MEM_IMAGE)
        return table_memory_e::image;
      if(m.Type == MEM_PRIVATE)
        return table_memory_e::private_memory;
      return table_memory_e::unknown;
      }

    ///\brief N: the object answers ID3D11DeviceContext1..N as itself (each answer's reference given back)
    [[nodiscard]]
    auto qi_prefix(ID3D11DeviceContext * context) noexcept -> std::uint8_t
      {
      std::array<IID, 4> const ids{__uuidof(ID3D11DeviceContext1), __uuidof(ID3D11DeviceContext2),
                                   __uuidof(ID3D11DeviceContext3), __uuidof(ID3D11DeviceContext4)};
      std::uint8_t n{};
      for(IID const & id : ids)
        {
        IUnknown * answer{};
        if(FAILED(context->QueryInterface(id, reinterpret_cast<void **>(&answer))) or not answer)
          break;
        bool const same{static_cast<void *>(answer) == static_cast<void *>(context)};
        answer->Release();
        if(not same)
          break;
        ++n;
        }
      return n;
      }

    ///\brief the slots of `list` put into the table of our own, each original its stub: false (nothing pointed at it)
    /// when a slot lies past its entries
    [[nodiscard]]
    auto put_all(own_table_t const & own, std::span<slot_patch_t> list) noexcept -> bool
      {
      for(slot_patch_t & s : list)
        {
        void * const stub{own_table_put(own, s.slot, s.hook)};
        if(not stub)
          {
          s.result = slot_result_e::not_writable;
          return false;
          }
        s.original = stub;
        s.result = slot_result_e::patched;
        }
      return true;
      }

    auto log_reading(context_reading_t const & r, context_pick_t const & pick) noexcept -> void
      {
      context_input_t const & in{r.input};
      std::wstring const module{r.module ? file_name_of(module_path(r.module)) : std::wstring{L"none"}};
      log_line("context: {} (rule {}); its table at {} ({} B from the object), memory {}, module {} ({}), section {}; "
               "of the first {} entries {} in the system's d3d11, {} in other modules, {} in none; answers "
               "ID3D11DeviceContext1..{} as itself, so at least {} entries; {} entries{}; contract {}",
               way_name(pick.way), point_name(pick.point), static_cast<void const *>(r.table), r.table_from_object,
               memory_name(in.memory), wide(module), module_name(in.module), r.section, context_first_entries,
               in.system_entries, in.other_entries, in.no_module_entries, in.qi_prefix,
               context_least_length[std::min<std::size_t>(in.qi_prefix, context_least_length.size() - 1)], in.length,
               in.length >= context_table_most ? " (the most read)" : "", hook_contract_version);
      if(r.mark)
        log_line("context: the table of another copy of the library (contract {}, {} entries over the table at {:#x})",
                 r.mark->contract, r.mark->count, r.mark->runtime);
      else if(r.mark_candidates)
        log_line("context: {} allocation(s) its first entries lead into looked at for a mark, none taken (the last: {})",
                 r.mark_candidates, check_name(r.mark_last));
      }

    std::atomic<bool> system_said{};

    ///\brief once in the process: the system's d3d11.dll not loaded or its image not read (its entries then count as
    /// another module's)
    auto log_system(chain::system_d3d11_t const & system) noexcept -> void
      {
      if((system.module and system.first != system.end) or system_said.exchange(true, std::memory_order_relaxed))
        return;
      if(not system.module)
        log_line("context: the system's d3d11.dll could not be loaded (error {}); its entries count as another module's",
                 system.error);
      else
        log_line("context: the image of the system's d3d11.dll could not be read; its entries count as another "
                 "module's");
      }
    }  // namespace

  auto read_context(ID3D11DeviceContext * context, chain::system_d3d11_t const & system,
                    context_ask_e ask) noexcept -> context_reading_t
    {
    context_reading_t r{};
    r.input.ask = ask;
    if(not context or not copy_readable(&r.table, context, sizeof r.table) or not r.table)
      return r;
    r.read = true;
    r.table_from_object = static_cast<std::int64_t>(address(r.table) - address(context));
    r.input.memory = memory_of(r.table);
    r.module = module_of(r.table);
    if(r.module)
      section_of(r.module, r.table, r.section);
    r.input.module = not r.module                  ? table_module_e::none
                     : r.module == system.module ? table_module_e::system_d3d11
                                                 : table_module_e::other;
    // the length: entries leading to code, from the first, each read under the fault handler
    std::array<void *, context_first_entries> first{};
    std::uint32_t length{};
    for(; length != context_table_most; ++length)
      {
      void * entry{};
      if(not copy_readable(&entry, r.table + length, sizeof entry) or not entry or not executable(entry))
        break;
      if(length < first.size())
        first[length] = entry;
      }
    r.input.length = length;
    std::size_t const counted{std::min<std::size_t>(length, first.size())};
    for(std::size_t i{}; i != counted; ++i)
      {
      if(system.contains(address(first[i])))
        ++r.input.system_entries;
      else if(module_of(first[i]))
        ++r.input.other_entries;
      else
        ++r.input.no_module_entries;
      }
    r.input.qi_prefix = qi_prefix(context);
    mark_search_t const found{find_mark(r.table, std::span<void * const>{first}.first(counted))};
    r.mark = found.mark;
    r.mark_candidates = found.candidates;
    r.mark_last = found.last;
    r.input.other_instance = found.mark.has_value();
    return r;
    }

  auto context_hooks_t::attach(ID3D11DeviceContext * context, context_ask_e ask, std::span<slot_patch_t> list) noexcept
    -> context_attached_t
    {
    context_attached_t out{};
    if(not context)
      return out;
    // one try per plugin, checked before anything changes: the first try's object, flag, table and hooks stay
    switch(attach_gate(tried_, attached_.load(std::memory_order_acquire),
                       object_.load(std::memory_order_acquire) == context))
      {
      case attach_gate_e::already:
        out.result = context_attach_e::already;
        return out;
      case attach_gate_e::refused:
        if(not refusal_said_)
          {
          refusal_said_ = true;
          log_line("context: another attach ({} after {}{}) refused: one try per plugin; the first's hooks stay",
                   static_cast<void const *>(context), object_.load(std::memory_order_acquire),
                   attached_.load(std::memory_order_acquire) ? "" : ", a try that failed");
          }
        out.result = context_attach_e::refused;
        return out;
      case attach_gate_e::first:
        break;
      }
    tried_ = true;
    // not attached from here until the whole list is in: the hooks pass their calls meanwhile
    attached_.store(false, std::memory_order_release);
    object_.store(context, std::memory_order_release);
    chain::system_d3d11_t const & system{chain::system_d3d11()};
    log_system(system);
    context_reading_t const r{read_context(context, system, ask)};
    if(not r.read)
      {
      log_line("context: its table could not be read; the hooks not written");
      return out;
      }
    out.pick = context_pick(r.input);
    log_reading(r, out.pick);
    out.way = out.pick.way;
    // the list within the table's entries (another copy's table: within its mark's count too), either way
    std::size_t const entries{r.mark ? std::min<std::size_t>(r.input.length, r.mark->count) : r.input.length};
    if(std::optional<std::size_t> const past{slot_past(list, entries)})
      {
      log_line("context: slot {} of the list past the table's {} entries; the hooks not written", *past, entries);
      out.result = context_attach_e::slot_out;
      return out;
      }
    if(out.way == context_way_e::own_table)
      {
      own_table_t own{};
      out.made = make_own_table(r.table, r.input.length, own);
      if(out.made->result != own_table_make_e::made)
        {
        log_line("context: no table of our own (step {}, error {}); in place", static_cast<int>(out.made->result),
                 out.made->error);
        out.way = context_way_e::in_place;
        }
      else
        {
        if(not put_all(own, list))
          {
          log_line("context: a slot past the {} entries of the table of our own; the hooks not attached", own.count);
          out.result = context_attach_e::slot_out;
          return out;
          }
        own_table_point(own, context);
        own_ = own;
        table_ = own.entries;
        runtime_ = own.runtime;
        log_line("context: on a table of our own ({} entries at {}, stubs at {}{}), {} hooks",
                 own.count, static_cast<void const *>(own.entries), static_cast<void const *>(own.stubs),
                 out.made->header_copied ? "" : ", the words before entry 0 unread, zero", list.size());
        context->AddRef();  // never released (contract c)
        out.result = context_attach_e::attached;
        attached_.store(true, std::memory_order_release);
        return out;
        }
      }
    out.slots = patch_slots(r.table, list);
    own_ = own_table_t{};
    table_ = r.table;
    // in place on another copy's table its runtime's table is the runtime's; on another table each entry is its own
    runtime_ = r.mark ? reinterpret_cast<void * const *>(static_cast<std::uintptr_t>(r.mark->runtime)) : nullptr;
    log_line("context: in place, {} hooks written, {} already ours, {} read back as another; {} pages made writable, "
             "{} left so (step {}, error {}, slot {}, protection {:#x})",
             out.slots.patched, out.slots.already_ours, out.slots.read_back_differs, out.slots.pages_changed,
             out.slots.restore_failed, static_cast<int>(out.slots.result), out.slots.error, out.slots.failed_slot,
             out.slots.failed_protect);
    if(not out.slots.all_ours())
      {
      log_line("context: not every hook in; the hooks pass their calls on");
      out.result = context_attach_e::slots_failed;
      return out;
      }
    context->AddRef();  // never released (contract c)
    out.result = context_attach_e::attached;
    attached_.store(true, std::memory_order_release);
    return out;
    }

  auto context_hooks_t::watch_setup(std::span<slot_patch_t const> list) const noexcept -> watch_setup_t
    {
    chain::system_d3d11_t const & system{chain::system_d3d11()};
    return watch_setup_t{static_cast<IUnknown *>(object_.load(std::memory_order_acquire)),
                         table_,
                         list,
                         own_.made() ? &own_ : nullptr,
                         runtime_,
                         system.first,
                         system.end};
    }
  }  // namespace edplugin_common::hook
