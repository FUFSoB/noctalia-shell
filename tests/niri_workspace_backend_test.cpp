#include "compositors/niri/niri_runtime.h"
#include "compositors/niri/niri_workspace_backend.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

  bool check(bool cond, const char* msg) {
    if (!cond) {
      std::cerr << "FAIL: " << msg << '\n';
    }
    return cond;
  }

} // namespace

int main() {
  bool ok = true;

  const std::string socketPath = "/tmp/noctalia-niri-workspace-test-" + std::to_string(getpid()) + ".sock";
  ::unlink(socketPath.c_str());

  const int listener = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (!check(listener >= 0, "socket() failed")) {
    return 1;
  }

  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (!check(socketPath.size() < sizeof(address.sun_path), "socket path too long")) {
    return 1;
  }
  std::memcpy(address.sun_path, socketPath.c_str(), socketPath.size() + 1);
  if (!check(::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0, "bind() failed")) {
    return 1;
  }
  if (!check(::listen(listener, 1) == 0, "listen() failed")) {
    return 1;
  }

  // Exercise backend event handling without NIRI_SOCKET so construction does not
  // open the persistent event-stream connection (that path is covered elsewhere).
  compositors::niri::NiriRuntime runtime;
  NiriWorkspaceBackend backend(runtime);
  int changes = 0;
  backend.setChangeCallback([&]() { ++changes; });
  backend.handleEvent(
      "WorkspacesChanged",
      {{"workspaces",
        {
            {{"id", 1}, {"idx", 1}, {"name", "development"}, {"output", "DP-1"}},
            {{"id", 2}, {"idx", 2}, {"output", "DP-1"}},
        }}}
  );
  ok &= check(
      backend.workspaceKeys("DP-1") == std::vector<std::string>({"development", "2"}),
      "workspace keys should prefer names and fall back to indices"
  );
  changes = 0;
  backend.handleEvent(
      "WindowsChanged",
      {{"windows",
        {
            {{"id", 41},
             {"workspace_id", 1},
             {"app_id", "com.mitchellh.ghostty"},
             {"is_focused", true},
             {"layout", {{"pos_in_scrolling_layout", {1, 1}}}}},
            {{"id", 42},
             {"workspace_id", 2},
             {"app_id", "com.mitchellh.ghostty"},
             {"is_focused", false},
             {"layout", {{"pos_in_scrolling_layout", {1, 1}}}}},
        }}}
  );
  ok &= check(backend.focusedWindowId() == std::optional<std::string>("41"), "initial focused window should be 41");

  backend.handleEvent("WindowFocusChanged", {{"id", 42}});
  ok &= check(backend.focusedWindowId() == std::optional<std::string>("42"), "focused window should be 42 after event");
  ok &= check(changes == 2, "expected 2 changes after windows+focus events");

  // A new scrolling-layout position must invalidate taskbar ordering even
  // though workspace/app membership is unchanged.
  backend.handleEvent(
      "WindowOpenedOrChanged",
      {{"window",
        {{"id", 41},
         {"workspace_id", 1},
         {"app_id", "com.mitchellh.ghostty"},
         {"is_focused", false},
         {"layout", {{"pos_in_scrolling_layout", {2, 1}}}}}}}
  );
  ok &= check(changes == 3, "layout position change should trigger a change");
  auto windows = backend.workspaceWindows();
  const auto window41 = std::ranges::find(windows, "41", &WorkspaceWindow::windowId);
  ok &= check(
      window41 != windows.end() && window41->workspaceKey == "development" && window41->x == 2 && window41->y == 1,
      "window 41 should use its named workspace and updated position"
  );

  // Window attributes, sticky placement and casts.
  {
    NiriWorkspaceBackend states(runtime);
    int stateChanges = 0;
    states.setChangeCallback([&]() { ++stateChanges; });
    states.handleEvent(
        "WorkspacesChanged",
        {{"workspaces",
          {
              {{"id", 10}, {"idx", 1}, {"output", "DP-1"}, {"is_active", true}, {"is_focused", true}},
              {{"id", 11}, {"idx", 2}, {"output", "DP-1"}, {"is_active", false}, {"is_focused", false}},
              {{"id", 20}, {"idx", 1}, {"output", "HDMI-A-1"}, {"is_active", true}, {"is_focused", false}},
          }}}
    );
    states.handleEvent(
        "WindowsChanged",
        {{"windows",
          {
              {{"id", 1}, {"workspace_id", 10}, {"app_id", "a"}, {"is_urgent", true}, {"is_floating", true}},
              {{"id", 2},
               {"workspace_id", nullptr},
               {"app_id", "b"},
               {"is_sticky", true},
               {"is_mirror", false},
               {"is_block_out", true}},
              {{"id", 3}, {"workspace_id", 11}, {"app_id", "c"}, {"is_mirror", true}},
          }}}
    );

    auto find = [&](const std::vector<WorkspaceWindow>& list, std::string_view id) -> const WorkspaceWindow* {
      const auto it = std::ranges::find(list, id, &WorkspaceWindow::windowId);
      return it != list.end() ? &*it : nullptr;
    };

    auto dp1 = states.workspaceWindows("DP-1");
    const auto* first = find(dp1, "1");
    ok &= check(
        first != nullptr && first->attributes.urgent && first->attributes.floating && !first->attributes.sticky,
        "window 1 should be urgent and floating"
    );
    const auto* sticky = find(dp1, "2");
    ok &= check(
        sticky != nullptr && sticky->workspaceKey == "1" && sticky->attributes.sticky,
        "sticky window without a workspace should show on the focused output's active workspace"
    );
    ok &= check(find(states.workspaceWindows("HDMI-A-1"), "2") == nullptr, "sticky window stays on its output");
    const auto* mirror = find(dp1, "3");
    ok &= check(mirror != nullptr && mirror->attributes.mirror, "window 3 should be a mirror");

    stateChanges = 0;
    states.handleEvent("WorkspaceActivated", {{"id", 11}, {"focused", true}});
    ok &= check(stateChanges == 1, "workspace activation should notify");
    dp1 = states.workspaceWindows("DP-1");
    sticky = find(dp1, "2");
    ok &= check(sticky != nullptr && sticky->workspaceKey == "2", "sticky window should follow the active workspace");

    states.handleEvent("WindowUrgencyChanged", {{"id", 1}, {"urgent", false}});
    first = find(states.workspaceWindows("DP-1"), "1");
    ok &= check(first != nullptr && !first->attributes.urgent, "urgency event should clear window urgency");

    states.handleEvent("BlockOutEnabledChanged", {{"is_enabled", true}});
    states.handleEvent(
        "CastsChanged",
        {{"casts",
          {
              {{"stream_id", 5},
               {"session_id", 7},
               {"target", {{"Workspace", {{"id", 11}}}}},
               {"is_dynamic_target", false},
               {"is_active", true}},
          }}}
    );
    sticky = find(states.workspaceWindows("DP-1"), "2");
    ok &= check(
        sticky != nullptr && sticky->attributes.hiddenFromCast && !sticky->attributes.cast,
        "blocked-out window inside a cast workspace should be hidden from the cast"
    );

    stateChanges = 0;
    states.handleEvent(
        "CastStartedOrChanged",
        {{"cast",
          {{"stream_id", 6},
           {"session_id", 8},
           {"target", {{"Window", {{"id", 3}}}}},
           {"is_dynamic_target", true},
           {"is_active", true}}}}
    );
    ok &= check(stateChanges == 1, "a new cast should notify");
    mirror = find(states.workspaceWindows("DP-1"), "3");
    ok &= check(mirror != nullptr && mirror->attributes.cast, "window 3 should be cast");

    states.handleEvent("CastStopped", {{"stream_id", 6}});
    mirror = find(states.workspaceWindows("DP-1"), "3");
    ok &= check(mirror != nullptr && !mirror->attributes.cast, "stopped cast should clear the window cast flag");

    // Unsticking hands the window back to a real workspace.
    states.handleEvent(
        "WindowOpenedOrChanged",
        {{"window", {{"id", 2}, {"workspace_id", 20}, {"app_id", "b"}, {"is_sticky", false}, {"is_block_out", true}}}}
    );
    ok &= check(find(states.workspaceWindows("DP-1"), "2") == nullptr, "unstuck window leaves DP-1");
    sticky = find(states.workspaceWindows("HDMI-A-1"), "2");
    ok &= check(
        sticky != nullptr && !sticky->attributes.sticky && !sticky->attributes.hiddenFromCast,
        "unstuck window should live on its workspace outside the cast"
    );
  }

  std::string request;
  int serverError = 0;
  std::jthread server([&]() {
    const int client = ::accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
    if (client < 0) {
      serverError = 1;
      return;
    }
    char buffer[512];
    const ssize_t size = ::read(client, buffer, sizeof(buffer));
    if (size <= 0) {
      ::close(client);
      serverError = 2;
      return;
    }
    request.assign(buffer, static_cast<std::size_t>(size));
    constexpr std::string_view response = "{\"Ok\":null}\n";
    const ssize_t written = ::write(client, response.data(), response.size());
    if (written != static_cast<ssize_t>(response.size())) {
      serverError = 3;
    }
    ::close(client);
  });

  if (!check(::setenv("NIRI_SOCKET", socketPath.c_str(), 1) == 0, "setenv(NIRI_SOCKET) failed")) {
    return 1;
  }
  runtime.refresh();

  ok &= check(backend.closeWindowById("42"), "closeWindowById should succeed");
  server.join();
  ok &= check(serverError == 0, "server thread encountered an error");
  const nlohmann::json expectedRequest{{"Action", {{"CloseWindow", {{"id", 42}}}}}};
  ok &= check(nlohmann::json::parse(request) == expectedRequest, "request payload mismatch");
  ok &= check(!backend.closeWindowById("not-an-id"), "closeWindowById with bad id should fail");

  ::close(listener);
  ::unlink(socketPath.c_str());
  return ok ? 0 : 1;
}
