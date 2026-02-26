window.onload = async function () {
  try { bindUiEvents(); } catch (_) {}

  try { initCanvas(); } catch (_) {}
  try { setupEventListeners(); } catch (_) {}

  try { await updateViewerMapOptions(); } catch (_) {}
  try { await applyHotConfigInitToFrontend(); } catch (_) {}
  try { loadMapData(); } catch (_) {}

  try { loadFrontendConfig(); } catch (_) {}
  try { loadSimSettingsCache(); } catch (_) {}
  try { loadRobotList(); } catch (_) {}
  try { loadEquipmentList(); } catch (_) {}

  try { startPerfMonitor(); } catch (_) {}
  try { initWebSocket(); } catch (_) {}
  try { startRenderLoop(); } catch (_) {}
  try { setupKeyboardControl(); } catch (_) {}
  try { startCommStatusGuard(); } catch (_) {}

  try { switchTab('list'); } catch (_) {}
};
