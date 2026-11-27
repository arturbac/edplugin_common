// edplugin_common - the rule that picks how a plugin watches the game's immediate context: on a table of its own
// (the object pointed at a copy whose free slots are stubs through the runtime's entries, so the runtime's rewriting
// of its own table does not take the hooks away) or in place (the hooks written into the table the object already
// points at). Its inputs come from reading the table (hook/context.h); this header has no Windows header and is
// checked natively.
#pragma once

#include <array>
#include <cstdint>

namespace edplugin_common::hook
  {
  ///\brief the kind of memory the context's table lies in
  enum struct table_memory_e : std::uint8_t
    {
    unknown,         ///< the reading failed, or mapped, free, reserved or another kind
    image,           ///< a module's image
    private_memory,  ///< private memory (Windows' heap: the runtime's table in the object)
    };

  ///\brief the module the context's table lies in
  enum struct table_module_e : std::uint8_t
    {
    none,          ///< no module (private memory)
    system_d3d11,  ///< the system's d3d11.dll
    other,         ///< another module (3Dmigoto's d3d11_edhm.dll, ReShade, an overlay)
    };

  ///\brief what the plugin's settings ask for (`context_hook` in its ini)
  enum struct context_ask_e : std::uint8_t
    {
    automatic,  ///< 0: the rule below
    in_place,   ///< 1: in place
    own_table,  ///< 2: a table of its own, unless the table is too short or too long
    };

  ///\brief the way picked
  enum struct context_way_e : std::uint8_t
    {
    in_place,
    own_table,
    };

  ///\brief the point of the rule the way was picked by, said in the log
  enum struct context_point_e : std::uint8_t
    {
    input_invalid,          ///< the reading gave numbers out of their domain: in place
    asked_in_place,         ///< context_hook = 1
    memory_unknown,         ///< 1: memory of no known kind
    image_of_other_module,  ///< 2: the table in another module's image (3Dmigoto, ReShade)
    other_instance,         ///< 3: a table of another copy of this library (its mark read)
    few_modules,            ///< 4: fewer than 3/4 of the first entries lead to a module (stubs of another hook)
    too_short,              ///< 5: shorter than the least length the interfaces the context answers need
    too_long,               ///< 5: as long as the most read, so its end is unknown
    own_table,              ///< 4 and 5 hold (or context_hook = 2 and 5 holds)
    };

  ///\brief the entries counted by the module they lead to
  inline constexpr std::uint32_t context_first_entries{96};
  ///\brief the most entries read: a table this long is taken as one of unknown length
  inline constexpr std::uint32_t context_table_most{2048};
  ///\brief the least length of a table for a context answering ID3D11DeviceContext1..N (index N, 0 to 4): the methods
  /// of ID3D11DeviceContext (7 of IUnknown and ID3D11DeviceChild, 108 of its own) and of each later interface
  inline constexpr std::array<std::uint16_t, 5> context_least_length{115, 134, 144, 147, 149};

  ///\brief what the reading of the table found
  struct context_input_t
    {
    table_memory_e memory{table_memory_e::unknown};
    table_module_e module{table_module_e::none};
    std::uint16_t system_entries{};     ///< of the first 96, those in the system's d3d11.dll
    std::uint16_t other_entries{};      ///< of the first 96, those in another module
    std::uint16_t no_module_entries{};  ///< of the first 96, those in no module
    bool other_instance{};              ///< the table carries the mark of another copy of this library
    std::uint8_t qi_prefix{};           ///< N: the object answers ID3D11DeviceContext1..N as itself (0 to 4)
    std::uint32_t length{};             ///< entries leading to code from the first, at most context_table_most
    context_ask_e ask{context_ask_e::automatic};
    };

  ///\brief the way and the point of the rule it was picked by
  struct context_pick_t
    {
    context_way_e way{context_way_e::in_place};
    context_point_e point{context_point_e::input_invalid};

    [[nodiscard]]
    constexpr auto operator==(context_pick_t const &) const noexcept -> bool = default;
    };

  ///\brief whether every number of `in` lies in its domain (the reading's result is checked here, at the border)
  [[nodiscard]]
  constexpr auto context_input_valid(context_input_t const & in) noexcept -> bool
    {
    return in.memory <= table_memory_e::private_memory and in.module <= table_module_e::other
           and in.ask <= context_ask_e::own_table and in.qi_prefix < context_least_length.size()
           and in.length <= context_table_most
           and std::uint32_t{in.system_entries} + in.other_entries + in.no_module_entries <= context_first_entries;
    }

  ///\brief rule point 5 for a table of its own: long enough for the interfaces the context answers, and of a known end
  [[nodiscard]]
  constexpr auto context_length_pick(context_input_t const & in) noexcept -> context_pick_t
    {
    if(in.length < context_least_length[in.qi_prefix])
      return {context_way_e::in_place, context_point_e::too_short};
    if(in.length >= context_table_most)
      return {context_way_e::in_place, context_point_e::too_long};
    return {context_way_e::own_table, context_point_e::own_table};
    }

  ///\brief the way to watch the context (rule R0 of the W1 design): 6 the setting, 1 unknown memory, 2 another
  /// module's image, 3 another copy of this library, 4 at least 3/4 of the first 96 entries in modules, 5 the length
  [[nodiscard]]
  constexpr auto context_pick(context_input_t const & in) noexcept -> context_pick_t
    {
    if(not context_input_valid(in))
      return {context_way_e::in_place, context_point_e::input_invalid};
    if(in.ask == context_ask_e::in_place)
      return {context_way_e::in_place, context_point_e::asked_in_place};
    if(in.ask == context_ask_e::own_table)
      return context_length_pick(in);
    if(in.memory == table_memory_e::unknown)
      return {context_way_e::in_place, context_point_e::memory_unknown};
    if(in.memory == table_memory_e::image and in.module == table_module_e::other)
      return {context_way_e::in_place, context_point_e::image_of_other_module};
    if(in.other_instance)
      return {context_way_e::in_place, context_point_e::other_instance};
    if(4u * (std::uint32_t{in.system_entries} + in.other_entries) < 3u * context_first_entries)
      return {context_way_e::in_place, context_point_e::few_modules};
    return context_length_pick(in);
    }

  ///\brief the point's name in the log
  [[nodiscard]]
  constexpr auto point_name(context_point_e p) noexcept -> char const *
    {
    switch(p)
      {
      case context_point_e::input_invalid:         return "the reading out of its domain";
      case context_point_e::asked_in_place:        return "context_hook = 1";
      case context_point_e::memory_unknown:        return "1: memory of no known kind";
      case context_point_e::image_of_other_module: return "2: another module's image";
      case context_point_e::other_instance:        return "3: a table of another copy of the library";
      case context_point_e::few_modules:           return "4: fewer than 3/4 of the first entries in modules";
      case context_point_e::too_short:             return "5: too short for the interfaces it answers";
      case context_point_e::too_long:              return "5: of an unknown end";
      case context_point_e::own_table:             return "4 and 5 hold";
      }
    return "?";
    }

  [[nodiscard]]
  constexpr auto memory_name(table_memory_e m) noexcept -> char const *
    {
    switch(m)
      {
      case table_memory_e::unknown:        return "unknown";
      case table_memory_e::image:          return "image";
      case table_memory_e::private_memory: return "private";
      }
    return "?";
    }

  [[nodiscard]]
  constexpr auto module_name(table_module_e m) noexcept -> char const *
    {
    switch(m)
      {
      case table_module_e::none:         return "none";
      case table_module_e::system_d3d11: return "the system's d3d11.dll";
      case table_module_e::other:        return "another module";
      }
    return "?";
    }

  [[nodiscard]]
  constexpr auto way_name(context_way_e w) noexcept -> char const *
    {
    return w == context_way_e::own_table ? "a table of our own" : "in place";
    }

  ///\brief what an attach call may do: one try per plugin (per context_hooks_t)
  enum struct attach_gate_e : std::uint8_t
    {
    first,    ///< no try yet: this one is the try
    already,  ///< the same context attached in whole before: nothing to do
    refused,  ///< another context after the try, or any after a try that failed: nothing changed
    };

  ///\brief the gate of an attach call, before anything is changed: a second try could make a table of our own whose
  /// stubs lead through entries already holding our hooks (a call loop), and would take the first context's state
  [[nodiscard]]
  constexpr auto attach_gate(bool tried, bool attached, bool same_context) noexcept -> attach_gate_e
    {
    if(not tried)
      return attach_gate_e::first;
    if(attached and same_context)
      return attach_gate_e::already;
    return attach_gate_e::refused;
    }

  static_assert(attach_gate(false, false, false) == attach_gate_e::first
                and attach_gate(true, true, true) == attach_gate_e::already
                and attach_gate(true, true, false) == attach_gate_e::refused
                and attach_gate(true, false, true) == attach_gate_e::refused
                and attach_gate(true, false, false) == attach_gate_e::refused);

  // the cases of the W1 design's table, on a context answering ID3D11DeviceContext1..4 (N = 4)
  namespace rule_cases
    {
    inline constexpr context_input_t windows{
      table_memory_e::private_memory, table_module_e::none, 96, 0, 0, false, 4, 319, context_ask_e::automatic};
    inline constexpr context_input_t windows_hooked{
      table_memory_e::private_memory, table_module_e::none, 71, 25, 0, false, 4, 319, context_ask_e::automatic};
    inline constexpr context_input_t wine{
      table_memory_e::image, table_module_e::system_d3d11, 96, 0, 0, false, 4, 149, context_ask_e::automatic};
    inline constexpr context_input_t wine_hooked{
      table_memory_e::image, table_module_e::system_d3d11, 71, 25, 0, false, 4, 149, context_ask_e::automatic};
    inline constexpr context_input_t migoto{
      table_memory_e::image, table_module_e::other, 0, 96, 0, false, 1, 137, context_ask_e::automatic};
    inline constexpr context_input_t instance{
      table_memory_e::private_memory, table_module_e::none, 0, 27, 69, true, 4, 319, context_ask_e::automatic};
    inline constexpr context_input_t live_copy{
      table_memory_e::private_memory, table_module_e::none, 0, 10, 86, false, 4, 319, context_ask_e::automatic};

    static_assert(context_pick(windows) == context_pick_t{context_way_e::own_table, context_point_e::own_table});
    static_assert(context_pick(rule_cases::windows_hooked).way == context_way_e::own_table);
    static_assert(context_pick(rule_cases::wine).way == context_way_e::own_table,
                  "wine: 149 entries are enough for N = 4");
    static_assert(context_pick(rule_cases::wine_hooked).way == context_way_e::own_table);
    static_assert(context_pick(rule_cases::migoto).point == context_point_e::image_of_other_module);
    static_assert(context_pick(rule_cases::instance).point == context_point_e::other_instance);
    static_assert(context_pick(rule_cases::live_copy).point == context_point_e::few_modules);
    // 3/4 of 96 is 72: any module counts, so 71 in the system's d3d11 and 25 in another plugin give 96 (own table)
    static_assert(context_pick(context_input_t{windows.memory, windows.module, 70, 2, 24, false, 4, 319}).way
                  == context_way_e::own_table);
    static_assert(context_pick(context_input_t{windows.memory, windows.module, 70, 1, 25, false, 4, 319}).point
                  == context_point_e::few_modules);
    static_assert(context_pick(context_input_t{table_memory_e::unknown, table_module_e::none, 96, 0, 0, false, 4, 319})
                    .point
                  == context_point_e::memory_unknown);
    // point 5: the least length by N, and the most read
    static_assert(context_pick(context_input_t{windows.memory, windows.module, 96, 0, 0, false, 0, 115}).way
                  == context_way_e::own_table);
    static_assert(context_pick(context_input_t{windows.memory, windows.module, 96, 0, 0, false, 0, 114}).point
                  == context_point_e::too_short);
    static_assert(context_pick(context_input_t{windows.memory, windows.module, 96, 0, 0, false, 4, 148}).point
                  == context_point_e::too_short);
    static_assert(context_pick(context_input_t{windows.memory, windows.module, 96, 0, 0, false, 4, 2047}).way
                  == context_way_e::own_table);
    static_assert(context_pick(context_input_t{windows.memory, windows.module, 96, 0, 0, false, 4, 2048}).point
                  == context_point_e::too_long);
    // point 6: the setting
    static_assert(context_pick(context_input_t{windows.memory, windows.module, 96, 0, 0, false, 4, 319,
                                               context_ask_e::in_place})
                  == context_pick_t{context_way_e::in_place, context_point_e::asked_in_place});
    static_assert(context_pick(context_input_t{table_memory_e::image, table_module_e::other, 0, 96, 0, false, 1, 137,
                                               context_ask_e::own_table})
                    .way
                  == context_way_e::own_table, "context_hook = 2 over 3Dmigoto's table: own table when long enough");
    static_assert(context_pick(context_input_t{table_memory_e::image, table_module_e::other, 0, 96, 0, false, 4, 137,
                                               context_ask_e::own_table})
                    .point
                  == context_point_e::too_short);
    // the domain: numbers out of it give in place
    static_assert(context_pick(context_input_t{windows.memory, windows.module, 96, 1, 0, false, 4, 319}).point
                  == context_point_e::input_invalid, "97 of the first 96");
    static_assert(context_pick(context_input_t{windows.memory, windows.module, 96, 0, 0, false, 5, 319}).point
                  == context_point_e::input_invalid, "N above 4");
    static_assert(context_pick(context_input_t{windows.memory, windows.module, 96, 0, 0, false, 4, 2049}).point
                  == context_point_e::input_invalid, "a length above the most read");
    static_assert(context_pick(context_input_t{static_cast<table_memory_e>(3), windows.module, 96, 0, 0, false, 4, 319})
                    .point
                  == context_point_e::input_invalid);
    static_assert(context_pick(context_input_t{windows.memory, windows.module, 96, 0, 0, false, 4, 319,
                                               static_cast<context_ask_e>(3)})
                    .point
                  == context_point_e::input_invalid, "context_hook = 3");
    // every point has a name in the log
    static_assert([]
                  {
                    for(std::uint8_t p{}; p <= static_cast<std::uint8_t>(context_point_e::own_table); ++p)
                      if(point_name(static_cast<context_point_e>(p))[0] == '?')
                        return false;
                    return point_name(static_cast<context_point_e>(9))[0] == '?';
                  }());
    }  // namespace rule_cases
  }  // namespace edplugin_common::hook
