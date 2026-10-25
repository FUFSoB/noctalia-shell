#pragma once

#include "system/window_state.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <poll.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct wl_output;
struct ext_workspace_manager_v1;
struct org_kde_plasma_virtual_desktop_management;
struct zdwl_ipc_manager_v2;

struct Workspace {
  std::string id;
  std::string name;
  std::vector<std::uint32_t> coordinates;
  std::uint32_t index = 0;
  bool active = false;
  bool urgent = false;
  bool occupied = false;
  // Target of an active screencast (niri workspace casts).
  bool cast = false;
};

// Per-window compositor state beyond workspace membership. Only backends that
// track a field set it; everything stays false elsewhere.
struct WindowAttributes {
  bool urgent = false;
  bool floating = false;
  // Shown on every workspace of its output (niri fork).
  bool sticky = false;
  // A live copy of another window (niri fork).
  bool mirror = false;
  bool mirrorLinked = false;
  // Rendered transparent in screencasts while block-out is enabled (niri fork).
  bool blockOut = false;
  // Target of an active screencast.
  bool cast = false;
  // Inside an active workspace or output cast, but blocked out of it.
  bool hiddenFromCast = false;

  bool operator==(const WindowAttributes&) const = default;
};

struct WorkspaceWindow {
  std::string windowId;
  std::string workspaceKey;
  std::string appId;
  std::string title;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::string outputName;
  std::optional<WindowMinimizeState> minimizeState;
  WindowAttributes attributes;
};

struct WorkspaceWindowAssignment {
  std::string windowId;
  std::string workspaceKey;
  std::string appId;
  std::string title;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::optional<WindowMinimizeState> minimizeState;
  WindowAttributes attributes;
};

// A compositor-specific action offered in a window's context menu.
struct CompositorWindowAction {
  std::string id;
  std::string labelKey;
};

struct TaskbarWindowCandidate {
  std::uintptr_t handleKey = 0;
  std::vector<std::string> appIds;
  std::string title;
};

enum class TaskbarAssignmentMode {
  Generic,
  WorkspaceOccurrenceTitle,
};

class WorkspaceBackend {
public:
  using ChangeCallback = std::function<void()>;

  virtual ~WorkspaceBackend() = default;

  [[nodiscard]] virtual const char* backendName() const = 0;
  [[nodiscard]] virtual bool isAvailable() const noexcept = 0;
  virtual void setChangeCallback(ChangeCallback callback) = 0;
  virtual void activate(const std::string& id) = 0;
  virtual void activateForOutput(wl_output* output, const std::string& id) = 0;
  virtual void activateForOutput(wl_output* output, const Workspace& workspace) = 0;
  [[nodiscard]] virtual std::vector<Workspace> all() const = 0;
  [[nodiscard]] virtual std::vector<Workspace> forOutput(wl_output* output) const = 0;
  [[nodiscard]] virtual std::unordered_map<std::string, std::vector<std::string>>
  appIdsByWorkspace(wl_output* /*output*/) const {
    return {};
  }
  [[nodiscard]] virtual TaskbarAssignmentMode taskbarAssignmentMode() const noexcept {
    return TaskbarAssignmentMode::Generic;
  }
  [[nodiscard]] virtual std::unordered_map<std::uintptr_t, WorkspaceWindow>
  assignTaskbarWindows(const std::vector<TaskbarWindowCandidate>& /*windows*/, wl_output* /*output*/) const {
    return {};
  }
  [[nodiscard]] virtual std::vector<WorkspaceWindow> workspaceWindows(wl_output* /*output*/) const { return {}; }
  // Workspace keys of the overlay workspaces currently open on `output` (nullptr = every
  // output), such as Hyprland special workspaces, which backends hide from all()/forOutput().
  // Non-empty means the overlay supersedes the reported active workspace. Keys use the
  // WorkspaceWindow::workspaceKey namespace.
  [[nodiscard]] virtual std::vector<std::string> openOverlayWorkspaceKeys(wl_output* /*output*/) const { return {}; }
  virtual void focusWindow(const std::string& /*windowId*/) {}
  // Ext-foreign-toplevel identifier of the focused window, for compositors that publish one in
  // their IPC tree. Window capture itself needs sway >= 1.12, where
  // ext_foreign_toplevel_image_capture_source_manager_v1 landed. Separate namespace from
  // focusedWindowId(): sway's node ids drive focus/close actions, while this joins the
  // compositor-assigned ext capture source. Read together with publishesExtToplevelIdentity():
  // a nullopt here with true there means the compositor publishes one but has none right now,
  // whereas false there means it never will and callers may fall back.
  [[nodiscard]] virtual std::optional<std::string> focusedExtToplevelIdentifier() const { return std::nullopt; }
  // True only when this compositor is connected and observed publishing the identifier.
  [[nodiscard]] virtual bool publishesExtToplevelIdentity() const noexcept { return false; }
  virtual void cleanup() = 0;

