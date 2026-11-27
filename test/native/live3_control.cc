// The control of the ThreadSanitizer check of live3_settings_t::live_fields (ini_native.cc): a thread other than the
// render thread reads the render thread's slot (slots_[front_]) instead of the start copy, while the render thread
// takes publications. ThreadSanitizer has to report a data race here; a run without one means the check in
// ini_native.cc cannot see that mistake. Built and run with -fsanitize=thread only (tools/test-native.sh expects a
// report and a failed run).
#include <edplugin_common/ini/live.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace
  {
  using namespace edplugin_common;

  struct control_t
    {
    std::vector<std::uint64_t> list{1};
    std::wstring text{L"start"};
    };

  enum struct group_e : std::size_t
    {
    first
    };

  ini_setting_t<control_t, group_e> const rows[]{
    {"list", ini_list_t<control_t>{&control_t::list}, group_e::first, setting_read_e::live, "", "a list"},
    {"text", &control_t::text, group_e::first, setting_read_e::live, "", "a text"},
  };
  ini_table_t<control_t, group_e> const table{rows, {}};

  auto told(std::string_view) noexcept -> void {}
  }  // namespace

namespace edplugin_common::detail
  {
  struct live3_settings_peek_t
    {
    ///\brief the mistake: the render thread's slot read on another thread
    template<typename settings_t>
    [[nodiscard]]
    static auto front(live3_settings_t<settings_t> const & l) noexcept -> settings_t const &
      {
      return l.slots_[l.front_].settings;
      }
    };
  }  // namespace edplugin_common::detail

auto main() -> int
  {
  live3_settings_t<control_t> l;
  l.start(control_t{}, told);
  constexpr int rounds{200};
  std::atomic<bool> done{};
  std::atomic<std::size_t> sizes{};
  std::thread other{[&]
                      {
                        while(not done.load(std::memory_order_acquire))
                          {
                          control_t const & s{detail::live3_settings_peek_t::front(l)};
                          sizes.fetch_add(s.list.size() + s.text.size(), std::memory_order_relaxed);
                          }
                      }};
  std::thread render{[&]
                       {
                         while(not done.load(std::memory_order_acquire))
                           sizes.fetch_add(l.acquire().settings.list.size(), std::memory_order_relaxed);
                       }};
  control_t read{};
  for(int n{1}; n <= rounds; ++n)
    {
    read.list.assign(static_cast<std::size_t>(n % 5) + 1u, static_cast<std::uint64_t>(n));
    read.text = std::wstring(static_cast<std::size_t>(n % 50) + 20u, L'x');
    l.publish(read, table);
    std::this_thread::yield();
    }
  done.store(true, std::memory_order_release);
  other.join();
  render.join();
  std::printf("live3_control: %d publications ran without a report (%zu)\n", rounds, sizes.load());
  return 0;
  }
