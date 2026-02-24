// See inline references in index.html; this file contains the main UI logic.
// Keeping the same global API as before to avoid breaking changes.

// 配置：指定只接收来自特定 MQTT Broker 的消息（例如 '192.168.1.100'）
// 默认为 null，表示接收所有来源
window.VDA5050_FILTER_BROKER_IP = '127.0.0.1';
window.VDA5050_FILTER_BROKER_PORT = 1883;

(function () { let _statusRafId = null; function _stopStatusAutoScroll() { try { if (_statusRafId) cancelAnimationFrame(_statusRafId); } catch (e) { } _statusRafId = null; } function startStatusAutoScroll() { const el = document.getElementById('status'); if (!el) return; _stopStatusAutoScroll(); const max = el.scrollWidth - el.clientWidth; if (max <= 0) return; let pos = el.scrollLeft || 0; let dir = 1; const step = 1; function tick() { const node = document.getElementById('status'); if (!node) return; pos += dir * step; if (pos >= max) { pos = max; dir = -1; } else if (pos <= 0) { pos = 0; dir = 1; } node.scrollLeft = pos; _statusRafId = requestAnimationFrame(tick); } _statusRafId = requestAnimationFrame(tick); } function setStatusMessage(msg) { try { const el = document.getElementById('status'); if (el) { el.textContent = String(msg); setTimeout(startStatusAutoScroll, 0); } } catch (e) { } } window.printErrorToStatus = function (err, context) { const msg = (err && err.message) ? err.message : String(err); const prefix = context ? '[' + String(context) + '] ' : ''; setStatusMessage(prefix + msg); }; const originalFetch = window.fetch; window.fetch = async function (input, init) { const url = (typeof input === 'string') ? input : (input && input.url ? input.url : ''); const method = (init && init.method) || (typeof input === 'object' && input && input.method) || 'GET'; try { const res = await originalFetch(input, init); if (!res.ok) { let detail = ''; try { const txt = await res.clone().text(); try { const obj = JSON.parse(txt); detail = obj && (obj.detail || obj.message) ? (obj.detail || obj.message) : txt; } catch (_) { detail = txt; } } catch (_) { detail = res.statusText || '未知错误'; } setStatusMessage(`错误 ${res.status} ${method} ${url}: ${String(detail).trim()}`); } return res; } catch (err) { setStatusMessage(`网络错误 ${method} ${url}: ${err && err.message ? err.message : String(err)}`); throw err; } }; window.addEventListener('unhandledrejection', function (ev) { const reason = ev && ev.reason ? (ev.reason.message || String(ev.reason)) : '未知异常'; setStatusMessage(`未捕获错误: ${reason}`); }); window.addEventListener('error', function (ev) { setStatusMessage(`脚本错误: ${ev.message} @ ${ev.filename}:${ev.lineno}`); }); try { setTimeout(startStatusAutoScroll, 0); } catch (_) { } })();

