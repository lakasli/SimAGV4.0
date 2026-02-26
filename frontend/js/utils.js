// Utility functions

function formatBytes(bytes) { 
  const n = Number(bytes); 
  if (!isFinite(n) || n < 0) return '-'; 
  if (n === 0) return '0 B'; 
  const units = ['B', 'KB', 'MB', 'GB', 'TB']; 
  let v = n; 
  let idx = 0; 
  while (v >= 1024 && idx < units.length - 1) { v /= 1024; idx++; } 
  const digits = (idx <= 1) ? 0 : 1; 
  return `${v.toFixed(digits)} ${units[idx]}`; 
}

function formatCpuPercent(v) { const n = Number(v); if (!isFinite(n)) return '-'; return `${n.toFixed(1)}%`; }
function formatPercent(v) { const n = Number(v); if (!isFinite(n)) return '-'; return `${n.toFixed(1)}%`; }
function escapeHtml(s) { return String(s ?? '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&#39;'); }

function truncateToOneDecimal(v) { 
  const num = Number(v); 
  if (!isFinite(num)) return 0; 
  const t = Math.floor(num * 10) / 10; 
  return Math.max(0, Math.min(100, t)); 
}

function normalizeViewerMapId(raw) { 
  let s = String(raw || '').replace(/\\/g, '/').replace(/^\/+/, '').trim(); 
  if (!s) return ''; 
  const mVehicle = /^VehicleMap\/([^\/]+\.scene)$/i.exec(s); 
  if (mVehicle) return 'ViewerMap/' + mVehicle[1]; 
  if (/^ViewerMap\/[^\/]+\.scene$/i.test(s)) return s; 
  if (/^[^\/]+\.scene$/i.test(s)) return 'ViewerMap/' + s; 
  if (s.includes('/')) { const last = s.split('/').pop(); if (last && /\.scene$/i.test(last)) return 'ViewerMap/' + last; } 
  return 'ViewerMap/' + s + '.scene'; 
}

function canonicalizeMapId(raw) { 
  let s = String(raw || '').replace(/\\/g, '/').replace(/^\/+/, '').trim(); 
  if (!s) return ''; 
  if (s.includes('/')) s = s.split('/').pop() || s; 
  s = s.replace(/\.scene$/i, ''); 
  return s; 
}

function extractFloorNameFromLayer(layer) { 
  try { 
    if (typeof layer === 'string') { const v = layer.trim(); if (v) return v; } 
    if (layer && Array.isArray(layer.list)) { 
      for (const item of layer.list) { 
        if (item && typeof item.name === 'string') { const v = item.name.trim(); if (v) return v; } 
      } 
    } 
    if (layer && typeof layer.name === 'string' && layer.name.trim() !== '') { return layer.name.trim(); } 
  } catch (e) { } 
  return null; 
}

function extractFloorFromMapId(mapId) { 
  const raw = String(mapId || '').trim(); 
  if (!raw) return null; 
  const mScene = /^VehicleMap\/(.+)\.scene$/i.exec(raw); 
  if (mScene) return mScene[1]; 
  const mViewer = /^ViewerMap\/(.+)\.scene$/i.exec(raw); 
  if (mViewer) return mViewer[1]; 
  const fname = raw.replace(/\\/g, '/').split('/').pop(); 
  return (fname || '').replace(/\.scene$/i, '') || null; 
}

function distanceXY(a, b) { const dx = Number(a.x) - Number(b.x); const dy = Number(a.y) - Number(b.y); return Math.hypot(dx, dy); }

function deriveClassFromName(name) { 
  if (!name) return 'Station'; 
  if (/^CP/i.test(name)) return 'CP'; 
  if (/^DP/i.test(name)) return 'DP'; 
  if (/^WP/i.test(name)) return 'WP'; 
  if (/^Pallet/i.test(name)) return 'Pallet'; 
  return 'Station'; 
}

