// edplugin_common - the attach of a plugin's hooks to the game's immediate context: the table the context object
// points at read (its memory, module and section, the modules of its first entries, its length, the interfaces the
// object answers as itself, the mark of another copy of the library), the way picked by hook/context_rule.h, said
// once in the log, and the hooks written: into a table of our own (hook/own_table.h, the object pointed at it after
// every hook is in) or in place (hook/slot.h). Until the whole list is in, the context counts as not attached and
// every hook only passes its calls on (works() is false); a list written in part stays so, its slots written stay
// (contract c). A context attached in whole gets a reference, never released (contract c). One try per plugin: a
// second attach (another context, or any after a try that failed) is refused before anything changes. Another
// context than the one attached (a second device's) is never the plugin's: its calls pass.
// From the path that makes the device only (contract d), one thread at a time.
#pragma once

#include <edplugin_common/chain/system_d3d11.h>
#include <edplugin_common/hook/context_rule.h>
#include <edplugin_common/hook/own_table.h>
#include <edplugin_common/hook/slot_plan.h>
#include <edplugin_common/hook/watch.h>

#include <windows.h>
#include <d3d11.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <span>

namespace edplugin_common::hook
  {
  ///\brief what the reading of the context's table found
  struct context_reading_t
    {
    bool read{};                              ///< the object's table pointer could be read
    void ** table{};                          ///< the table the object points at
    std::int64_t table_from_object{};         ///< table − object, in bytes
    HMODULE module{};                         ///< the module holding the table, nullptr when none
    char section[9]{"-"};                     ///< its PE section
    context_input_t input{};                  ///< the rule's input (its `ask` the plugin's)
    std::optional<own_table_mark_t> mark{};   ///< the mark of another copy of the library's table
    std::uint8_t mark_candidates{};           ///< allocations looked at for a mark
    mark_check_e mark_last{mark_check_e::unread};  ///< the last candidate's check
    };

  ///\brief the object's table read, with the system's d3d11 to tell its modules by
  [[nodiscard]]
  auto read_context(ID3D11DeviceContext * context, chain::system_d3d11_t const & system,
                    context_ask_e ask) noexcept -> context_reading_t;

  enum struct context_attach_e : std::uint8_t
    {
    attached,        ///< every hook of the list in
    already,         ///< this context attached before: nothing done
    not_read,        ///< no context, or its table could not be read: nothing written
    slot_out,        ///< a slot of the list past the table's entries (its length; another copy's table: its mark's
                     ///< count): nothing written
    slots_failed,    ///< in place, not every slot holds its hook (slots_patched_t): the hooks pass their calls
    refused,         ///< a second try (another context, or any after a try that failed): nothing changed, said once
    };

  struct context_attached_t
    {
    context_attach_e result{context_attach_e::not_read};
    context_pick_t pick{};                        ///< the rule's pick
    context_way_e way{context_way_e::in_place};   ///< the way taken (in place after a table of our own failed)
    std::optional<own_table_made_t> made{};       ///< the table of our own's making, when tried
    slots_patched_t slots{};                      ///< in place: the write of the list

    [[nodiscard]]
    auto attached() const noexcept -> bool
      {
      return result == context_attach_e::attached or result == context_attach_e::already;
      }
    };

  class context_hooks_t
    {
  public:
    ///\brief `context` read, the way picked and logged once, the hooks of `list` written (each slot's original and
    /// result in it). One try per plugin: after it the same context attached in whole is `already`, any other call
    /// (another context, or any after a try that failed) is `refused` before anything changes
    [[nodiscard]]
    auto attach(ID3D11DeviceContext * context, context_ask_e ask, std::span<slot_patch_t> list) noexcept
      -> context_attached_t;

    ///\brief from a hook, before anything else: whether the call of `object` is the plugin's to work on (the context
    /// attached, and attached in whole)
    [[nodiscard]]
    auto works(void const * object) const noexcept -> bool
      {
      // the object first: attach() clears `attached_` before it stores the object (once, at the one try), so a hook
      // that sees the object sees the flag as it was set after it
      return object == object_.load(std::memory_order_acquire) and attached_.load(std::memory_order_acquire);
      }

    ///\brief for context_watch_t::start() after an attach: `list` the one attached
    [[nodiscard]]
    auto watch_setup(std::span<slot_patch_t const> list) const noexcept -> watch_setup_t;

  private:
    std::atomic<void *> object_{};
    std::atomic<bool> attached_{};
    void ** table_{};               ///< the table the hooks are in
    void * const * runtime_{};      ///< the runtime's table of the way (hook/watch_class.h)
    own_table_t own_{};             ///< the table of our own (none in place)
    bool tried_{};                  ///< the one try made (attach from one thread at a time, contract d)
    bool refusal_said_{};           ///< a refused try said in the log
    };
  }  // namespace edplugin_common::hook