const API_BASE_URL = window.location.origin + '/api';
window.FRONTEND_POLL_INTERVAL_MS = 1000;
window.CENTER_FORWARD_OFFSET_M = 0.1;
window.SIM_TIME_SCALE = 1.0;
window.SIM_SETTINGS_CACHE = null;
window.HOT_SIM_INIT = null;
let perfPollTimer = null;
async function loadFrontendConfig() { try { const resp = await fetch(`${API_BASE_URL}/config`); if (resp && resp.ok) { const data = await resp.json(); const v = Number(data && data.polling_interval_ms); if (isFinite(v) && v > 0) { window.FRONTEND_POLL_INTERVAL_MS = v; } const off = 0.0; if (isFinite(off)) { window.CENTER_FORWARD_OFFSET_M = Math.max(0, Math.min(5, off)); } } } catch (_) { } }
async function loadSimSettingsCache() { try { const resp = await fetch(`${API_BASE_URL}/sim/settings`); if (resp && resp.ok) { const data = await resp.json(); window.SIM_SETTINGS_CACHE = data; const ts = Number(data && data.sim_time_scale); if (isFinite(ts) && ts > 0) window.SIM_TIME_SCALE = ts; } } catch (_) { } }
function normalizeViewerMapId(raw) { let s = String(raw || '').replace(/\\/g, '/').replace(/^\/+/, '').trim(); if (!s) return ''; const mVehicle = /^VehicleMap\/([^\/]+\.scene)$/i.exec(s); if (mVehicle) return 'ViewerMap/' + mVehicle[1]; if (/^ViewerMap\/[^\/]+\.scene$/i.test(s)) return s; if (/^[^\/]+\.scene$/i.test(s)) return 'ViewerMap/' + s; if (s.includes('/')) { const last = s.split('/').pop(); if (last && /\.scene$/i.test(last)) return 'ViewerMap/' + last; } return 'ViewerMap/' + s + '.scene'; }
function canonicalizeMapId(raw) { let s = String(raw || '').replace(/\\/g, '/').replace(/^\/+/, '').trim(); if (!s) return ''; if (s.includes('/')) s = s.split('/').pop() || s; s = s.replace(/\.scene$/i, ''); return s; }
function parseConfigInitFromYamlText(text) { try { const lines = String(text || '').split(/\r?\n/); let inPose = false; let poseIndent = 0; let inSim = false; let simIndent = 0; const out = {}; for (const rawLine of lines) { if (!rawLine.trim() || rawLine.trim().startsWith('#')) continue; const indent = rawLine.length - rawLine.replace(/^ */, '').length; const line = rawLine.trim(); if (!inPose) { const mPose = /^initial_pose\s*:\s*$/.exec(line); if (mPose) { inPose = true; poseIndent = indent; continue; } } else { if (indent <= poseIndent) { inPose = false; } else { const m = /^(pose_x|pose_y|pose_theta)\s*:\s*(.+?)\s*$/.exec(line); if (m) { const key = String(m[1] || '').trim(); let val = String(m[2] || '').trim(); if ((val.startsWith('"') && val.endsWith('"')) || (val.startsWith("'") && val.endsWith("'"))) { val = val.slice(1, -1); } out[key] = val; continue; } } } if (!inSim) { const mSim = /^sim_config\s*:\s*$/.exec(line); if (mSim) { inSim = true; simIndent = indent; continue; } } else { if (indent <= simIndent) { inSim = false; } else { const m = /^map_id\s*:\s*(.+?)\s*$/.exec(line); if (m) { let val = String(m[1] || '').trim(); if ((val.startsWith('\"') && val.endsWith('\"')) || (val.startsWith("'") && val.endsWith("'"))) { val = val.slice(1, -1); } out.map_id = val; continue; } } } } const mapIdRaw = String(out.map_id || '').trim(); if (!mapIdRaw) return null; const mapId = normalizeViewerMapId(mapIdRaw); const x = Number(out.pose_x); const y = Number(out.pose_y); const theta = Number(out.pose_theta); const pose = (isFinite(x) && isFinite(y) && isFinite(theta)) ? { x, y, theta } : null; return { mapId, pose }; } catch (_) { return null; } }
async function loadConfigInit() { try { const resp = await fetch('/simagv/config.yaml', { cache: 'no-store' }); if (!resp.ok) return null; const text = await resp.text(); return parseConfigInitFromYamlText(text); } catch (_) { return null; } }
async function applyHotConfigInitToFrontend() { try { const init = await loadConfigInit(); if (!init || !init.mapId) return; window.HOT_SIM_INIT = { mapId: init.mapId, pose: init.pose || { x: 0, y: 0, theta: 0 } }; CURRENT_MAP_ID = init.mapId; try { const sel = document.getElementById('viewerMapSelect'); if (sel) { const exists = Array.from(sel.options).some(o => o.value === CURRENT_MAP_ID); if (!exists) { const opt = document.createElement('option'); opt.value = CURRENT_MAP_ID; opt.textContent = String(CURRENT_MAP_ID).replace(/^ViewerMap\//i, ''); sel.appendChild(opt); } sel.value = CURRENT_MAP_ID; } } catch (_) { } } catch (_) { } }
function formatBytes(bytes) { const n = Number(bytes); if (!isFinite(n) || n < 0) return '-'; if (n === 0) return '0 B'; const units = ['B', 'KB', 'MB', 'GB', 'TB']; let v = n; let idx = 0; while (v >= 1024 && idx < units.length - 1) { v /= 1024; idx++; } const digits = (idx <= 1) ? 0 : 1; return `${v.toFixed(digits)} ${units[idx]}`; }
function formatCpuPercent(v) { const n = Number(v); if (!isFinite(n)) return '-'; return `${n.toFixed(1)}%`; }
function formatPercent(v) { const n = Number(v); if (!isFinite(n)) return '-'; return `${n.toFixed(1)}%`; }
function escapeHtml(s) { return String(s ?? '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&#39;'); }
function renderPerfProcessList(containerId, processes) { const el = document.getElementById(containerId); if (!el) return; if (!Array.isArray(processes) || processes.length === 0) { el.innerHTML = '<div class="perf-table-empty">-</div>'; return; } const rows = processes.slice().sort((a, b) => Number(b && b.cpu_percent) - Number(a && a.cpu_percent)).map(p => { const name = escapeHtml(p && p.name ? p.name : '-'); const pid = Number(p && p.pid) || 0; const cpu = formatCpuPercent(p && p.cpu_percent); const memPct = formatPercent(p && p.memory_percent); const mem = formatBytes(p && p.memory_bytes); const title = pid ? `${name} (pid ${pid})` : name; return `<div class="perf-table-row"><div>${title}</div><div class="perf-table-cell-muted">${cpu}</div><div class="perf-table-cell-muted">${memPct}</div><div class="perf-table-cell-muted">${mem}</div></div>`; }).join(''); el.innerHTML = `<div class="perf-table-row perf-table-header"><div>实例</div><div>CPU</div><div>内存%</div><div>内存</div></div>${rows}`; }
async function refreshPerfPanel() { try { const resp = await fetch(`${API_BASE_URL}/perf/summary`); if (!resp.ok) return; const data = await resp.json(); const agv = data && data.simagv ? data.simagv : {}; const equip = data && data.equip ? data.equip : {}; const agvCpuEl = document.getElementById('perfAgvCpu'); const agvMemEl = document.getElementById('perfAgvMem'); const equipCpuEl = document.getElementById('perfEquipCpu'); const equipMemEl = document.getElementById('perfEquipMem'); if (agvCpuEl) agvCpuEl.textContent = formatCpuPercent(agv.cpu_percent); if (agvMemEl) agvMemEl.textContent = formatBytes(agv.memory_bytes); if (equipCpuEl) equipCpuEl.textContent = formatCpuPercent(equip.cpu_percent); if (equipMemEl) equipMemEl.textContent = formatBytes(equip.memory_bytes); renderPerfProcessList('perfAgvList', agv && agv.processes ? agv.processes : []); renderPerfProcessList('perfEquipList', equip && equip.processes ? equip.processes : []); } catch (_) { } }
function startPerfMonitor() { if (perfPollTimer) { try { clearInterval(perfPollTimer); } catch (_) { } perfPollTimer = null; } refreshPerfPanel(); perfPollTimer = setInterval(refreshPerfPanel, 1000); }

let CURRENT_MAP_ID = '';
try {
  const q = new URLSearchParams(window.location.search || '');
  let mv = q.get('map');
  if (mv && typeof mv === 'string') {
    mv = mv.replace(/\\/g, '/').replace(/^\/+/, '');
    if (/^(VehicleMap|ViewerMap)\/[^\/]+\.scene$/i.test(mv)) {
      CURRENT_MAP_ID = mv;
    } else if (/^[^\/]+\.scene$/i.test(mv)) {
      CURRENT_MAP_ID = 'ViewerMap/' + mv;
    }
  }
} catch (_) {}
const POINT_TYPE_INFO = { 1: { label: '普通点', color: '#f39c12' }, 2: { label: '等待点', color: '#2980b9' }, 3: { label: '避让点', color: '#8e44ad' }, 4: { label: '临时避让点', color: '#9b59b6' }, 5: { label: '库区点', color: '#16a085' }, 7: { label: '不可避让点', color: '#c0392b' }, 11: { label: '电梯点', color: '#e91e63' }, 12: { label: '自动门点', color: '#00bcd4' }, 13: { label: '充电点', color: '#4caf50' }, 14: { label: '停靠点', color: '#7f8c8d' }, 15: { label: '动作点', color: '#e74c3c' }, 16: { label: '禁行点', color: '#ff3860' } };
const PASS_TYPE_INFO = { 0: { label: '普通路段', color: '#3498db' }, 1: { label: '仅空载可通行', color: '#f1c40f' }, 2: { label: '仅载货可通行', color: '#e67e22' }, 10: { label: '禁行路段', color: '#e74c3c' } };

let canvas, ctx, mapData = null; let viewTransform = { x: 0, y: 0, scale: 1 }; let isDragging = false; let lastMousePos = { x: 0, y: 0 }; let showGrid = true; let sidebarOpen = false; let registeredRobots = []; let ws = null; let selectedRobotId = null; let selectedStationId = null; let selectedRouteId = null; let selectedNavStation = null; let lastRouteCandidates = []; let lastRouteCandidateIndex = 0; let mapLayers = []; let floorNames = []; let currentFloorIndex = 0;

window.onload = async function () { initCanvas(); await updateViewerMapOptions(); await applyHotConfigInitToFrontend(); loadMapData(); setupEventListeners(); updateRegisterMapOptions(); Promise.all([loadFrontendConfig(), loadSimSettingsCache(), loadRobotList(), loadEquipmentList()]).catch(() => { }); startPerfMonitor(); initWebSocket(); startRenderLoop(); setupKeyboardControl(); startCommStatusGuard(); };

function initCanvas() { canvas = document.getElementById('mapCanvas'); ctx = canvas.getContext('2d'); resizeCanvas(); window.addEventListener('resize', resizeCanvas); } function resizeCanvas() { const c = document.querySelector('.canvas-container'); canvas.width = c.clientWidth; canvas.height = c.clientHeight; if (mapData) { drawMap(); } }
function setupEventListeners() { canvas.addEventListener('mousedown', onMouseDown); canvas.addEventListener('mousemove', onMouseMove); canvas.addEventListener('mouseup', onMouseUp); canvas.addEventListener('wheel', onWheel); canvas.addEventListener('contextmenu', e => e.preventDefault()); document.addEventListener('click', (ev) => { const robotMenu = document.getElementById('robotContextMenu'); const stationMenu = document.getElementById('stationContextMenu'); const tgt = ev.target; if (robotMenu && robotMenu.style.display === 'block' && !robotMenu.contains(tgt)) { hideRobotContextMenu(); } if (stationMenu && stationMenu.style.display === 'block' && !stationMenu.contains(tgt)) { hideStationContextMenu(); } }); const regMapSel = document.getElementById('registerMapSelect'); if (regMapSel) { regMapSel.addEventListener('change', () => { const lbl = document.getElementById('registerMapSelectedLabel'); if (lbl) lbl.textContent = regMapSel.value || '未设置'; updateRegisterInitialPositionOptions(); }); } }

async function onMouseDown(e) { const p = getMousePos(e); if (e.button === 2) { const robot = findRobotAtScreenPoint(p.x, p.y); if (robot) { selectedRobotId = robot.robot_name; showRobotContextMenu(p.x, p.y, robot); } else { const hitPoint = findPointAtScreenPos(p.x, p.y); if (hitPoint) { showStationContextMenu(p.x, p.y, hitPoint); } else { hideRobotContextMenu(); hideStationContextMenu(); } } return; } const hitPoint = findPointAtScreenPos(p.x, p.y); if (hitPoint) { selectedStationId = hitPoint.id; const info = getStationDetails(hitPoint); updateStationInfoPanel(info); selectedRouteId = null; lastRouteCandidates = []; lastRouteCandidateIndex = 0; updateRouteInfoPanel(null); drawMap(); hideRobotContextMenu(); return; } const routeCandidates = findRoutesAtScreenPos(p.x, p.y); if (routeCandidates.length > 0) { const candidateIds = routeCandidates.map(rt => String(rt.id ?? (`${rt.from}->${rt.to}`))); const sameSet = (lastRouteCandidates.length === candidateIds.length) && lastRouteCandidates.every((id, idx) => id === candidateIds[idx]); if (!sameSet) { lastRouteCandidates = candidateIds; lastRouteCandidateIndex = 0; } else { lastRouteCandidateIndex = (lastRouteCandidateIndex + 1) % lastRouteCandidates.length; } const targetRoute = routeCandidates[lastRouteCandidateIndex]; selectedRouteId = lastRouteCandidates[lastRouteCandidateIndex]; const rinfo = getRouteDetails(targetRoute); updateRouteInfoPanel(rinfo); selectedStationId = null; updateStationInfoPanel(null); drawMap(); hideRobotContextMenu(); return; } isDragging = true; lastMousePos = p; canvas.style.cursor = 'grabbing'; const mi = document.getElementById('mapInfoSection'); if (mi) mi.style.display = 'block'; const rh = document.getElementById('routeHeader'); if (rh) rh.style.display = 'none'; const sh = document.getElementById('stationHeader'); if (sh) sh.style.display = 'none'; const rd = document.getElementById('routeDetails'); if (rd) rd.style.display = 'none'; const sd = document.getElementById('stationDetails'); if (sd) sd.style.display = 'none'; hideRobotContextMenu(); hideStationContextMenu(); }
function onMouseMove(e) { const p = getMousePos(e); const wp = screenToWorld(p.x, p.y); document.getElementById('mousePos').textContent = `${wp.x.toFixed(2)}, ${wp.y.toFixed(2)}`; if (isDragging) { viewTransform.x += p.x - lastMousePos.x; viewTransform.y += p.y - lastMousePos.y; drawMap(); lastMousePos = p; } } function onMouseUp() { isDragging = false; canvas.style.cursor = 'grab'; }
function onWheel(e) { e.preventDefault(); const p = getMousePos(e); const wp = screenToWorld(p.x, p.y); const sf = e.deltaY > 0 ? 0.9 : 1.1; const ns = Math.max(0.1, Math.min(5, viewTransform.scale * sf)); if (ns !== viewTransform.scale) { viewTransform.scale = ns; viewTransform.x = p.x - (wp.x * 20) * viewTransform.scale; viewTransform.y = p.y + (wp.y * 20) * viewTransform.scale; updateZoomDisplay(); drawMap(); } }
function getMousePos(e) { const r = canvas.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; }
function screenToWorld(sx, sy) { return { x: (sx - viewTransform.x) / viewTransform.scale / 20, y: -((sy - viewTransform.y) / viewTransform.scale / 20) }; } function worldToScreen(wx, wy) { return { x: (wx * 20) * viewTransform.scale + viewTransform.x, y: (-wy * 20) * viewTransform.scale + viewTransform.y }; }

function deriveClassFromName(name) { if (!name) return 'Station'; if (/^CP/i.test(name)) return 'CP'; if (/^DP/i.test(name)) return 'DP'; if (/^WP/i.test(name)) return 'WP'; if (/^Pallet/i.test(name)) return 'Pallet'; return 'Station'; }
function pointDrawSize() { const base = 3; const s = Math.max(4, Math.min(24, base * viewTransform.scale)); return s; }

function findPointAtScreenPos(sx, sy) { if (!mapData || !mapData.points) return null; const radius = pointDrawSize() + 2; const currentFloorName = floorNames[currentFloorIndex] || null; for (const p of mapData.points) { const pfloor = extractFloorNameFromLayer(p.layer || ''); if (currentFloorName && pfloor && String(pfloor).trim().toLowerCase() !== String(currentFloorName).trim().toLowerCase()) continue; const sp = worldToScreen(p.x, p.y); const dx = sx - sp.x, dy = sy - sp.y; if (dx * dx + dy * dy <= radius * radius) { return { id: String(p.id || p.name || `${p.x},${p.y}`), name: p.name || p.id || '未知', type: p.type, properties: (Array.isArray(p.properties) ? p.properties : (Array.isArray(p.property) ? p.property : [])), associatedStorageLocations: (Array.isArray(p.associatedStorageLocations) ? p.associatedStorageLocations : []), layer: p.layer || '', pos: { x: p.x, y: p.y }, dir: p.dir || 0 }; } } return null; }

async function loadSceneStations() { }
function getStationDetails(pt) { const id = String(pt.id || pt.name || ''); const name = String(pt.name || pt.id || ''); const typeCode = (pt.type !== undefined) ? Number(pt.type) : NaN; const className = (POINT_TYPE_INFO[typeCode]?.label) || '未知类型'; const dir = (typeof pt.dir === 'number') ? pt.dir : undefined; const ignoreDir = false; let spin = false; const props = Array.isArray(pt.properties) ? pt.properties : (Array.isArray(pt.property) ? pt.property : []); for (const prop of props) { if (prop && prop.key === 'spin' && prop.boolValue === true) { spin = true; break; } } const stor = Array.isArray(pt.associatedStorageLocations) ? pt.associatedStorageLocations : []; return { id, className, instanceName: name, pos: pt.pos, dir, ignoreDir, spin, associatedStorageLocations: stor }; }
function updateStationInfoPanel(info) { if (!info) { document.getElementById('stId').textContent = '-'; document.getElementById('stType').textContent = '-'; document.getElementById('stName').textContent = '-'; document.getElementById('stPos').textContent = '-'; document.getElementById('stDir').textContent = '-'; document.getElementById('stSpin').textContent = '-'; document.getElementById('stStorage').textContent = '-'; const sr = document.getElementById('stStorageRow'); if (sr) sr.style.display = 'none'; const sdr = document.getElementById('stStorageDetailsRow'); if (sdr) sdr.style.display = 'none'; const sdt = document.getElementById('stStorageDetails'); if (sdt) sdt.textContent = '-'; const sd = document.getElementById('stationDetails'); if (sd) sd.style.display = 'none'; const sh = document.getElementById('stationHeader'); if (sh) sh.style.display = 'none'; return; } document.getElementById('stId').textContent = info.id || '-'; document.getElementById('stType').textContent = info.className || '-'; document.getElementById('stName').textContent = info.instanceName || '-'; const posStr = info.pos ? `(${Number(info.pos.x).toFixed(2)}, ${Number(info.pos.y).toFixed(2)})` : '-'; document.getElementById('stPos').textContent = posStr; const orientationText = (info.ignoreDir === true) ? '任意' : (typeof info.dir === 'number' ? `${Number(info.dir * 180 / Math.PI).toFixed(2)}°` : '-'); document.getElementById('stDir').textContent = orientationText; document.getElementById('stSpin').textContent = (info.spin === true) ? 'true' : 'false'; const bin = lookupBinLocationInfo(info.instanceName || info.id); const storNames = Array.isArray(bin?.locationNames) ? bin.locationNames : []; const hasDetails = renderBinTaskDetails(bin); const sr = document.getElementById('stStorageRow'); const sdr = document.getElementById('stStorageDetailsRow'); if (sr) sr.style.display = (storNames.length > 0) ? 'block' : 'none'; if (sdr) sdr.style.display = hasDetails ? 'block' : 'none'; const storEl = document.getElementById('stStorage'); if (storEl) storEl.textContent = (storNames.length > 0) ? storNames.join(', ') : '-'; const sdt = document.getElementById('stStorageDetails'); if (sdt) sdt.style.display = hasDetails ? 'block' : 'none'; const sd = document.getElementById('stationDetails'); if (sd) sd.style.display = 'block'; const sh = document.getElementById('stationHeader'); if (sh) sh.style.display = 'block'; const rh = document.getElementById('routeHeader'); if (rh) rh.style.display = 'none'; const rd = document.getElementById('routeDetails'); if (rd) rd.style.display = 'none'; const mi = document.getElementById('mapInfoSection'); if (mi) mi.style.display = 'none'; }
function getRoutePassLabel(passCode) { const code = Number(passCode); const info = Object.prototype.hasOwnProperty.call(PASS_TYPE_INFO, code) ? PASS_TYPE_INFO[code] : PASS_TYPE_INFO[0]; return info.label; } function getRouteColorByPass(passCode) { const code = Number(passCode); const info = Object.prototype.hasOwnProperty.call(PASS_TYPE_INFO, code) ? PASS_TYPE_INFO[code] : PASS_TYPE_INFO[0]; return info.color; }
function getRouteDetails(route) { if (!route) return null; const id = String(route.id ?? (`${route.from}->${route.to}`)); const desc = String(route.desc || route.name || '-'); const type = String(route.type || '-'); const pass = (route.pass !== undefined) ? Number(route.pass) : 0; return { id, desc, type, passLabel: getRoutePassLabel(pass), passCode: pass }; }
function updateRouteInfoPanel(info) { const panel = document.getElementById('routeDetails'); if (!panel) return; if (!info) { document.getElementById('rtId').textContent = '-'; document.getElementById('rtDesc').textContent = '-'; document.getElementById('rtType').textContent = '-'; document.getElementById('rtPass').textContent = '-'; panel.style.display = 'none'; const rh = document.getElementById('routeHeader'); if (rh) rh.style.display = 'none'; return; } document.getElementById('rtId').textContent = info.id || '-'; document.getElementById('rtDesc').textContent = info.desc || '-'; document.getElementById('rtType').textContent = info.type || '-'; document.getElementById('rtPass').textContent = info.passLabel || '-'; panel.style.display = 'block'; const rh = document.getElementById('routeHeader'); if (rh) rh.style.display = 'block'; const sd = document.getElementById('stationDetails'); if (sd) sd.style.display = 'none'; const sh = document.getElementById('stationHeader'); if (sh) sh.style.display = 'none'; const mi = document.getElementById('mapInfoSection'); if (mi) mi.style.display = 'none'; }
function pointToSegmentDistance(px, py, x1, y1, x2, y2) { const dx = x2 - x1, dy = y2 - y1; if (dx === 0 && dy === 0) return Math.hypot(px - x1, py - y1); const t = ((px - x1) * dx + (py - y1) * dy) / (dx * dx + dy * dy); const tt = Math.max(0, Math.min(1, t)); const cx = x1 + tt * dx, cy = y1 + tt * dy; return Math.hypot(px - cx, py - cy); }
function findRoutesAtScreenPos(sx, sy) { if (!mapData || !Array.isArray(mapData.routes)) return []; const pmap = {}; (mapData.points || []).forEach(p => { pmap[p.id] = p; }); const threshold = Math.max(6, 8 * (viewTransform.scale)); const candidates = []; for (let i = (mapData.routes.length - 1); i >= 0; i--) { const route = mapData.routes[i]; const fp = pmap[route.from], tp = pmap[route.to]; if (!fp || !tp) continue; if (route.type === 'bezier3' && route.c1 && route.c2) { const N = 24; let minD = Infinity; let prev = worldToScreen(fp.x, fp.y); for (let j = 1; j <= N; j++) { const t = j / N; const bt = bezierPoint({ x: fp.x, y: fp.y }, { x: route.c1.x, y: route.c1.y }, { x: route.c2.x, y: route.c2.y }, { x: tp.x, y: tp.y }, t); const cur = worldToScreen(bt.x, bt.y); const d = pointToSegmentDistance(sx, sy, prev.x, prev.y, cur.x, cur.y); if (d < minD) minD = d; prev = cur; } if (minD <= threshold) candidates.push(route); } else { const s1 = worldToScreen(fp.x, fp.y); const s2 = worldToScreen(tp.x, tp.y); const d = pointToSegmentDistance(sx, sy, s1.x, s1.y, s2.x, s2.y); if (d <= threshold) candidates.push(route); } } return candidates; }
function findRobotAtScreenPoint(sx, sy) { for (const robot of registeredRobots) { if (robot.currentMap) { const robotFloor = extractFloorFromMapId(robot.currentMap); const currentFloorName = floorNames[currentFloorIndex] || null; if (robotFloor && currentFloorName && robotFloor !== currentFloorName) continue; } const pos = robot.currentPosition || robot.initialPosition || null; if (!pos) continue; const sp = worldToScreen(pos.x, pos.y); const dx = sx - sp.x; const dy = sy - sp.y; const ang = (pos.theta ?? pos.orientation ?? 0); const cosA = Math.cos(ang + Math.PI / 2), sinA = Math.sin(ang + Math.PI / 2); const lx = dx * cosA - dy * sinA; const ly = dx * sinA + dy * cosA; const s = Math.max(0.5, Math.min(2, viewTransform.scale)); const half = 12 * s; if (lx >= -half && lx <= half && ly >= -half && ly <= half) { return robot; } } return null; }
function showRobotContextMenu(sx, sy, robot) { const menu = document.getElementById('robotContextMenu'); if (!menu) return; const hasPallet = !!robot.hasPallet; menu.innerHTML = ''; const item = document.createElement('div'); item.style.padding = '8px 12px'; item.style.cursor = 'pointer'; item.textContent = hasPallet ? '卸载托盘' : '加载托盘'; item.onclick = function () { if (hasPallet) unloadPallet(robot.robot_name); else loadPallet(robot.robot_name); hideRobotContextMenu(); }; menu.appendChild(item); const container = document.getElementById('canvasContainer'); const rect = container.getBoundingClientRect(); menu.style.left = (sx + rect.left - rect.left) + 'px'; menu.style.top = (sy + rect.top - rect.top) + 'px'; menu.style.display = 'block'; }
function hideRobotContextMenu() { const menu = document.getElementById('robotContextMenu'); if (menu) menu.style.display = 'none'; }
function isWorkStationPoint(point) { const nm = String(point?.name || point?.id || '').trim(); return /^AP/i.test(nm); } function isChargingStationPoint(point) { const nm = String(point?.name || point?.id || '').trim(); const typeCode = (point && typeof point.type !== 'undefined') ? Number(point.type) : NaN; if (typeCode === 13) return true; return /^CP/i.test(nm); }
function showStationContextMenu(sx, sy, point) { const menu = document.getElementById('stationContextMenu'); if (!menu) return; menu.innerHTML = ''; const itemNav = document.createElement('div'); itemNav.style.padding = '8px 12px'; itemNav.style.cursor = 'pointer'; itemNav.textContent = '导航至该站点'; itemNav.onclick = async function (ev) { if (ev && ev.stopPropagation) ev.stopPropagation(); try { await navigateRobotToStation(point); } catch (err) { console.error('导航失败:', err); try { window.printErrorToStatus(err, '导航失败'); } catch (_) { } } finally { hideStationContextMenu(); } }; if (!selectedRobotId) { itemNav.style.opacity = '0.6'; itemNav.style.pointerEvents = 'none'; const tip = document.createElement('div'); tip.style.padding = '6px 12px'; tip.style.fontSize = '12px'; tip.style.color = '#bdc3c7'; tip.textContent = '请先在列表中选中机器人'; menu.appendChild(tip); } menu.appendChild(itemNav); const isCharge = isChargingStationPoint(point); if (isCharge) { const itemCharge = document.createElement('div'); itemCharge.style.padding = '8px 12px'; itemCharge.style.cursor = 'pointer'; itemCharge.textContent = '执行充电任务'; itemCharge.onclick = async function (ev) { if (ev && ev.stopPropagation) ev.stopPropagation(); try { await navigateRobotToStationWithAction(point, 'StartCharging', { source: 'CPMenu' }); } catch (err) { console.error('充电任务发布失败:', err); try { window.printErrorToStatus(err, '充电任务发布失败'); } catch (_) { } } finally { hideStationContextMenu(); } }; if (!selectedRobotId) { itemCharge.style.opacity = '0.6'; itemCharge.style.pointerEvents = 'none'; } menu.appendChild(itemCharge); } const options = extractBinTaskOptionsForPoint(point); const isWork = isWorkStationPoint(point); if (isWork && options.length > 0) { const actionItem = document.createElement('div'); actionItem.style.padding = '8px 12px'; actionItem.style.cursor = 'pointer'; actionItem.textContent = '至站点执行动作'; const sub = document.createElement('div'); sub.style.display = 'none'; sub.style.borderTop = '1px solid #34495e'; sub.style.marginTop = '6px'; sub.style.paddingTop = '6px'; for (const opt of options) { const btn = document.createElement('div'); btn.style.padding = '6px 12px'; btn.style.cursor = 'pointer'; btn.textContent = opt.title; btn.onclick = async function (ev) { if (ev && ev.stopPropagation) ev.stopPropagation(); try { await navigateRobotToStationWithAction(point, opt.title, opt.params); } catch (err) { console.error('执行动作失败:', err); try { window.printErrorToStatus(err, '执行动作失败'); } catch (_) { } } finally { hideStationContextMenu(); } }; if (!selectedRobotId) { btn.style.opacity = '0.6'; btn.style.pointerEvents = 'none'; } sub.appendChild(btn); } actionItem.onclick = function (ev) { if (ev && ev.stopPropagation) ev.stopPropagation(); sub.style.display = (sub.style.display === 'none') ? 'block' : 'none'; }; menu.appendChild(actionItem); menu.appendChild(sub); } const container = document.getElementById('canvasContainer'); const rect = container.getBoundingClientRect(); menu.style.left = (sx + rect.left - rect.left) + 'px'; menu.style.top = (sy + rect.top - rect.top) + 'px'; menu.style.display = 'block'; }
function hideStationContextMenu() { const menu = document.getElementById('stationContextMenu'); if (menu) menu.style.display = 'none'; }
function extractBinTaskOptionsForPoint(point) { try { const key = String(point?.name || point?.id || '').trim(); if (!key) return []; const entry = lookupBinLocationInfo(key); const objs = Array.isArray(entry?.binTaskObjects) ? entry.binTaskObjects : []; const options = []; for (const obj of objs) { const list = Array.isArray(obj) ? obj : [obj]; for (const item of list) { if (item && typeof item === 'object' && Object.keys(item).length > 0) { const actionName = Object.keys(item)[0]; const params = item[actionName]; options.push({ title: String(actionName), params: (params && typeof params === 'object') ? params : {} }); } } } if (options.length === 0) { const raws = Array.isArray(entry?.binTaskStrings) ? entry.binTaskStrings : []; for (const s of raws) { let title = String(s).trim(); title = title.replace(/\s+/g, ' ').slice(0, 50) || 'RawAction'; options.push({ title, params: { content: s } }); } } return options; } catch (e) { return []; } }
async function navigateRobotToStation(point) { if (!selectedRobotId) { try { window.printErrorToStatus('请先在机器人列表中选中机器人', '导航'); } catch (_) { } return; } const robot = registeredRobots.find(r => r.robot_name === selectedRobotId); if (!robot) { try { window.printErrorToStatus('未找到选中机器人', '导航'); } catch (_) { } return; } const opMode = String(robot.operatingMode || 'AUTOMATIC').toUpperCase(); if (opMode !== 'SERVICE') { try { window.printErrorToStatus(`当前仿真车无控制权（operatingMode=${opMode}），不可下发导航订单`, '导航'); } catch (_) { } return; } const pos = robot.currentPosition || robot.initialPosition; if (!pos) { try { window.printErrorToStatus('机器人没有当前位置或初始位置', '导航'); } catch (_) { } return; } const mapName = resolveMapNameForNav(robot, point); if (!mapName) { try { window.printErrorToStatus('无法解析地图文件名', '导航'); } catch (_) { } return; } document.getElementById('status').textContent = '正在读取地图并规划路径...'; const resp = await fetch('/maps/' + mapName); if (!resp.ok) { try { window.printErrorToStatus(`读取地图失败: HTTP ${resp.status}`, '导航'); } catch (_) { } return; } const sceneData = await resp.json(); const topo = parseSceneTopology(sceneData); if (!Array.isArray(topo.stations) || topo.stations.length === 0 || !Array.isArray(topo.paths) || topo.paths.length === 0) { try { window.printErrorToStatus('地图缺少站点或路段（routes），无法规划', '导航'); } catch (_) { } return; } const stations = await getSceneStationsForMap(mapName); const targetId = findStationIdForPoint(stations, point); if (!targetId) { try { window.printErrorToStatus('无法匹配到对应的站点', '导航'); } catch (_) { } return; } const targetStation = (stations || []).find(s => String(s.instanceName || s.id || s.pointName) === String(targetId)); if (!targetStation || !targetStation.pos) { try { window.printErrorToStatus('无法获取目标站点坐标', '导航'); } catch (_) { } return; } const startStationId = findNearestStation({ x: Number(pos.x), y: Number(pos.y) }, topo.stations); const endStationId = findNearestStation({ x: Number(targetStation.pos.x), y: Number(targetStation.pos.y) }, topo.stations); if (!startStationId || !endStationId) { try { window.printErrorToStatus('无法选取起止站点', '导航'); } catch (_) { } return; } const nodePath = aStar(String(startStationId), String(endStationId), topo.stations, topo.paths); if (!nodePath || nodePath.length < 2) { try { window.printErrorToStatus('未找到可达路径，请检查地图 routes', '导航'); } catch (_) { } return; } const agvInfo = { serial_number: selectedRobotId, manufacturer: robot.manufacturer || 'SEER', version: (robot.version || '2.0.0') }; const mapIdShort = extractFloorFromMapId(mapName); const order = generateVdaOrder(nodePath, agvInfo, topo, mapIdShort, { allowedDeviationXY: 0, allowedDeviationTheta: 0 }); try { const pubResp = await fetch(`${API_BASE_URL}/agv/${encodeURIComponent(selectedRobotId)}/order`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(order) }); if (!pubResp.ok) throw new Error(`HTTP ${pubResp.status}: ${pubResp.statusText}`); await pubResp.json(); const stationNameById = new Map((topo.stations || []).map(s => [String(s.id), s.name || String(s.id)])); const displayPath = nodePath.map(id => stationNameById.get(String(id)) || String(id)); document.getElementById('status').textContent = `订单已发布，路径: ${displayPath.join(' -> ')}`; } catch (e) { console.error('订单发布失败:', e); try { window.printErrorToStatus(e, '订单发布失败'); } catch (_) { } } }
async function navigateRobotToStationWithAction(point, actionTitle, actionParams) { if (!selectedRobotId) { try { window.printErrorToStatus('请先在机器人列表中选中机器人', '执行动作'); } catch (_) { } return; } const robot = registeredRobots.find(r => r.robot_name === selectedRobotId); if (!robot) { try { window.printErrorToStatus('未找到选中机器人', '执行动作'); } catch (_) { } return; } const opMode = String(robot.operatingMode || 'AUTOMATIC').toUpperCase(); if (opMode !== 'SERVICE') { try { window.printErrorToStatus(`当前仿真车无控制权（operatingMode=${opMode}），不可下发导航订单`, '执行动作'); } catch (_) { } return; } const pos = robot.currentPosition || robot.initialPosition; if (!pos) { try { window.printErrorToStatus('机器人没有当前位置或初始位置', '执行动作'); } catch (_) { } return; } const mapName = resolveMapNameForNav(robot, point); if (!mapName) { try { window.printErrorToStatus('无法解析地图文件名', '执行动作'); } catch (_) { } return; } document.getElementById('status').textContent = '正在读取地图并规划路径...'; const resp = await fetch('/maps/' + mapName); if (!resp.ok) { try { window.printErrorToStatus(`读取地图失败: HTTP ${resp.status}`, '执行动作'); } catch (_) { } return; } const sceneData = await resp.json(); const topo = parseSceneTopology(sceneData); if (!Array.isArray(topo.stations) || topo.stations.length === 0 || !Array.isArray(topo.paths) || topo.paths.length === 0) { try { window.printErrorToStatus('地图缺少站点或路段（routes），无法规划', '执行动作'); } catch (_) { } return; } const stations = await getSceneStationsForMap(mapName); const targetId = findStationIdForPoint(stations, point); if (!targetId) { try { window.printErrorToStatus('无法匹配到对应的站点', '执行动作'); } catch (_) { } return; } const targetStation = (stations || []).find(s => String(s.instanceName || s.id || s.pointName) === String(targetId)); if (!targetStation || !targetStation.pos) { try { window.printErrorToStatus('无法获取目标站点坐标', '执行动作'); } catch (_) { } return; } const startStationId = findNearestStation({ x: Number(pos.x), y: Number(pos.y) }, topo.stations); const endStationId = findNearestStation({ x: Number(targetStation.pos.x), y: Number(targetStation.pos.y) }, topo.stations); if (!startStationId || !endStationId) { try { window.printErrorToStatus('无法选取起止站点', '执行动作'); } catch (_) { } return; } const nodePath = aStar(String(startStationId), String(endStationId), topo.stations, topo.paths); if (!nodePath || nodePath.length < 2) { try { window.printErrorToStatus('未找到可达路径，请检查地图 routes', '执行动作'); } catch (_) { } return; } const agvInfo = { serial_number: selectedRobotId, manufacturer: robot.manufacturer || 'SEER', version: (robot.version || '2.0.0') }; const mapIdShort = extractFloorFromMapId(mapName); const order = generateVdaOrder(nodePath, agvInfo, topo, mapIdShort, { allowedDeviationXY: 0, allowedDeviationTheta: 0 }); try { const lastIdx = (order.nodes || []).length - 1; if (lastIdx >= 0) { const paramsList = []; if (actionParams && typeof actionParams === 'object') { for (const k of Object.keys(actionParams)) { paramsList.push({ key: String(k), value: actionParams[k] }); } } const actionObj = { actionType: String(actionTitle || ''), actionId: 'act-' + String(Date.now()), blockingType: 'HARD', actionDescription: String(actionTitle || ''), actionParameters: paramsList }; if (!Array.isArray(order.nodes[lastIdx].actions)) order.nodes[lastIdx].actions = []; order.nodes[lastIdx].actions.push(actionObj); } const pubResp = await fetch(`${API_BASE_URL}/agv/${encodeURIComponent(selectedRobotId)}/order`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(order) }); if (!pubResp.ok) throw new Error(`HTTP ${pubResp.status}: ${pubResp.statusText}`); await pubResp.json(); const stationNameById = new Map((topo.stations || []).map(s => [String(s.id), s.name || String(s.id)])); const displayPath = nodePath.map(id => stationNameById.get(String(id)) || String(id)); document.getElementById('status').textContent = `订单已发布，路径: ${displayPath.join(' -> ')}，动作: ${String(actionTitle)}`; } catch (e) { console.error('订单发布失败:', e); try { window.printErrorToStatus(e, '订单发布失败'); } catch (_) { } } }
function resolveMapNameForNav(robot, point) {
  try {
    if (CURRENT_MAP_ID) return normalizeViewerMapId(CURRENT_MAP_ID);
    const pointLayer = String(point && point.layer ? point.layer : '').trim();
    if (pointLayer) {
      const floorName = extractFloorNameFromLayer(pointLayer);
      if (floorName) return normalizeViewerMapId(`${floorName}.scene`);
    }
    const fname = floorNames[currentFloorIndex];
    if (fname && typeof fname === 'string' && fname.trim() !== '') {
      return normalizeViewerMapId(`${fname}.scene`);
    }
    if (robot && typeof robot.currentMap === 'string' && /\.scene$/i.test(robot.currentMap)) {
      return normalizeViewerMapId(robot.currentMap);
    }
    return null;
  } catch (_) {
    return null;
  }
}
function distanceXY(a, b) { const dx = Number(a.x) - Number(b.x); const dy = Number(a.y) - Number(b.y); return Math.hypot(dx, dy); }
async function getSceneStationsForMap(mapName) { try { const path = String(mapName || '').trim(); if (!path) return []; const resp = await fetch('/maps/' + path); if (!resp.ok) return []; const raw = await resp.json(); const root = Array.isArray(raw) ? raw[0] : raw; const points = (root && Array.isArray(root.points)) ? root.points : []; const allowedPrefixes = ['AP', 'CP', 'PP', 'LM', 'WP']; const startsAllowed = (txt) => allowedPrefixes.some(pref => String(txt || '').trim().toUpperCase().startsWith(pref)); const out = []; for (const p of points) { const nm = String(p.name || p.id || '').trim(); if (!nm || !startsAllowed(nm)) continue; out.push({ id: String(p.id || nm), instanceName: nm, pointName: nm, className: deriveClassFromName(nm), pos: { x: Number(p.x), y: Number(p.y) } }); } return out; } catch (err) { console.error('getSceneStationsForMap error:', err); return []; } }
function findNearestSceneStation(stations, worldPos) { const allowedPrefixes = ['AP', 'CP', 'PP', 'LM', 'WP']; const allowedClasses = new Set(['ActionPoint', 'LocationMark', 'ChargingPoint', 'ParkingPoint', 'WayPoint', 'Waypoint']); let best = null; let bestD = Infinity; (stations || []).forEach(s => { const id = String(s.id || '').trim(); const iName = String(s.instanceName || '').trim(); const pName = String(s.pointName || '').trim(); const cls = String(s.className || '').trim(); const startsAllowed = (txt) => allowedPrefixes.some(pref => txt.startsWith(pref)); const isAllowed = allowedClasses.has(cls) || startsAllowed(id) || startsAllowed(iName) || startsAllowed(pName); if (!isAllowed) return; const sx = (s && s.pos && typeof s.pos.x !== 'undefined') ? Number(s.pos.x) : Number(s.x); const sy = (s && s.pos && typeof s.pos.y !== 'undefined') ? Number(s.pos.y) : Number(s.y); const d = Math.hypot(Number(worldPos.x) - sx, Number(worldPos.y) - sy); if (d < bestD) { bestD = d; best = s; } }); return best; }
function findStationIdForPoint(stations, pt) { const key1 = (pt?.name || '').trim(); const key2 = (pt?.id || '').trim(); const allowedPrefixes = ['AP', 'CP', 'PP', 'LM', 'WP']; const allowedClasses = new Set(['ActionPoint', 'LocationMark', 'ChargingPoint', 'ChargePoint', 'ParkingPoint', 'ParkPoint', 'WayPoint', 'Waypoint']); const startsAllowed = (txt) => allowedPrefixes.some(pref => String(txt || '').trim().startsWith(pref)); const isAllowedStation = (s) => { const id = String(s.id || '').trim(); const iName = String(s.instanceName || '').trim(); const pName = String(s.pointName || '').trim(); const cls = String(s.className || '').trim(); return allowedClasses.has(cls) || startsAllowed(id) || startsAllowed(iName) || startsAllowed(pName); }; for (const s of (stations || [])) { const iName = (s.instanceName || '').trim(); const pName = (s.pointName || '').trim(); if (!isAllowedStation(s)) continue; if (key1 && (iName === key1 || pName === key1)) return s.instanceName || s.id || s.pointName; if (key2 && (iName === key2 || pName === key2)) return s.instanceName || s.id || s.pointName; } const nearest = findNearestSceneStation(stations, pt.pos || { x: 0, y: 0 }); return nearest ? (nearest.instanceName || nearest.id || nearest.pointName) : null; }
async function callNavToStation(robotId, stationId, mapName) { const url = `${API_BASE_URL}/sim/agv/${encodeURIComponent(robotId)}/nav/station?station_id=${encodeURIComponent(stationId)}&map_name=${encodeURIComponent(mapName)}`; const resp = await fetch(url, { method: 'POST' }); if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`); return await resp.json(); }
async function pollNavUntilDone(robotId, timeoutMs = 60000) { const start = Date.now(); while (Date.now() - start < timeoutMs) { try { const resp = await fetch(`${API_BASE_URL}/agv/${encodeURIComponent(robotId)}/nav/status`); if (resp.ok) { const st = await resp.json(); if (!st.running) return true; } } catch (e) { } await new Promise(r => setTimeout(r, window.FRONTEND_POLL_INTERVAL_MS || 100)); } return false; }
function loadPallet(id) {
  const robot = registeredRobots.find(r => r.robot_name === id);
  if (!robot) return;
  if (robot.hasPallet) return;
  robot._palletAnimLoading = true;
  robot._palletAnimStart = performance.now();
  robot._palletAnimProgress = 0;
  startPalletAnimationLoop();
}
function unloadPallet(id) {
  const robot = registeredRobots.find(r => r.robot_name === id);
  if (!robot) return;
  robot._palletAnimLoading = false;
  robot._palletAnimUnloading = true;
  robot._palletAnimStart = performance.now();
  robot._palletAnimProgress = 0;
  startPalletAnimationLoop();
}
let palletAnimTimer = null;
function startPalletAnimationLoop() {
  if (palletAnimTimer) return;
  palletAnimTimer = setInterval(() => {
    const now = performance.now();
    let any = false;
    const scale = Math.max(0.0001, Number(window.SIM_TIME_SCALE) || 1.0);
    const durBase = 3000;
    const dur = Math.max(150, Math.round(durBase / scale));
    registeredRobots.forEach(r => {
      if (r._palletAnimLoading) {
        const p = Math.max(0, Math.min(1, (now - (r._palletAnimStart || now)) / dur));
        r._palletAnimProgress = p;
        if (p >= 1) {
          r._palletAnimLoading = false;
          r.hasPallet = true;
        } else {
          any = true;
        }
      } else if (r._palletAnimUnloading) {
        const p = Math.max(0, Math.min(1, (now - (r._palletAnimStart || now)) / dur));
        r._palletAnimProgress = p;
        if (p >= 1) {
          r._palletAnimUnloading = false;
          r.hasPallet = false;
          r.shelfModel = null;
        } else {
          any = true;
        }
      }
    });
    drawMap();
    if (!any) {
      clearInterval(palletAnimTimer);
      palletAnimTimer = null;
    }
  }, 16);
}

function getRouteDirection(route) { let d = route.direction; if (d === undefined && Array.isArray(route.properties)) { const p = route.properties.find(pr => (pr.name === 'direction' || pr.key === 'direction')); if (p) d = Number(p.value); } if (typeof d !== 'number' || isNaN(d)) return 1; if (d === 0) return 0; return d > 0 ? 1 : -1; }
function drawArrowAt(x, y, angle) { const arrowLength = 10; const arrowAngle = Math.PI / 6; ctx.save(); ctx.fillStyle = ctx.strokeStyle; ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x - arrowLength * Math.cos(angle - arrowAngle), y - arrowLength * Math.sin(angle - arrowAngle)); ctx.lineTo(x - arrowLength * Math.cos(angle + arrowAngle), y - arrowLength * Math.sin(angle + arrowAngle)); ctx.closePath(); ctx.fill(); ctx.restore(); }
function drawArrow(start, end) { const angle = Math.atan2(end.y - start.y, end.x - start.x); drawArrowAt(end.x, end.y, angle); }
function bezierPoint(p0, p1, p2, p3, t) { const u = 1 - t; const tt = t * t; const uu = u * u; const uuu = uu * u; const ttt = tt * t; return { x: uuu * p0.x + 3 * uu * t * p1.x + 3 * u * tt * p2.x + ttt * p3.x, y: uuu * p0.y + 3 * uu * t * p1.y + 3 * u * tt * p2.y + ttt * p3.y }; }
function bezierTangent(p0, p1, p2, p3, t) { const u = 1 - t; return { x: 3 * u * u * (p1.x - p0.x) + 6 * u * t * (p2.x - p1.x) + 3 * t * t * (p3.x - p2.x), y: 3 * u * u * (p1.y - p0.y) + 6 * u * t * (p2.y - p1.y) + 3 * t * t * (p3.y - p2.y) }; }

function buildBinLocationIndex(layers) { const byFloor = []; function buildIndexForLayer(layer) { const byPointName = new Map(); function recordEntry(pointName, instanceName, binTaskString) { if (!pointName) return; const key = String(pointName).trim().toUpperCase(); let entry = byPointName.get(key); if (!entry) { entry = { locationNames: [], binTaskStrings: [], binTaskObjects: [] }; byPointName.set(key, entry); } if (instanceName) entry.locationNames.push(String(instanceName).trim()); if (typeof binTaskString === 'string' && binTaskString.trim() !== '') { entry.binTaskStrings.push(binTaskString); try { const parsed = JSON.parse(binTaskString); entry.binTaskObjects.push(parsed); } catch (_) { } } } function traverse(obj) { if (!obj) return; if (Array.isArray(obj)) { obj.forEach(traverse); return; } if (typeof obj === 'object') { if (Array.isArray(obj.binLocationList)) { for (const loc of obj.binLocationList) { const pName = String(loc?.pointName || '').trim(); const iName = String(loc?.instanceName || '').trim(); const props = Array.isArray(loc?.property) ? loc.property : (Array.isArray(loc?.properties) ? loc.properties : []); let taskStrs = []; for (const prop of (props || [])) { if (prop && String(prop.key || '').trim() === 'binTask' && typeof prop.stringValue === 'string') { taskStrs.push(prop.stringValue); } } if (taskStrs.length === 0) { recordEntry(pName, iName, undefined); } else { for (const s of taskStrs) recordEntry(pName, iName, s); } } } for (const k in obj) { if (!Object.prototype.hasOwnProperty.call(obj, k)) continue; const v = obj[k]; if (v && (typeof v === 'object' || Array.isArray(v))) traverse(v); } } } try { traverse(layer); } catch (_) { } return byPointName; } try { for (const layer of (layers || [])) { byFloor.push(buildIndexForLayer(layer)); } } catch (_) { } window.binLocationIndex = { byFloor }; }
function lookupBinLocationInfo(pointName) { const key = String(pointName || '').trim().toUpperCase(); const idx = (typeof currentFloorIndex === 'number') ? currentFloorIndex : 0; const byFloor = window.binLocationIndex?.byFloor; const map = Array.isArray(byFloor) ? byFloor[idx] : null; return (map && map.get(key)) || null; }
function formatBinTaskDetails(entry) { try { const objs = Array.isArray(entry?.binTaskObjects) ? entry.binTaskObjects : []; const sections = []; for (const obj of objs) { const arr = Array.isArray(obj) ? obj : [obj]; for (const item of arr) { if (item && typeof item === 'object') { for (const actionName of Object.keys(item)) { const params = item[actionName]; const lines = []; lines.push(`${actionName}：`); if (params && typeof params === 'object') { for (const k of Object.keys(params)) { const v = params[k]; const vs = (typeof v === 'string') ? `"${v}"` : String(v); lines.push(` "${k}": ${vs}`); } } else { lines.push(` ${String(params)}`); } sections.push(lines.join('\n')); } } else if (typeof item === 'string') { sections.push(item); } else { sections.push(String(item)); } } } if (sections.length > 0) return sections.join('\n\n'); const raw = Array.isArray(entry?.binTaskStrings) ? entry.binTaskStrings : []; return raw.join('\n'); } catch (_) { const raw = Array.isArray(entry?.binTaskStrings) ? entry.binTaskStrings : []; return raw.join('\n'); } }
function renderBinTaskDetails(entry) { const container = document.getElementById('stStorageDetails'); if (!container) return false; container.innerHTML = ''; const objs = Array.isArray(entry?.binTaskObjects) ? entry.binTaskObjects : []; let made = false; function makeCard(title, kv) { const card = document.createElement('div'); card.className = 'bin-card'; const t = document.createElement('div'); t.className = 'bin-card-title'; t.textContent = title || 'Detail'; card.appendChild(t); for (const [k, v] of kv) { const row = document.createElement('div'); row.className = 'bin-kv'; const kEl = document.createElement('div'); kEl.className = 'key'; kEl.textContent = String(k); const vEl = document.createElement('div'); vEl.className = 'val'; vEl.textContent = (typeof v === 'string') ? v : String(v); row.appendChild(kEl); row.appendChild(vEl); card.appendChild(row); } container.appendChild(card); } for (const obj of objs) { const list = Array.isArray(obj) ? obj : [obj]; for (const item of list) { if (item && typeof item === 'object' && Object.keys(item).length > 0) { const actionName = Object.keys(item)[0]; const params = item[actionName]; const kv = []; if (params && typeof params === 'object') { for (const k of Object.keys(params)) kv.push([k, params[k]]); } makeCard(actionName, kv); made = true; } else if (typeof item === 'string') { makeCard('Raw', [['string', item]]); made = true; } } } if (!made) { const raws = Array.isArray(entry?.binTaskStrings) ? entry.binTaskStrings : []; for (const r of raws) { makeCard('Raw', [['string', r]]); made = true; } } return made; }

async function loadMapData() { try { if (!CURRENT_MAP_ID) { const el = document.getElementById('loading'); if (el) el.style.display = 'none'; const st = document.getElementById('status'); if (st) st.textContent = '请先选择地图'; return; } document.getElementById('loading').style.display = 'block'; document.getElementById('status').textContent = '正在加载地图数据...'; const resp = await fetch('/maps/' + CURRENT_MAP_ID); if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`); const raw = await resp.json(); mapLayers = Array.isArray(raw) ? raw : (raw ? [raw] : []); buildBinLocationIndex(mapLayers); floorNames = mapLayers.map(extractFloorNameFromLayer); if (!Array.isArray(floorNames) || floorNames.filter(v => !!v).length === 0) { const root = mapLayers.length > 0 ? mapLayers[0] : null; const unique = new Set(); const pts = (root && Array.isArray(root.points)) ? root.points : []; pts.forEach(p => { const nm = extractFloorNameFromLayer(p.layer || ''); if (nm) unique.add(nm); }); if (unique.size > 0) { floorNames = Array.from(unique); } } currentFloorIndex = 0; mapData = mapLayers.length > 0 ? normalizeScene(mapLayers[currentFloorIndex]) : null; await loadSceneStations(); populateFloorSelect(); updateMapInfo(); fitMapToView(); drawMap(); document.getElementById('status').textContent = `地图加载完成: ${CURRENT_MAP_ID}`; } catch (err) { console.error('加载地图数据失败:', err); document.getElementById('status').textContent = '加载失败: ' + err.message; } finally { document.getElementById('loading').style.display = 'none'; } }

async function updateViewerMapOptions() { const sel = document.getElementById('viewerMapSelect'); if (!sel) return; sel.innerHTML = '<option value="">请选择地图</option>'; const normalizeMapId = (raw) => { let s = String(raw || '').replace(/\\/g, '/').replace(/^\/+/, '').trim(); if (!s) return ''; if (/^(VehicleMap|ViewerMap)\/[^\/]+\.scene$/i.test(s)) return s; if (/^[^\/]+\.scene$/i.test(s)) return 'ViewerMap/' + s; const last = s.split('/').pop(); if (last && /\.scene$/i.test(last)) return 'ViewerMap/' + last; return s; }; const displayName = (mapId) => { let s = String(mapId || '').replace(/\\/g, '/').replace(/^\/+/, '').trim(); s = s.replace(/^ViewerMap\//i, ''); const last = s.split('/').pop(); return last || s || '-'; }; try { const resp = await fetch(`${API_BASE_URL}/maps/ViewerMap`); if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`); const files = await resp.json(); (files || []).forEach(name => { const mapId = normalizeMapId(name); if (!mapId) return; const opt = document.createElement('option'); opt.value = mapId; opt.textContent = displayName(mapId); sel.appendChild(opt); }); if (CURRENT_MAP_ID) { const exists = Array.from(sel.options).some(o => o.value === CURRENT_MAP_ID); if (exists) sel.value = CURRENT_MAP_ID; } } catch (err) { const opt = document.createElement('option'); opt.value = ''; opt.textContent = '加载失败'; sel.appendChild(opt); } }
function applyViewerMap() { const sel = document.getElementById('viewerMapSelect'); if (!sel) return; const v = sel.value; if (!v) return; if (CURRENT_MAP_ID !== v) { CURRENT_MAP_ID = v; } currentFloorIndex = 0; loadMapData(); }
function normalizeScene(scene) { if (!scene) return null; return JSON.parse(JSON.stringify(scene)); }
function populateFloorSelect() { const sel = document.getElementById('floorSelect'); if (!sel) return; sel.innerHTML = ''; const availableFloorCount = (Array.isArray(floorNames) && floorNames.length > 0) ? floorNames.length : mapLayers.length; if (!Array.isArray(mapLayers) || mapLayers.length === 0 || availableFloorCount === 0) { const opt = document.createElement('option'); opt.value = ''; opt.textContent = '无楼层'; sel.appendChild(opt); sel.disabled = true; return; } sel.disabled = false; for (let i = 0; i < availableFloorCount; i++) { const opt = document.createElement('option'); opt.value = String(i); const fname = floorNames[i]; opt.textContent = fname ? `${fname}` : `${i + 1}层`; sel.appendChild(opt); } sel.value = String(currentFloorIndex); }
function switchFloor(idx) { const i = parseInt(idx, 10); const availableFloorCount = (Array.isArray(floorNames) && floorNames.length > 0) ? floorNames.length : mapLayers.length; if (isNaN(i) || i < 0 || i >= availableFloorCount) return; currentFloorIndex = i; const layerIndex = Math.min(currentFloorIndex, Math.max(0, mapLayers.length - 1)); mapData = normalizeScene(mapLayers[layerIndex]); loadSceneStations(); updateMapInfo(); fitMapToView(); drawMap(); const fname = floorNames[currentFloorIndex]; document.getElementById('status').textContent = fname ? `已切换到楼层: ${fname}` : `已切换到${i + 1}层`; }
function cycleFloor() { const availableFloorCount = (Array.isArray(floorNames) && floorNames.length > 0) ? floorNames.length : mapLayers.length; if (availableFloorCount === 0) return; const next = (currentFloorIndex + 1) % availableFloorCount; switchFloor(next); const sel = document.getElementById('floorSelect'); if (sel) sel.value = String(next); }
function updateMapInfo() { const pc = mapData?.points?.length || 0; const rc = mapData?.routes?.length || 0; document.getElementById('pointCount').textContent = pc; document.getElementById('routeCount').textContent = rc; updateInitialPositionOptions(); }
function updateInitialPositionOptions() { const regMapSel = document.getElementById('registerMapSelect'); const chosen = regMapSel ? regMapSel.value : ''; if (regMapSel && /(^|\/)VehicleMap\/[^\/]+\.scene$/.test(chosen)) { updateRegisterInitialPositionOptions(); return; } const sel = document.getElementById('initialPosition'); if (!sel) return; sel.innerHTML = '<option value="">选择初始位置</option>'; (mapData?.points || []).forEach(p => { if (p.name) { const opt = document.createElement('option'); opt.value = p.id; opt.textContent = `${p.name} (${p.x.toFixed(2)}, ${p.y.toFixed(2)})`; sel.appendChild(opt); } }); sel.disabled = false; }
function fitMapToView() { if (!mapData || !mapData.points || mapData.points.length === 0) return; let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity; mapData.points.forEach(p => { const sx = p.x * 20, sy = p.y * 20; minX = Math.min(minX, sx); minY = Math.min(minY, sy); maxX = Math.max(maxX, sx); maxY = Math.max(maxY, sy); }); const mapW = maxX - minX, mapH = maxY - minY, cx = (minX + maxX) / 2, cy = (minY + maxY) / 2; const pad = 50; const safeW = Math.max(1e-6, mapW); const safeH = Math.max(1e-6, mapH); const sX = (canvas.width - pad * 2) / safeW; const sY = (canvas.height - pad * 2) / safeH; viewTransform.scale = Math.max(0.1, Math.min(5, Math.min(sX, sY))); viewTransform.x = canvas.width / 2 - cx * viewTransform.scale; viewTransform.y = canvas.height / 2 + cy * viewTransform.scale; updateZoomDisplay(); }
function updateZoomDisplay() { document.getElementById('zoomLevel').textContent = Math.round(viewTransform.scale * 100) + '%'; }
function drawMap() { if (!mapData) return; ctx.clearRect(0, 0, canvas.width, canvas.height); if (showGrid) drawGrid(); if (mapData.routes) drawRoutes(); if (mapData.points) drawPoints(); if (registeredRobots.length > 0) drawRobots(); try { updateDoorOverlayPositions(); } catch (e) { } }
function drawGrid() { ctx.save(); ctx.strokeStyle = '#34495e'; ctx.lineWidth = 1; const g = 50 * viewTransform.scale; const ox = viewTransform.x % g, oy = viewTransform.y % g; for (let x = ox; x < canvas.width; x += g) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, canvas.height); ctx.stroke(); } for (let y = oy; y < canvas.height; y += g) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(canvas.width, y); ctx.stroke(); } ctx.restore(); }
function drawPoints() { (mapData.points || []).forEach(point => { const currentFloorName = floorNames[currentFloorIndex] || null; const pfloor = extractFloorNameFromLayer(point.layer || ''); if (currentFloorName && pfloor && String(pfloor).trim().toLowerCase() !== String(currentFloorName).trim().toLowerCase()) { return; } const sp = worldToScreen(point.x, point.y); if (sp.x < -20 || sp.x > canvas.width + 20 || sp.y < -20 || sp.y > canvas.height + 20) return; ctx.save(); let color = '#95a5a6'; let size = pointDrawSize(); const t = Number(point.type); if (!Number.isNaN(t) && POINT_TYPE_INFO[t]) { color = POINT_TYPE_INFO[t].color; } else { const nm = point.name || ''; if (nm.startsWith('AP')) { color = '#e74c3c'; } else if (nm.startsWith('LM')) { color = '#f39c12'; } else if (nm.startsWith('CP')) { color = '#27ae60'; } } ctx.fillStyle = color; ctx.beginPath(); ctx.arc(sp.x, sp.y, size, 0, 2 * Math.PI); ctx.fill(); ctx.strokeStyle = '#2c3e50'; ctx.lineWidth = 2; ctx.stroke(); const name = point.name || ''; const key = name || point.id; let isSelected = !!selectedStationId && key === selectedStationId; if (!isSelected && selectedNavStation && selectedNavStation.pos) { const hitRadius = pointDrawSize() + 2; const spSel = worldToScreen(selectedNavStation.pos.x, selectedNavStation.pos.y); const dxs = sp.x - spSel.x, dys = sp.y - spSel.y; if (dxs * dxs + dys * dys <= (hitRadius + 4) * (hitRadius + 4)) { isSelected = true; } } if (isSelected) { ctx.strokeStyle = '#f1c40f'; ctx.lineWidth = 3; ctx.beginPath(); ctx.arc(sp.x, sp.y, size + 4, 0, 2 * Math.PI); ctx.stroke(); } if (viewTransform.scale > 0.5 && name) { ctx.fillStyle = '#ecf0f1'; ctx.font = '12px Arial'; ctx.textAlign = 'center'; ctx.fillText(name, sp.x, sp.y - size - 6); } ctx.restore(); }); }
function drawRoutes() { if (!mapData || !mapData.points) return; const pmap = {}; mapData.points.forEach(p => pmap[p.id] = p); (mapData.routes || []).forEach(route => { const fp = pmap[route.from], tp = pmap[route.to]; if (!fp || !tp) return; const dir = getRouteDirection(route); const currentFloorName = floorNames[currentFloorIndex] || null; if (currentFloorName) { const nf = String(currentFloorName).trim().toLowerCase(); const ff = extractFloorNameFromLayer(fp.layer || ''); const tf = extractFloorNameFromLayer(tp.layer || ''); const ffn = ff ? String(ff).trim().toLowerCase() : ''; const tfn = tf ? String(tf).trim().toLowerCase() : ''; if (ffn && ffn !== nf) return; if (tfn && tfn !== nf) return; } ctx.save(); const passCode = (route.pass !== undefined) ? Number(route.pass) : 0; const baseColor = getRouteColorByPass(passCode); ctx.strokeStyle = baseColor; ctx.lineWidth = 2; ctx.lineCap = 'round'; if (passCode === 10) { ctx.setLineDash([6, 4]); } else { ctx.setLineDash([]); } const rid = String(route.id ?? (`${route.from}->${route.to}`)); const isSelected = !!selectedRouteId && String(selectedRouteId) === rid; if (isSelected) { ctx.lineWidth = 3; } if (route.type === 'bezier3' && route.c1 && route.c2) { const p0 = { x: fp.x, y: fp.y }; const p1 = { x: route.c1.x, y: route.c1.y }; const p2 = { x: route.c2.x, y: route.c2.y }; const p3 = { x: tp.x, y: tp.y }; const fromScreen = worldToScreen(p0.x, p0.y); const c1Screen = worldToScreen(p1.x, p1.y); const c2Screen = worldToScreen(p2.x, p2.y); const toScreen = worldToScreen(p3.x, p3.y); ctx.beginPath(); ctx.moveTo(fromScreen.x, fromScreen.y); ctx.bezierCurveTo(c1Screen.x, c1Screen.y, c2Screen.x, c2Screen.y, toScreen.x, toScreen.y); ctx.stroke(); if (viewTransform.scale > 0.3) { if (dir !== 0) { const t = 0.7; const q0 = dir > 0 ? p0 : p3; const q1 = dir > 0 ? p1 : p2; const q2 = dir > 0 ? p2 : p1; const q3 = dir > 0 ? p3 : p0; const pt = bezierPoint(q0, q1, q2, q3, t); const tg = bezierTangent(q0, q1, q2, q3, t); const ptScreen = worldToScreen(pt.x, pt.y); const tgScreen = { x: worldToScreen(q0.x + tg.x, q0.y + tg.y).x - worldToScreen(q0.x, q0.y).x, y: worldToScreen(q0.x + tg.y, q0.y + tg.y).y - worldToScreen(q0.x, q0.y).y }; const angle = Math.atan2(tgScreen.y, tgScreen.x); drawArrowAt(ptScreen.x, ptScreen.y, angle); } else { const t1 = 0.3; const pt1 = bezierPoint(p0, p1, p2, p3, t1); const tg1 = bezierTangent(p0, p1, p2, p3, t1); const pt1Screen = worldToScreen(pt1.x, pt1.y); const tg1Screen = { x: worldToScreen(p0.x + tg1.x, p0.y + tg1.y).x - worldToScreen(p0.x, p0.y).x, y: worldToScreen(p0.x + tg1.y, p0.y + tg1.y).y - worldToScreen(p0.x, p0.y).y }; drawArrowAt(pt1Screen.x, pt1Screen.y, Math.atan2(tg1Screen.y, tg1Screen.x)); const r0 = p3, r1 = p2, r2 = p1, r3 = p0; const pt2 = bezierPoint(r0, r1, r2, r3, t1); const tg2 = bezierTangent(r0, r1, r2, r3, t1); const pt2Screen = worldToScreen(pt2.x, pt2.y); const tg2Screen = { x: worldToScreen(r0.x + tg2.x, r0.y + tg2.y).x - worldToScreen(r0.x, r0.y).x, y: worldToScreen(r0.x + tg2.y, r0.y + tg2.y).y - worldToScreen(r0.x, r0.y).y }; drawArrowAt(pt2Screen.x, pt2Screen.y, Math.atan2(tg2Screen.y, tg2Screen.x)); } } } else { const fromScreen = worldToScreen(fp.x, fp.y); const toScreen = worldToScreen(tp.x, tp.y); ctx.beginPath(); ctx.moveTo(fromScreen.x, fromScreen.y); ctx.lineTo(toScreen.x, toScreen.y); ctx.stroke(); if (viewTransform.scale > 0.3) { if (dir === 0) { drawArrow(fromScreen, toScreen); drawArrow(toScreen, fromScreen); } else { const start = dir > 0 ? fromScreen : toScreen; const end = dir > 0 ? toScreen : fromScreen; drawArrow(start, end); } } } ctx.restore(); }); }
function drawRobots() {
  registeredRobots.forEach(robot => {
    if (robot.currentMap) {
      const robotFloor = extractFloorFromMapId(robot.currentMap);
      const currentFloorName = floorNames[currentFloorIndex] || null;
      if (robotFloor && currentFloorName && String(robotFloor).trim().toLowerCase() !== String(currentFloorName).trim().toLowerCase()) {
        // Hide DOM element if floor doesn't match
        const existingEl = document.getElementById('sim-agv-' + robot.robot_name);
        if (existingEl) existingEl.style.display = 'none';
        return;
      }
    }
    let pos = robot.currentPosition || robot.initialPosition || null;
    if (!pos) return;
    const sp = worldToScreen(pos.x, pos.y);
    
    // Check bounds (with some margin for DOM element size)
    const margin = 100; 
    const inBounds = (sp.x >= -margin && sp.x <= canvas.width + margin && sp.y >= -margin && sp.y <= canvas.height + margin);

    let agvEl = document.getElementById('sim-agv-' + robot.robot_name);
    if (!agvEl) {
      agvEl = document.createElement('sim-agv');
      agvEl.id = 'sim-agv-' + robot.robot_name;
      agvEl.style.position = 'absolute';
      agvEl.style.zIndex = '100'; // Ensure it's on top of map elements
      agvEl.style.pointerEvents = 'none'; // Let clicks pass through to canvas for selection
      agvEl.style.transformOrigin = 'center center';
      // Append to a container overlaid on canvas. 
      // If no specific overlay container exists, we can use the mapCanvas parent or create one.
      // Assuming 'canvasContainer' or similar exists or we append to body/canvas parent.
      const container = document.getElementById('canvasContainer') || canvas.parentNode;
      // Ensure container has relative positioning
      if (getComputedStyle(container).position === 'static') container.style.position = 'relative';
      container.appendChild(agvEl);
      
      // Initialize config if available
      const defaults = { width: 0.745, length: 1.03 };
      
      let w = defaults.width;
      let l = defaults.length;
      
      if (robot && robot.vehicleDimensions) {
          w = Number(robot.vehicleDimensions.width);
          l = Number(robot.vehicleDimensions.length);
      } else if (robot && robot.collisionParams) {
          w = Number(robot.collisionParams.width);
          l = Number(robot.collisionParams.length);
      }
      
      // We might need to map meters to pixels or pass meters if component handles scaling
      // Component setConfig expects config object. 
      // Let's pass raw meters and let component/update handle scaling or we scale here.
      // Actually, the component styling is fixed in CSS mostly. We might need to scale it via transform.
      // Let's pass the dimensions for potential internal use.
      if (agvEl.setConfig) agvEl.setConfig({ width: w, length: l, serial_number: robot.robot_name });
    }

    if (!inBounds) {
      agvEl.style.display = 'none';
      return;
    }

    agvEl.style.display = 'block';
    
    // Calculate scale based on viewTransform
    // 1 meter = 20 world units? No, screenToWorld uses /20.
    // worldToScreen: (wx * 20) * scale
    // So 1 meter in world coords = 1.0? usually.
    // Let's check drawRobotIcon: meterScale = 20.
    // So 1 meter = 20 pixels at scale 1.
    // The CSS for sim-agv is likely fixed pixel size (e.g. 120px).
    // We need to scale the DOM element to match the map zoom.
    
    const meterScale = 20;
    const s = viewTransform.scale;
    
    // Get robot dimensions to calculate scale factor for the DOM element
    // Default CSS size of sim-agv is roughly based on... let's assume it represents a standard AGV.
    // If agv.css defines specific px dimensions (e.g. width: 120px, height: 160px), 
    // we should scale it so that it matches the robot's actual dimensions in meters on screen.
    
    const defaults = { width: 0.745, length: 1.03 };
    
    let finalW = defaults.width;
    let finalL = defaults.length;
    
    if (robot && robot.vehicleDimensions) {
        finalW = Number(robot.vehicleDimensions.width);
        finalL = Number(robot.vehicleDimensions.length);
    } else if (robot && robot.collisionParams) {
        finalW = Number(robot.collisionParams.width);
        finalL = Number(robot.collisionParams.length);
    }

    // SimAGV model is horizontal (facing Right). 
    // Length corresponds to CSS width (X-axis).
    // Width corresponds to CSS height (Y-axis).
    
    const cssWidth = finalL * meterScale * s;
    const cssHeight = finalW * meterScale * s;
    
    agvEl.style.width = cssWidth + 'px';
    agvEl.style.height = cssHeight + 'px';
    
    // Position (Center)
    agvEl.style.left = (sp.x - cssWidth / 2) + 'px';
    agvEl.style.top = (sp.y - cssHeight / 2) + 'px';
    
    // Rotation
    const theta = Number(pos.theta ?? pos.orientation ?? 0);
    const cssRotDeg = ((-theta * 180) / Math.PI) - 180;
    if (typeof agvEl.setTheta === 'function') {
       agvEl.setTheta(theta);
       agvEl.style.transform = 'none';
    } else if (typeof agvEl.setRotation === 'function') {
       agvEl.setRotation(cssRotDeg);
       agvEl.style.transform = 'none';
    } else {
       agvEl.style.transform = `rotate(${cssRotDeg}deg)`;
    }

    // Adjust aspect ratio via CSS variable if needed or just rely on width/height
    // SimAGV CSS has aspect-ratio: 2 / 1; which might conflict with our explicit width/height.
    // We should override aspect-ratio or ensure the container follows our size.
    agvEl.style.aspectRatio = 'auto'; // Override CSS

    
    // Sync internal state (like tray rotation)
    if (agvEl.setRotation) {
       // Use internal method if it handles more specific logic, 
       // but here we are rotating the whole 'vehicle' in the world.
       // The component's setRotation might be for visual wheels/steering?
       // Let's use the component's method if it's intended for 'steering' 
       // vs 'body orientation'.
       // Actually, for a simple top-down view, transforming the container is safest.
    }
    
    // Handle Tray/Pallet state
    // robot.hasPallet
    // The component has a tray. We might want to show it loaded.
    // SimAGV CSS: .tray background... maybe change color or add 'loaded' class?
    if (robot.hasPallet) {
      agvEl.classList.add('has-pallet');
    } else {
      agvEl.classList.remove('has-pallet');
    }

    // 货架渲染与淡入淡出：使用 loads 或缓存的 shelfModel footprint
    try {
      const loads = robot && robot._latestLoadsArray ? robot._latestLoadsArray : null;
      const hasLoads = Array.isArray(loads) ? loads.length > 0 : false;
      const isUnloading = !!robot._palletAnimUnloading;
      try {
        let trayTheta = 0;
        if (hasLoads) {
          const l0 = loads[0] || {};
          const bbox = l0.boundingBoxReference || l0.bounding_box_reference || {};
          const t = Number(bbox && bbox.theta);
          trayTheta = isFinite(t) ? t : 0;
        }
        if (typeof agvEl.setTrayTheta === 'function') {
          agvEl.setTrayTheta(-trayTheta);
        }
      } catch (_) {}
      let wm = NaN, lm = NaN;
      if (hasLoads) {
        const l0 = loads[0] || {};
        const dim = l0.loadDimensions || l0.load_dimensions || {};
        wm = Number(dim && (dim.width_m ?? dim.width));
        lm = Number(dim && (dim.length_m ?? dim.length));
      } else if (robot && robot.shelfModel && robot.shelfModel.footprint) {
        const fp = robot.shelfModel.footprint;
        wm = Number(fp && (fp.width_m ?? fp.width));
        lm = Number(fp && (fp.length_m ?? fp.length));
      }
      const defaults = { width: 0.745, length: 1.03 };
      const vw = Number(robot?.vehicleDimensions?.width ?? robot?.collisionParams?.width ?? defaults.width);
      const vl = Number(robot?.vehicleDimensions?.length ?? robot?.collisionParams?.length ?? defaults.length);
      const canRenderShelf = isFinite(wm) && isFinite(lm) && agvEl.setShelfDimensions;
      if (canRenderShelf) {
        agvEl.setShelfDimensions(wm, lm, vw, vl);
      }
      // 计算淡入淡出比例
      let fadeScale = 0;
      if (robot._palletAnimLoading) {
        fadeScale = Math.max(0, Math.min(1, Number(robot._palletAnimProgress || 0)));
      } else if (robot._palletAnimUnloading) {
        fadeScale = 1 - Math.max(0, Math.min(1, Number(robot._palletAnimProgress || 0)));
      } else {
        fadeScale = (hasLoads || robot.hasPallet || canRenderShelf) ? 1 : 0;
      }
      if (typeof agvEl.setShelfOpacityScale === 'function') {
        agvEl.setShelfOpacityScale(fadeScale);
      }
      // 非动画且无尺寸数据时清理
      if (!robot._palletAnimLoading && !robot._palletAnimUnloading && !canRenderShelf) {
        if (typeof agvEl.clearShelf === 'function') agvEl.clearShelf();
      }
    } catch (_) {}
    
    // Keep Collision Overlay (optional, maybe keep drawing it on canvas for debug/safety view)
    try {
       try { const errs = Array.isArray(robot?.stateErrors) ? robot.stateErrors : []; const blocked = errs.some(e => { const en = String((e && (e.errorName ?? e.error_name ?? e.code)) || '').trim().toLowerCase(); const ds = String((e && (e.errorDescription ?? e.message ?? e.reason)) || '').trim().toLowerCase(); return en === '54231' || en === '54330' || en === 'robotblocked' || ds.includes('robot is blocked') || ds.includes('radar blocked'); }); robot._collisionActive = blocked; } catch (_) {}
       drawCollisionOverlayV2(sp.x, sp.y, theta, robot);
    } catch (_) {}
    
  });
  
  // Cleanup: Remove DOM elements for robots that are no longer registered
  const activeIds = new Set(registeredRobots.map(r => 'sim-agv-' + r.robot_name));
  const allAgvs = document.querySelectorAll('sim-agv');
  allAgvs.forEach(el => {
    if (!activeIds.has(el.id)) {
      el.remove();
    }
  });
}
function drawCollisionOverlayV2(cx, cy, thetaRad, robot) {
  const meterScale = 20;
  const s = viewTransform.scale;
  const r = robot || {};
  const radar = r && r.radar ? r.radar : null;
  try {
    if (robot && robot.safety) {
      const c = robot.safety.center || { x: cx, y: cy };
      const ln = Number(robot.safety.length ?? 0);
      const wd = Number(robot.safety.width ?? 0);
      const th = Number(robot.safety.theta ?? thetaRad);
      const sp = worldToScreen(c.x, c.y);
      ctx.save();
      ctx.translate(sp.x, sp.y);
      ctx.rotate(-th - Math.PI / 2);
      const rwPx = wd * meterScale * s;
      const rlPx = ln * meterScale * s;
      ctx.globalAlpha = 0.10;
      ctx.fillStyle = '#9b59b6';
      ctx.strokeStyle = '#8e44ad';
      ctx.lineWidth = Math.max(1, s);
      ctx.beginPath();
      ctx.moveTo(-rwPx / 2, rlPx / 2);
      ctx.lineTo(rwPx / 2, rlPx / 2);
      ctx.lineTo(rwPx / 2, -rlPx / 2);
      ctx.lineTo(-rwPx / 2, -rlPx / 2);
      ctx.closePath();
      ctx.fill();
      ctx.stroke();
      ctx.restore();
    }
  } catch (_) {}
  try {
    if (radar && radar.origin) {
      const o = radar.origin;
      const fov = Number(radar.fovDeg ?? radar.fov_deg ?? 60);
      const radius = Number(radar.radius ?? 0.8);
      const th = Number(radar.theta ?? thetaRad);
      const isBlocked = (() => {
        try {
          const errs = (r && r.stateErrors) || [];
          return errs.some(e => {
            const en = String((e && (e.errorName ?? e.error_name ?? e.code)) || '').trim().toLowerCase();
            const ds = String((e && (e.errorDescription ?? e.message ?? e.reason)) || '').trim().toLowerCase();
            return en === '54231' || en === '54330' || en === 'robotblocked' || ds.includes('robot is blocked') || ds.includes('radar blocked');
          });
        } catch (_) {
          return false;
        }
      })();
      const sp = worldToScreen(o.x, o.y);
      ctx.save();
      ctx.translate(sp.x, sp.y);
      ctx.rotate(-th - Math.PI / 2);
      const half = (fov * Math.PI / 180) / 2;
      const radiusPx = radius * meterScale * s;
      for (let i = 0; i < 3; i++) {
        const alpha = 0.12 - i * 0.03;
        const expand = i * (radiusPx * 0.08);
        ctx.beginPath();
        ctx.moveTo(0, 0);
        ctx.arc(0, 0, radiusPx + expand, -Math.PI / 2 - half, -Math.PI / 2 + half);
        ctx.closePath();
        ctx.globalAlpha = Math.max(0.02, alpha);
        ctx.fillStyle = isBlocked ? '#e74c3c' : '#1abc9c';
        ctx.fill();
        ctx.strokeStyle = isBlocked ? '#c0392b' : '#16a085';
        ctx.lineWidth = Math.max(1, s * 0.8);
        ctx.stroke();
      }
      ctx.restore();
    }
  } catch (_) {}
  try {
    const ov = (r && r.overlaps) || null;
    if (ov && Array.isArray(ov.radar)) {
      for (const item of ov.radar) {
        const pts = Array.isArray(item.points) ? item.points : [];
        if (pts.length >= 3) {
          ctx.save();
          ctx.beginPath();
          const p0 = worldToScreen(pts[0].x, pts[0].y);
          ctx.moveTo(p0.x, p0.y);
          for (let i = 1; i < pts.length; i++) {
            const pi = worldToScreen(pts[i].x, pts[i].y);
            ctx.lineTo(pi.x, pi.y);
          }
          ctx.closePath();
          ctx.globalAlpha = 0.18;
          ctx.fillStyle = '#f39c12';
          ctx.fill();
          ctx.strokeStyle = '#d35400';
          ctx.lineWidth = Math.max(1, s * 0.8);
          ctx.stroke();
          ctx.restore();
        }
      }
    }
    if (ov && Array.isArray(ov.safety)) {
      for (const item of ov.safety) {
        const pts = Array.isArray(item.points) ? item.points : [];
        if (pts.length >= 3) {
          ctx.save();
          ctx.beginPath();
          const p0 = worldToScreen(pts[0].x, pts[0].y);
          ctx.moveTo(p0.x, p0.y);
          for (let i = 1; i < pts.length; i++) {
            const pi = worldToScreen(pts[i].x, pts[i].y);
            ctx.lineTo(pi.x, pi.y);
          }
          ctx.closePath();
          ctx.globalAlpha = 0.20;
          ctx.fillStyle = '#e74c3c';
          ctx.fill();
          ctx.strokeStyle = '#c0392b';
          ctx.lineWidth = Math.max(1, s * 0.8);
          ctx.stroke();
          ctx.restore();
        }
      }
    }
  } catch (_) {}
}
function extractFloorNameFromLayer(layer) { try { if (typeof layer === 'string') { const v = layer.trim(); if (v) return v; } if (layer && Array.isArray(layer.list)) { for (const item of layer.list) { if (item && typeof item.name === 'string') { const v = item.name.trim(); if (v) return v; } } } if (layer && typeof layer.name === 'string' && layer.name.trim() !== '') { return layer.name.trim(); } } catch (e) { } return null; }
function extractFloorFromMapId(mapId) { const raw = String(mapId || '').trim(); if (!raw) return null; const mScene = /^VehicleMap\/(.+)\.scene$/i.exec(raw); if (mScene) return mScene[1]; const mViewer = /^ViewerMap\/(.+)\.scene$/i.exec(raw); if (mViewer) return mViewer[1]; const fname = raw.replace(/\\/g, '/').split('/').pop(); return (fname || '').replace(/\.scene$/i, '') || null; }
function normalizeEquipMapIdList(mapId) { try { const list = Array.isArray(mapId) ? mapId : (typeof mapId !== 'undefined' && mapId !== null ? [mapId] : []); const out = []; for (const v of list) { const floor = extractFloorFromMapId(v); if (floor) out.push(String(floor).trim().toLowerCase()); } return Array.from(new Set(out)).filter(Boolean); } catch (_) { return []; } }
function isEquipVisibleOnCurrentMap(eq) { const currentFloorName = floorNames[currentFloorIndex] || null; if (!currentFloorName) return true; const allowed = normalizeEquipMapIdList(eq && eq.map_id); if (!Array.isArray(allowed) || allowed.length === 0) return true; return allowed.includes(String(currentFloorName).trim().toLowerCase()); }
function drawRobotIcon(x, y, rotationDeg, robot) { ctx.save(); ctx.translate(x, y); ctx.rotate(-rotationDeg - Math.PI / 2); const s = viewTransform.scale; const meterScale = 20; const defaults = { width: 0.745, length: 1.03 };
    let w = defaults.width;
    let l = defaults.length;
    if (robot && robot.vehicleDimensions) {
        w = Number(robot.vehicleDimensions.width);
        l = Number(robot.vehicleDimensions.length);
    } else if (robot && robot.collisionParams) {
        w = Number(robot.collisionParams.width);
        l = Number(robot.collisionParams.length);
    }
    const wPx = w * meterScale * s; const lRectPx = Math.max(2, (l - (w / 2)) * meterScale * s); ctx.fillStyle = '#4A90E2'; ctx.strokeStyle = '#2E5C8A'; ctx.lineWidth = 1; ctx.fillRect(-wPx / 2, -lRectPx / 2, wPx, lRectPx); ctx.strokeRect(-wPx / 2, -lRectPx / 2, wPx, lRectPx); ctx.fillStyle = '#6BB6FF'; ctx.beginPath(); ctx.arc(0, -lRectPx / 2, Math.max(2, wPx / 2), 0, Math.PI, true); ctx.closePath(); ctx.fill(); ctx.stroke(); ctx.fillStyle = '#FF6B6B'; ctx.strokeStyle = '#D63031'; const arrowLen = Math.max(8, (lRectPx + wPx / 2) * 0.12); const arrowHalf = Math.max(4, wPx * 0.18); ctx.beginPath(); ctx.moveTo(0, -lRectPx / 2 - (wPx / 2) - arrowLen); ctx.lineTo(-arrowHalf, -lRectPx / 2 - (wPx / 2)); ctx.lineTo(arrowHalf, -lRectPx / 2 - (wPx / 2)); ctx.closePath(); ctx.fill(); ctx.stroke(); const fp = robot.shelfModel && robot.shelfModel.footprint; const wm = Number(fp && (fp.width_m ?? fp.width)); const lm = Number(fp && (fp.length_m ?? fp.length)); const palletW = Math.max(2, (isFinite(wm) && wm > 0 ? wm : 1.0) * meterScale * s); const palletL = Math.max(2, (isFinite(lm) && lm > 0 ? lm : 1.2) * meterScale * s); const shouldDrawPallet = !!robot.hasPallet || !!robot._palletAnimLoading || !!robot._palletAnimUnloading; if (shouldDrawPallet) { let offsetX = 0; let offsetY = 0; try { const pos = robot.currentPosition || robot.initialPosition || null; const saf = robot.safety || null; if (pos && saf && saf.center && typeof saf.center.x === 'number' && typeof saf.center.y === 'number') { const dx = Number(saf.center.x) - Number(pos.x); const dy = Number(saf.center.y) - Number(pos.y); const rad = Number(pos.theta || 0); const localX = dx * (-Math.sin(rad)) + dy * (Math.cos(rad)); const localY = dx * (-Math.cos(rad)) + dy * (-Math.sin(rad)); offsetX = localX * meterScale * s; offsetY = localY * meterScale * s; } } catch (_) { } let alpha = 1.0; let slide = 0; if (robot._palletAnimLoading) { const p = Math.max(0, Math.min(1, robot._palletAnimProgress || 0)); alpha = p; slide = (1 - p) * 6; } else if (robot._palletAnimUnloading) { const p = Math.max(0, Math.min(1, robot._palletAnimProgress || 0)); alpha = 1 - p; slide = p * 6; } let trayTheta = 0; try { const loads = robot && robot._latestLoadsArray ? robot._latestLoadsArray : null; const hasLoads = Array.isArray(loads) ? loads.length > 0 : false; if (hasLoads) { const l0 = loads[0] || {}; const bbox = l0.boundingBoxReference || l0.bounding_box_reference || {}; const t = Number(bbox && bbox.theta); trayTheta = isFinite(t) ? t : 0; } } catch (_) { trayTheta = 0; } const trayRot = -trayTheta; ctx.save(); ctx.globalAlpha = alpha; ctx.fillStyle = '#8B5E3C'; ctx.strokeStyle = '#5D3A1A'; ctx.lineWidth = 1; ctx.translate(-offsetX, slide - offsetY); ctx.rotate(trayRot); ctx.fillRect(-palletW / 2, -palletL / 2, palletW, palletL); ctx.strokeRect(-palletW / 2, -palletL / 2, palletW, palletL); ctx.strokeStyle = '#704A2B'; for (let i = 1; i <= 2; i++) { const y = -palletL / 2 + (i * palletL / 3); ctx.beginPath(); ctx.moveTo(-palletW / 2 + 2, y); ctx.lineTo(palletW / 2 - 2, y); ctx.stroke(); } ctx.restore(); } ctx.restore(); if (viewTransform.scale > 0.5 && robot.robot_name) { ctx.save(); ctx.fillStyle = '#ecf0f1'; ctx.font = '12px Arial'; ctx.textAlign = 'center'; ctx.strokeStyle = '#2c3e50'; ctx.lineWidth = 3; ctx.strokeText(robot.robot_name, x, y - 5); ctx.fillText(robot.robot_name, x, y - 5); ctx.restore(); } }

function applyWSVisualization(visMsg) { try { const payload = visMsg || {}; const sn = String(payload.serial_number || payload.serialNumber || '').trim(); if (!sn) return; const robot = registeredRobots.find(r => r.robot_name === sn); if (!robot) return; try { const saf = payload.safety || null; if (saf && saf.center && typeof saf.length === 'number' && typeof saf.width === 'number') { robot.safety = { center: { x: Number(saf.center.x), y: Number(saf.center.y) }, length: Number(saf.length), width: Number(saf.width), theta: Number(saf.theta ?? (robot.currentPosition?.theta ?? 0)) }; } } catch (_) { } try { const radar = payload.radar || null; if (radar && radar.origin && typeof radar.radius === 'number') { robot.radar = { origin: { x: Number(radar.origin.x), y: Number(radar.origin.y) }, fovDeg: Number(radar.fovDeg ?? radar.fov_deg ?? 60), radius: Number(radar.radius ?? 0.8), theta: Number(radar.theta ?? (robot.currentPosition?.theta ?? 0)) }; } } catch (_) { } try { const ov = payload.overlaps || null; if (ov && typeof ov === 'object') { const rad = Array.isArray(ov.radar) ? ov.radar : []; const saf = Array.isArray(ov.safety) ? ov.safety : []; const readWith = (it) => { const w = String(it && it.with ? it.with : '').trim(); if (w) return w; const manu = String(it && (it.manufacturer || it.manu || '') ? (it.manufacturer || it.manu) : '').trim(); const sn2 = String(it && (it.serialNumber || it.serial_number || '') ? (it.serialNumber || it.serial_number) : '').trim(); if (manu && sn2) return manu + '/' + sn2; if (sn2) return sn2; return ''; }; robot.overlaps = { radar: rad.map(it => ({ with: readWith(it), points: (Array.isArray(it.points) ? it.points : []).map(p => ({ x: Number(p.x), y: Number(p.y) })) })), safety: saf.map(it => ({ with: readWith(it), points: (Array.isArray(it.points) ? it.points : []).map(p => ({ x: Number(p.x), y: Number(p.y) })) })) }; } else { robot.overlaps = { radar: [], safety: [] }; } } catch (_) { } try { reportVisualizationHazards(robot); } catch (_) { } } catch (e) { } }

function postVisualizationEvent(serialNumber, eventType, details) { try { const sn = String(serialNumber || '').trim(); if (!sn) return; const url = `${API_BASE_URL}/agv/${encodeURIComponent(sn)}/visualization/events`; const payload = { serial_number: sn, event_type: String(eventType || ''), timestamp_ms: Date.now(), details: (details && typeof details === 'object') ? details : {} }; if (typeof navigator !== 'undefined' && navigator && typeof navigator.sendBeacon === 'function') { try { const blob = new Blob([JSON.stringify(payload)], { type: 'application/json' }); const ok = navigator.sendBeacon(url, blob); if (ok) return; } catch (_) { } } try { fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload), keepalive: true }).catch(() => { }); } catch (_) { } } catch (_) { } }

