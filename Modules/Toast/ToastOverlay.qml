import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Wayland
import qs.Commons
import qs.Services.Compositor
import qs.Widgets

Variants {
  model: {
    const renderable = CompositorService.renderableScreens(Quickshell.screens);
    const screens = renderable.filter(screen => Settings.data.notifications.monitors.includes(screen.name));
    // Empty list can mean two things :
    // - No (visible) notification display activated in settings
    // - One or more (not visible) displays are activated but unplugged
    // In both cases we fallback to show notification on all screens
    return screens.length === 0 ? renderable : screens;
  }

  delegate: ToastScreen {
    required property ShellScreen modelData
    screen: modelData
  }
}
