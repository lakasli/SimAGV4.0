function bindUiEvents() {
  const viewerMapApplyBtn = document.getElementById('viewerMapApplyBtn');
  if (viewerMapApplyBtn) viewerMapApplyBtn.addEventListener('click', () => applyViewerMap());

  const floorSelect = document.getElementById('floorSelect');
  if (floorSelect) floorSelect.addEventListener('change', (e) => switchFloor(e.target.value));

  const cycleFloorBtn = document.getElementById('cycleFloorBtn');
  if (cycleFloorBtn) cycleFloorBtn.addEventListener('click', () => cycleFloor());

  const errorInjectBtn = document.getElementById('openErrorInjectBtn');
  if (errorInjectBtn) errorInjectBtn.addEventListener('click', () => openErrorInjectModal());

  const perfBtn = document.getElementById('openPerfBtn');
  if (perfBtn) perfBtn.addEventListener('click', () => openPerfModal());

  const settingsBtn = document.getElementById('openSettingsBtn');
  if (settingsBtn) settingsBtn.addEventListener('click', () => openSettingsModal());

  const sidebarToggle = document.getElementById('sidebarToggle');
  if (sidebarToggle) sidebarToggle.addEventListener('click', () => toggleSidebar());

  document.querySelectorAll('.tab-button[data-tab]').forEach(btn => {
    btn.addEventListener('click', () => switchTab(btn.dataset.tab));
  });

  const openRegisterBtn = document.getElementById('openRegisterBtn');
  if (openRegisterBtn) openRegisterBtn.addEventListener('click', () => openRegisterModal());

  document.querySelectorAll('[data-close="register"]').forEach(btn => {
    btn.addEventListener('click', () => closeRegisterModal());
  });

  const submitRegisterBtn = document.getElementById('submitRegisterBtn');
  if (submitRegisterBtn) submitRegisterBtn.addEventListener('click', () => registerRobot());

  document.querySelectorAll('[data-close="perf"]').forEach(btn => {
    btn.addEventListener('click', () => closePerfModal());
  });

  document.querySelectorAll('[data-close="settings"]').forEach(btn => {
    btn.addEventListener('click', () => closeSettingsModal());
  });

  const saveSettingsBtn = document.getElementById('saveSettingsBtn');
  if (saveSettingsBtn) saveSettingsBtn.addEventListener('click', () => saveSimSettings());

  document.querySelectorAll('[data-close="error-inject"]').forEach(btn => {
    btn.addEventListener('click', () => closeErrorInjectModal());
  });

  document.querySelectorAll('[data-close="config"]').forEach(btn => {
    btn.addEventListener('click', () => closeConfigModal());
  });

  const configTabBtnBasic = document.getElementById('configTabBtnBasic');
  if (configTabBtnBasic) configTabBtnBasic.addEventListener('click', () => switchConfigTab('basic'));
  const configTabBtnPhysical = document.getElementById('configTabBtnPhysical');
  if (configTabBtnPhysical) configTabBtnPhysical.addEventListener('click', () => switchConfigTab('physical'));

  const configMapSelect = document.getElementById('configMapSelect');
  if (configMapSelect) configMapSelect.addEventListener('change', () => updateConfigInitialPositionOptions());

  const configModal = document.getElementById('configModal');
  if (configModal) {
    const saveBtn = configModal.querySelector('.btn-saveconfig');
    if (saveBtn) saveBtn.addEventListener('click', () => saveRobotConfig());
  }

  const robotListContainer = document.getElementById('robotListContainer');
  if (robotListContainer) {
    robotListContainer.addEventListener('click', (ev) => {
      const t = ev.target;
      if (!(t instanceof HTMLElement)) return;
      const actionEl = t.closest('[data-action]');
      if (!(actionEl instanceof HTMLElement)) return;
      const action = actionEl.dataset.action;
      const id = actionEl.dataset.robotId;
      if (action === 'select-robot') {
        selectRobot(id);
      } else if (action === 'remove-robot') {
        if (!id) return;
        removeRobot(id);
      } else if (action === 'open-config') {
        if (!id) return;
        openRobotConfig(id);
      } else if (action === 'toggle-control') {
        if (!id) return;
        toggleAgvControl(id);
      } else if (action === 'toggle-pm2') {
        if (!id) return;
        toggleAgvPm2(id);
      }
    });
  }
}
