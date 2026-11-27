// edplugin_common - the settings thread [u] of a plugin: its state, one pass of its loop without the thread and without
// Windows, the point another module extends it at, and the start a plugin instantiates for its own settings.
//
// The state (ini_thread_t) is made once when the thread starts and never deleted: the keeper (ini/keeper.h), the ini's
// watch and reads behind function pointers (Windows' in the library, a test's own natively), the event that wakes the
// thread (Windows' handle, kept without its type) and the extension point. A pass, on the settings thread: a look at
// the ini, when due once a second, and a read of it when it changed (the keeper's steps); then the extension's step,
// when there is one; the wait until the next pass back, the look's rhythm or shorter when the extension asks.
//
// The extension point: an atomic pointer, nullptr until another module (the connection to edtune) sets it, once, with
// release; each pass loads it with acquire. Its step runs on the settings thread with the keeper; nothing of the
// module's types shows here.
//
// The start: a plugin, after its synchronous first read and live3_start, calls ini_live_start with its table, its live
// object, its clamps, its watch (marked seen at the start) and what was said at the start; the view of its types is
// made here, and the library's Windows part (src/ini_thread.cc) makes the state and the thread. From then on the watch
// and what was said are the settings thread's only. Nothing here logs: the lines go back to the caller.
#pragma once