function getRoutePassLabel(passCode) { 
  const code = Number(passCode); 
  const info = Object.prototype.hasOwnProperty.call(PASS_TYPE_INFO, code) ? PASS_TYPE_INFO[code] : PASS_TYPE_INFO[0]; 
  return info.label; 
}

function getRouteColorByPass(passCode) { 
  const code = Number(passCode); 
  const info = Object.prototype.hasOwnProperty.call(PASS_TYPE_INFO, code) ? PASS_TYPE_INFO[code] : PASS_TYPE_INFO[0]; 
  return info.color; 
}

function getRouteDirection(route) { 
  let d = route.direction; 
  if (d === undefined && Array.isArray(route.properties)) { 
    const p = route.properties.find(pr => (pr.name === 'direction' || pr.key === 'direction')); 
    if (p) d = Number(p.value); 
  } 
  if (typeof d !== 'number' || isNaN(d)) return 1; 
  if (d === 0) return 0; 
  return d > 0 ? 1 : -1; 
}

function pointToSegmentDistance(px, py, x1, y1, x2, y2) { 
  const dx = x2 - x1, dy = y2 - y1; 
  if (dx === 0 && dy === 0) return Math.hypot(px - x1, py - y1); 
  const t = ((px - x1) * dx + (py - y1) * dy) / (dx * dx + dy * dy); 
  const tt = Math.max(0, Math.min(1, t)); 
  const cx = x1 + tt * dx, cy = y1 + tt * dy; 
  return Math.hypot(px - cx, py - cy); 
}

function bezierPoint(p0, p1, p2, p3, t) { 
  const u = 1 - t; const tt = t * t; const uu = u * u; const uuu = uu * u; const ttt = tt * t; 
  return { x: uuu * p0.x + 3 * uu * t * p1.x + 3 * u * tt * p2.x + ttt * p3.x, y: uuu * p0.y + 3 * uu * t * p1.y + 3 * u * tt * p2.y + ttt * p3.y }; 
}

function bezierTangent(p0, p1, p2, p3, t) { 
  const u = 1 - t; 
  return { x: 3 * u * u * (p1.x - p0.x) + 6 * u * t * (p2.x - p1.x) + 3 * t * t * (p3.x - p2.x), y: 3 * u * u * (p1.y - p0.y) + 6 * u * t * (p2.y - p1.y) + 3 * t * t * (p3.y - p2.y) }; 
}

function normalizeEquipMapIdList(mapId) { 
  try { 
    const list = Array.isArray(mapId) ? mapId : (typeof mapId !== 'undefined' && mapId !== null ? [mapId] : []); 
    const out = []; 
    for (const v of list) { const floor = extractFloorFromMapId(v); if (floor) out.push(String(floor).trim().toLowerCase()); } 
    return Array.from(new Set(out)).filter(Boolean); 
  } catch (_) { return []; } 
}

function isEquipVisibleOnCurrentMap(eq) { 
  const currentFloorName = floorNames[currentFloorIndex] || null; 
  if (!currentFloorName) return true; 
  const allowed = normalizeEquipMapIdList(eq && eq.map_id); 
  if (!Array.isArray(allowed) || allowed.length === 0) return true; 
  return allowed.includes(String(currentFloorName).trim().toLowerCase()); 
}

function getEquipmentType(eq) { 
  const t = String(eq?.type || '').toLowerCase(); 
  if (t) return t; 
  const nm = String(eq?.name || eq?.dir_name || '').toLowerCase(); 
  if (nm.includes('door')) return 'door'; 
  if (nm.includes('elevator')) return 'elevator'; 
  if (nm.includes('light')) return 'light'; 
  if (nm.includes('caller')) return 'caller'; 
  return 'device'; 
}

function isWorkStationPoint(point) { const nm = String(point?.name || point?.id || '').trim(); return /^AP/i.test(nm); }
function isChargingStationPoint(point) { 
  const nm = String(point?.name || point?.id || '').trim(); 
  const typeCode = (point && typeof point.type !== 'undefined') ? Number(point.type) : NaN; 
  if (typeCode === 13) return true; 
  return /^CP/i.test(nm); 
}
