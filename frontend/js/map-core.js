// Map core functions - loading, view transform, zoom/pan

async function loadFrontendConfig() { 
  try { 
    const resp = await fetch(`${API_BASE_URL}/config`); 
    if (resp && resp.ok) { 
      const data = await resp.json(); 
      const v = Number(data && data.polling_interval_ms); 
      if (isFinite(v) && v > 0) { window.FRONTEND_POLL_INTERVAL_MS = v; } 
      const off = 0.0; 
      if (isFinite(off)) { window.CENTER_FORWARD_OFFSET_M = Math.max(0, Math.min(5, off)); } 
    } 
  } catch (_) { } 
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
  } catch (_) { } 
}

function parseConfigInitFromYamlText(text) { 
  try { 
    const lines = String(text || '').split(/\r?\n/); 
    let inPose = false; let poseIndent = 0; 
    let inSim = false; let simIndent = 0; 
    const out = {}; 
    for (const rawLine of lines) { 
      if (!rawLine.trim() || rawLine.trim().startsWith('#')) continue; 
      const indent = rawLine.length - rawLine.replace(/^ */, '').length; 
      const line = rawLine.trim(); 
      if (!inPose) { const mPose = /^initial_pose\s*:\s*$/.exec(line); if (mPose) { inPose = true; poseIndent = indent; continue; } } 
      else { 
        if (indent <= poseIndent) { inPose = false; } 
        else { 
          const m = /^(pose_x|pose_y|pose_theta)\s*:\s*(.+?)\s*$/.exec(line); 
          if (m) { 
            const key = String(m[1] || '').trim(); 
            let val = String(m[2] || '').trim(); 
            if ((val.startsWith('"') && val.endsWith('"')) || (val.startsWith("'") && val.endsWith("'"))) { val = val.slice(1, -1); } 
            out[key] = val; continue; 
          } 
        } 
      } 
      if (!inSim) { const mSim = /^sim_config\s*:\s*$/.exec(line); if (mSim) { inSim = true; simIndent = indent; continue; } } 
      else { 
        if (indent <= simIndent) { inSim = false; } 
        else { 
          const m = /^map_id\s*:\s*(.+?)\s*$/.exec(line); 
          if (m) { 
            let val = String(m[1] || '').trim(); 
            if ((val.startsWith('"') && val.endsWith('"')) || (val.startsWith("'") && val.endsWith("'"))) { val = val.slice(1, -1); } 
            out.map_id = val; continue; 
          } 
        } 
      } 
    } 
    const mapIdRaw = String(out.map_id || '').trim(); 
    if (!mapIdRaw) return null; 
    const mapId = normalizeViewerMapId(mapIdRaw); 
    const x = Number(out.pose_x); const y = Number(out.pose_y); const theta = Number(out.pose_theta); 
    const pose = (isFinite(x) && isFinite(y) && isFinite(theta)) ? { x, y, theta } : null; 
    return { mapId, pose }; 
  } catch (_) { return null; } 
}

async function loadConfigInit() { 
  try { 
    const resp = await fetch('/simagv/config.yaml', { cache: 'no-store' }); 
    if (!resp.ok) return null; 
    const text = await resp.text(); 
    return parseConfigInitFromYamlText(text); 
  } catch (_) { return null; } 
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
        if (!exists) { const opt = document.createElement('option'); opt.value = CURRENT_MAP_ID; opt.textContent = String(CURRENT_MAP_ID).replace(/^ViewerMap\//i, ''); sel.appendChild(opt); } 
        sel.value = CURRENT_MAP_ID; 
      } 
    } catch (_) { } 
  } catch (_) { } 
}

function initCanvas() { 
  canvas = document.getElementById('mapCanvas'); 
  ctx = canvas.getContext('2d'); 
  resizeCanvas(); 
  window.addEventListener('resize', resizeCanvas); 
}

function resizeCanvas() { 
  const c = document.querySelector('.canvas-container'); 
  canvas.width = c.clientWidth; 
  canvas.height = c.clientHeight; 
  if (mapData) { drawMap(); } 
}

function getMousePos(e) { 
  const r = canvas.getBoundingClientRect(); 
  return { x: e.clientX - r.left, y: e.clientY - r.top }; 
}

function screenToWorld(sx, sy) { 
  return { x: (sx - viewTransform.x) / viewTransform.scale / 20, y: -((sy - viewTransform.y) / viewTransform.scale / 20) }; 
}

function worldToScreen(wx, wy) { 
  return { x: (wx * 20) * viewTransform.scale + viewTransform.x, y: (-wy * 20) * viewTransform.scale + viewTransform.y }; 
}

