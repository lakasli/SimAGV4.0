function openRegisterModal() {
  const modal = document.getElementById('registerModal');
  if (!modal) return;
  modal.style.display = 'flex';
}

function closeRegisterModal() {
  const modal = document.getElementById('registerModal');
  if (!modal) return;
  modal.style.display = 'none';
}

async function registerRobot() {
  const name = String(document.getElementById('robotName')?.value || '').trim();
  const type = String(document.getElementById('robotType')?.value || 'AGV');
  const ip = String(document.getElementById('robotIP')?.value || '').trim();
  const manu = String(document.getElementById('robotManufacturer')?.value || '').trim() || 'SEER';
  const ver = String(document.getElementById('robotVersion')?.value || '').trim() || 'v2';
  if (!name) {
    try { window.printErrorToStatus('请输入机器人名称', '注册'); } catch (_) {}
    return;
  }
  if (!ip) {
    try { window.printErrorToStatus('请输入机器人IP地址', '注册'); } catch (_) {}
    return;
  }
  const ipRegex = /^(\d{1,3}\.){3}\d{1,3}$/;
  if (!ipRegex.test(ip)) {
    try { window.printErrorToStatus('请输入有效的IP地址格式', '注册'); } catch (_) {}
    return;
  }
  const payload = [{ serial_number: name, manufacturer: manu, type: type, vda_version: ver, IP: ip }];
  try {
    const resp = await fetch(`${API_BASE_URL}/agvs/register`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`);
    await resp.json();
    await loadRobotList();
    const st = document.getElementById('status');
    if (st) st.textContent = `机器人 ${name} 注册成功`;
    setTimeout(() => {
      const s2 = document.getElementById('status');
      if (s2 && s2.textContent === `机器人 ${name} 注册成功`) s2.textContent = '准备就绪';
    }, 2000);
    closeRegisterModal();
  } catch (err) {
    try { window.printErrorToStatus('注册失败: ' + (err && err.message ? err.message : String(err)), '注册'); } catch (_) {}
  }
}

function openSettingsModal() {
  const modal = document.getElementById('settingsModal');
  if (!modal) return;
  modal.style.display = 'flex';
}

function closeSettingsModal() {
  const modal = document.getElementById('settingsModal');
  if (!modal) return;
  modal.style.display = 'none';
}

async function saveSimSettings() {
  try {
    const patch = {};
    const ts = Number(document.getElementById('settingsTimeScale')?.value);
    const sf = Number(document.getElementById('settingsStateFreq')?.value);
    const vf = Number(document.getElementById('settingsVisFreq')?.value);
    const at = Number(document.getElementById('settingsActionTime')?.value);
    const fp = Number(document.getElementById('settingsFrontendPoll')?.value);

    if (isFinite(ts)) patch.sim_time_scale = ts;
    if (Number.isInteger(sf)) patch.state_frequency = sf;
    if (Number.isInteger(vf)) patch.visualization_frequency = vf;
    if (isFinite(at)) patch.action_time = at;
    if (Number.isInteger(fp)) patch.frontend_poll_interval_ms = fp;

    const resp = await fetch(`${API_BASE_URL}/sim/settings`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(patch)
    });
    if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`);
    if (patch.frontend_poll_interval_ms) window.FRONTEND_POLL_INTERVAL_MS = patch.frontend_poll_interval_ms;
    if (patch.sim_time_scale) window.SIM_TIME_SCALE = patch.sim_time_scale;
    closeSettingsModal();
  } catch (err) {
    try { window.printErrorToStatus('保存失败: ' + (err && err.message ? err.message : String(err)), '设置'); } catch (_) {}
  }
}

function openErrorInjectModal() {
  const modal = document.getElementById('errorInjectModal');
  if (!modal) return;
  modal.style.display = 'flex';
}

function closeErrorInjectModal() {
  const modal = document.getElementById('errorInjectModal');
  if (!modal) return;
  modal.style.display = 'none';
}

function switchConfigTab(tab) {
  const t = String(tab || '').toLowerCase();
  const basic = document.getElementById('configTabBasic');
  const physical = document.getElementById('configTabPhysical');
  const btnBasic = document.getElementById('configTabBtnBasic');
  const btnPhysical = document.getElementById('configTabBtnPhysical');
  if (!basic || !physical || !btnBasic || !btnPhysical) return;
  if (t === 'physical') {
    basic.style.display = 'none';
    physical.style.display = 'block';
    btnBasic.style.background = '#2b3b4b';
    btnPhysical.style.background = '#34495e';
  } else {
    basic.style.display = 'block';
    physical.style.display = 'none';
    btnBasic.style.background = '#34495e';
    btnPhysical.style.background = '#2b3b4b';
  }
}