function reportVisualizationHazards(robot) { try { const r = robot || {}; const sn = String(r.robot_name || r.serial_number || '').trim(); if (!sn) return; const ov = r.overlaps || {}; const radarItems = Array.isArray(ov.radar) ? ov.radar : []; const safetyItems = Array.isArray(ov.safety) ? ov.safety : []; const isBlocked = !!r._collisionActive; const hasRadar = radarItems.length > 0; const hasSafety = safetyItems.length > 0; const state = (r._visHazardReport && typeof r._visHazardReport === 'object') ? r._visHazardReport : { blocked: false, radar: false, safety: false, lastKey: '', lastTs: 0 }; const now = Date.now(); const gapMs = 800; const shouldSend = (key) => { try { if (!key) return false; if (String(state.lastKey || '') !== String(key)) return true; return (now - Number(state.lastTs || 0)) > gapMs; } catch (_) { return true; } }; if (isBlocked && !state.blocked) { const key = 'blocked'; if (shouldSend(key)) { postVisualizationEvent(sn, 'blocked', { source: 'state', errors: Array.isArray(r.stateErrors) ? r.stateErrors : [], position: r.currentPosition || null, map_id: r.currentMap || null }); state.lastKey = key; state.lastTs = now; } state.blocked = true; } else if (!isBlocked) { state.blocked = false; } if (hasSafety && !state.safety) { const withIds = Array.from(new Set(safetyItems.map(it => String(it && it.with ? it.with : '')).filter(Boolean))); const key = 'safety_overlap:' + withIds.join(','); if (shouldSend(key)) { postVisualizationEvent(sn, 'collision', { source: 'visualization', kind: 'safety_overlap', with: withIds, count: safetyItems.length, position: r.currentPosition || null, map_id: r.currentMap || null }); state.lastKey = key; state.lastTs = now; } state.safety = true; } else if (!hasSafety) { state.safety = false; } if (hasRadar && !state.radar) { const withIds = Array.from(new Set(radarItems.map(it => String(it && it.with ? it.with : '')).filter(Boolean))); const key = 'radar_overlap:' + withIds.join(','); if (shouldSend(key)) { postVisualizationEvent(sn, 'blocking', { source: 'visualization', kind: 'radar_overlap', with: withIds, count: radarItems.length, position: r.currentPosition || null, map_id: r.currentMap || null }); state.lastKey = key; state.lastTs = now; } state.radar = true; } else if (!hasRadar) { state.radar = false; } r._visHazardReport = state; } catch (_) { } }