function pointDrawSize() { 
  const base = 3; 
  const s = Math.max(4, Math.min(24, base * viewTransform.scale)); 
  return s; 
}

async function loadMapData() { 
  try { 
    if (!CURRENT_MAP_ID) { 
      const el = document.getElementById('loading'); 
      if (el) el.style.display = 'none'; 
      const st = document.getElementById('status'); 
      if (st) st.textContent = '请先选择地图'; 
      return; 
    } 
    document.getElementById('loading').style.display = 'block'; 
    document.getElementById('status').textContent = '正在加载地图数据...'; 
    const resp = await fetch('/maps/' + CURRENT_MAP_ID); 
    if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`); 
    const raw = await resp.json(); 
    mapLayers = Array.isArray(raw) ? raw : (raw ? [raw] : []); 
    buildBinLocationIndex(mapLayers); 
    floorNames = mapLayers.map(extractFloorNameFromLayer); 
    if (!Array.isArray(floorNames) || floorNames.filter(v => !!v).length === 0) { 
      const root = mapLayers.length > 0 ? mapLayers[0] : null; 
      const unique = new Set(); 
      const pts = (root && Array.isArray(root.points)) ? root.points : []; 
      pts.forEach(p => { const nm = extractFloorNameFromLayer(p.layer || ''); if (nm) unique.add(nm); }); 
      if (unique.size > 0) { floorNames = Array.from(unique); } 
    } 
    currentFloorIndex = 0; 
    mapData = mapLayers.length > 0 ? normalizeScene(mapLayers[currentFloorIndex]) : null; 
    await loadSceneStations(); 
    populateFloorSelect(); 
    updateMapInfo(); 
    fitMapToView(); 
    drawMap(); 
    document.getElementById('status').textContent = `地图加载完成: ${CURRENT_MAP_ID}`; 
  } catch (err) { 
    console.error('加载地图数据失败:', err); 
    document.getElementById('status').textContent = '加载失败: ' + err.message; 
  } finally { 
    document.getElementById('loading').style.display = 'none'; 
  } 
}

async function updateViewerMapOptions() { 
  const sel = document.getElementById('viewerMapSelect'); 
  if (!sel) return; 
  sel.innerHTML = '<option value="">请选择地图</option>'; 
  const normalizeMapId = (raw) => { 
    let s = String(raw || '').replace(/\\/g, '/').replace(/^\/+/, '').trim(); 
    if (!s) return ''; 
    if (/^(VehicleMap|ViewerMap)\/[^\/]+\.scene$/i.test(s)) return s; 
    if (/^[^\/]+\.scene$/i.test(s)) return 'ViewerMap/' + s; 
    const last = s.split('/').pop(); 
    if (last && /\.scene$/i.test(last)) return 'ViewerMap/' + last; 
    return s; 
  }; 
  const displayName = (mapId) => { 
    let s = String(mapId || '').replace(/\\/g, '/').replace(/^\/+/, '').trim(); 
    s = s.replace(/^ViewerMap\//i, ''); 
    const last = s.split('/').pop(); 
    return last || s || '-'; 
  }; 
  try { 
    const resp = await fetch(`${API_BASE_URL}/maps/ViewerMap`); 
    if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`); 
    const files = await resp.json(); 
    (files || []).forEach(name => { 
      const mapId = normalizeMapId(name); 
      if (!mapId) return; 
      const opt = document.createElement('option'); 
      opt.value = mapId; 
      opt.textContent = displayName(mapId); 
      sel.appendChild(opt); 
    }); 
    if (CURRENT_MAP_ID) { 
      const exists = Array.from(sel.options).some(o => o.value === CURRENT_MAP_ID); 
      if (exists) sel.value = CURRENT_MAP_ID; 
    } 
  } catch (err) { 
    const opt = document.createElement('option'); 
    opt.value = ''; 
    opt.textContent = '加载失败'; 
    sel.appendChild(opt); 
  } 
}

function applyViewerMap() { 
  const sel = document.getElementById('viewerMapSelect'); 
  if (!sel) return; 
  const v = sel.value; 
  if (!v) return; 
  if (CURRENT_MAP_ID !== v) { CURRENT_MAP_ID = v; } 
  currentFloorIndex = 0; 
  loadMapData(); 
}

function normalizeScene(scene) { 
  if (!scene) return null; 
  return JSON.parse(JSON.stringify(scene)); 
}