#include <edplugin_common/ini/ini.h>
#include <edplugin_common/ini/keeper.h>
#include <edplugin_common/ini/live.h>
#include <edplugin_common/ini/view.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace edplugin_common
  {
  namespace detail
    {
    ///\brief for the library's test only: the destruction of an ini_thread_t
    struct ini_thread_peek_t;
    }  // namespace detail

  ///\brief the ini under the settings thread: its watch and reads (ini_watch_t and file/file.h in the library)
  struct ini_files_t
    {
    void * context;
    ///\brief ini_watch_t::look
    ini_look_e (*look)(void * context, std::uint64_t now_ms) noexcept;
    ///\brief the file's text; nullopt when it could not be read
    std::optional<std::string> (*read)(void * context) noexcept;
    ///\brief whether the file is there (asked for an empty text only)
    bool (*there)(void * context) noexcept;
    ///\brief ini_watch_t::unreadable: true to say it
    bool (*unreadable)(void * context) noexcept;
    ///\brief ini_watch_t::readable
    void (*readable)(void * context) noexcept;
    };
  static_assert(std::is_trivially_copyable_v<ini_files_t>);

  class ini_thread_t;

  ///\brief another module's step in each pass of the settings thread, after the ini's; set once (ini_thread_t::extend)
  /// and kept to the end of the game
  struct ini_extension_t
    {
    void * context;
    ///\brief on the settings thread, with the keeper; lines for the log into `lines`; the wait it allows before the
    /// next pass, in milliseconds
    std::uint32_t (*step)(void * context, ini_keeper_t & keeper, std::vector<std::string> & lines) noexcept;
    };
  static_assert(std::is_trivially_copyable_v<ini_extension_t>);

  class ini_thread_t
    {
  public:
    ///\brief the rhythm of the ini's look (ini_watch_t looks once a second at most)
    static constexpr std::uint32_t look_ms{1000};

  private:
    ini_keeper_t & keeper_;
    ini_files_t files_;
    void * wake_;
    int attempts_;
    std::atomic<ini_extension_t const *> extension_{};
    std::uint64_t looked_ms_{};
    bool looked_{};

    friend struct detail::ini_thread_peek_t;

    ini_thread_t(ini_keeper_t & keeper, ini_files_t files, void * wake, int attempts) noexcept :
        keeper_{keeper}, files_{files}, wake_{wake}, attempts_{attempts}
      {
      }

    ~ini_thread_t() = default;

  public:
    ///\brief the state over `keeper`, the ini's `files`, the thread's `wake` event and its `attempts`; nullptr when it
    /// cannot be made (no memory)
    [[nodiscard]]
    static auto make(ini_keeper_t & keeper, ini_files_t const & files, void * wake, int attempts) noexcept
      -> ini_thread_t *
      {
      try
        {
        return new ini_thread_t{keeper, files, wake, attempts};
        }
      catch(...)
        {
        return nullptr;
        }
      }

    ini_thread_t(ini_thread_t const &) = delete;
    auto operator=(ini_thread_t const &) -> ini_thread_t & = delete;

    [[nodiscard]]
    auto keeper() noexcept -> ini_keeper_t &
      {
      return keeper_;
      }

    ///\brief the event that wakes the thread before its wait is over (Windows' HANDLE)
    [[nodiscard]]
    auto wake() const noexcept -> void *
      {
      return wake_;
      }

    [[nodiscard]]
    auto attempts() const noexcept -> int
      {
      return attempts_;
      }

    ///\brief the extension set, from any thread, once: false (nothing changed) when one is set already or `e` is
    /// nullptr. `*e` is kept to the end of the game
    [[nodiscard]]
    auto extend(ini_extension_t const * e) noexcept -> bool
      {
      if(e == nullptr)
        return false;
      ini_extension_t const * none{};
      return extension_.compare_exchange_strong(none, e, std::memory_order_release, std::memory_order_relaxed);
      }

    ///\brief at every start of the thread's body, the first one too (ini_keeper_t::body_started)
    auto body_started(std::vector<std::string> & lines) noexcept -> void { keeper_.body_started(lines); }

    ///\brief the thread's attempts spent (ini_keeper_t::attempts_spent)
    auto attempts_spent(std::vector<std::string> & lines) noexcept -> void { keeper_.attempts_spent(lines); }

    ///\brief one pass at `now_ms` (a tick count in milliseconds): the ini looked at and read when it changed, the
    /// extension's step; the lines for the log into `lines`; the wait until the next pass, in milliseconds
    [[nodiscard]]
    auto pass(std::uint64_t now_ms, std::vector<std::string> & lines) noexcept -> std::uint32_t
      {
      ini_look_e const look{files_.look(files_.context, now_ms)};
      if(look != ini_look_e::not_due or not looked_)
        {
        looked_ = true;
        looked_ms_ = now_ms;
        }
      if(keeper_.looked(look, lines))
        {
        std::optional<std::string> const text{files_.read(files_.context)};
        bool const there{not text or not text->empty() or files_.there(files_.context)};
        bool const say{not text and files_.unreadable(files_.context)};
        std::optional<std::string_view> const view{text ? std::optional<std::string_view>{*text} : std::nullopt};
        if(keeper_.read(view, there, say, lines) == ini_file_e::read)
          files_.readable(files_.context);
        }
      std::uint64_t const due{looked_ms_ + look_ms};
      std::uint32_t wait{due > now_ms ? static_cast<std::uint32_t>(due - now_ms) : 0u};
      if(ini_extension_t const * const e{extension_.load(std::memory_order_acquire)})
        wait = std::min(wait, e->step(e->context, keeper_, lines));
      return wait;
      }
    };

  ///\brief the library's Windows part (src/ini_thread.cc): the keeper over `view`, the state and the thread, `watch`
  /// and `told` the thread's from now; the body started again after a fault, `attempts` times at most. Every failure
  /// logged; nullptr then (no thread)
  auto ini_thread_start(ini_view_t const & view, ini_watch_t & watch, ini_told_t && told, int attempts) noexcept
    -> ini_thread_t *;

  ///\brief from any thread: the settings thread woken for a pass now (also after ini_watch_t::ask_soon)
  auto ini_thread_wake(ini_thread_t & thread) noexcept -> void;

  ///\brief the settings thread of the plugin's `table` and `live` (started), with its clamps (`clamp`, nullptr: none),
  /// its ini's `watch` (marked seen at the start) and what was said at the start (`told`): ini_thread_start over the
  /// view of the plugin's types. Once a session; on the plugin's thread after its start, never from DllMain
  template<typename settings_t, typename group_t>
  auto ini_live_start(ini_table_t<settings_t, group_t> const & table, live3_settings_t<settings_t> & live,
                      std::type_identity_t<void (*)(settings_t &) noexcept> clamp, ini_watch_t & watch,
                      ini_told_t && told, int attempts = 8) noexcept -> ini_thread_t *
    {
    return ini_thread_start(ini_view_of(table, live, clamp), watch, std::move(told), attempts);
    }
  }  // namespace edplugin_common