function drawCollisionOverlay(cx, cy, thetaRad, robot) { const meterScale = 20; const s = viewTransform.scale; const r = robot || {}; const radar = r && r.radar ? r.radar : null; try { if (robot && robot.safety) { const c = robot.safety.center || { x: cx, y: cy }; const ln = Number(robot.safety.length ?? 0); const wd = Number(robot.safety.width ?? 0); const th = Number(robot.safety.theta ?? thetaRad); const sp = worldToScreen(c.x, c.y); ctx.save(); ctx.translate(sp.x, sp.y); ctx.rotate(-th - Math.PI / 2); const rwPx = wd * meterScale * s; const rlPx = ln * meterScale * s; ctx.globalAlpha = 0.10; ctx.fillStyle = '#9b59b6'; ctx.strokeStyle = '#8e44ad'; ctx.lineWidth = Math.max(1, s); ctx.beginPath(); ctx.moveTo(-rwPx / 2, rlPx / 2); ctx.lineTo(rwPx / 2, rlPx / 2); ctx.lineTo(rwPx / 2, -rlPx / 2); ctx.lineTo(-rwPx / 2, -rlPx / 2); ctx.closePath(); ctx.fill(); ctx.stroke(); ctx.restore(); } } catch (_) { } try { if (radar && radar.origin) { const o = radar.origin; const fov = Number(radar.fovDeg ?? radar.fov_deg ?? 60); const radius = Number(radar.radius ?? 0.8); const th = Number(radar.theta ?? thetaRad); const isBlocked = (() => { try { const errs = (r && r.stateErrors) || []; return errs.some(e => String(e.errorType) === '54231' || String(e.errorName).toLowerCase() === 'robotblocked'); } catch (_) { return false; } })(); const sp = worldToScreen(o.x, o.y); ctx.save(); ctx.translate(sp.x, sp.y); ctx.rotate(-th - Math.PI / 2); const half = (fov * Math.PI / 180) / 2; const radiusPx = radius * meterScale * s; for (let i = 0; i < 3; i++) { const alpha = 0.12 - i * 0.03; const expand = i * (radiusPx * 0.08); ctx.beginPath(); ctx.moveTo(0, 0); ctx.arc(0, 0, radiusPx + expand, -Math.PI / 2 - half, -Math.PI / 2 + half); ctx.closePath(); ctx.globalAlpha = Math.max(0.02, alpha); ctx.fillStyle = isBlocked ? '#e74c3c' : '#1abc9c'; ctx.fill(); ctx.strokeStyle = isBlocked ? '#c0392b' : '#16a085'; ctx.lineWidth = Math.max(1, s * 0.8); ctx.stroke(); } ctx.restore(); } } catch (_) { } try { const ov = (r && r.overlaps) || null; if (ov && Array.isArray(ov.radar)) { for (const item of ov.radar) { const pts = Array.isArray(item.points) ? item.points : []; if (pts.length >= 3) { ctx.save(); ctx.beginPath(); const p0 = worldToScreen(pts[0].x, pts[0].y); ctx.moveTo(p0.x, p0.y); for (let i = 1; i < pts.length; i++) { const pi = worldToScreen(pts[i].x, pts[i].y); ctx.lineTo(pi.x, pi.y); } ctx.closePath(); ctx.globalAlpha = 0.18; ctx.fillStyle = '#f39c12'; ctx.fill(); ctx.strokeStyle = '#d35400'; ctx.lineWidth = Math.max(1, s * 0.8); ctx.stroke(); ctx.restore(); } } } if (ov && Array.isArray(ov.safety)) { for (const item of ov.safety) { const pts = Array.isArray(item.points) ? item.points : []; if (pts.length >= 3) { ctx.save(); ctx.beginPath(); const p0 = worldToScreen(pts[0].x, pts[0].y); ctx.moveTo(p0.x, p0.y); for (let i = 1; i < pts.length; i++) { const pi = worldToScreen(pts[i].x, pts[i].y); ctx.lineTo(pi.x, pi.y); } ctx.closePath(); ctx.globalAlpha = 0.20; ctx.fillStyle = '#e74c3c'; ctx.fill(); ctx.strokeStyle = '#c0392b'; ctx.lineWidth = Math.max(1, s * 0.8); ctx.stroke(); ctx.restore(); } } } } catch (_) { } }

function toggleSidebar() { const s = document.getElementById('sidebar'), t = document.getElementById('sidebarToggle'), c = document.getElementById('canvasContainer'); sidebarOpen = !sidebarOpen; if (sidebarOpen) { s.classList.add('open'); c.classList.add('sidebar-open'); t.classList.add('open'); t.textContent = '收起'; } else { s.classList.remove('open'); c.classList.remove('sidebar-open'); t.classList.remove('open'); t.textContent = '打开'; } setTimeout(resizeCanvas, 400); }
function switchTab(name) { document.querySelectorAll('.tab-button').forEach(b => b.classList.remove('active')); document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active')); if (name === 'equip') { document.querySelector('.tab-button[onclick="switchTab(\'equip\')"]').classList.add('active'); document.getElementById('equipTab').classList.add('active'); loadEquipmentList(); } else { document.querySelector('.tab-button[onclick="switchTab(\'list\')"]').classList.add('active'); document.getElementById('listTab').classList.add('active'); } }
// We should try to fetch the vehicle dimensions from config to ensure we render correctly
  // This function is called after fetching agvs list.
  async function loadRobotList() { try { const resp = await fetch(`${API_BASE_URL}/agvs`); if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`); const agvs = await resp.json(); registeredRobots = (agvs || []).map(info => ({ robot_name: info.serial_number, type: info.type, ip: info.IP, manufacturer: info.manufacturer, vda_version: info.vda_version, battery: 100, initialPosition: null, currentPosition: null, currentMap: null, commConnected: false, instanceStatus: '停止', lastUpdateTs: 0, hasPallet: false, forkHeight: 0, operatingMode: 'AUTOMATIC' })); 
  try { const hot = window.HOT_SIM_INIT; if (hot && hot.pose && isFinite(Number(hot.pose.x)) && isFinite(Number(hot.pose.y)) && isFinite(Number(hot.pose.theta))) { for (const r of registeredRobots) { if (r && !r.currentPosition && !r.initialPosition) { r.initialPosition = { x: Number(hot.pose.x), y: Number(hot.pose.y), theta: Number(hot.pose.theta) }; r.currentPosition = { x: Number(hot.pose.x), y: Number(hot.pose.y), theta: Number(hot.pose.theta) }; } if (r && !r.currentMap) { r.currentMap = String(hot.mapId || ''); } } } } catch (_) { }
  
  // Fetch config for each robot to get dimensions
  for (const r of registeredRobots) {
      try {
          const cRes = await fetch(`${API_BASE_URL}/agv/${r.robot_name}/sim/settings`);
          if (cRes.ok) {
              const settings = await cRes.json();
              if (settings) {
                  // Check if width/length are present (they might be in overrides or defaults)
                  // The endpoint returns the merged settings.
                  // We expect keys "width" and "length"
                  const w = settings.width;
                  const l = settings.length;
                  if (w !== undefined && l !== undefined) {
                      r.vehicleDimensions = { width: Number(w), length: Number(l) };
                  }
              }
          }
      } catch (_) {}
  }

  updateRobotList(); drawMap(); } catch (err) { console.error('加载机器人列表失败:', err); document.getElementById('status').textContent = '加载机器人列表失败: ' + err.message; } }
function clearRobotForm() { document.getElementById('robotName').value = ''; document.getElementById('robotType').value = 'AGV'; document.getElementById('robotIP').value = ''; }
let equipments = []; async function loadEquipmentList() { try { const resp = await fetch(`${API_BASE_URL}/equipments`); if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`); equipments = await resp.json(); updateEquipmentList(); } catch (err) { console.error('加载设备列表失败:', err); const el = document.getElementById('status'); if (el) el.textContent = '加载设备列表失败: ' + err.message; } }
function updateEquipmentList() { const c = document.getElementById('equipListContainer'); if (!c) return; const list = Array.isArray(equipments) ? equipments : []; if (list.length === 0) { c.innerHTML = '<p style="color:#bdc3c7;font-style:italic;">暂无设备</p>'; try { ensureDoorOverlays(); updateDoorOverlayPositions(); ensureElevatorOverlays(); updateElevatorOverlayPositions(); ensureLightOverlays(); updateLightOverlayPositions(); ensureCallerOverlays(); updateCallerOverlayPositions(); } catch (_) { } return; } c.innerHTML = list.map(eq => { const mqtt = eq.mqtt || {}; const title = (eq.serial_number || eq.dir_name || '设备'); const eqType = getEquipmentType(eq); const siteStr = Array.isArray(eq.site) ? eq.site.join(',') : (eq.site || '-'); const line1 = `类型: ${eqType} | IP: ${eq.ip || '-'} | 厂商: ${eq.manufacturer || '-'}`; const line2 = `站点: ${siteStr} | 地图: ${eq.map_id || '-'}`; const line3 = `动作时长: ${typeof eq.action_time !== 'undefined' && eq.action_time !== null ? String(eq.action_time) : '-'} | 触发模式: ${eq.trigger_mode || '-'}`; const line4 = `MQTT: ${mqtt.host || '-'}:${mqtt.port || '-'} (${mqtt.vda_interface || '-'}) | 版本: ${eq.vda_full_version || eq.vda_version || '-'}`; const dir = String(eq.dir_name || eq.dirName || eq.serial_number || '').trim(); let controlsHtml = ''; if (eqType === 'door' && dir) { controlsHtml = `<div class="equip-door-controls"><button class="btn" onclick="equipPostInstant('${dir}', buildEquipAction('cmd:open'))">开门</button><button class="btn" onclick="equipPostInstant('${dir}', buildEquipAction('cmd:close'))">关门</button></div>`; } else if (eqType === 'light' && dir) { controlsHtml = `<div class="equip-door-controls"><button class="btn" onclick="equipPostInstant('${dir}', buildEquipAction('cmd:on'))">开启</button><button class="btn" onclick="equipPostInstant('${dir}', buildEquipAction('cmd:off'))">关闭</button></div>`; } else if (eqType === 'caller' && dir) { controlsHtml = `<div class="equip-door-controls"><button class="btn" onclick="equipPostInstant('${dir}', buildEquipAction('cmd:press'))">按下</button><button class="btn" onclick="equipPostInstant('${dir}', buildEquipAction('cmd:release'))">释放</button></div>`; } return `<div class=\"robot-item\"><div class=\"robot-header\"><div class=\"robot-name\">${title}</div></div><div class=\"robot-info\" style=\"word-break:break-all;\">${line1}<br>${line2}<br>${line3}<br>${line4}</div>${controlsHtml}</div>`; }).join(''); try { ensureDoorOverlays(); updateDoorOverlayPositions(); ensureElevatorOverlays(); updateElevatorOverlayPositions(); ensureLightOverlays(); updateLightOverlayPositions(); ensureCallerOverlays(); updateCallerOverlayPositions(); } catch (_) { } }
function getEquipmentType(eq) { const t = String(eq?.type || '').toLowerCase(); if (t) return t; const nm = String(eq?.name || eq?.dir_name || '').toLowerCase(); if (nm.includes('door')) return 'door'; if (nm.includes('elevator')) return 'elevator'; if (nm.includes('light')) return 'light'; if (nm.includes('caller')) return 'caller'; return 'device'; }
const doorOverlayMap = new Map(); function ensureDoorOverlays() { const layer = document.getElementById('equipOverlayLayer'); if (!layer) return; const list = Array.isArray(equipments) ? equipments : []; for (const eq of list) { if (getEquipmentType(eq) !== 'door') continue; const serial = String(eq.serial_number || eq.serialNumber || '').trim(); const dir = String(eq.dir_name || eq.dirName || serial || '').trim(); if (!serial) continue; if (doorOverlayMap.has(serial)) continue; const wrap = document.createElement('div'); wrap.className = 'equip-item equip-door'; wrap.style.position = 'absolute'; wrap.style.width = '90px'; wrap.style.pointerEvents = 'none'; wrap.dataset.serial = serial; wrap.dataset.dir = dir; const icon = document.createElement('div'); icon.className = 'double-door'; icon.id = 'doubleDoor_' + serial; const left = document.createElement('div'); left.className = 'door-left'; const lp = document.createElement('div'); lp.className = 'door-panel'; const lh = document.createElement('div'); lh.className = 'door-handle'; left.appendChild(lp); left.appendChild(lh); const right = document.createElement('div'); right.className = 'door-right'; const rp = document.createElement('div'); rp.className = 'door-panel'; const rh = document.createElement('div'); rh.className = 'door-handle'; right.appendChild(rp); right.appendChild(rh); icon.appendChild(left); icon.appendChild(right); wrap.appendChild(icon); layer.appendChild(wrap); doorOverlayMap.set(serial, { element: wrap, icon: icon, eq: eq }); } } function updateDoorOverlayPositions() { const layer = document.getElementById('equipOverlayLayer'); if (!layer) return; doorOverlayMap.forEach((entry, serial) => { const eq = entry.eq; const posInfo = computeDoorOverlayPos(eq); if (!posInfo || !posInfo.visible) { entry.element.style.display = 'none'; return; } entry.element.style.display = 'block'; entry.element.style.left = (posInfo.screen.x - 45) + 'px'; entry.element.style.top = (posInfo.screen.y - 45) + 'px'; entry.element.style.transform = 'rotate(' + posInfo.angleRad + 'rad)'; }); } function computeDoorOverlayPos(eq) { if (!mapData || !mapData.points) return null; let site = eq.site; if (typeof site === 'string') site = [site]; site = Array.isArray(site) ? site : []; const ids = site.map(s => String(s)).filter(s => s); const pmap = {}; (mapData.points || []).forEach(p => { pmap[String(p.id || p.name)] = p; }); const routes = Array.isArray(mapData.routes) ? mapData.routes : []; const routeById = {}; const routeByDesc = {}; routes.forEach(r => { routeById[String(r.id)] = r; if (r.desc) routeByDesc[String(r.desc)] = r; }); const matchedRoutes = []; ids.forEach(s => { const r1 = routeById[s]; if (r1) matchedRoutes.push(r1); else { const r2 = routeByDesc[s]; if (r2) matchedRoutes.push(r2); } }); const pts = ids.map(id => pmap[id]).filter(Boolean); if (matchedRoutes.length < 2) { pts.forEach(pt => { if (!pt) return; const candidates = routes.filter(r => String(r.from) === String(pt.id || pt.name) || String(r.to) === String(pt.id || pt.name)); if (candidates.length > 0) matchedRoutes.push(candidates.sort((a, b) => { const pa = pmap[String(a.from)], qa = pmap[String(a.to)]; const pb = pmap[String(b.from)], qb = pmap[String(b.to)]; const la = (pa && qa) ? Math.hypot(qa.x - pa.x, qa.y - pa.y) : Infinity; const lb = (pb && qb) ? Math.hypot(qb.x - pb.x, pb.y - pb.y) : Infinity; return la - lb; })[0]); }); } if (matchedRoutes.length < 2 && routes.length >= 2) { matchedRoutes.push(routes[0], routes[1]); } const currentFloorName = floorNames[currentFloorIndex] || null; let anyPoint = pts[0] || (matchedRoutes[0] ? pmap[String(matchedRoutes[0].from)] : null); if (anyPoint) { const pfloor = extractFloorNameFromLayer(anyPoint.layer || ''); if (currentFloorName && pfloor && String(pfloor).trim().toLowerCase() !== String(currentFloorName).trim().toLowerCase()) return { visible: false }; } const midpoints = []; for (let i = 0; i < Math.min(2, matchedRoutes.length); i++) { const r = matchedRoutes[i]; if (!r) continue; const p0 = pmap[String(r.from)], p1 = pmap[String(r.to)]; if (!p0 || !p1) continue; let mx = (p0.x + p1.x) / 2, my = (p0.y + p1.y) / 2; try { if (r.type === 'bezier3' && r.c1 && r.c2) { const bt = bezierPoint({ x: p0.x, y: p0.y }, { x: r.c1.x, y: r.c1.y }, { x: r.c2.x, y: r.c2.y }, { x: p1.x, y: p1.y }, 0.5); mx = bt.x; my = bt.y; } } catch (_) { } midpoints.push({ x: mx, y: my }); } let center = null; let ang = 0; if (midpoints.length >= 2) { const m0 = midpoints[0], m1 = midpoints[1]; center = { x: (m0.x + m1.x) / 2, y: (m0.y + m1.y) / 2 }; ang = Math.atan2((m1.y - m0.y), (m1.x - m0.x)) - Math.PI / 2; } else if (pts.length >= 2) { const a = pts[0], b = pts[1]; center = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 }; ang = Math.atan2((b.y - a.y), (b.x - a.x)) - Math.PI / 2; } else if (pts.length === 1) { const a = pts[0]; center = { x: a.x, y: a.y }; ang = 0; } if (!center) return null; const sp = worldToScreen(center.x, center.y); return { screen: { x: sp.x, y: sp.y }, angleRad: ang, visible: true }; }
async function equipPostInstant(dirName, payload) { try { await fetch(`${API_BASE_URL}/equipments/${encodeURIComponent(dirName)}/instant`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) }); } catch (_) { } } function buildEquipAction(cmd) { return { "actions": [{ "actionType": "writeValue", "actionId": String(Math.random()), "blockingType": "HARD", "actionParameters": [{ "key": "command", "value": cmd }] }], "headerId": Date.now(), "timeStamp": "", "version": "v1", "manufacturer": "", "serialNumber": "" }; }
async function agvPostInstant(serialNumber, payload) { const resp = await fetch(`${API_BASE_URL}/agv/${encodeURIComponent(serialNumber)}/instant`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) }); if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`); return resp; }
async function agvPostSwitchMap(serialNumber, body) { const resp = await fetch(`${API_BASE_URL}/agv/${encodeURIComponent(serialNumber)}/instant/switch-map`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body || {}) }); if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`); return resp; }
function buildAgvChangeControlInstant(serialNumber, manufacturer, version, controlMode) { const mode = String(controlMode || 'AUTOMATIC').toUpperCase(); return { "actions": [{ "actionType": "changeControl", "actionId": "changeControl_" + String(Date.now()), "actionDescription": "changeControl", "blockingType": "HARD", "actionParameters": [{ "key": "control", "value": mode }] }], "headerId": Date.now(), "timestamp": new Date().toISOString(), "version": String(version || '2.0.0'), "manufacturer": String(manufacturer || ''), "serialNumber": String(serialNumber || '') }; }
function buildAgvInitPositionInstant(serialNumber, manufacturer, version, position) { const x = Number(position && position.x); const y = Number(position && position.y); const theta = Number(position && position.theta); const mapId = String((position && position.mapId) || (position && position.map_id) || ''); return { "actions": [{ "actionType": "initPosition", "actionId": "initPosition_" + String(Date.now()), "actionDescription": "initPosition", "blockingType": "HARD", "actionParameters": [{ "key": "x", "value": isFinite(x) ? x : 0 }, { "key": "y", "value": isFinite(y) ? y : 0 }, { "key": "theta", "value": isFinite(theta) ? theta : 0 }, { "key": "mapId", "value": mapId }] }], "headerId": Date.now(), "timestamp": new Date().toISOString(), "version": String(version || '2.0.0'), "manufacturer": String(manufacturer || ''), "serialNumber": String(serialNumber || '') }; }
async function toggleAgvControl(serialNumber) { try { const robot = registeredRobots.find(r => r.robot_name === String(serialNumber)); const manu = robot?.manufacturer || ''; const ver = robot?.vda_version || '2.0.0'; const cur = String(robot?.operatingMode || 'AUTOMATIC').toUpperCase(); const next = (cur === 'SERVICE') ? 'AUTOMATIC' : 'SERVICE'; const payload = buildAgvChangeControlInstant(serialNumber, manu, ver, next); await agvPostInstant(serialNumber, payload); try { document.getElementById('status').textContent = `控制权切换已发送: ${serialNumber} -> ${next}`; } catch (_) { } } catch (_) { } }
function applyEquipmentWSState(p) { try { const sn = String(p.serial_number || p.serialNumber || '').trim(); const info = Array.isArray(p.information) ? p.information : []; if (!sn) return; const doorInfo = info.find(i => String(i.infoType || i.info_type || i.infotype || '').toUpperCase() === 'DOORSTATE'); if (doorInfo) { const refs = Array.isArray(doorInfo.infoReferences || doorInfo.info_references || doorInfo.inforeferences) ? (doorInfo.infoReferences || doorInfo.info_references || doorInfo.inforeferences) : []; const vals = refs.filter(r => String(r.referenceKey || r.reference_key || r.referencekey || '') === 'value').map(r => String(r.referenceValue || r.reference_value || r.referencevalue || '')); const reg = doorOverlayMap.get(sn); if (reg && reg.icon) { if (vals.includes('open')) { reg.icon.classList.add('door-open'); } else if (vals.includes('close')) { reg.icon.classList.remove('door-open'); } } }
  const lightInfo = info.find(i => String(i.infoType || i.info_type || i.infotype || '').toUpperCase() === 'LIGHTSTATE'); if (lightInfo) { const refs2 = Array.isArray(lightInfo.infoReferences || lightInfo.info_references || lightInfo.inforeferences) ? (lightInfo.infoReferences || lightInfo.info_references || lightInfo.inforeferences) : []; const vals2 = refs2.filter(r => String(r.referenceKey || r.reference_key || r.referencekey || '') === 'value').map(r => String(r.referenceValue || r.reference_value || r.referencevalue || '')); const regL = lightOverlayMap.get(sn); if (regL && regL.icon) { if (vals2.includes('on')) regL.icon.classList.add('light-on'); else if (vals2.includes('off')) regL.icon.classList.remove('light-on'); } } } catch (_) { } }
