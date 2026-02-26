async function loadFrontendConfig() {
  try {
    const resp = await fetch(`${API_BASE_URL}/config`);
    if (resp && resp.ok) {
      const data = await resp.json();
      const v = Number(data && data.polling_interval_ms);
      if (isFinite(v) && v > 0) {
        window.FRONTEND_POLL_INTERVAL_MS = v;
      }
      const off = 0.0;
      if (isFinite(off)) {
        window.CENTER_FORWARD_OFFSET_M = Math.max(0, Math.min(5, off));
      }
    }
  } catch (_) {}
}

async function loadSimSettingsCache() {
  try {
    const resp = await fetch(`${API_BASE_URL}/sim/settings`);
    if (resp && resp.ok) {
      const data = await resp.json();
      window.SIM_SETTINGS_CACHE = data;
      const ts = Number(data && data.sim_time_scale);
      if (isFinite(ts) && ts > 0) window.SIM_TIME_SCALE = ts;
    }
  } catch (_) {}
}

function normalizeViewerMapId(raw) {
  let s = String(raw || '').replace(/\\/g, '/').replace(/^\/+/, '').trim();
  if (!s) return '';
  const mVehicle = /^VehicleMap\/([^\/]+\.scene)$/i.exec(s);
  if (mVehicle) return 'ViewerMap/' + mVehicle[1];
  if (/^ViewerMap\/[^\/]+\.scene$/i.test(s)) return s;
  if (/^[^\/]+\.scene$/i.test(s)) return 'ViewerMap/' + s;
  if (s.includes('/')) {
    const last = s.split('/').pop();
    if (last && /\.scene$/i.test(last)) return 'ViewerMap/' + last;
  }
  return 'ViewerMap/' + s + '.scene';
}

function canonicalizeMapId(raw) {
  let s = String(raw || '').replace(/\\/g, '/').replace(/^\/+/, '').trim();
  if (!s) return '';
  if (s.includes('/')) s = s.split('/').pop() || s;
  s = s.replace(/\.scene$/i, '');
  return s;
}

function parseConfigInitFromYamlText(text) {
  try {
    const lines = String(text || '').split(/\r?\n/);
    let inPose = false;
    let poseIndent = 0;
    let inSim = false;
    let simIndent = 0;
    const out = {};

    for (const rawLine of lines) {
      if (!rawLine.trim() || rawLine.trim().startsWith('#')) continue;
      const indent = rawLine.length - rawLine.replace(/^ */, '').length;
      const line = rawLine.trim();

      if (!inPose) {
        const mPose = /^initial_pose\s*:\s*$/.exec(line);
        if (mPose) {
          inPose = true;
          poseIndent = indent;
          continue;
        }
      } else {
        if (indent <= poseIndent) {
          inPose = false;
        } else {
          const m = /^(pose_x|pose_y|pose_theta)\s*:\s*(.+?)\s*$/.exec(line);
          if (m) {
            const key = String(m[1] || '').trim();
            let val = String(m[2] || '').trim();
            if ((val.startsWith('"') && val.endsWith('"')) || (val.startsWith("'") && val.endsWith("'"))) {
              val = val.slice(1, -1);
            }
            out[key] = val;
            continue;
          }
        }
      }

      if (!inSim) {
        const mSim = /^sim_config\s*:\s*$/.exec(line);
        if (mSim) {
          inSim = true;
          simIndent = indent;
          continue;
        }
      } else {
        if (indent <= simIndent) {
          inSim = false;
        } else {
          const m = /^map_id\s*:\s*(.+?)\s*$/.exec(line);
          if (m) {
            let val = String(m[1] || '').trim();
            if ((val.startsWith('"') && val.endsWith('"')) || (val.startsWith("'") && val.endsWith("'"))) {
              val = val.slice(1, -1);
            }
            out.map_id = val;
            continue;
          }
        }
      }
    }

    const mapIdRaw = String(out.map_id || '').trim();
    if (!mapIdRaw) return null;
    const mapId = normalizeViewerMapId(mapIdRaw);
    const x = Number(out.pose_x);
    const y = Number(out.pose_y);
    const theta = Number(out.pose_theta);
    const pose = (isFinite(x) && isFinite(y) && isFinite(theta)) ? { x, y, theta } : null;
    return { mapId, pose };
  } catch (_) {
    return null;
  }
}

async function loadConfigInit() {
  try {
    const resp = await fetch('/simagv/config.yaml', { cache: 'no-store' });
    if (!resp.ok) return null;
    const text = await resp.text();
    return parseConfigInitFromYamlText(text);
  } catch (_) {
    return null;
  }
}

async function applyHotConfigInitToFrontend() {
  try {
    const init = await loadConfigInit();
    if (!init || !init.mapId) return;
    window.HOT_SIM_INIT = { mapId: init.mapId, pose: init.pose || { x: 0, y: 0, theta: 0 } };
    CURRENT_MAP_ID = init.mapId;
    try {
      const sel = document.getElementById('viewerMapSelect');
      if (sel) {
        const exists = Array.from(sel.options).some(o => o.value === CURRENT_MAP_ID);
        if (!exists) {
          const opt = document.createElement('option');
          opt.value = CURRENT_MAP_ID;
          opt.textContent = String(CURRENT_MAP_ID).replace(/^ViewerMap\//i, '');
          sel.appendChild(opt);
        }
        sel.value = CURRENT_MAP_ID;
      }
    } catch (_) {}
  } catch (_) {}
}