function populateFloorSelect() { 
  const sel = document.getElementById('floorSelect'); 
  if (!sel) return; 
  sel.innerHTML = ''; 
  const availableFloorCount = (Array.isArray(floorNames) && floorNames.length > 0) ? floorNames.length : mapLayers.length; 
  if (!Array.isArray(mapLayers) || mapLayers.length === 0 || availableFloorCount === 0) { 
    const opt = document.createElement('option'); 
    opt.value = ''; 
    opt.textContent = '无楼层'; 
    sel.appendChild(opt); 
    sel.disabled = true; 
    return; 
  } 
  sel.disabled = false; 
  for (let i = 0; i < availableFloorCount; i++) { 
    const opt = document.createElement('option'); 
    opt.value = String(i); 
    const fname = floorNames[i]; 
    opt.textContent = fname ? `${fname}` : `${i + 1}层`; 
    sel.appendChild(opt); 
  } 
  sel.value = String(currentFloorIndex); 
}

function switchFloor(idx) { 
  const i = parseInt(idx, 10); 
  const availableFloorCount = (Array.isArray(floorNames) && floorNames.length > 0) ? floorNames.length : mapLayers.length; 
  if (isNaN(i) || i < 0 || i >= availableFloorCount) return; 
  currentFloorIndex = i; 
  const layerIndex = Math.min(currentFloorIndex, Math.max(0, mapLayers.length - 1)); 
  mapData = normalizeScene(mapLayers[layerIndex]); 
  loadSceneStations(); 
  updateMapInfo(); 
  fitMapToView(); 
  drawMap(); 
  const fname = floorNames[currentFloorIndex]; 
  document.getElementById('status').textContent = fname ? `已切换到楼层: ${fname}` : `已切换到${i + 1}层`; 
}

function cycleFloor() { 
  const availableFloorCount = (Array.isArray(floorNames) && floorNames.length > 0) ? floorNames.length : mapLayers.length; 
  if (availableFloorCount === 0) return; 
  const next = (currentFloorIndex + 1) % availableFloorCount; 
  switchFloor(next); 
  const sel = document.getElementById('floorSelect'); 
  if (sel) sel.value = String(next); 
}

function updateMapInfo() { 
  const pc = mapData?.points?.length || 0; 
  const rc = mapData?.routes?.length || 0; 
  document.getElementById('pointCount').textContent = pc; 
  document.getElementById('routeCount').textContent = rc; 
  updateInitialPositionOptions(); 
}

function updateInitialPositionOptions() { 
  const regMapSel = document.getElementById('registerMapSelect'); 
  const chosen = regMapSel ? regMapSel.value : ''; 
  if (regMapSel && /(^|\/)VehicleMap\/[^\/]+\.scene$/.test(chosen)) { 
    updateRegisterInitialPositionOptions(); 
    return; 
  } 
  const sel = document.getElementById('initialPosition'); 
  if (!sel) return; 
  sel.innerHTML = '<option value="">选择初始位置</option>'; 
  (mapData?.points || []).forEach(p => { 
    if (p.name) { 
      const opt = document.createElement('option'); 
      opt.value = p.id; 
      opt.textContent = `${p.name} (${p.x.toFixed(2)}, ${p.y.toFixed(2)})`; 
      sel.appendChild(opt); 
    } 
  }); 
  sel.disabled = false; 
}

function fitMapToView() { 
  if (!mapData || !mapData.points || mapData.points.length === 0) return; 
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity; 
  mapData.points.forEach(p => { 
    const sx = p.x * 20, sy = p.y * 20; 
    minX = Math.min(minX, sx); minY = Math.min(minY, sy); 
    maxX = Math.max(maxX, sx); maxY = Math.max(maxY, sy); 
  }); 
  const mapW = maxX - minX, mapH = maxY - minY, cx = (minX + maxX) / 2, cy = (minY + maxY) / 2; 
  const pad = 50; 
  const safeW = Math.max(1e-6, mapW); 
  const safeH = Math.max(1e-6, mapH); 
  const sX = (canvas.width - pad * 2) / safeW; 
  const sY = (canvas.height - pad * 2) / safeH; 
  viewTransform.scale = Math.max(0.1, Math.min(5, Math.min(sX, sY))); 
  viewTransform.x = canvas.width / 2 - cx * viewTransform.scale; 
  viewTransform.y = canvas.height / 2 + cy * viewTransform.scale; 
  updateZoomDisplay(); 
}

function updateZoomDisplay() { 
  document.getElementById('zoomLevel').textContent = Math.round(viewTransform.scale * 100) + '%'; 
}

async function loadSceneStations() { }
async function updateRegisterMapOptions() { }
async function updateRegisterInitialPositionOptions() { }