function applyEquipmentElevatorWSState(p) { try { const sn = String(p.serial_number || p.serialNumber || '').trim(); const info = Array.isArray(p.information) ? p.information : []; if (!sn) return; const doorInfo = info.find(i => String(i.infoType || i.info_type || i.infotype || '').toUpperCase() === 'EDOORSTATE'); const actionInfo = info.find(i => String(i.infoType || i.info_type || i.infotype || '').toUpperCase() === 'ACTIONSTATE'); const floorInfo = info.find(i => String(i.infoType || i.info_type || i.infotype || '').toUpperCase() === 'FLOOR'); let doorOpen = false; let isUp = false; let isDown = false; let floorVal = 1; const extractValue = (inf) => { if (!inf) return ''; const refs = Array.isArray(inf.infoReferences || inf.info_references || inf.inforeferences) ? (inf.infoReferences || inf.info_references || inf.inforeferences) : []; for (const r of refs) { const k = String(r.referenceKey || r.reference_key || r.referencekey || '').toLowerCase(); if (k === 'value') { const v = r.referenceValue || r.reference_value || r.referencevalue; return v != null ? String(v) : ''; } } return ''; }; if (doorInfo || actionInfo || floorInfo) { const dval = extractValue(doorInfo); const aval = extractValue(actionInfo); const fstr = extractValue(floorInfo); doorOpen = dval === 'open'; isUp = aval === 'up'; isDown = aval === 'down'; const fnum = parseInt(fstr, 10); floorVal = Number.isFinite(fnum) ? fnum : 1; } else { const elevInfo = info.find(i => String(i.infoType || i.info_type || i.infotype || '').toUpperCase() === 'DOORSTATE'); if (!elevInfo) return; const refs = Array.isArray(elevInfo.infoReferences || elevInfo.info_references || elevInfo.inforeferences) ? (elevInfo.infoReferences || elevInfo.info_references || elevInfo.inforeferences) : []; const getParam = (nm) => { for (let i = 0; i < refs.length - 1; i++) { const k1 = String(refs[i].referenceKey || refs[i].reference_key || refs[i].referencekey || ''); const v1 = String(refs[i].referenceValue || refs[i].reference_value || refs[i].referencevalue || ''); if (k1 === 'name' && v1 === nm) { const r2 = refs[i + 1] || {}; const k2 = String(r2.referenceKey || r2.reference_key || r2.referencekey || ''); if (k2 === 'value') { const vv = r2.referenceValue || r2.reference_value || r2.referencevalue; return vv != null ? String(vv) : ''; } } } return ''; }; const doorState = getParam('door_state'); const actionState = getParam('action_state'); const floorStr = getParam('floor'); doorOpen = doorState === 'open'; isUp = actionState === 'up'; isDown = actionState === 'down'; let fnum2 = parseInt(floorStr, 10); floorVal = Number.isFinite(fnum2) ? fnum2 : 1; } const reg2 = elevatorOverlayMap.get(sn); if (reg2) { if (doorOpen) reg2.door.classList.add('door-open'); else reg2.door.classList.remove('door-open'); if (reg2.floor) reg2.floor.textContent = String(floorVal); if (reg2.up) reg2.up.classList.toggle('active', !!isUp); if (reg2.down) reg2.down.classList.toggle('active', !!isDown); } } catch (_) { } }
async function updateRegisterMapOptions() { }
async function updateRegisterInitialPositionOptions() { }
async function registerRobot() { const name = document.getElementById('robotName').value.trim(); const type = document.getElementById('robotType').value; const ip = document.getElementById('robotIP').value.trim(); const manu = document.getElementById('robotManufacturer').value.trim() || 'SEER'; const ver = document.getElementById('robotVersion').value.trim() || 'v2'; if (!name) { try { window.printErrorToStatus('请输入机器人名称', '注册'); } catch (_) { } return; } if (!ip) { try { window.printErrorToStatus('请输入机器人IP地址', '注册'); } catch (_) { } return; } const ipRegex = /^(\d{1,3}\.){3}\d{1,3}$/; if (!ipRegex.test(ip)) { try { window.printErrorToStatus('请输入有效的IP地址格式', '注册'); } catch (_) { } return; } const payload = [{ serial_number: name, manufacturer: manu, type: type, vda_version: ver, IP: ip }]; try { const resp = await fetch(`${API_BASE_URL}/agvs/register`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) }); if (!resp.ok) { let msg = `HTTP ${resp.status}: ${resp.statusText}`; try { const data = await resp.json(); const detail = (data && (data.detail || data.message)) ? String(data.detail || data.message) : ''; if (detail) msg += ` | ${detail}`; } catch (_) { try { const text = await resp.text(); const t = String(text || '').trim(); if (t) msg += ` | ${t}`; } catch (_) { } } throw new Error(msg); } const result = await resp.json(); const registered = Array.isArray(result?.registered) ? result.registered : []; const skipped = Array.isArray(result?.skipped) ? result.skipped : []; if (registered.includes(name)) { await loadRobotList(); clearRobotForm(); document.getElementById('status').textContent = `机器人 ${name} 注册成功`; setTimeout(() => document.getElementById('status').textContent = '准备就绪', 3000); } else if (skipped.includes(name)) { await loadRobotList(); clearRobotForm(); document.getElementById('status').textContent = `机器人 ${name} 已注册，无需重复注册`; setTimeout(() => document.getElementById('status').textContent = '准备就绪', 3000); } else { try { window.printErrorToStatus('注册失败: ' + JSON.stringify(result), '注册'); } catch (_) { } } } catch (err) { console.error('注册机器人失败:', err); try { window.printErrorToStatus('注册机器人失败: ' + (err && err.message ? err.message : String(err)), '注册'); } catch (_) { } document.getElementById('status').textContent = '注册失败: ' + (err && err.message ? err.message : String(err)); } }
async function removeRobot(serial) { if (!confirm('确定要删除这个机器人吗？')) return; try { const resp = await fetch(`${API_BASE_URL}/agvs/${serial}`, { method: 'DELETE' }); if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`); await loadRobotList(); document.getElementById('status').textContent = '机器人已删除'; setTimeout(() => document.getElementById('status').textContent = '准备就绪', 2000); } catch (err) { console.error('删除机器人失败:', err); try { window.printErrorToStatus('删除机器人失败: ' + (err && err.message ? err.message : String(err)), '删除'); } catch (_) { } } }
function updateRobotList() { const c = document.getElementById('robotListContainer'); if (registeredRobots.length === 0) { c.innerHTML = '<p style="color:#bdc3c7;font-style:italic;">暂无注册的机器人</p>'; return; } c.innerHTML = registeredRobots.map(robot => { const battery = robot.battery || 0; const batteryText = truncateToOneDecimal(Number(battery)).toFixed(1); const pos = robot.currentPosition || robot.initialPosition; const posText = pos ? `当前位置: (${pos.x.toFixed(2)}, ${pos.y.toFixed(2)}) | 方向: ${(pos.theta || 0).toFixed(2)}` : '位置未知'; const selectedStyle = (selectedRobotId === robot.robot_name) ? 'border-left:4px solid #2ecc71;background:#3b4f63;' : ''; const commText = robot.commConnected ? '连接' : '断开'; const commColor = robot.commConnected ? '#00ff00' : '#ff0000'; const instStatus = robot.instanceStatus || '停止'; const instText = instStatus; const instColor = instStatus === '启动' ? '#00ff00' : '#ff0000'; const mapText = robot.currentMap ? robot.currentMap : '未设置'; const fhText = (typeof robot.forkHeight !== 'undefined' && robot.forkHeight !== null) ? `${truncateToOneDecimal(Number(robot.forkHeight)).toFixed(1)} m` : '-'; const opMode = String(robot.operatingMode || 'AUTOMATIC').toUpperCase(); const hasControl = opMode === 'SERVICE'; const ctrlText = hasControl ? '手动控制' : '调度控制'; const ctrlColor = hasControl ? '#2ecc71' : '#e74c3c'; const isInstRunning = instStatus === '启动'; const isPm2Busy = !!robot._pm2Busy; const powerClass = (isInstRunning ? 'on' : 'off') + (isPm2Busy ? ' busy' : ''); const powerTitle = isPm2Busy ? '操作中...' : (isInstRunning ? '关机' : '开机'); const powerLabel = isPm2Busy ? '...' : '⏻'; return `<div class=\"robot-item\" style=\"${selectedStyle}\" onclick=\"selectRobot('${robot.robot_name}')\"><div class=\"robot-header\"><div class=\"robot-name\">${robot.robot_name}<span style=\"margin-left:8px;color:${ctrlColor};font-weight:700;\">${ctrlText}</span></div><button class=\"btn-power-small ${powerClass}\" title=\"${powerTitle}\" onclick=\"event.stopPropagation(); toggleAgvPm2('${robot.robot_name}')\" ${isPm2Busy ? 'disabled' : ''}>${powerLabel}</button></div><div class=\"robot-info\" style=\"word-break:break-all;\">类型: ${robot.type} | IP: ${robot.ip} | 厂商: ${robot.manufacturer || '未知'} <br> 地图: ${mapText} <br>${posText} | 叉高: ${fhText}<br><span style=\"color:${commColor}\">电量: ${batteryText}%</span> | <span style=\"color:${commColor}\">通信状态: ${commText}</span> | <span style=\"color:${instColor}\">实例状态: ${instText}</span></div><div class=\"robot-actions\"><button class=\"btn-small btn-danger\" onclick=\"removeRobot('${robot.robot_name}')\">删除</button><button class=\"btn-small btn-config-small\" onclick=\"openRobotConfig('${robot.robot_name}')\">配置</button><button class=\"btn-small btn-control-small\" onclick=\"toggleAgvControl('${robot.robot_name}')\">切换控制权</button></div></div>`; }).join(''); }

async function toggleAgvPm2(serialNumber) { const id = String(serialNumber || '').trim(); if (!id) return; const robot = registeredRobots.find(r => r.robot_name === id); if (robot && robot._pm2Busy) return; const currentStatus = String(robot?.instanceStatus || '停止'); const shouldStop = currentStatus === '启动'; const action = shouldStop ? 'stop' : 'start'; try { if (robot) robot._pm2Busy = true; try { updateRobotList(); } catch (_) { } const resp = await fetch(`${API_BASE_URL}/agvs/${encodeURIComponent(id)}/pm2/${action}`, { method: 'POST' }); if (!resp.ok) { let msg = `HTTP ${resp.status}: ${resp.statusText}`; try { const data = await resp.json(); const detail = (data && (data.detail || data.message)) ? String(data.detail || data.message) : ''; if (detail) msg += ` | ${detail}`; } catch (_) { try { const text = await resp.text(); const t = String(text || '').trim(); if (t) msg += ` | ${t}`; } catch (_) { } } throw new Error(msg); } const lastBefore = robot ? Number(robot.lastUpdateTs || 0) : 0; if (robot) { robot.instanceStatus = shouldStop ? '停止' : '启动'; if (shouldStop) { robot.commConnected = false; robot.lastUpdateTs = 0; } } try { const el = document.getElementById('status'); if (el) el.textContent = `${id} 实例${shouldStop ? '停止' : '启动'}指令已发送`; setTimeout(() => { try { const el2 = document.getElementById('status'); if (el2 && String(el2.textContent || '').includes('指令已发送')) el2.textContent = '准备就绪'; } catch (_) { } }, 2000); } catch (_) { } if (shouldStop && robot) { await new Promise(r => setTimeout(r, 1200)); if (Number(robot.lastUpdateTs || 0) > lastBefore) { try { window.printErrorToStatus('关机可能未生效：仍在接收状态数据，请检查 PM2/进程归属', 'PM2'); } catch (_) { } } } } catch (err) { try { window.printErrorToStatus('实例操作失败: ' + (err && err.message ? err.message : String(err)), 'PM2'); } catch (_) { } } finally { if (robot) robot._pm2Busy = false; try { updateRobotList(); } catch (_) { } } }

let currentConfigRobotId = null;
async function openRobotConfig(id) {
  currentConfigRobotId = id;
  const robot = registeredRobots.find(r => r.robot_name === id);
  if (!robot) return;
  document.getElementById('configRobotName').value = robot.robot_name || '';
  const t = robot.type || 'AGV';
  document.getElementById('configRobotType').value = (t === 'Forklift' ? 'Fork' : (t === 'AMR' ? 'Load' : t));
  await updateConfigMapOptions();
  const selMap = document.getElementById('configMapSelect');
  if (selMap) {
    selMap.addEventListener('change', () => { updateConfigInitialPositionOptions(); });
  }
  const mapLabel = document.getElementById('configMapSelectedLabel');
  if (mapLabel) mapLabel.textContent = robot.currentMap || '未设置';
  document.getElementById('configRobotIP').value = robot.ip || '';
  document.getElementById('configRobotManufacturer').value = robot.manufacturer || '';
  document.getElementById('configRobotVersion').value = 'v2';
  updateConfigInitialPositionOptions();
  const pos = robot.currentPosition || robot.initialPosition || { x: 0, y: 0, theta: 0 };
  document.getElementById('configBattery').value = robot.battery ?? 100;
  document.getElementById('configOrientation').value = pos.theta ?? 0;
  try { await prefillAgvSimSettingsForConfigModal(robot.robot_name); } catch (e) { }
  switchConfigTab('basic');
  const modal = document.getElementById('configModal');
  modal.style.display = 'flex';
}
function closeConfigModal() { const modal = document.getElementById('configModal'); modal.style.display = 'none'; currentConfigRobotId = null; }

async function updateConfigInitialPositionOptions() {
  const sel = document.getElementById('configInitialPosition');
  const selMap = document.getElementById('configMapSelect');
  const chosen = selMap ? String(selMap.value || '').trim() : '';
  const robot = registeredRobots.find(r => r.robot_name === currentConfigRobotId);
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
    try {
      stations = await getSceneStationsForMap(activeMap);
    } catch (e) {
      stations = [];
    }
    window.configSceneStationsCache[activeMap] = stations;
  }
  sel.innerHTML = '<option value="">选择初始位置</option>';
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
  const addOpt = (mapId, label) => {
    const id = String(mapId || '').trim();
    if (!id || seen.has(id)) return;
    seen.add(id);
    const opt = document.createElement('option');
    opt.value = id;
    opt.textContent = label || displayName(id);
    sel.appendChild(opt);
  };
  try {
    const resp = await fetch(`${API_BASE_URL}/maps/ViewerMap`);
    if (resp.ok) {
      const files = await resp.json();
      (files || []).forEach(name => {
        const mapId = normalizeMapId(name);
        addOpt(mapId, displayName(mapId));
      });
    }
  } catch (e) { }
  if (CURRENT_MAP_ID) {
    const cur = normalizeMapId(CURRENT_MAP_ID);
    addOpt(cur, `已加载地图 (${displayName(cur)})`);
  }
  const robot = registeredRobots.find(r => r.robot_name === currentConfigRobotId);
  if (robot && robot.currentMap) {
    const cur = normalizeMapId(robot.currentMap);
    addOpt(cur, `当前地图 (${displayName(cur)})`);
  }
  const mapLabel = document.getElementById('configMapSelectedLabel');
  if (mapLabel) {
    const robot2 = registeredRobots.find(r => r.robot_name === currentConfigRobotId);
    mapLabel.textContent = (robot2 && robot2.currentMap) ? robot2.currentMap : '未设置';
  }
}

async function saveConfig() {
  const id = currentConfigRobotId;
  if (!id) { closeConfigModal(); return; }
  const robot = registeredRobots.find(r => r.robot_name === id);
  if (!robot) { closeConfigModal(); return; }
  const newType = document.getElementById('configRobotType').value;
  const newIP = document.getElementById('configRobotIP').value.trim();
  const newManu = document.getElementById('configRobotManufacturer').value.trim();
  const battery = parseInt(document.getElementById('configBattery').value, 10);
  const orientation = parseFloat(document.getElementById('configOrientation').value);
  const initId = String(document.getElementById('configInitialPosition').value || '').trim();
  const mapVal = String(document.getElementById('configMapSelect').value || '').trim();
  const n = (v) => { if (v === '' || v === null || typeof v === 'undefined') return undefined; const x = Number(v); return isFinite(x) ? x : undefined; };
  const phys = { speed: n(document.getElementById('factsheetSpeed').value), speedMin: n(document.getElementById('factsheetSpeedMin').value), speedMax: n(document.getElementById('factsheetSpeedMax').value), accelerationMax: n(document.getElementById('factsheetAccelMax').value), decelerationMax: n(document.getElementById('factsheetDecelMax').value), heightMin: n(document.getElementById('factsheetHeightMin').value), heightMax: n(document.getElementById('factsheetHeightMax').value), width: n(document.getElementById('factsheetWidth').value), length: n(document.getElementById('factsheetLength').value) };
  const batterySim = { batteryDefault: n(document.getElementById('agvBatteryDefault')?.value), batteryIdle: n(document.getElementById('agvBatteryIdle')?.value), batteryEmptyMult: n(document.getElementById('agvBatteryEmptyMult')?.value), batteryLoadedMult: n(document.getElementById('agvBatteryLoadedMult')?.value), batteryCharge: n(document.getElementById('agvBatteryCharge')?.value) };
  const radarFov = n(document.getElementById('factsheetRadarFov')?.value);
  const radarRadius = n(document.getElementById('factsheetRadarRadius')?.value);
  const safetyScale = n(document.getElementById('factsheetSafetyScale')?.value);
  const chosenMapRaw = mapVal || robot.currentMap || CURRENT_MAP_ID || '';
  const mapSceneId = chosenMapRaw ? normalizeViewerMapId(chosenMapRaw) : '';
  const mapId = canonicalizeMapId(chosenMapRaw);
  let pos = robot.currentPosition || robot.initialPosition || { x: 0, y: 0, theta: 0 };
  if (initId) {
    window.configSceneStationsCache = window.configSceneStationsCache || {};
    let stations = mapSceneId ? window.configSceneStationsCache[mapSceneId] : null;
    if (!stations && mapSceneId) {
      try { stations = await getSceneStationsForMap(mapSceneId); } catch (e) { stations = []; }
      window.configSceneStationsCache[mapSceneId] = stations;
    }
    const match = (stations || []).find(s => {
      const a = String(s.instanceName || '').trim();
      const b = String(s.pointName || '').trim();
      const c = String(s.id || '').trim();
      return a === initId || b === initId || c === initId;
    });
    if (match && match.pos) {
      const px = Number(match.pos.x);
      const py = Number(match.pos.y);
      if (!isFinite(px) || !isFinite(py)) { try { window.printErrorToStatus('站点坐标无效，无法保存配置', '配置'); } catch (_) { } return; }
      pos = { x: px, y: py, theta: isNaN(orientation) ? 0 : orientation };
    } else {
      const point = (mapData?.points || []).find(p => String(p.id) === initId || String(p.name || '') === initId);
      if (point) pos = { x: point.x, y: point.y, theta: isNaN(orientation) ? 0 : orientation };
    }
  }
  try {
    if (newIP && newIP !== robot.ip) {
      const sResp = await fetch(`${API_BASE_URL}/agv/${id}/config/static`, { method: 'PATCH', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ IP: newIP }) });
      if (!sResp.ok) throw new Error(`静态配置更新失败: HTTP ${sResp.status}`);
      robot.ip = newIP;
    }
    if (Number.isFinite(battery)) robot.battery = Math.max(0, Math.min(100, battery));
    if (newType) robot.type = newType;
    if (newManu) robot.manufacturer = newManu;
    let instantSent = false;
    const shouldSwitchMap = !!initId || !!mapVal;
    if (shouldSwitchMap && mapId) {
      await agvPostSwitchMap(id, { map: mapId, switch_point: initId || undefined, center_x: pos.x, center_y: pos.y, initiate_angle: pos.theta ?? 0 });
      instantSent = true;
    }
    try { await applyAgvSimSettingsPatch(id, phys, { radarFov, radarRadius, safetyScale, batterySim }); robot.motionParams = phys; robot.radarParams = { radarFov, radarRadius, safetyScale }; } catch (e) { }
    try { await updateCollisionParameters(id, phys); robot.collisionParams = { width: phys.width, length: phys.length, heightMin: phys.heightMin, heightMax: phys.heightMax }; } catch (e) { }
    updateRobotList(); drawMap();
    document.getElementById('status').textContent = instantSent ? `机器人 ${id} 已发送 switchMap 即时动作` : `机器人 ${id} 配置已更新`;
    setTimeout(() => document.getElementById('status').textContent = '准备就绪', 2000);
  } catch (err) {
    console.error('更新配置失败:', err);
    try { window.printErrorToStatus('更新配置失败: ' + (err && err.message ? err.message : String(err)), '配置'); } catch (_) { }
  } finally { closeConfigModal(); }
}
function switchConfigTab(tab) { const btnBasic = document.getElementById('configTabBtnBasic'); const btnPhysical = document.getElementById('configTabBtnPhysical'); const vBasic = document.getElementById('configTabBasic'); const vPhysical = document.getElementById('configTabPhysical'); const isBasic = tab === 'basic'; vBasic.style.display = isBasic ? 'block' : 'none'; vPhysical.style.display = isBasic ? 'none' : 'block'; btnBasic.style.background = isBasic ? '#34495e' : '#2b3b4b'; btnPhysical.style.background = isBasic ? '#2b3b4b' : '#34495e'; }

