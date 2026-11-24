// edplugin_common - a plugin's settings as the game's threads see them while the ini is read again: two slots and an
// atomic pointer to the one in use (edworld's model), without Windows.
//
// The contract:
//   - start(s) fills both slots with the same settings, before any other thread reads them;
//   - the start-only fields (setting_read_e::at_start) never change after it: any thread may read them, through get()
//     or a reference kept as long as it likes;
//   - the live fields, and the slot's generation, are read only on the thread that publishes (the render thread, where
//     the plugin looks at its ini): publish() writes the slot not in use, which a reference from two publications
//     before may still show;
//   - a field read on another thread while the game runs gets a copy of its own the publishing thread keeps (a value
//     taken at the start, or a std::atomic mirror set at publish, as edupscale's cpu_probe for its hooks on the game's
//     loading threads).
#pragma once

#include <edplugin_common/ini/ini_table.h>

#include <atomic>
#include <cstdint>

namespace edplugin_common
  {
  template<typename settings_t>
  class live_settings_t
    {
  public:
    struct slot_t
      {
      settings_t settings{};
      std::uint32_t generation{};  ///< one more at each publication; 0 from start()
      };

  private:
    slot_t slots_[2]{};
    std::atomic<slot_t const *> live_{&slots_[0]};

  public:
    live_settings_t() = default;
    live_settings_t(live_settings_t const &) = delete;
    auto operator=(live_settings_t const &) -> live_settings_t & = delete;

    ///\brief both slots `s`, generation 0; before any other thread reads them
    auto start(settings_t const & s) -> void
      {
      slots_[0].settings = s;
      slots_[1].settings = s;
      slots_[0].generation = 0;
      slots_[1].generation = 0;
      live_.store(&slots_[0], std::memory_order_release);
      }

    [[nodiscard]]
    auto get() const noexcept -> settings_t const &
      {
      return live_.load(std::memory_order_acquire)->settings;
      }

    ///\brief the settings and their generation through one pointer (on the publishing thread)
    [[nodiscard]]
    auto slot() const noexcept -> slot_t const &
      {
      return *live_.load(std::memory_order_acquire);
      }

    ///\brief the live rows of `read` into the slot not in use, field by field, its generation one more, then that slot
    /// in use. On the publishing thread only
    template<typename group_t>
    auto publish(settings_t const & read, ini_table_t<settings_t, group_t> const & table) -> void
      {
      slot_t const & now{*live_.load(std::memory_order_relaxed)};
      slot_t & spare{&now == &slots_[0] ? slots_[1] : slots_[0]};
      ini_copy_live(spare.settings, read, table);
      spare.generation = now.generation + 1u;
      live_.store(&spare, std::memory_order_release);
      }
    };
  }  // namespace edplugin_common