function closeConfigModal() {
  const modal = document.getElementById('configModal');
  if (!modal) return;
  modal.style.display = 'none';
  window.currentConfigRobotId = null;
}

async function updateConfigMapOptions() {
  const sel = document.getElementById('configMapSelect');
  if (!sel) return;
  sel.innerHTML = '<option value="">不变</option>';
  const normalizeMapId = (raw) => {
    let s = String(raw || '').replace(/\\/g, '/').replace(/^\/+/, '').trim();
    if (!s) return '';
    if (/^(VehicleMap|ViewerMap)\/[^\/]+\.scene$/i.test(s)) return normalizeViewerMapId(s);
    if (/^[^\/]+\.scene$/i.test(s)) return 'ViewerMap/' + s;
    if (s.includes('/')) {
      const last = s.split('/').pop();
      if (last && /\.scene$/i.test(last)) return 'ViewerMap/' + last;
    }
    if (/^[^\/]+$/i.test(s)) return 'ViewerMap/' + s + '.scene';
    return normalizeViewerMapId(s);
  };
  const displayName = (mapId) => {
    const s = String(mapId || '').replace(/\\/g, '/').replace(/^\/+/, '').trim();
    const base = s.replace(/^ViewerMap\//i, '');
    return base || s || '-';
  };
  const seen = new Set();
  const addOpt = (mapId) => {
    const id = String(mapId || '').trim();
    if (!id || seen.has(id)) return;
    seen.add(id);
    const opt = document.createElement('option');
    opt.value = id;
    opt.textContent = displayName(id);
    sel.appendChild(opt);
  };
  try {
    const resp = await fetch(`${API_BASE_URL}/maps/ViewerMap`);
    if (resp && resp.ok) {
      const files = await resp.json();
      (files || []).forEach(name => addOpt(normalizeMapId(name)));
    }
  } catch (_) {}
  try {
    const robot = registeredRobots.find(r => r.robot_name === window.currentConfigRobotId);
    if (robot && robot.currentMap) addOpt(normalizeMapId(robot.currentMap));
  } catch (_) {}
  try {
    if (CURRENT_MAP_ID) addOpt(normalizeMapId(CURRENT_MAP_ID));
  } catch (_) {}
}

async function updateConfigInitialPositionOptions() {
  const sel = document.getElementById('configInitialPosition');
  const selMap = document.getElementById('configMapSelect');
  if (!sel || !selMap) return;
  const chosen = String(selMap.value || '').trim();
  const robot = registeredRobots.find(r => r.robot_name === window.currentConfigRobotId);
  const fallbackMap = robot?.currentMap ? normalizeViewerMapId(robot.currentMap) : (CURRENT_MAP_ID ? normalizeViewerMapId(CURRENT_MAP_ID) : '');
  const activeMap = chosen ? normalizeViewerMapId(chosen) : fallbackMap;
  sel.innerHTML = '';
  const placeholder = document.createElement('option');
  placeholder.value = '';
  placeholder.textContent = activeMap ? '站点加载中...' : '请先选择地图';
  sel.appendChild(placeholder);
  sel.disabled = true;
  if (!activeMap) return;
  window.configSceneStationsCache = window.configSceneStationsCache || {};
  let stations = window.configSceneStationsCache[activeMap];
  if (!stations) {
    try { stations = await getSceneStationsForMap(activeMap); } catch (_) { stations = []; }
    window.configSceneStationsCache[activeMap] = stations;
  }
  sel.innerHTML = '<option value="">不变</option>';
  (stations || []).forEach(s => {
    const id = String(s.instanceName || s.pointName || s.id || '').trim();
    const name = String(s.instanceName || s.pointName || s.id || '').trim();
    const px = (s && s.pos && typeof s.pos.x !== 'undefined') ? Number(s.pos.x) : Number(s.x);
    const py = (s && s.pos && typeof s.pos.y !== 'undefined') ? Number(s.pos.y) : Number(s.y);
    if (!id) return;
    const opt = document.createElement('option');
    opt.value = id;
    opt.textContent = `${name} (${isFinite(px) ? px.toFixed(2) : 'NaN'}, ${isFinite(py) ? py.toFixed(2) : 'NaN'})`;
    sel.appendChild(opt);
  });
  sel.disabled = false;
}

async function openRobotConfig(id) {
  const rid = String(id || '').trim();
  if (!rid) return;
  window.currentConfigRobotId = rid;
  const robot = registeredRobots.find(r => r.robot_name === rid);
  if (!robot) return;

  const nameEl = document.getElementById('configRobotName');
  if (nameEl) nameEl.value = robot.robot_name || '';
  const t = robot.type || 'AGV';
  const typeEl = document.getElementById('configRobotType');
  if (typeEl) typeEl.value = (t === 'Forklift' ? 'Fork' : (t === 'AMR' ? 'Load' : t));
  const ipEl = document.getElementById('configRobotIP');
  if (ipEl) ipEl.value = robot.ip || '';
  const manuEl = document.getElementById('configRobotManufacturer');
  if (manuEl) manuEl.value = robot.manufacturer || '';
  const verEl = document.getElementById('configRobotVersion');
  if (verEl) verEl.value = robot.vda_version || 'v2';

  await updateConfigMapOptions();
  const mapLabel = document.getElementById('configMapSelectedLabel');
  if (mapLabel) mapLabel.textContent = robot.currentMap || '未设置';
  const selMap = document.getElementById('configMapSelect');
  if (selMap) selMap.value = '';

  await updateConfigInitialPositionOptions();

  const pos = robot.currentPosition || robot.initialPosition || { x: 0, y: 0, theta: 0 };
  const battEl = document.getElementById('configBattery');
  if (battEl) battEl.value = String(robot.battery ?? 100);
  const oriEl = document.getElementById('configOrientation');
  if (oriEl) oriEl.value = String(pos.theta ?? 0);

  switchConfigTab('basic');
  const modal = document.getElementById('configModal');
  if (modal) modal.style.display = 'flex';
}

async function saveRobotConfig() {
  const id = String(window.currentConfigRobotId || '').trim();
  if (!id) return;
  const robot = registeredRobots.find(r => r.robot_name === id);
  if (!robot) return;

  const ip = String(document.getElementById('configRobotIP')?.value || '').trim();
  const ver = String(document.getElementById('configRobotVersion')?.value || '').trim();
  const battery = Number(document.getElementById('configBattery')?.value);
  const selMap = String(document.getElementById('configMapSelect')?.value || '').trim();
  const posKey = String(document.getElementById('configInitialPosition')?.value || '').trim();
  const theta = Number(document.getElementById('configOrientation')?.value);

  const patches = [];
  if (ip && ip !== String(robot.ip || '').trim()) patches.push(['IP', ip]);
  if (ver && ver !== String(robot.vda_version || '').trim()) patches.push(['vda_version', ver]);

  try {
    if (patches.length > 0) {
      const body = {};
      for (const [k, v] of patches) body[k] = v;
      const resp = await fetch(`${API_BASE_URL}/agv/${encodeURIComponent(id)}/config/static`, {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
      });
      if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`);
      if (body.IP) robot.ip = body.IP;
      if (body.vda_version) robot.vda_version = body.vda_version;
    }

    const dyn = {};
    if (isFinite(battery)) dyn.battery_level = battery;
    if (selMap) dyn.current_map = selMap;

    if (posKey) {
      const activeMap = selMap ? normalizeViewerMapId(selMap) : (robot.currentMap ? normalizeViewerMapId(robot.currentMap) : (CURRENT_MAP_ID ? normalizeViewerMapId(CURRENT_MAP_ID) : ''));
      const stations = (activeMap && window.configSceneStationsCache) ? window.configSceneStationsCache[activeMap] : null;
      const st = Array.isArray(stations) ? stations.find(s => String(s.instanceName || s.pointName || s.id || '').trim() === posKey) : null;
      const px = st?.pos?.x ?? st?.x;
      const py = st?.pos?.y ?? st?.y;
      if (isFinite(Number(px)) && isFinite(Number(py))) {
        dyn.position = { x: Number(px), y: Number(py), theta: isFinite(theta) ? theta : (robot.currentPosition?.theta ?? 0) };
      }
    } else if (isFinite(theta) && robot.currentPosition && isFinite(Number(robot.currentPosition.x)) && isFinite(Number(robot.currentPosition.y))) {
      dyn.position = { x: Number(robot.currentPosition.x), y: Number(robot.currentPosition.y), theta: theta };
    }

    if (Object.keys(dyn).length > 0) {
      const resp2 = await fetch(`${API_BASE_URL}/agv/${encodeURIComponent(id)}/config/dynamic`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(dyn)
      });
      if (!resp2.ok) throw new Error(`HTTP ${resp2.status}: ${resp2.statusText}`);
    }

    closeConfigModal();
    try { await loadRobotList(); } catch (_) {}
  } catch (err) {
    try { window.printErrorToStatus('保存配置失败: ' + (err && err.message ? err.message : String(err)), '配置'); } catch (_) {}
  }
}