async function prefillAgvSimSettingsForConfigModal(serialNumber) {
  const setVal = (id, v) => { const el = document.getElementById(id); if (el && typeof v !== 'undefined' && v !== null) el.value = String(v); };
  const defaults = { speed: 1.0, speedMin: 0.01, speedMax: 2, accelerationMax: 2, decelerationMax: 2, heightMin: 0.01, heightMax: 0.10, width: 0.745, length: 1.03, radarFov: 60, radarRadius: 0.5, safetyScale: 1.1 };
  const id = String(serialNumber || '').trim();
  if (!id) return;
  try {
    const resp = await fetch(`${API_BASE_URL}/agv/${encodeURIComponent(id)}/sim/settings`);
    if (!resp || !resp.ok) return;
    const s = await resp.json();
    setVal('factsheetSpeed', s.speed ?? defaults.speed);
    setVal('factsheetSpeedMin', s.speed_min ?? defaults.speedMin);
    setVal('factsheetSpeedMax', s.speed_max ?? (typeof s.speed === 'number' ? s.speed : defaults.speedMax));
    setVal('factsheetAccelMax', s.acceleration_max ?? defaults.accelerationMax);
    setVal('factsheetDecelMax', s.deceleration_max ?? defaults.decelerationMax);
    setVal('factsheetHeightMin', s.height_min ?? defaults.heightMin);
    setVal('factsheetHeightMax', s.height_max ?? defaults.heightMax);
    setVal('factsheetWidth', s.width ?? defaults.width);
    setVal('factsheetLength', s.length ?? defaults.length);
    setVal('factsheetRadarFov', s.radar_fov_deg ?? defaults.radarFov);
    setVal('factsheetRadarRadius', s.radar_radius_m ?? defaults.radarRadius);
    setVal('factsheetSafetyScale', s.safety_scale ?? defaults.safetyScale);
    setVal('agvBatteryDefault', s.battery_default);
    setVal('agvBatteryIdle', s.battery_idle_drain_per_min);
    setVal('agvBatteryEmptyMult', s.battery_move_empty_multiplier);
    setVal('agvBatteryLoadedMult', s.battery_move_loaded_multiplier);
    setVal('agvBatteryCharge', s.battery_charge_per_min);
  } catch (_) { }
}

async function applyAgvSimSettingsPatch(serialNumber, params, extra) {
  const patch = {};
  const n = (v) => { const x = Number(v); return isFinite(x) ? x : undefined; };
  const intIn = (x, a, b) => Number.isInteger(x) && x >= a && x <= b;
  const numIn = (x, a, b, openLeft = false, openRight = false) => { if (typeof x !== 'number' || !isFinite(x)) return false; const leftOk = openLeft ? (x > a) : (x >= a); const rightOk = openRight ? (x < b) : (x <= b); return leftOk && rightOk; };

  const s = n(params?.speed);
  const sMin = n(params?.speedMin);
  const sMax = n(params?.speedMax);
  const aMax = n(params?.accelerationMax);
  const dMax = n(params?.decelerationMax);
  const hMin = n(params?.heightMin);
  const hMax = n(params?.heightMax);
  const w = n(params?.width);
  const l = n(params?.length);
  const rf = n(extra?.radarFov);
  const rr = n(extra?.radarRadius);
  const ss = n(extra?.safetyScale);
  const bd = n(extra?.batterySim?.batteryDefault);
  const bi = n(extra?.batterySim?.batteryIdle);
  const be = n(extra?.batterySim?.batteryEmptyMult);
  const bl = n(extra?.batterySim?.batteryLoadedMult);
  const bc = n(extra?.batterySim?.batteryCharge);

  if (typeof s !== 'undefined') patch.speed = s;
  if (typeof sMin !== 'undefined') patch.speed_min = sMin;
  if (typeof sMax !== 'undefined') patch.speed_max = sMax;
  if (typeof aMax !== 'undefined') patch.acceleration_max = aMax;
  if (typeof dMax !== 'undefined') patch.deceleration_max = dMax;
  if (typeof hMin !== 'undefined') patch.height_min = hMin;
  if (typeof hMax !== 'undefined') patch.height_max = hMax;
  if (typeof w !== 'undefined') patch.width = w;
  if (typeof l !== 'undefined') patch.length = l;
  if (typeof rf !== 'undefined') patch.radar_fov_deg = rf;
  if (typeof rr !== 'undefined') patch.radar_radius_m = rr;
  if (typeof ss !== 'undefined') patch.safety_scale = ss;
  if (typeof bd !== 'undefined') patch.battery_default = bd;
  if (typeof bi !== 'undefined') patch.battery_idle_drain_per_min = bi;
  if (typeof be !== 'undefined') patch.battery_move_empty_multiplier = be;
  if (typeof bl !== 'undefined') patch.battery_move_loaded_multiplier = bl;
  if (typeof bc !== 'undefined') patch.battery_charge_per_min = bc;

  const errs = [];
  if (typeof sMin !== 'undefined' && typeof sMax !== 'undefined' && sMin > sMax) errs.push('最小速度不能大于最大速度');
  if (typeof s !== 'undefined') {
    if (!numIn(s, 0, 2)) errs.push('运行速度需在[0,2]范围内');
    if (typeof sMin !== 'undefined' && s < sMin) errs.push('运行速度应不低于最小速度');
    if (typeof sMax !== 'undefined' && s > sMax) errs.push('运行速度应不高于最大速度');
  }
  if (typeof rf !== 'undefined' && !numIn(rf, 1, 360)) errs.push('雷达扇面需在[1,360]范围内');
  if (typeof rr !== 'undefined' && !numIn(rr, 0.01, 10)) errs.push('雷达半径需在[0.01,10]范围内');
  if (typeof ss !== 'undefined' && !numIn(ss, 1.0, 5.0)) errs.push('安全范围系数需在[1.0,5.0]范围内');
  if (typeof bd !== 'undefined' && !numIn(bd, 0, 100, true, false)) errs.push('默认电量需在(0,100]内');
  if (typeof bi !== 'undefined') { if (!intIn(bi, 1, 100) && !numIn(bi, 1, 100)) errs.push('空闲耗电需在[1,100]内'); }
  if (typeof be !== 'undefined') { if (!intIn(be, 1, 100) && !numIn(be, 1, 100)) errs.push('空载耗电系数需在[1,100]内'); }
  if (typeof bl !== 'undefined') { if (!intIn(bl, 1, 100) && !numIn(bl, 1, 100)) errs.push('载重耗电系数需在[1,100]内'); }
  if (typeof bc !== 'undefined') { if (!intIn(bc, 1, 100) && !numIn(bc, 1, 100)) errs.push('充电速度需在[1,100]内'); }

  if (errs.length) { try { window.printErrorToStatus('参数无效: ' + errs.join('; '), '配置'); } catch (_) { } return; }
  if (Object.keys(patch).length === 0) return;
  const id = String(serialNumber || '').trim();
  if (!id) return;
  const resp = await fetch(`${API_BASE_URL}/agv/${encodeURIComponent(id)}/sim/settings`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(patch) });
  if (!resp.ok) throw new Error(`仿真参数更新失败: HTTP ${resp.status}: ${resp.statusText}`);
}
async function prefillFactsheetPhysicalParams(robot) { const setVal = (id, v) => { const el = document.getElementById(id); if (el && typeof v !== 'undefined' && v !== null) el.value = String(v); }; const defaults = { speedMin: 0.01, speedMax: 2, accelerationMax: 2, decelerationMax: 2, heightMin: 0.01, heightMax: 0.10, width: 0.745, length: 1.03 }; let cfg = null; let radar = { radarFov: 60, radarRadius: 0.5, safetyScale: 1.1 }; try { const id = (robot && robot.robot_name) || selectedRobotId || (((registeredRobots || [])[0] && (registeredRobots || [])[0].robot_name) || ''); if (id) { const resp = await fetch(`${API_BASE_URL}/agv/${encodeURIComponent(id)}/sim/settings`); if (resp && resp.ok) { const s = await resp.json(); cfg = { speedMin: Number(s.speed_min ?? defaults.speedMin), speedMax: Number(s.speed_max ?? (typeof s.speed === 'number' ? s.speed : defaults.speedMax)), accelerationMax: Number(s.acceleration_max ?? defaults.accelerationMax), decelerationMax: Number(s.deceleration_max ?? defaults.decelerationMax), heightMin: Number(s.height_min ?? defaults.heightMin), heightMax: Number(s.height_max ?? defaults.heightMax), width: Number(s.width ?? defaults.width), length: Number(s.length ?? defaults.length) }; radar = { radarFov: Number(s.radar_fov_deg ?? 60), radarRadius: Number(s.radar_radius_m ?? 0.5), safetyScale: Number(s.safety_scale ?? 1.1) }; } } } catch (e) { } if (!cfg && robot && robot.motionParams) { cfg = Object.assign({}, defaults, robot.motionParams); } if (!cfg) { try { let resp = await fetch('/factsheet.json'); if (!resp || !resp.ok) resp = await fetch('../factsheet.json'); if (resp && resp.ok) { const data = await resp.json(); const p = (data && data.physicalParameters) ? data.physicalParameters : {}; cfg = { speedMin: Number(p.speedMin ?? defaults.speedMin), speedMax: Number(p.speedMax ?? defaults.speedMax), accelerationMax: Number(p.accelerationMax ?? defaults.accelerationMax), decelerationMax: Number(p.decelerationMax ?? defaults.decelerationMax), heightMin: Number(p.heightMin ?? defaults.heightMin), heightMax: Number(p.heightMax ?? defaults.heightMax), width: Number(p.width ?? defaults.width), length: Number(p.length ?? defaults.length) }; radar = { radarFov: Number(p.radarFovDeg ?? 60), radarRadius: Number(p.radarRadiusM ?? 0.5), safetyScale: Number(p.safetyScale ?? 1.1) }; } } catch (e) { } } cfg = cfg || defaults; setVal('factsheetSpeedMin', cfg.speedMin); setVal('factsheetSpeedMax', cfg.speedMax); setVal('factsheetAccelMax', cfg.accelerationMax); setVal('factsheetDecelMax', cfg.decelerationMax); setVal('factsheetHeightMin', cfg.heightMin); setVal('factsheetHeightMax', cfg.heightMax); setVal('factsheetWidth', cfg.width); setVal('factsheetLength', cfg.length); setVal('factsheetRadarFov', radar.radarFov); setVal('factsheetRadarRadius', radar.radarRadius); setVal('factsheetSafetyScale', radar.safetyScale); }
async function applyPhysicalParamsToMotionControl(id, params, extra) { const patch = {}; const n = (v) => { const x = Number(v); return isFinite(x) ? x : undefined; }; const sMin = n(params.speedMin), sMax = n(params.speedMax), aMax = n(params.accelerationMax), dMax = n(params.decelerationMax); if (typeof sMax !== 'undefined') { const candidate = Number(sMax); if (isFinite(candidate) && candidate >= 0 && candidate <= 2) { patch.speed = candidate; } } if (typeof sMin !== 'undefined') patch.speed_min = sMin; if (typeof sMax !== 'undefined') patch.speed_max = sMax; if (typeof aMax !== 'undefined') patch.acceleration_max = aMax; if (typeof dMax !== 'undefined') patch.deceleration_max = dMax; const hMin = n(params.heightMin), hMax = n(params.heightMax), w = n(params.width), l = n(params.length); if (typeof hMin !== 'undefined') patch.height_min = hMin; if (typeof hMax !== 'undefined') patch.height_max = hMax; if (typeof w !== 'undefined') patch.width = w; if (typeof l !== 'undefined') patch.length = l; const rf = n(extra?.radarFov), rr = n(extra?.radarRadius), ss = n(extra?.safetyScale); if (typeof rf !== 'undefined') patch.radar_fov_deg = rf; if (typeof rr !== 'undefined') patch.radar_radius_m = rr; if (typeof ss !== 'undefined') patch.safety_scale = ss; const errs = []; if (typeof sMin !== 'undefined' && typeof sMax !== 'undefined' && sMin > sMax) errs.push('最小速度不能大于最大速度'); if (typeof patch.speed !== 'undefined') { if (!(patch.speed >= 0 && patch.speed <= 2)) errs.push('运行速度需在[0,2]范围内'); if (typeof sMin !== 'undefined' && patch.speed < sMin) errs.push('运行速度应不低于最小速度'); } if (typeof rf !== 'undefined') { if (!(rf >= 1 && rf <= 360)) errs.push('雷达扇面需在[1,360]范围内'); } if (typeof rr !== 'undefined') { if (!(rr >= 0.01 && rr <= 10)) errs.push('雷达半径需在[0.01,10]范围内'); } if (typeof ss !== 'undefined') { if (!(ss >= 1.0 && ss <= 5.0)) errs.push('安全范围系数需在[1.0,5.0]范围内'); } if (errs.length) { try { window.printErrorToStatus('参数无效: ' + errs.join('; '), '配置'); } catch (_) { } return; } window.MOTION_LIMITS = { speedMin: sMin, speedMax: sMax, accelerationMax: aMax, decelerationMax: dMax }; const resp = await fetch(`${API_BASE_URL}/agv/${encodeURIComponent(id)}/sim/settings`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(patch) }); if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`); }
async function updateCollisionParameters(id, params) { try { const robot = registeredRobots.find(r => r.robot_name === id); if (robot) { robot.collisionParams = { width: params.width, length: params.length, heightMin: params.heightMin, heightMax: params.heightMax }; } } catch (e) { } }

function initWebSocket() { try { const url = (location.protocol === 'https:' ? 'wss' : 'ws') + '://' + location.host + '/ws'; ws = new WebSocket(url); ws.onopen = () => { document.getElementById('status').textContent = 'WS已连接'; setTimeout(() => document.getElementById('status').textContent = '准备就绪', 2000); }; ws.onmessage = (ev) => { try { const msg = JSON.parse(ev.data);
      const shouldAcceptMqttMsg = (m) => {
        const ip = (window.VDA5050_FILTER_BROKER_IP !== undefined && window.VDA5050_FILTER_BROKER_IP !== null) ? String(window.VDA5050_FILTER_BROKER_IP).trim() : '';
        const portRaw = window.VDA5050_FILTER_BROKER_PORT;
        const hasPort = !(portRaw === undefined || portRaw === null || portRaw === '');
        const port = hasPort ? Number(portRaw) : NaN;
        if (!ip && !hasPort) return true;
        if (hasPort && !isFinite(port)) return false;

        const broker = (m && m.broker != null) ? String(m.broker).trim() : '';
        if (!broker) return false;

        let hostPart = broker;
        let portPart = null;
        if (broker.startsWith('[')) {
          const end = broker.indexOf(']');
          if (end > 1) {
            hostPart = broker.slice(1, end);
            const rest = broker.slice(end + 1);
            const mi = /^:(\d+)$/.exec(rest);
            if (mi) portPart = Number(mi[1]);
          }
        } else {
          const idx = broker.lastIndexOf(':');
          if (idx > 0 && idx < broker.length - 1) {
            const tail = broker.slice(idx + 1);
            if (/^\d+$/.test(tail)) {
              hostPart = broker.slice(0, idx);
              portPart = Number(tail);
            }
          }
        }

        const host = String(hostPart || '').trim();
        if (ip && host !== ip) return false;
        if (hasPort) {
          if (portPart === null || !isFinite(portPart)) return false;
          if (Number(portPart) !== Number(port)) return false;
        }
        return true;
      };

      if (msg && typeof msg.type === 'string' && msg.type.startsWith('mqtt_')) {
        if (!shouldAcceptMqttMsg(msg)) return;
      }

      if (msg && msg.type === 'mqtt_state' && msg.payload) { applyWSState(msg.payload); try { applyEquipmentWSState(msg.payload); } catch (_) { } } else if (msg && msg.type === 'mqtt_visualization' && msg.payload) { applyWSVisualization(msg.payload); } else if (msg && msg.type === 'mqtt_instantActions' && msg.payload) { try { handleWSInstantActions(msg.payload); } catch (e) { } } else if (msg && msg.type === 'pallet_action') { const serial = String(msg.serial || msg.serial_number || '').trim(); const op = String(msg.operation || '').trim(); const recfile = String(msg.recfile || '').trim(); if (serial && op) handlePalletAction(serial, op, recfile); } } catch (e) { } }; ws.onclose = () => { document.getElementById('status').textContent = 'WS断开，重连中...'; try { registeredRobots.forEach(r => { r.commConnected = false; r.instanceStatus = '断开'; }); updateRobotList(); } catch (e) { } setTimeout(initWebSocket, 1000); }; ws.onerror = () => { try { ws.close(); } catch (e) { } }; } catch (err) { } }
function handleWSInstantActions(payload) { try { const serial = String((payload && (payload.serial_number || payload.serialNumber)) || '').trim(); if (!serial) return; const robot = registeredRobots.find(r => r.robot_name === serial); if (!robot) return; const actions = Array.isArray(payload.actions) ? payload.actions : []; for (const a of actions) { const type = String((a && (a.action_type || a.actionType)) || '').trim(); const aid = String((a && (a.action_id || a.actionId)) || '').trim(); if (type.toLowerCase() === 'stoppause' && /^collision_stop_/i.test(aid)) { robot._collisionActive = true; } else if (type.toLowerCase() === 'startpause' && /^collision_start_/i.test(aid)) { robot._collisionActive = false; } } } catch (e) { } }
function truncateToOneDecimal(v) { const num = Number(v); if (!isFinite(num)) return 0; const t = Math.floor(num * 10) / 10; return Math.max(0, Math.min(100, t)); }
;(function(){ const origReg = window.registerRobot; if (typeof origReg === 'function') { window.registerRobot = async function() { const name = (document.getElementById('robotName')?.value || '').trim(); await origReg(); try { const resp = await fetch(`${API_BASE_URL}/agvs`); if (resp && resp.ok) { const list = await resp.json(); const exists = Array.isArray(list) && list.some(r => String(r.robot_name || r.serial_number || '').trim() === name); if (exists && name) { try { await fetch(`${API_BASE_URL}/agvs/${encodeURIComponent(name)}/pm2/start`, { method: 'POST' }); } catch (_) { } } } } catch (_) { } }; } const origDel = window.removeRobot; if (typeof origDel === 'function') { window.removeRobot = async function(serial) { try { await fetch(`${API_BASE_URL}/agvs/${encodeURIComponent(serial)}/pm2/stop`, { method: 'POST' }); } catch (_) { } await origDel(serial); }; } })();

let buildModalEventSource = null;
let buildModalFinished = false;
function openBuildModal(titleText) { const modal = document.getElementById('buildModal'); const title = modal ? modal.querySelector('.modal-title') : null; const phase = document.getElementById('buildModalPhase'); const log = document.getElementById('buildModalLog'); const closeBtn = document.getElementById('buildModalCloseBtn'); const okBtn = document.getElementById('buildModalOkBtn'); buildModalFinished = false; if (title) title.textContent = titleText ? String(titleText) : '实例启动'; if (phase) phase.textContent = '准备中...'; if (log) log.textContent = ''; if (closeBtn) closeBtn.disabled = true; if (okBtn) okBtn.disabled = true; if (modal) modal.style.display = 'flex'; }
function appendBuildModalLog(lineText) { const log = document.getElementById('buildModalLog'); if (!log) return; const s = String(lineText || ''); if (!s) return; log.textContent += s + '\n'; log.scrollTop = log.scrollHeight; }
function setBuildModalPhase(phaseText) { const phase = document.getElementById('buildModalPhase'); if (phase) phase.textContent = String(phaseText || ''); }
function finishBuildModal() { buildModalFinished = true; const closeBtn = document.getElementById('buildModalCloseBtn'); const okBtn = document.getElementById('buildModalOkBtn'); if (closeBtn) closeBtn.disabled = false; if (okBtn) okBtn.disabled = false; }
function closeBuildModal() { try { if (buildModalEventSource) { buildModalEventSource.close(); buildModalEventSource = null; } } catch (_) { } const modal = document.getElementById('buildModal'); if (modal) modal.style.display = 'none'; }
async function startAgvPm2WithBuildStream(id) { const serial = String(id || '').trim(); if (!serial) throw new Error('serial empty'); openBuildModal(`实例启动: ${serial}`); appendBuildModalLog(`[${new Date().toLocaleTimeString()}] 请求启动...`); return await new Promise((resolve, reject) => { let done = false; const url = `${API_BASE_URL}/agvs/${encodeURIComponent(serial)}/pm2/start/stream`; const es = new EventSource(url); buildModalEventSource = es; const safeClose = () => { try { es.close(); } catch (_) { } if (buildModalEventSource === es) buildModalEventSource = null; }; es.addEventListener('status', (ev) => { try { const data = JSON.parse(String(ev.data || '{}')); const phase = String(data.phase || ''); if (phase === 'prepare') { setBuildModalPhase('准备中...'); return; } if (phase === 'precheck') { const needBuild = !!data.need_build; setBuildModalPhase(needBuild ? '检测到缺少可执行文件，开始编译...' : '已存在可执行文件，跳过编译，准备启动...'); if (!needBuild) appendBuildModalLog(`[${new Date().toLocaleTimeString()}] 已存在可执行文件，跳过编译`); return; } if (phase === 'build_start') { setBuildModalPhase('编译中...'); appendBuildModalLog(`[${new Date().toLocaleTimeString()}] 编译开始`); return; } if (phase === 'build_done') { setBuildModalPhase('编译完成，准备启动...'); appendBuildModalLog(`[${new Date().toLocaleTimeString()}] 编译完成`); return; } if (phase === 'pm2_start') { setBuildModalPhase('启动中...'); appendBuildModalLog(`[${new Date().toLocaleTimeString()}] 启动中`); return; } } catch (_) { } }); es.addEventListener('log', (ev) => { try { const data = JSON.parse(String(ev.data || '{}')); const line = data.line != null ? String(data.line) : ''; if (line) appendBuildModalLog(line); } catch (_) { } }); es.addEventListener('done', (ev) => { done = true; safeClose(); try { const data = JSON.parse(String(ev.data || '{}')); setBuildModalPhase('启动完成'); appendBuildModalLog(`[${new Date().toLocaleTimeString()}] 启动完成`); finishBuildModal(); resolve(data); } catch (e) { setBuildModalPhase('启动完成'); finishBuildModal(); resolve({ started: true, serial: serial }); } }); es.addEventListener('fail', (ev) => { done = true; safeClose(); try { const data = JSON.parse(String(ev.data || '{}')); const msg = data.error ? String(data.error) : '启动失败'; setBuildModalPhase('启动失败'); appendBuildModalLog(`[${new Date().toLocaleTimeString()}] ${msg}`); finishBuildModal(); reject(new Error(msg)); } catch (e) { setBuildModalPhase('启动失败'); finishBuildModal(); reject(new Error('启动失败')); } }); es.onerror = () => { if (done || buildModalFinished) return; done = true; safeClose(); setBuildModalPhase('连接失败'); appendBuildModalLog(`[${new Date().toLocaleTimeString()}] 连接失败或服务端中断`); finishBuildModal(); reject(new Error('连接失败或服务端中断')); }; }); }
async function toggleAgvPm2(serialNumber) { const id = String(serialNumber || '').trim(); if (!id) return; const robot = registeredRobots.find(r => r.robot_name === id); if (robot && robot._pm2Busy) return; const currentStatus = String(robot?.instanceStatus || '停止'); const shouldStop = currentStatus === '启动'; const action = shouldStop ? 'stop' : 'start'; try { if (robot) robot._pm2Busy = true; try { updateRobotList(); } catch (_) { } if (action === 'start') { await startAgvPm2WithBuildStream(id); } else { const resp = await fetch(`${API_BASE_URL}/agvs/${encodeURIComponent(id)}/pm2/stop`, { method: 'POST' }); if (!resp.ok) { let msg = `HTTP ${resp.status}: ${resp.statusText}`; try { const data = await resp.json(); const detail = (data && (data.detail || data.message)) ? String(data.detail || data.message) : ''; if (detail) msg += ` | ${detail}`; } catch (_) { try { const text = await resp.text(); const t = String(text || '').trim(); if (t) msg += ` | ${t}`; } catch (_) { } } throw new Error(msg); } } if (robot) { robot.instanceStatus = shouldStop ? '停止' : '启动'; if (shouldStop) { robot.commConnected = false; robot.lastUpdateTs = 0; } } try { const el = document.getElementById('status'); if (el) el.textContent = `${id} 实例${shouldStop ? '停止' : '启动'}完成`; setTimeout(() => { try { const el2 = document.getElementById('status'); if (el2 && String(el2.textContent || '').includes('完成')) el2.textContent = '准备就绪'; } catch (_) { } }, 2000); } catch (_) { } } catch (err) { try { window.printErrorToStatus('实例操作失败: ' + (err && err.message ? err.message : String(err)), 'PM2'); } catch (_) { } } finally { if (robot) robot._pm2Busy = false; try { updateRobotList(); } catch (_) { } } }
function applyWSState(stateMsg) {
  try {
    const payload = stateMsg || {};
    const sn = String(payload.serial_number || payload.serialNumber || '').trim();
    if (!sn) return;
    const robot = registeredRobots.find(r => r.robot_name === sn);
    if (!robot) return; const agvPos = payload.agv_position || payload.agvPosition || {}; const batt = payload.battery_state || payload.batteryState || {}; const fork = payload.fork_state || payload.forkState || {}; const opMode = String(payload.operating_mode || payload.operatingMode || '').trim(); if (opMode) robot.operatingMode = String(opMode).toUpperCase(); const mapId = (agvPos && (agvPos.map_id || agvPos.mapId)) || null; const px = Number(agvPos && agvPos.x); const py = Number(agvPos && agvPos.y); const th = Number(agvPos && (agvPos.theta ?? agvPos.orientation)); const bc = Number(batt && (batt.battery_charge || batt.batteryCharge)); if (isFinite(bc)) robot.battery = truncateToOneDecimal(bc); if (mapId) robot.currentMap = String(mapId); if (isFinite(px) && isFinite(py)) { robot.currentPosition = { x: px, y: py, theta: isFinite(th) ? th : (robot.currentPosition?.theta || 0) }; } const fh = Number(fork && (fork.fork_height ?? fork.forkHeight)); if (isFinite(fh)) robot.forkHeight = fh; robot.instanceStatus = '启动'; robot.commConnected = true; robot.lastUpdateTs = Date.now(); try { const errs = Array.isArray(payload.errors) ? payload.errors : []; let collision = false; for (const e of errs) { const et = String((e && (e.errorType ?? e.error_type ?? e.code)) || '').trim(); const en = String((e && (e.errorName ?? e.error_name)) || '').trim().toLowerCase(); const ds = String((e && (e.errorDescription ?? e.message ?? e.reason)) || '').trim().toLowerCase(); if (et === '54231' || en === 'robotblocked' || ds.includes('robot is blocked')) { collision = true; break; } } robot._collisionActive = collision; robot.stateErrors = errs.map(e => ({ errorType: String(e.errorType ?? e.code ?? ''), errorName: String(e.errorName ?? e.type ?? '') })); } catch (_) { } try { const loads = Array.isArray(payload.loads) ? payload.loads : []; const has = loads.length > 0; let model = null; if (has) { const l0 = loads[0] || {}; const dim = l0.loadDimensions || l0.load_dimensions || {}; const w = Number(dim && (dim.width_m ?? dim.width)); const l = Number(dim && (dim.length_m ?? dim.length)); const h = Number(dim && (dim.height_m ?? dim.height)); if (isFinite(w) && isFinite(l)) { model = { footprint: { width_m: w, length_m: l, height_m: (isFinite(h) ? h : undefined) } }; } } const curId = has ? String((loads[0] && (loads[0].loadId ?? loads[0].load_id)) || '') : ''; const lastId = String(robot._lastLoadId || ''); robot._lastLoadId = curId; const prevHas = !!robot._lastHasLoad; robot._lastHasLoad = has; robot._latestLoadsArray = loads; if (has) { if (model) robot.shelfModel = model; if (!robot.hasPallet && !robot._palletAnimLoading && !robot._palletAnimUnloading) { loadPallet(sn); } else if (!prevHas && !robot._palletAnimLoading) { loadPallet(sn); } } else { if ((robot.hasPallet || robot._palletAnimLoading || prevHas) && !robot._palletAnimUnloading) { unloadPallet(sn); } } } catch (_) { } try { reportVisualizationHazards(robot); } catch (_) { } } catch (e) { } }
async function fetchShelfModel(recfile) { try { let path = String(recfile || '').trim(); if (!path) return null; path = path.replace(/\\/g, '/'); let url = ''; if (/^\/shelf\//i.test(path)) { url = path; } else if (/\/shelf\//i.test(path)) { url = path.substring(path.toLowerCase().indexOf('/shelf/')); } else if (/\.shelf$/i.test(path)) { const name = path.split('/').pop(); url = '/shelf/' + name; } else { const base = path.split('/').pop(); url = '/shelf/' + base; } if (!url.startsWith('/')) url = '/' + url; const resp = await fetch(url); if (!resp.ok) return null; return await resp.json(); } catch (_) { return null; } }
async function handlePalletAction(serial, operation, recfile) { const robot = registeredRobots.find(r => r.robot_name === serial); if (!robot) return; const key = String(operation || '') + '|' + String(recfile || ''); if (robot._lastPalletOpKey === key) return; robot._lastPalletOpKey = key; const op = String(operation || '').toLowerCase(); if (op === 'pick') { const model = await fetchShelfModel(recfile); if (model) robot.shelfModel = model; } else if (op === 'drop') { } }
let renderTimer = null; let lastListUpdate = 0; function startRenderLoop() { if (renderTimer) return; renderTimer = setInterval(() => { try { drawMap(); } catch (e) { } const now = Date.now(); if (now - lastListUpdate > 250) { lastListUpdate = now; try { updateRobotList(); } catch (e) { } } }, 10); }
function applyStatusUpdates(list) { (list || []).forEach(st => { const robot = registeredRobots.find(r => r.robot_name === st.serial_number); if (robot) { if (st.battery_level !== undefined && st.battery_level !== null) { const bc = Number(st.battery_level); if (isFinite(bc)) robot.battery = truncateToOneDecimal(bc); } robot.currentPosition = st.position || robot.currentPosition; robot.currentMap = st.current_map || robot.currentMap; robot.instanceStatus = (st.status === 'running') ? '启动' : '停止'; robot.commConnected = true; robot.lastUpdateTs = Date.now(); } }); }
async function fetchRobotStatus(id) { try { const resp = await fetch(`${API_BASE_URL}/agv/${encodeURIComponent(id)}/status`); if (!resp.ok) return; const st = await resp.json(); const robot = registeredRobots.find(r => r.robot_name === (st.serial_number || id)); if (!robot) return; if (st.battery_level !== undefined && st.battery_level !== null) { const bc = Number(st.battery_level); if (isFinite(bc)) robot.battery = truncateToOneDecimal(bc); } if (st.current_map) { robot.currentMap = st.current_map; } if (st.position && typeof st.position.x !== 'undefined' && typeof st.position.y !== 'undefined') { const px = Number(st.position.x); const py = Number(st.position.y); const th = Number(st.position.theta ?? 0); if (isFinite(px) && isFinite(py)) { robot.currentPosition = { x: px, y: py, theta: isFinite(th) ? th : (robot.currentPosition?.theta || 0) }; } } robot.instanceStatus = (st.status === 'running') ? '启动' : '停止'; robot.commConnected = true; robot.lastUpdateTs = Date.now(); } catch (_) { } }
async function pollAllRobotStatus() { const ids = registeredRobots.map(r => r.robot_name); if (ids.length === 0) return; await Promise.all(ids.map(id => fetchRobotStatus(id))); updateRobotList(); drawMap(); }
async function selectRobot(id) { selectedRobotId = id; updateRobotList(); }
let keyboardGuardLastTs = 0;
let keyboardControlKeys = new Set();
let keyboardControlTimer = null;
let keyboardControlLastTickTs = 0;
let keyboardControlBusy = false;
let keyboardControlHintLastTs = 0;
function showKeyboardControlHint() { const now = Date.now(); if (now - keyboardControlHintLastTs < 1500) return; keyboardControlHintLastTs = now; try { const el = document.getElementById('status'); if (!el) return; const text = '键盘控制：W/S 前后，A/D 左右旋转（Shift 加速，方向键同效）'; el.textContent = text; setTimeout(() => { try { const el2 = document.getElementById('status'); if (el2 && el2.textContent === text) el2.textContent = '准备就绪'; } catch (_) { } }, 1200); } catch (_) { } }
function normalizeKeyboardControlKey(key) { const k = String(key || ''); if (!k) return ''; if (k === 'ArrowUp') return 'w'; if (k === 'ArrowDown') return 's'; if (k === 'ArrowLeft') return 'a'; if (k === 'ArrowRight') return 'd'; return k.toLowerCase(); }
function isKeyboardControlKey(key) { const k = normalizeKeyboardControlKey(key); return k === 'w' || k === 'a' || k === 's' || k === 'd'; }
function getSelectedRobotIfControllable() { if (!selectedRobotId) return null; const robot = registeredRobots.find(r => r.robot_name === String(selectedRobotId)); const opMode = String(robot?.operatingMode || 'AUTOMATIC').toUpperCase(); if (opMode === 'SERVICE') return robot || null; const now = Date.now(); if (now - keyboardGuardLastTs > 1000) { keyboardGuardLastTs = now; try { window.printErrorToStatus(`当前仿真车无控制权（operatingMode=${opMode}），不可键盘控制`, '键盘控制'); } catch (_) { } } return null; }
function stopKeyboardControlLoop() { if (keyboardControlTimer) { clearInterval(keyboardControlTimer); keyboardControlTimer = null; } keyboardControlKeys = new Set(); keyboardControlLastTickTs = 0; keyboardControlBusy = false; }
function startKeyboardControlLoop() { if (keyboardControlTimer) return; keyboardControlLastTickTs = performance.now(); keyboardControlTimer = setInterval(async () => { if (!selectedRobotId || keyboardControlKeys.size === 0) { stopKeyboardControlLoop(); return; } const robot = getSelectedRobotIfControllable(); if (!robot) { stopKeyboardControlLoop(); return; } if (keyboardControlBusy) return; const now = performance.now(); const dt = Math.max(0.01, Math.min(0.2, (now - keyboardControlLastTickTs) / 1000)); keyboardControlLastTickTs = now; const hasForward = keyboardControlKeys.has('w'); const hasBackward = keyboardControlKeys.has('s'); const hasLeft = keyboardControlKeys.has('a'); const hasRight = keyboardControlKeys.has('d'); const linearDir = (hasForward ? 1 : 0) + (hasBackward ? -1 : 0); const angularDir = (hasLeft ? 1 : 0) + (hasRight ? -1 : 0); if (linearDir === 0 && angularDir === 0) return; const shiftPressed = keyboardControlKeys.has('shift'); const speedScale = shiftPressed ? 2.0 : 1.0; const linearSpeed = 0.5 * speedScale; const angularSpeed = 1.2 * speedScale; const pos = robot?.currentPosition || robot?.initialPosition || { x: 0, y: 0, theta: 0 }; const heading = Number(pos.theta ?? pos.orientation ?? 0) || 0; const forwardStep = linearSpeed * dt; const dtheta = angularDir !== 0 ? (angularDir * angularSpeed * dt) : 0; const translateStep = linearDir !== 0 ? (linearDir * forwardStep) : 0; const dx = translateStep !== 0 ? (-translateStep * Math.cos(heading)) : 0; const dy = translateStep !== 0 ? (-translateStep * Math.sin(heading)) : 0; keyboardControlBusy = true; try { if (translateStep !== 0) await moveTranslate(selectedRobotId, dx, dy, linearDir > 0 ? 'forward' : 'backward'); if (dtheta !== 0) await moveRotate(selectedRobotId, dtheta); } catch (_) { } finally { keyboardControlBusy = false; } }, 50); }
function setupKeyboardControl() { window.addEventListener('keydown', function (e) { const tag = document.activeElement && document.activeElement.tagName; if (tag && ['INPUT', 'TEXTAREA', 'SELECT'].includes(tag)) return; const k = normalizeKeyboardControlKey(e.key); if (!k) return; if (k === 'shift') { keyboardControlKeys.add('shift'); return; } if (!isKeyboardControlKey(e.key)) return; e.preventDefault(); if (!selectedRobotId) return; const robot = getSelectedRobotIfControllable(); if (!robot) return; keyboardControlKeys.add(normalizeKeyboardControlKey(e.key)); showKeyboardControlHint(); startKeyboardControlLoop(); }); window.addEventListener('keyup', function (e) { const k = normalizeKeyboardControlKey(e.key); if (!k) return; if (k === 'shift') keyboardControlKeys.delete('shift'); if (isKeyboardControlKey(e.key)) keyboardControlKeys.delete(normalizeKeyboardControlKey(e.key)); if (keyboardControlKeys.size === 0 || (keyboardControlKeys.size === 1 && keyboardControlKeys.has('shift'))) stopKeyboardControlLoop(); }); window.addEventListener('blur', function () { stopKeyboardControlLoop(); }); }
async function moveTranslate(id, dx, dy, movementState) { const serial = encodeURIComponent(String(id || '')); if (!serial) return; try { const resp = await fetch(`${API_BASE_URL}/agv/${serial}/move/translate`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ dx, dy, movement_state: movementState }) }); if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`); } catch (_) { } }
async function moveRotate(id, dtheta) { const serial = encodeURIComponent(String(id || '')); if (!serial) return; try { const resp = await fetch(`${API_BASE_URL}/agv/${serial}/move/rotate`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ dtheta }) }); if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`); } catch (_) { } }

let commGuardTimer = null; function startCommStatusGuard() { if (commGuardTimer) return; commGuardTimer = setInterval(() => { const now = Date.now(); let changed = false; registeredRobots.forEach(r => { if (!r.lastUpdateTs || (now - r.lastUpdateTs > 5000)) { if (r.commConnected || r.instanceStatus === '启动') { r.commConnected = false; r.instanceStatus = '断开'; changed = true; } } }); if (changed) { updateRobotList(); drawMap(); } }, 1000); }

async function openSettingsModal() { const modal = document.getElementById('settingsModal'); modal.style.display = 'flex'; requestAnimationFrame(() => { const content = modal.querySelector('.modal-content'); if (content && !content.dataset.fixedHeight) { const rect = content.getBoundingClientRect(); content.style.height = rect.height + 'px'; content.style.maxHeight = rect.height + 'px'; content.dataset.fixedHeight = '1'; } }); try { const resp = await fetch(`${API_BASE_URL}/sim/settings`); if (resp && resp.ok) { const data = await resp.json(); prefillSimSettings(data); } } catch (e) { try { window.printErrorToStatus('读取当前仿真设置失败: ' + (e && e.message ? e.message : String(e)), '设置'); } catch (_) { } } }
function closeSettingsModal() { document.getElementById('settingsModal').style.display = 'none'; }
function openErrorInjectModal() { const modal = document.getElementById('errorInjectModal'); if (!modal) return; modal.style.display = 'flex'; requestAnimationFrame(() => { const content = modal.querySelector('.modal-content'); if (content && !content.dataset.fixedHeight) { const rect = content.getBoundingClientRect(); content.style.height = rect.height + 'px'; content.style.maxHeight = rect.height + 'px'; content.dataset.fixedHeight = '1'; } }); try { populateErrorInjectRobotOptions(); } catch (_) { } try { const sel = document.getElementById('errorInjectRobotSelect'); if (sel && !sel.value && selectedRobotId) { sel.value = selectedRobotId; } } catch (_) { } try { updateErrorInjectPreview(); } catch (_) { } }
function closeErrorInjectModal() { const modal = document.getElementById('errorInjectModal'); if (modal) modal.style.display = 'none'; }
function openRegisterModal() { const modal = document.getElementById('registerModal'); if (!modal) return; modal.style.display = 'flex'; requestAnimationFrame(() => { const content = modal.querySelector('.modal-content'); if (content && !content.dataset.fixedHeight) { const rect = content.getBoundingClientRect(); content.style.height = rect.height + 'px'; content.style.maxHeight = rect.height + 'px'; content.dataset.fixedHeight = '1'; } }); }
function closeRegisterModal() { const modal = document.getElementById('registerModal'); if (modal) modal.style.display = 'none'; }
window.openRegisterModal = openRegisterModal;
window.closeRegisterModal = closeRegisterModal;
function populateErrorInjectRobotOptions() { const sel = document.getElementById('errorInjectRobotSelect'); if (!sel) return; const current = String(sel.value || ''); const preferred = String(selectedRobotId || ''); sel.innerHTML = '<option value="">请选择仿真车</option>'; const list = Array.isArray(registeredRobots) ? registeredRobots.slice() : []; list.sort((a, b) => String(a.robot_name || '').localeCompare(String(b.robot_name || ''))); for (const r of list) { const id = String(r && r.robot_name ? r.robot_name : '').trim(); if (!id) continue; const opt = document.createElement('option'); opt.value = id; opt.textContent = id; sel.appendChild(opt); } if (current && list.some(r => String(r.robot_name || '').trim() === current)) { sel.value = current; } else if (preferred && list.some(r => String(r.robot_name || '').trim() === preferred)) { sel.value = preferred; } }
function updateErrorInjectPreview() { const preview = document.getElementById('errorInjectPreview'); if (!preview) return; const sel = document.getElementById('errorInjectRobotSelect'); const robotId = String(sel && sel.value ? sel.value : '').trim(); if (!robotId) { preview.textContent = '-'; return; } const robot = (Array.isArray(registeredRobots) ? registeredRobots : []).find(r => String(r.robot_name || '').trim() === robotId); if (!robot) { preview.textContent = '未找到目标仿真车'; return; } const pos = robot.currentPosition || robot.initialPosition || null; if (!pos || !isFinite(Number(pos.x)) || !isFinite(Number(pos.y))) { preview.textContent = '目标仿真车暂无有效位姿'; return; } const theta = Number(pos.theta ?? pos.orientation ?? 0) || 0; const mapId = String(robot.currentMap || '').trim(); const all = Array.isArray(registeredRobots) ? registeredRobots : []; const hasPose = (r) => { const p = r && (r.currentPosition || r.initialPosition); return !!(p && isFinite(Number(p.x)) && isFinite(Number(p.y))); }; const candidates = all.filter(r => String(r && r.robot_name ? r.robot_name : '').trim() && String(r.robot_name || '').trim() !== robotId && hasPose(r) && String(r.currentMap || '').trim() === mapId); const a = `目标仿真车: ${escapeHtml(robotId)}`; const b = `目标当前位姿: (${Number(pos.x).toFixed(2)}, ${Number(pos.y).toFixed(2)}) θ=${theta.toFixed(2)}`; const c = mapId ? `地图: ${escapeHtml(mapId)}` : '地图: -'; const d = candidates.length > 0 ? `可碰撞目标: ${candidates.length}（注入时随机选择 1 辆并重叠坐标，朝向取反）` : '可碰撞目标: 0（需同地图至少 2 辆车且位姿有效）'; preview.innerHTML = `${a}<br>${b}<br>${c}<br>${d}`; }
async function submitErrorInjection() { const typeSel = document.getElementById('errorInjectType'); const type = String(typeSel && typeSel.value ? typeSel.value : '').trim(); const robotSel = document.getElementById('errorInjectRobotSelect'); const robotId = String(robotSel && robotSel.value ? robotSel.value : '').trim(); if (!type) { try { window.printErrorToStatus('请选择错误注入类型', '错误注入'); } catch (_) { } return; } if (String(type).toLowerCase() !== 'collision') { try { window.printErrorToStatus('当前仅支持碰撞注入', '错误注入'); } catch (_) { } return; } if (!robotId) { try { window.printErrorToStatus('请选择目标仿真车', '错误注入'); } catch (_) { } return; } const all = Array.isArray(registeredRobots) ? registeredRobots : []; const robot = all.find(r => String(r && r.robot_name ? r.robot_name : '').trim() === robotId); if (!robot) { try { window.printErrorToStatus('未找到目标仿真车', '错误注入'); } catch (_) { } return; } const mapId = String(robot.currentMap || '').trim(); if (!mapId) { try { window.printErrorToStatus('目标仿真车未设置地图，无法进行碰撞注入', '错误注入'); } catch (_) { } return; } const hasPose = (r) => { const p = r && (r.currentPosition || r.initialPosition); return !!(p && isFinite(Number(p.x)) && isFinite(Number(p.y))); }; const candidates = all.filter(r => String(r && r.robot_name ? r.robot_name : '').trim() && String(r.robot_name || '').trim() !== robotId && hasPose(r) && String(r.currentMap || '').trim() === mapId); if (candidates.length === 0) { try { window.printErrorToStatus('当前地图没有其它可用仿真车，无法进行碰撞注入', '错误注入'); } catch (_) { } return; } const other = candidates[Math.floor(Math.random() * candidates.length)]; const otherPos = other.currentPosition || other.initialPosition || null; if (!otherPos || !isFinite(Number(otherPos.x)) || !isFinite(Number(otherPos.y))) { try { window.printErrorToStatus('随机选择的目标仿真车位姿无效，请重试', '错误注入'); } catch (_) { } return; } const normalizeTheta = (t) => { let v = Number(t) || 0; while (v > Math.PI) v -= 2 * Math.PI; while (v <= -Math.PI) v += 2 * Math.PI; return v; }; const baseTheta = Number(otherPos.theta ?? otherPos.orientation ?? 0) || 0; const newTheta = normalizeTheta(baseTheta + Math.PI); const manu = robot?.manufacturer || ''; const ver = robot?.vda_version || '2.0.0'; const waitMs = (ms) => new Promise(resolve => setTimeout(resolve, ms)); try { try { document.getElementById('status').textContent = `碰撞注入中: ${robotId}`; } catch (_) { } await agvPostInstant(robotId, buildAgvChangeControlInstant(robotId, manu, ver, 'SERVICE')); await waitMs(1000); await agvPostInstant(robotId, buildAgvInitPositionInstant(robotId, manu, ver, { x: Number(otherPos.x), y: Number(otherPos.y), theta: newTheta, mapId: mapId })); await waitMs(1000); await agvPostInstant(robotId, buildAgvChangeControlInstant(robotId, manu, ver, 'AUTOMATIC')); try { const otherId = String(other.robot_name || '').trim(); document.getElementById('status').textContent = `碰撞注入已发送: ${robotId} ⇢ ${otherId}`; } catch (_) { } closeErrorInjectModal(); } catch (e) { try { window.printErrorToStatus('碰撞注入失败: ' + (e && e.message ? e.message : String(e)), '错误注入'); } catch (_) { } } }
function openPerfModal() { const modal = document.getElementById('perfModal'); if (!modal) return; modal.style.display = 'flex'; requestAnimationFrame(() => { const content = modal.querySelector('.modal-content'); if (content && !content.dataset.fixedHeight) { const rect = content.getBoundingClientRect(); content.style.height = rect.height + 'px'; content.style.maxHeight = rect.height + 'px'; content.dataset.fixedHeight = '1'; } }); try { refreshPerfPanel(); } catch (_) { } }
function closePerfModal() { const modal = document.getElementById('perfModal'); if (modal) modal.style.display = 'none'; }
function prefillSimSettings(s) { const setVal = (id, v) => { const el = document.getElementById(id); if (el && typeof v !== 'undefined' && v !== null) el.value = String(v); }; setVal('settingsTimeScale', s.sim_time_scale); setVal('settingsStateFreq', s.state_frequency); setVal('settingsVisFreq', s.visualization_frequency); setVal('settingsActionTime', s.action_time); setVal('settingsFrontendPoll', s.frontend_poll_interval_ms); }
async function saveSimSettings() { try { const patch = {}; const n = (v) => { const x = Number(v); return isFinite(x) ? x : undefined; }; const intIn = (x, a, b) => Number.isInteger(x) && x >= a && x <= b; const numIn = (x, a, b, openLeft = false, openRight = false) => { if (typeof x !== 'number' || !isFinite(x)) return false; const leftOk = openLeft ? (x > a) : (x >= a); const rightOk = openRight ? (x < b) : (x <= b); return leftOk && rightOk; }; const ts = n(document.getElementById('settingsTimeScale').value); const sf = n(document.getElementById('settingsStateFreq').value); const vf = n(document.getElementById('settingsVisFreq').value); const at = n(document.getElementById('settingsActionTime').value); const fp = n(document.getElementById('settingsFrontendPoll').value); const errs = []; if (typeof ts !== 'undefined') { if (!numIn(ts, 0, 10)) errs.push('时间缩放需在(0,10)内'); else patch.sim_time_scale = ts; } if (typeof sf !== 'undefined') { if (!intIn(sf, 1, 10)) errs.push('状态频率需为[1,10]的正整数'); else patch.state_frequency = parseInt(sf, 10); } if (typeof vf !== 'undefined') { if (!intIn(vf, 1, 10)) errs.push('可视化频率需为[1,10]的正整数'); else patch.visualization_frequency = parseInt(vf, 10); } if (typeof at !== 'undefined') { if (!numIn(at, 1, 10)) errs.push('动作时长需在[1,10]内'); else patch.action_time = at; } if (typeof fp !== 'undefined') { if (!intIn(fp, 10, 1000)) errs.push('前端轮询间隔需为[10,1000]的正整数'); else patch.frontend_poll_interval_ms = parseInt(fp, 10); } if (errs.length) { try { window.printErrorToStatus('参数无效: ' + errs.join('; '), '设置'); } catch (_) { } return; } const resp = await fetch(`${API_BASE_URL}/sim/settings`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(patch) }); if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`); if (patch.frontend_poll_interval_ms) { window.FRONTEND_POLL_INTERVAL_MS = patch.frontend_poll_interval_ms; } if (typeof ts !== 'undefined' && isFinite(ts) && ts > 0) { window.SIM_TIME_SCALE = ts; window.SIM_SETTINGS_CACHE = Object.assign({}, window.SIM_SETTINGS_CACHE || {}, { sim_time_scale: ts }); }
    document.getElementById('status').textContent = '仿真设置已更新'; setTimeout(() => document.getElementById('status').textContent = '准备就绪', 2000); } catch (err) { try { window.printErrorToStatus('更新仿真设置失败: ' + (err && err.message ? err.message : String(err)), '设置'); } catch (_) { } return; } closeSettingsModal(); }