  [[nodiscard]] virtual int pollFd() const noexcept { return -1; }
  [[nodiscard]] virtual short pollEvents() const noexcept { return POLLIN | POLLHUP | POLLERR; }
  [[nodiscard]] virtual int pollTimeoutMs() const noexcept { return -1; }
  virtual void dispatchPoll(short /*revents*/) {}
};

class ExtWorkspaceProtocolBinder {
public:
  virtual ~ExtWorkspaceProtocolBinder() = default;
  virtual void bindExtWorkspace(ext_workspace_manager_v1* manager) = 0;
};

class DwlIpcWorkspaceProtocolBinder {
public:
  virtual ~DwlIpcWorkspaceProtocolBinder() = default;
  virtual void bindDwlIpcWorkspace(zdwl_ipc_manager_v2* manager) = 0;
};

class KdeVirtualDesktopProtocolBinder {
public:
  virtual ~KdeVirtualDesktopProtocolBinder() = default;
  virtual void bindKdeVirtualDesktop(org_kde_plasma_virtual_desktop_management* management) = 0;
};

class WorkspaceOutputNameResolver {
public:
  using Resolver = std::function<std::string(wl_output*)>;

  virtual ~WorkspaceOutputNameResolver() = default;
  virtual void setOutputNameResolver(Resolver resolver) = 0;
};

class WorkspaceSocketConnector {
public:
  virtual ~WorkspaceSocketConnector() = default;
  [[nodiscard]] virtual bool connectSocket() = 0;
};

namespace compositors {

  class WorkspaceMetadataBackend {
  public:
    using ChangeCallback = std::function<void()>;

    virtual ~WorkspaceMetadataBackend() = default;
    virtual void setChangeCallback(ChangeCallback callback) = 0;
    // Overrides take ownership of the callback.
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    virtual void setWindowChangeCallback(ChangeCallback callback) { (void)callback; }
    // Overrides take ownership of the callback.
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    virtual void setOverviewChangeCallback(ChangeCallback callback) { (void)callback; }
    [[nodiscard]] virtual int pollFd() const noexcept { return -1; }
    [[nodiscard]] virtual short pollEvents() const noexcept { return POLLIN | POLLHUP | POLLERR; }
    [[nodiscard]] virtual int pollTimeoutMs() const noexcept { return -1; }
    virtual void dispatchPoll(short /*revents*/) {}
    virtual void apply(std::vector<Workspace>& /*workspaces*/, const std::string& /*outputName*/ = {}) const {}
    [[nodiscard]] virtual std::vector<std::string> workspaceKeys(const std::string& /*outputName*/ = {}) const {
      return {};
    }
    [[nodiscard]] virtual std::unordered_map<std::string, std::vector<std::string>>
    appIdsByWorkspace(const std::string& /*outputName*/ = {}) const {
      return {};
    }
    [[nodiscard]] virtual std::vector<WorkspaceWindow> workspaceWindows(const std::string& /*outputName*/ = {}) const {
      return {};
    }
    // True when ext-foreign-toplevel identifiers produced by this compositor
    // are exact compositor window IDs suitable for focus/close actions and
    // stable taskbar identity. Backends return true when they can join those
    // identifiers with their own IPC window model.
    [[nodiscard]] virtual bool hasExactWindowIdentity() const noexcept { return false; }
    [[nodiscard]] virtual std::optional<std::string> focusedWindowId() const { return std::nullopt; }
    // Focus a window by its compositor-specific id. Returns true if the backend
    // handled the request (so the caller can skip other focus paths). Named
    // distinctly from WorkspaceBackend::focusWindow so backends that implement
    // both interfaces don't hit a conflicting-return-type override.
    virtual bool focusWindowById(const std::string& /*windowId*/, bool /*warpPointer*/ = false) { return false; }
    virtual bool closeWindowById(const std::string& /*windowId*/) { return false; }
    [[nodiscard]] virtual std::optional<WindowMinimizeState> windowMinimizeState(std::string_view /*windowId*/) const {
      return std::nullopt;
    }
    virtual bool minimizeWindowById(const std::string& /*windowId*/) { return false; }
    [[nodiscard]] virtual std::vector<CompositorWindowAction> windowActions(const std::string& /*windowId*/) const {
      return {};
    }
    virtual bool runWindowAction(const std::string& /*windowId*/, std::string_view /*actionId*/) { return false; }
    [[nodiscard]] virtual bool canTrackOverviewState() const noexcept { return false; }
    [[nodiscard]] virtual bool hasOverviewState() const noexcept { return false; }
    [[nodiscard]] virtual bool isOverviewOpen() const noexcept { return true; }
    virtual void cleanup() = 0;
  };

} // namespace compositors
