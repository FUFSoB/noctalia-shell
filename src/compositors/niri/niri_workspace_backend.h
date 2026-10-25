#pragma once

#include "compositors/niri/niri_event_handler.h"
#include "compositors/workspace_backend.h"

#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace compositors::niri {
  class NiriRuntime;
} // namespace compositors::niri

class NiriWorkspaceBackend final : public compositors::WorkspaceMetadataBackend,
                                   public compositors::niri::NiriEventHandler {
public:
  explicit NiriWorkspaceBackend(compositors::niri::NiriRuntime& runtime);
  ~NiriWorkspaceBackend() override;

  NiriWorkspaceBackend(const NiriWorkspaceBackend&) = delete;
  NiriWorkspaceBackend& operator=(const NiriWorkspaceBackend&) = delete;

  void setChangeCallback(ChangeCallback callback) override;
  void setOverviewChangeCallback(ChangeCallback callback) override;
  [[nodiscard]] bool canTrackOverviewState() const noexcept override;
  [[nodiscard]] bool hasOverviewState() const noexcept override { return m_overviewKnown; }
  [[nodiscard]] bool isOverviewOpen() const noexcept override { return m_overviewOpen; }
  [[nodiscard]] int pollFd() const noexcept override;
  [[nodiscard]] short pollEvents() const noexcept override;
  [[nodiscard]] int pollTimeoutMs() const noexcept override;
  void dispatchPoll(short revents) override;
  void handleEvent(std::string_view key, const nlohmann::json& value) override;
  void handleStreamReset() override;
  void apply(std::vector<Workspace>& workspaces, const std::string& outputName = {}) const override;
  [[nodiscard]] std::vector<std::string> workspaceKeys(const std::string& outputName = {}) const override;
  [[nodiscard]] std::unordered_map<std::string, std::vector<std::string>>
  appIdsByWorkspace(const std::string& outputName = {}) const override;
  [[nodiscard]] std::vector<WorkspaceWindow> workspaceWindows(const std::string& outputName = {}) const override;
  [[nodiscard]] bool hasExactWindowIdentity() const noexcept override { return true; }
  [[nodiscard]] std::optional<std::string> focusedWindowId() const override;
  bool focusWindowById(const std::string& windowId, bool warpPointer = false) override;
  bool closeWindowById(const std::string& windowId) override;
  [[nodiscard]] std::vector<CompositorWindowAction> windowActions(const std::string& windowId) const override;
  bool runWindowAction(const std::string& windowId, std::string_view actionId) override;
  void cleanup() override;

private:
  struct WindowState {
    std::optional<std::uint64_t> workspaceId;
    std::string appId;
    std::string title;
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool urgent = false;
    bool floating = false;
    bool sticky = false;
    bool mirror = false;
    bool mirrorLinked = false;
    bool blockOut = false;
    // niri reports no workspace for sticky windows. They show on the active workspace of the
    // output they were last seen on: the focused output while focused, else the output of the
    // workspace they were stuck from.
    std::string stickyOutput;
    std::optional<std::uint64_t> stickyAnchorWorkspaceId;

    bool operator==(const WindowState&) const = default;
  };

  struct WorkspaceState {
    std::uint64_t id = 0;
    std::uint8_t idx = 0;
    std::string name;
    std::string output;
    bool active = false;
    bool focused = false;

    bool operator==(const WorkspaceState&) const = default;
  };

  struct CastState {
    std::uint64_t sessionId = 0;
    std::string output;
    std::optional<std::uint64_t> workspaceId;
    std::optional<std::uint64_t> windowId;
    bool dynamic = false;
    bool active = false;

    bool operator==(const CastState&) const = default;
  };

  [[nodiscard]] bool handleWorkspacesChanged(const nlohmann::json& payload);
  [[nodiscard]] bool handleWindowsChanged(const nlohmann::json& payload);
  [[nodiscard]] bool handleOverviewChanged(const nlohmann::json& payload);
  [[nodiscard]] bool handleWindowOpenedOrChanged(const nlohmann::json& payload);
  [[nodiscard]] bool handleWindowLayoutsChanged(const nlohmann::json& payload);
  [[nodiscard]] bool handleWindowClosed(const nlohmann::json& payload);
  [[nodiscard]] bool handleWindowFocusChanged(const nlohmann::json& payload);
  [[nodiscard]] bool handleWorkspaceActivated(const nlohmann::json& payload);
  [[nodiscard]] bool handleWindowUrgencyChanged(const nlohmann::json& payload);
  [[nodiscard]] bool handleCastsChanged(const nlohmann::json& payload);
  [[nodiscard]] bool handleCastChanged(const nlohmann::json& payload);
  [[nodiscard]] bool handleCastStopped(const nlohmann::json& payload);
  [[nodiscard]] bool handleBlockOutChanged(const nlohmann::json& payload);
  [[nodiscard]] bool updateFocusedWindowIdFromWindowsJson(const nlohmann::json& windows);
  [[nodiscard]] static std::optional<std::pair<std::uint64_t, CastState>> parseCast(const nlohmann::json& json);
  [[nodiscard]] static std::optional<WorkspaceState> parseWorkspace(const nlohmann::json& json);
  [[nodiscard]] static std::optional<std::pair<std::uint64_t, WindowState>> parseWindow(const nlohmann::json& json);
  [[nodiscard]] static bool applyWindowFields(const nlohmann::json& json, WindowState& state);
  [[nodiscard]] static bool sameWindowMembership(const WindowState& lhs, const WindowState& rhs) noexcept;
  [[nodiscard]] static bool sameWindowMembership(
      const std::unordered_map<std::uint64_t, WindowState>& lhs,
      const std::unordered_map<std::uint64_t, WindowState>& rhs
  ) noexcept;
  [[nodiscard]] static bool sameWindowLayout(const WindowState& lhs, const WindowState& rhs) noexcept;
  [[nodiscard]] const WorkspaceState* focusedWorkspace() const;
  [[nodiscard]] const WorkspaceState* activeWorkspaceForOutput(const std::string& outputName) const;
  [[nodiscard]] std::string windowOutput(const WindowState& window) const;
  // The workspace a window is listed under: its own, or for sticky windows the active one of its output.
  [[nodiscard]] std::optional<std::uint64_t> displayWorkspaceId(const WindowState& window) const;
  [[nodiscard]] WindowAttributes windowAttributes(std::uint64_t windowId, const WindowState& window) const;
  [[nodiscard]] bool isWorkspaceCast(std::uint64_t workspaceId) const;
  [[nodiscard]] bool isOutputCast(const std::string& outputName) const;
  [[nodiscard]] bool isWindowCast(std::uint64_t windowId) const;
  [[nodiscard]] bool resolveStickyOutputs();
  [[nodiscard]] static std::optional<std::uint64_t> parseUnsigned(const std::string& value);
  [[nodiscard]] static std::optional<std::size_t> parseLeadingNumber(const std::string& value);
  [[nodiscard]] static std::string workspaceKey(const WorkspaceState& workspace);
  [[nodiscard]] std::vector<const WorkspaceState*>
  sortedWorkspaceCandidatesForOutput(const std::string& outputName) const;
  void recomputeOccupancy();
  void notifyChanged() const;
  void notifyOverviewChanged() const;

  std::unordered_map<std::uint64_t, WindowState> m_windows;
  std::unordered_map<std::uint64_t, std::size_t> m_occupancy;
  std::unordered_map<std::uint64_t, WorkspaceState> m_workspaces;
  std::optional<std::uint64_t> m_focusedWindowId;
  std::unordered_map<std::uint64_t, CastState> m_casts;
  bool m_blockOutEnabled = false;
  // Set once window JSON carries the fork-only fields (is_sticky, is_mirror, ...).
  bool m_extendedWindowState = false;
  bool m_overviewKnown = false;
  bool m_overviewOpen = false;
  ChangeCallback m_changeCallback;
  ChangeCallback m_overviewChangeCallback;
};