function computeDoorOverlayPosOverride(eq) { if (!mapData) return null; let site = eq.site; if (typeof site === 'string') site = [site]; site = Array.isArray(site) ? site : []; const ids = site.map(s => String(s)).filter(Boolean); const currentFloorName = floorNames[currentFloorIndex] || null; const pmap = {}; (mapData.points || []).forEach(p => { pmap[String(p.id || p.name)] = p; }); const routes = Array.isArray(mapData.routes) ? mapData.routes : []; let route = null; for (const id of ids) { const r = routes.find(rt => String(rt.id || '') === id); if (r) { route = r; break; } } if (route) { const fp = pmap[String(route.from)]; const tp = pmap[String(route.to)]; if (!fp || !tp) return null; const nf = currentFloorName ? String(currentFloorName).trim().toLowerCase() : null; const ff = extractFloorNameFromLayer(fp.layer || ''); const tf = extractFloorNameFromLayer(tp.layer || ''); const ffn = ff ? String(ff).trim().toLowerCase() : ''; const tfn = tf ? String(tf).trim().toLowerCase() : ''; if (nf) { if (ffn && ffn !== nf) return { visible: false }; if (tfn && tfn !== nf) return { visible: false }; } let mid, ang; if (route.type === 'bezier3' && route.c1 && route.c2) { const t = 0.5; const p0 = { x: fp.x, y: fp.y }; const p1 = { x: route.c1.x, y: route.c1.y }; const p2 = { x: route.c2.x, y: route.c2.y }; const p3 = { x: tp.x, y: tp.y }; const bt = bezierPoint(p0, p1, p2, p3, t); const tg = bezierTangent(p0, p1, p2, p3, t); mid = { x: bt.x, y: bt.y }; const s0 = worldToScreen(p0.x, p0.y); const s1 = worldToScreen(p0.x + tg.x, p0.y + tg.y); ang = Math.atan2(s1.y - s0.y, s1.x - s0.x); } else { mid = { x: (fp.x + tp.x) / 2, y: (fp.y + tp.y) / 2 }; const s0 = worldToScreen(fp.x, fp.y); const s1 = worldToScreen(tp.x, tp.y); ang = Math.atan2(s1.y - s0.y, s1.x - s0.x); } const sp = worldToScreen(mid.x, mid.y); return { screen: { x: sp.x, y: sp.y }, angleRad: ang, visible: true }; } const pts = ids.map(id => pmap[id]).filter(Boolean); if (pts.length === 0) return null; const p0 = pts[0]; const pfloor = extractFloorNameFromLayer(p0.layer || ''); if (currentFloorName && pfloor && String(pfloor).trim().toLowerCase() !== String(currentFloorName).trim().toLowerCase()) return { visible: false }; let mid = { x: p0.x, y: p0.y }; let ang = 0; try { if (pts.length >= 2) { const p1 = pts[1]; mid = { x: (p0.x + p1.x) / 2, y: (p0.y + p1.y) / 2 }; const s0 = worldToScreen(p0.x, p0.y); const s1 = worldToScreen(p1.x, p1.y); ang = Math.atan2(s1.y - s0.y, s1.x - s0.x); } } catch (_) { } const sp = worldToScreen(mid.x, mid.y); return { screen: { x: sp.x, y: sp.y }, angleRad: ang, visible: true }; }
function computeDoorOverlayPosOverride2(eq) { if (!mapData || !mapData.points) return null; let site = eq.site; if (typeof site === 'string') site = [site]; site = Array.isArray(site) ? site : []; const ids = site.map(s => String(s)).filter(Boolean); const currentFloorName = floorNames[currentFloorIndex] || null; if (!isEquipVisibleOnCurrentMap(eq)) return { visible: false }; const pmap = {}; (mapData.points || []).forEach(p => { pmap[String(p.id || p.name)] = p; }); const routes = Array.isArray(mapData.routes) ? mapData.routes : []; const routeById = {}; const routeByDesc = {}; routes.forEach(r => { routeById[String(r.id)] = r; if (r.desc) routeByDesc[String(r.desc)] = r; }); const matched = []; ids.forEach(s => { const r1 = routeById[s]; if (r1) matched.push(r1); else { const r2 = routeByDesc[s]; if (r2) matched.push(r2); } }); if (matched.length < 2) { const pts = ids.map(id => pmap[id]).filter(Boolean); pts.forEach(pt => { if (!pt) return; const candidates = routes.filter(r => String(r.from) === String(pt.id || pt.name) || String(r.to) === String(pt.id || pt.name)); if (candidates.length > 0) matched.push(candidates.sort((a, b) => { const pa = pmap[String(a.from)], qa = pmap[String(a.to)]; const pb = pmap[String(b.from)], qb = pmap[String(b.to)]; const la = (pa && qa) ? Math.hypot(qa.x - pa.x, qa.y - pa.y) : Infinity; const lb = (pb && qb) ? Math.hypot(qb.x - pb.x, qb.y - pb.y) : Infinity; return la - lb; })[0]); }); }
  
  // Extra filter: if we have matched routes but the site IDs are not actually on these routes, filter them out.
  // This helps when multiple map files share the same site ID but different routes.
  // We strictly check if the route endpoints match one of the site IDs.
  if (matched.length > 0) {
      const validMatched = [];
      for(const r of matched) {
          const fromId = String(r.from);
          const toId = String(r.to);
          // Check if either endpoint is in our requested site list
          if (ids.includes(fromId) || ids.includes(toId)) {
              validMatched.push(r);
          }
      }
      // Replace matched with filtered list if we found valid ones. 
      // If we found none valid (weird edge case), we might keep original behavior or clear it.
      // Let's clear it to be safe, so we don't render on wrong route.
      if (matched.length !== validMatched.length) {
          matched.length = 0;
          matched.push(...validMatched);
      }
  }

  if (matched.length < 2 && routes.length >= 2) { 
      // This fallback (using routes[0], routes[1]) is dangerous if the map has routes but they are unrelated to the equipment.
      // We should ONLY do this if we found NO specific matches and maybe the user wants to see *something*?
      // actually, for specific site binding, we should NOT fallback to random routes.
      // matched.push(routes[0], routes[1]); 
      // REMOVED unsafe fallback.
  } let anyRoutePoint = null; if (matched[0]) { const rp = pmap[String(matched[0].from)]; const tp = pmap[String(matched[0].to)]; anyRoutePoint = rp || tp; } if (anyRoutePoint) { const pfloor = extractFloorNameFromLayer(anyRoutePoint.layer || ''); if (currentFloorName && pfloor && String(pfloor).trim().toLowerCase() !== String(currentFloorName).trim().toLowerCase()) return { visible: false }; } const mids = []; for (let i = 0; i < Math.min(2, matched.length); i++) { const r = matched[i]; if (!r) continue; const fp = pmap[String(r.from)]; const tp = pmap[String(r.to)]; if (!fp || !tp) continue; const mx = (fp.x + tp.x) / 2; const my = (fp.y + tp.y) / 2; mids.push({ x: mx, y: my }); } let center = null; let ang = 0; if (mids.length >= 2) { const m0 = mids[0], m1 = mids[1]; center = { x: (m0.x + m1.x) / 2, y: (m0.y + m1.y) / 2 }; const s0 = worldToScreen(m0.x, m0.y); const s1 = worldToScreen(m1.x, m1.y); const screenAngle = Math.atan2(s1.y - s0.y, s1.x - s0.x); ang = screenAngle; } else if (mids.length === 1) { const a = mids[0]; center = { x: a.x, y: a.y }; ang = 0; } else { const pts = ids.map(id => pmap[id]).filter(Boolean); if (pts.length >= 2) { const a = pts[0], b = pts[1]; center = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 }; const sa = worldToScreen(a.x, a.y); const sb = worldToScreen(b.x, b.y); const screenAngle = Math.atan2(sb.y - sa.y, sb.x - sa.x); ang = screenAngle + Math.PI / 2; } else if (pts.length === 1) { const a = pts[0]; center = { x: a.x, y: a.y }; ang = 0; } } if (!center) return null; const sp = worldToScreen(center.x, center.y); return { screen: { x: sp.x, y: sp.y }, angleRad: ang, visible: true, worldCenter: center }; }
function updateDoorOverlayPositionsOverride() { const layer = document.getElementById('equipOverlayLayer'); if (!layer) return; const scaleFactor = Math.max(0.01, viewTransform.scale * 0.2); doorOverlayMap.forEach((entry) => { const eq = entry.eq; const posInfo = computeDoorOverlayPos(eq); if (!posInfo || !posInfo.visible) { entry.element.style.display = 'none'; return; } entry.element.style.display = 'block'; entry.element.style.left = posInfo.screen.x + 'px'; entry.element.style.top = posInfo.screen.y + 'px'; entry.element.style.transform = 'translate(-50%, -50%) rotate(' + posInfo.angleRad + 'rad) scale(' + scaleFactor + ')'; }); }
computeDoorOverlayPos = computeDoorOverlayPosOverride2;
updateDoorOverlayPositions = updateDoorOverlayPositionsOverride;
document.addEventListener('contextmenu', function (e) { e.preventDefault(); }, { capture: true });
const elevatorOverlayMap = new Map();
function ensureElevatorOverlays() {
  const layer = document.getElementById('equipOverlayLayer');
  if (!layer) return;
  const list = Array.isArray(equipments) ? equipments : [];
  for (const eq of list) {
    if (getEquipmentType(eq) !== 'elevator') continue;
    const serial = String(eq.serial_number || eq.serialNumber || '').trim();
    const dir = String(eq.dir_name || eq.dirName || serial || '').trim();
    if (!serial) continue;
    if (elevatorOverlayMap.has(serial)) continue;
    const wrap = document.createElement('div');
    wrap.className = 'equip-item equip-elevator';
    wrap.style.position = 'absolute';
    wrap.style.width = '90px';
    wrap.style.pointerEvents = 'none';
    wrap.dataset.serial = serial;
    wrap.dataset.dir = dir;
    const icon = document.createElement('div');
    icon.className = 'elevator';
    const door = document.createElement('div');
    door.className = 'elevator-door';
    door.id = 'elevatorDoor_' + serial;
    const dl = document.createElement('div');
    dl.className = 'door-left';
    const dr = document.createElement('div');
    dr.className = 'door-right';
    const gap = document.createElement('div');
    gap.className = 'door-gap';
    door.appendChild(dl);
    door.appendChild(dr);
    door.appendChild(gap);
    const interior = document.createElement('div');
    interior.className = 'elevator-interior';
    const pattern = document.createElement('div');
    pattern.className = 'interior-pattern';
    interior.appendChild(pattern);
    const panel = document.createElement('div');
    panel.className = 'control-panel';
    const floor = document.createElement('div');
    floor.className = 'floor-display';
    floor.id = 'floorDisplay_' + serial;
    floor.textContent = '1';
    const di = document.createElement('div');
    di.className = 'direction-indicator';
    const up = document.createElement('div');
    up.className = 'direction';
    up.id = 'up_' + serial;
    up.textContent = '↑';
    const down = document.createElement('div');
    down.className = 'direction';
    down.id = 'down_' + serial;
    down.textContent = '↓';
    di.appendChild(up);
    di.appendChild(down);
    panel.appendChild(floor);
    panel.appendChild(di);
    icon.appendChild(door);
    icon.appendChild(interior);
    icon.appendChild(panel);
    wrap.appendChild(icon);
    layer.appendChild(wrap);
    elevatorOverlayMap.set(serial, { element: wrap, icon, door, floor, up, down, eq });
  }
}
function updateElevatorOverlayPositions() {
  const layer = document.getElementById('equipOverlayLayer');
  if (!layer) return;
  const scaleFactor = Math.max(0.01, viewTransform.scale * 0.2);
  elevatorOverlayMap.forEach((entry) => {
    const posInfo = computeElevatorOverlayPos(entry.eq);
    if (!posInfo || !posInfo.visible) {
      entry.element.style.display = 'none';
      return;
    }
    entry.element.style.display = 'block';
    entry.element.style.left = posInfo.screen.x + 'px';
    entry.element.style.top = posInfo.screen.y + 'px';
    entry.element.style.transform = 'translate(-50%, -50%) rotate(' + posInfo.angleRad + 'rad) scale(' + scaleFactor + ')';
  });
}
function computeElevatorOverlayPos(eq) {
  if (!mapData || !mapData.points) return null;
  let site = eq.site;
  if (typeof site === 'string') site = [site];
  site = Array.isArray(site) ? site : [];
  const ids = site.map(s => String(s)).filter(Boolean);
  const currentFloorName = floorNames[currentFloorIndex] || null;
  if (!isEquipVisibleOnCurrentMap(eq)) return { visible: false };
  const pmap = {};
  (mapData.points || []).forEach(p => { pmap[String(p.id || p.name)] = p; });
  let point = null;
  for (const id of ids) { const p = pmap[id]; if (p) { point = p; break; } }
  if (!point) return null;
  const pfloor = extractFloorNameFromLayer(point.layer || '');
  if (currentFloorName && pfloor && String(pfloor).trim().toLowerCase() !== String(currentFloorName).trim().toLowerCase()) return { visible: false };
  const sp = worldToScreen(point.x, point.y);
  return { screen: { x: sp.x, y: sp.y }, angleRad: 0, visible: true };
}

const lightOverlayMap = new Map();
function ensureLightOverlays() {
  const layer = document.getElementById('equipOverlayLayer');
  if (!layer) return;
  const list = Array.isArray(equipments) ? equipments : [];
  for (const eq of list) {
    if (getEquipmentType(eq) !== 'light') continue;
    const serial = String(eq.serial_number || eq.serialNumber || '').trim();
    const dir = String(eq.dir_name || eq.dirName || serial || '').trim();
    if (!serial) continue;
    if (lightOverlayMap.has(serial)) continue;
    const wrap = document.createElement('div');
    wrap.className = 'equip-item equip-light';
    wrap.style.position = 'absolute';
    wrap.style.width = '90px';
    wrap.style.pointerEvents = 'none';
    wrap.dataset.serial = serial;
    wrap.dataset.dir = dir;
    const icon = document.createElement('div');
    icon.className = 'warning-light';
    icon.id = 'warningLight_' + serial;
    const bulb = document.createElement('div');
    bulb.className = 'light-bulb';
    const cover = document.createElement('div');
    cover.className = 'light-cover';
    const base = document.createElement('div');
    base.className = 'light-base';
    icon.appendChild(bulb);
    icon.appendChild(cover);
    icon.appendChild(base);
    wrap.appendChild(icon);
    layer.appendChild(wrap);
    lightOverlayMap.set(serial, { element: wrap, icon, eq });
  }
}
function updateLightOverlayPositions() {
  const layer = document.getElementById('equipOverlayLayer');
  if (!layer) return;
  const scaleFactor = Math.max(0.01, viewTransform.scale * 0.2);
  lightOverlayMap.forEach((entry) => {
    const posInfo = computeLightOverlayPos(entry.eq);
    if (!posInfo || !posInfo.visible) {
      entry.element.style.display = 'none';
      return;
    }
    entry.element.style.display = 'block';
    entry.element.style.left = posInfo.screen.x + 'px';
    entry.element.style.top = posInfo.screen.y + 'px';
    entry.element.style.transform = 'translate(-50%, -50%) rotate(' + (posInfo.angleRad || 0) + 'rad) scale(' + scaleFactor + ')';
  });
}
function computeLightOverlayPos(eq) {
  if (!mapData || !mapData.points) return null;
  let site = eq.site;
  if (typeof site === 'string') site = [site];
  site = Array.isArray(site) ? site : [];
  const ids = site.map(s => String(s)).filter(Boolean);
  const currentFloorName = floorNames[currentFloorIndex] || null;
  if (!isEquipVisibleOnCurrentMap(eq)) return { visible: false };
  const pmap = {};
  (mapData.points || []).forEach(p => { pmap[String(p.id || p.name)] = p; });
  let point = null;
  for (const id of ids) { const p = pmap[id]; if (p) { point = p; break; } }
  if (!point) return null;
  const pfloor = extractFloorNameFromLayer(point.layer || '');
  if (currentFloorName && pfloor && String(pfloor).trim().toLowerCase() !== String(currentFloorName).trim().toLowerCase()) return { visible: false };
  const sp = worldToScreen(point.x, point.y);
  return { screen: { x: sp.x, y: sp.y }, angleRad: 0, visible: true };
}
const callerOverlayMap = new Map();
function ensureCallerOverlays() {
  const layer = document.getElementById('equipOverlayLayer');
  if (!layer) return;
  const list = Array.isArray(equipments) ? equipments : [];
  for (const eq of list) {
    if (getEquipmentType(eq) !== 'caller') continue;
    const serial = String(eq.serial_number || eq.serialNumber || '').trim();
    const dir = String(eq.dir_name || eq.dirName || serial || '').trim();
    if (!serial) continue;
    if (callerOverlayMap.has(serial)) continue;
    const wrap = document.createElement('div');
    wrap.className = 'equip-item equip-caller';
    wrap.style.position = 'absolute';
    wrap.style.width = '90px';
    wrap.style.pointerEvents = 'none';
    wrap.dataset.serial = serial;
    wrap.dataset.dir = dir;
    const icon = document.createElement('div');
    icon.className = 'caller';
    icon.id = 'caller_' + serial;
    const button = document.createElement('div');
    button.className = 'caller-button';
    const led = document.createElement('div');
    led.className = 'caller-led';
    icon.appendChild(button);
    icon.appendChild(led);
    wrap.appendChild(icon);
    layer.appendChild(wrap);
    callerOverlayMap.set(serial, { element: wrap, icon, button, led, eq });
  }
}
function updateCallerOverlayPositions() {
  const layer = document.getElementById('equipOverlayLayer');
  if (!layer) return;
  const scaleFactor = Math.max(0.01, viewTransform.scale * 0.2);
  callerOverlayMap.forEach((entry) => {
    const posInfo = computeCallerOverlayPos(entry.eq);
    if (!posInfo || !posInfo.visible) {
      entry.element.style.display = 'none';
      return;
    }
    entry.element.style.display = 'block';
    entry.element.style.left = posInfo.screen.x + 'px';
    entry.element.style.top = posInfo.screen.y + 'px';
    entry.element.style.transform = 'translate(-50%, -50%) rotate(' + (posInfo.angleRad || 0) + 'rad) scale(' + scaleFactor + ')';
  });
}
function computeCallerOverlayPos(eq) {
  if (!mapData || !mapData.points) return null;
  let site = eq.site;
  if (typeof site === 'string') site = [site];
  site = Array.isArray(site) ? site : [];
  const ids = site.map(s => String(s)).filter(Boolean);
  const currentFloorName = floorNames[currentFloorIndex] || null;
  if (!isEquipVisibleOnCurrentMap(eq)) return { visible: false };
  const pmap = {};
  (mapData.points || []).forEach(p => { pmap[String(p.id || p.name)] = p; });
  let point = null;
  for (const id of ids) { const p = pmap[id]; if (p) { point = p; break; } }
  if (!point) return null;
  const pfloor = extractFloorNameFromLayer(point.layer || '');
  if (currentFloorName && pfloor && String(pfloor).trim().toLowerCase() !== String(currentFloorName).trim().toLowerCase()) return { visible: false };
  const sp = worldToScreen(point.x, point.y);
  return { screen: { x: sp.x, y: sp.y }, angleRad: 0, visible: true };
}
(function(){
  const _origDrawMap = drawMap;
  drawMap = function(){
    try { _origDrawMap(); } catch (_) {}
    try { ensureElevatorOverlays(); updateElevatorOverlayPositions(); ensureLightOverlays(); updateLightOverlayPositions(); ensureCallerOverlays(); updateCallerOverlayPositions(); } catch (_) {}
  };
  const _origEqWS = applyEquipmentWSState;
  applyEquipmentWSState = function(p){
    try { _origEqWS(p); } catch (_) {}
    try {
      const sn = String(p.serial_number || p.serialNumber || '').trim();
      const info = Array.isArray(p.information) ? p.information : [];
      if (!sn) return;
      const doorInfo = elevatorOverlayMap.has(sn) ? info.find(i => String(i.infoType || i.info_type || i.infotype || '').toUpperCase() === 'EDOORSTATE') : null;
      const actionInfo = elevatorOverlayMap.has(sn) ? info.find(i => String(i.infoType || i.info_type || i.infotype || '').toUpperCase() === 'ACTIONSTATE') : null;
      const floorInfo = elevatorOverlayMap.has(sn) ? info.find(i => String(i.infoType || i.info_type || i.infotype || '').toUpperCase() === 'FLOOR') : null;
      if (doorInfo || actionInfo || floorInfo) {
        let doorOpen = false, isUp = false, isDown = false, floorVal = 1;
        if (doorInfo) {
          const refs = Array.isArray(doorInfo.infoReferences || doorInfo.info_references || doorInfo.inforeferences) ? (doorInfo.infoReferences || doorInfo.info_references || doorInfo.inforeferences) : [];
          const vals = refs.filter(r => String(r.referenceKey || r.reference_key || r.referencekey || '') === 'value').map(r => String(r.referenceValue || r.reference_value || r.referencevalue || ''));
          doorOpen = vals.includes('open');
        }
        if (actionInfo) {
          const refsA = Array.isArray(actionInfo.infoReferences || actionInfo.info_references || actionInfo.inforeferences) ? (actionInfo.infoReferences || actionInfo.info_references || actionInfo.inforeferences) : [];
          const valA = refsA.find(r => String(r.referenceKey || r.reference_key || r.referencekey || '') === 'value');
          const aval = valA ? String(valA.referenceValue || valA.reference_value || valA.referencevalue || '') : '';
          isUp = aval === 'up'; isDown = aval === 'down';
        }
        if (floorInfo) {
          const refsF = Array.isArray(floorInfo.infoReferences || floorInfo.info_references || floorInfo.inforeferences) ? (floorInfo.infoReferences || floorInfo.info_references || floorInfo.inforeferences) : [];
          const valF = refsF.find(r => String(r.referenceKey || r.reference_key || r.referencekey || '') === 'value');
          const fstr = valF ? String(valF.referenceValue || valF.reference_value || valF.referencevalue || '') : '';
          const fnum = parseInt(fstr, 10);
          floorVal = Number.isFinite(fnum) ? fnum : 1;
        }
        const reg2 = elevatorOverlayMap.get(sn);
        if (reg2) {
          if (doorOpen) reg2.door.classList.add('door-open'); else reg2.door.classList.remove('door-open');
          if (reg2.floor) reg2.floor.textContent = String(floorVal);
          if (reg2.up) reg2.up.classList.toggle('active', !!isUp);
          if (reg2.down) reg2.down.classList.toggle('active', !!isDown);
        }
      } else {
        const elevInfo = elevatorOverlayMap.has(sn) ? info.find(i => String(i.infoType || i.info_type || i.infotype || '').toUpperCase() === 'DOORSTATE') : null;
        if (elevInfo) {
          const refs = Array.isArray(elevInfo.infoReferences || elevInfo.info_references || elevInfo.inforeferences) ? (elevInfo.infoReferences || elevInfo.info_references || elevInfo.inforeferences) : [];
          const getParam = (nm) => {
            for (let i = 0; i < refs.length - 1; i++) {
              const k1 = String(refs[i].referenceKey || refs[i].reference_key || refs[i].referencekey || '');
              const v1 = String(refs[i].referenceValue || refs[i].reference_value || refs[i].referencevalue || '');
              if (k1 === 'name' && v1 === nm) {
                const r2 = refs[i + 1] || {};
                const k2 = String(r2.referenceKey || r2.reference_key || r2.referencekey || '');
                if (k2 === 'value') {
                  const vv = r2.referenceValue || r2.reference_value || r2.referencevalue;
                  return vv != null ? String(vv) : '';
                }
              }
            }
            return '';
          };
          const doorState = getParam('door_state');
          const actionState = getParam('action_state');
          const floorStr = getParam('floor');
          const doorOpen = doorState === 'open';
          const isUp = actionState === 'up';
          const isDown = actionState === 'down';
          let floorVal = parseInt(floorStr, 10);
          if (!Number.isFinite(floorVal)) floorVal = 1;
          const reg2 = elevatorOverlayMap.get(sn);
          if (reg2) {
            if (doorOpen) reg2.door.classList.add('door-open'); else reg2.door.classList.remove('door-open');
            if (reg2.floor) reg2.floor.textContent = String(floorVal);
            if (reg2.up) reg2.up.classList.toggle('active', !!isUp);
            if (reg2.down) reg2.down.classList.toggle('active', !!isDown);
          }
        }
      }
      const callerInfo = info.find(i => String(i.infoType || i.info_type || i.infotype || '').toUpperCase() === 'CALLERSTATE');
      if (callerInfo) {
        const refs3 = Array.isArray(callerInfo.infoReferences || callerInfo.info_references || callerInfo.inforeferences) ? (callerInfo.infoReferences || callerInfo.info_references || callerInfo.inforeferences) : [];
        const vals3 = refs3.filter(r => String(r.referenceKey || r.reference_key || r.referencekey || '') === 'value').map(r => String(r.referenceValue || r.reference_value || r.referencevalue || ''));
        const hasPress = vals3.includes('press');
        const hasRelease = vals3.includes('release');
        const regC = callerOverlayMap.get(sn);
        if (regC && regC.icon) {
          if (hasPress) regC.icon.classList.add('caller-pressed');
          else if (hasRelease) regC.icon.classList.remove('caller-pressed');
        }
      }
    } catch (_) {}
  };
})();
(
  function(){
    const _origUpdateEquipmentList = updateEquipmentList;
    updateEquipmentList = function(){
      const c = document.getElementById('equipListContainer');
      if (!c) return;
      const list = Array.isArray(equipments) ? equipments : [];
      if (list.length === 0) {
        c.innerHTML = '<p style="color:#bdc3c7;font-style:italic;">暂无设备</p>';
        try { ensureDoorOverlays(); updateDoorOverlayPositions(); ensureElevatorOverlays(); updateElevatorOverlayPositions(); ensureLightOverlays(); updateLightOverlayPositions(); ensureCallerOverlays(); updateCallerOverlayPositions(); } catch (_) {}
        return;
      }
      c.innerHTML = list.map(eq => {
        const mqtt = eq.mqtt || {};
        const title = (eq.serial_number || eq.dir_name || '设备');
        const eqType = getEquipmentType(eq);
        const siteStr = Array.isArray(eq.site) ? eq.site.join(',') : (eq.site || '-');
        const line1 = `类型: ${eqType} | IP: ${eq.ip || '-'} | 厂商: ${eq.manufacturer || '-'}`;
        const line2 = `站点: ${siteStr} | 地图: ${eq.map_id || '-'}`;
        const line3 = `动作时长: ${typeof eq.action_time !== 'undefined' && eq.action_time !== null ? String(eq.action_time) : '-' } | 触发模式: ${eq.trigger_mode || '-'}`;
        const line4 = `MQTT: ${mqtt.host || '-'}:${mqtt.port || '-'} (${mqtt.vda_interface || '-'}) | 版本: ${eq.vda_full_version || eq.vda_version || '-'}`;
        const dir = String(eq.dir_name || eq.dirName || eq.serial_number || '').trim();
        let controlsHtml = '';
        if (eqType === 'door' && dir) {
          controlsHtml = `<div class="equip-door-controls"><button class="btn" onclick="equipPostInstant('${dir}', buildEquipAction('cmd:open'))">开门</button><button class="btn" onclick="equipPostInstant('${dir}', buildEquipAction('cmd:close'))">关门</button></div>`;
        } else if (eqType === 'light' && dir) {
          controlsHtml = `<div class="equip-door-controls"><button class="btn" onclick="equipPostInstant('${dir}', buildEquipAction('cmd:on'))">开启</button><button class="btn" onclick="equipPostInstant('${dir}', buildEquipAction('cmd:off'))">关闭</button></div>`;
        } else if (eqType === 'caller' && dir) {
          controlsHtml = `<div class="equip-door-controls"><button class="btn" onclick="equipPostInstant('${dir}', buildEquipAction('cmd:press'))">按下</button><button class="btn" onclick="equipPostInstant('${dir}', buildEquipAction('cmd:release'))">释放</button></div>`;
        } else if (eqType === 'elevator' && dir) {
          controlsHtml = `<div class="equip-door-controls" style="display: flex; justify-content: space-between; align-items: center;"><div style="display:flex; gap:5px;"><button class="btn" onclick="equipElevatorUp('${dir}')">上行</button><button class="btn" onclick="equipElevatorDown('${dir}')">下行</button><button class="btn" onclick="equipElevatorOpen('${dir}')">开门</button></div><button class="btn-config-small" onclick="openEquipConfig('${dir}')" style="margin-left: auto;">配置</button></div>`;
        }
        
        if (dir && eqType !== 'elevator') {
           if (controlsHtml) {
             const btnMatches = controlsHtml.match(/<button.*?<\/button>/g);
             const innerBtns = btnMatches ? btnMatches.join('') : '';
             controlsHtml = `<div class="equip-door-controls" style="display: flex; justify-content: space-between; align-items: center;"><div style="display:flex; gap:5px;">${innerBtns}</div><button class="btn-config-small" onclick="openEquipConfig('${dir}')" style="margin-left: auto;">配置</button></div>`;
           } else {
             controlsHtml = `<div class="equip-door-controls" style="display: flex; justify-content: flex-end;"><button class="btn-config-small" onclick="openEquipConfig('${dir}')">配置</button></div>`;
           }
        }
        return `<div class="robot-item"><div class="robot-header"><div class="robot-name">${title}</div></div><div class="robot-info" style="word-break:break-all;">${line1}<br>${line2}<br>${line3}<br>${line4}</div>${controlsHtml}</div>`;
      }).join('');
      try { ensureDoorOverlays(); updateDoorOverlayPositions(); ensureElevatorOverlays(); updateElevatorOverlayPositions(); ensureLightOverlays(); updateLightOverlayPositions(); ensureCallerOverlays(); updateCallerOverlayPositions(); } catch (_) {}
    };
  }
)();

// Equipment Configuration
window.openEquipConfig = async function(dirName) {
  const modal = document.getElementById('equipConfigModal');
  if (!modal) return;
  const formContainer = document.getElementById('equipConfigForm');
  if (!formContainer) return;
  
  formContainer.innerHTML = '<div style="color:#bdc3c7;">加载配置中...</div>';
  modal.style.display = 'flex';
  window._currentEquipDir = dirName;

  try {
    const res = await fetch(`${API_BASE_URL}/equipments/${dirName}/config`);
    if (!res.ok) throw new Error(`Status ${res.status}`);
    const config = await res.json();
    renderEquipConfigForm(config);
  } catch (e) {
    formContainer.innerHTML = `<div style="color:#e74c3c;">加载失败: ${e.message}</div>`;
  }
};

window.closeEquipConfigModal = function() {
  const modal = document.getElementById('equipConfigModal');
  if (modal) modal.style.display = 'none';
  window._currentEquipDir = null;
};

window.saveEquipConfig = async function() {
  if (!window._currentEquipDir) return;
  
  // Collect form data
  const newConfig = {};
  const inputs = document.querySelectorAll('#equipConfigForm input, #equipConfigForm select');
  
  // Helper to set nested value
  const setNested = (obj, path, value) => {
    const keys = path.split('.');
    let current = obj;
    for (let i = 0; i < keys.length - 1; i++) {
      if (!current[keys[i]]) current[keys[i]] = {};
      current = current[keys[i]];
    }
    current[keys[keys.length - 1]] = value;
  };

  inputs.forEach(input => {
    const path = input.getAttribute('data-path');
    if (!path) return;
    
    let val = input.value;
    if (input.type === 'number') {
      val = Number(val);
    } else if (input.type === 'checkbox') {
        val = input.checked;
    }
    
    // Special handling for arrays like 'site'
    if (path === 'site') {
       val = val.split(',').map(s => s.trim()).filter(s => s);
    }

    setNested(newConfig, path, val);
  });

  try {
    const res = await fetch(`${API_BASE_URL}/equipments/${window._currentEquipDir}/config`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(newConfig)
    });
    if (!res.ok) throw new Error(`Status ${res.status}`);
    alert('配置已保存，请手动重启设备服务以生效');
    closeEquipConfigModal();
    loadEquipmentList(); // Reload list to update display
  } catch (e) {
    alert(`保存失败: ${e.message}`);
  }
};

function renderEquipConfigForm(config) {
  const formContainer = document.getElementById('equipConfigForm');
  if (!formContainer) return;
  
  let html = '';
  
  // Recursive function to flatten config for display, or just handle known fields?
  // Let's handle known top-level and some nested fields for better UX, 
  // but also support generic editing for others if needed.
  // For simplicity and robustness, let's explicitly map common fields and JSON dump others?
  // Or better: Recursively generate inputs.
  
  const generateInput = (key, value, path, level = 0) => {
      const indent = level * 15;
      const isObj = typeof value === 'object' && value !== null && !Array.isArray(value);
      
      if (isObj) {
          let subHtml = `<div style="margin-left:${indent}px; margin-bottom: 8px;"><strong style="color:#3498db;">${key}</strong></div>`;
          for (const k in value) {
              subHtml += generateInput(k, value[k], path ? `${path}.${k}` : k, level + 1);
          }
          return subHtml;
      }
      
      let inputType = 'text';
      let valStr = value;
      
      if (typeof value === 'number') inputType = 'number';
      if (Array.isArray(value)) valStr = value.join(', ');
      
      // Read-only fields
      const readOnly = (key === 'type' || key === 'serial_number' || key === 'manufacturer') ? 'readonly style="background:#34495e; color:#95a5a6;"' : '';
      
      return `
      <div class="form-group" style="margin-left:${indent}px;">
        <label class="form-label">${key}</label>
        <input type="${inputType}" class="form-input" data-path="${path || key}" value="${valStr}" ${readOnly}>
      </div>`;
  };

  // Custom ordering/grouping could be done here
  // For now, just iterate keys
  for (const k in config) {
      html += generateInput(k, config[k], k);
  }
  
  formContainer.innerHTML = html;
}

function buildElevatorActionCommand(cmd) { return { actions: [{ actionType: 'writeValue', actionId: String(Math.random()), blockingType: 'HARD', actionParameters: [{ key: 'command', value: cmd }] }], headerId: 0, timeStamp: '', version: 'v1', manufacturer: '', serialNumber: '' }; }
function getEquipSerialByDir(dirName) { const d = String(dirName || ''); const eq = (Array.isArray(equipments) ? equipments : []).find(e => String(e.dir_name || e.dirName || '') === d); return eq ? String(eq.serial_number || eq.serialNumber || '') : ''; }
function getElevatorCurrentFloorBySerial(serial) { const reg = elevatorOverlayMap.get(String(serial || '')); if (reg && reg.floor && typeof reg.floor.textContent === 'string') { const n = parseInt(reg.floor.textContent, 10); return Number.isFinite(n) ? n : 1; } return 1; }
function equipElevatorOpen(dirName) { const payload = buildElevatorActionCommand('cmd:press'); return equipPostInstant(dirName, payload); }
function equipElevatorMoveTo(dirName, floor) { const payload = buildElevatorActionCommand(`cmd:press:${floor}`); return equipPostInstant(dirName, payload); }
function equipElevatorUp(dirName) { const serial = getEquipSerialByDir(dirName); const cur = getElevatorCurrentFloorBySerial(serial); const tgt = cur + 1; return equipElevatorMoveTo(dirName, tgt); }
function equipElevatorDown(dirName) { const serial = getEquipSerialByDir(dirName); const cur = getElevatorCurrentFloorBySerial(serial); const tgt = cur - 1; return equipElevatorMoveTo(dirName, tgt); }
