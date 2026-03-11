function extractFloorNameFromLayer(layer) {
  try {
    if (typeof layer === 'string') {
      const v = layer.trim();
      if (v) return v;
    }
    if (layer && Array.isArray(layer.list)) {
      for (const item of layer.list) {
        if (item && typeof item.name === 'string') {
          const v = item.name.trim();
          if (v) return v;
        }
      }
    }
    if (layer && typeof layer.name === 'string' && layer.name.trim() !== '') {
      return layer.name.trim();
    }
  } catch (_) {}
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

function validateSiteForRender(site) {
  if (!site || !Array.isArray(site) || site.length === 0) {
    return { valid: false, error: 'site 必须是非空数组' };
  }
  for (let i = 0; i < site.length; i++) {
    const s = site[i];
    if (typeof s !== 'object' || s === null) {
      return { valid: false, error: `site[${i}] 必须是对象` };
    }
    if (typeof s.x !== 'number' || !isFinite(s.x)) {
      return { valid: false, error: `site[${i}].x 必须是有效数字` };
    }
    if (typeof s.y !== 'number' || !isFinite(s.y)) {
      return { valid: false, error: `site[${i}].y 必须是有效数字` };
    }
    if (typeof s.theta !== 'number' || !isFinite(s.theta)) {
      return { valid: false, error: `site[${i}].theta 必须是有效数字` };
    }
    if (!s.map_id || typeof s.map_id !== 'string' || s.map_id.trim() === '') {
      return { valid: false, error: `site[${i}].map_id 必须是非空字符串` };
    }
  }
  return { valid: true };
}

function normalizeEquipMapIdList(eq) {
  try {
    const site = eq && eq.site;
    const validation = validateSiteForRender(site);
    if (!validation.valid) {
      return [];
    }
    const mapIds = [];
    for (const s of site) {
      const floor = extractFloorFromMapId(s.map_id);
      if (floor) mapIds.push(String(floor).trim().toLowerCase());
    }
    return Array.from(new Set(mapIds)).filter(Boolean);
  } catch (_) {
    return [];
  }
}

function isEquipVisibleOnCurrentMap(eq) {
  const currentFloorName = floorNames[currentFloorIndex] || null;
  if (!currentFloorName) return true;
  const allowed = normalizeEquipMapIdList(eq);
  if (!Array.isArray(allowed) || allowed.length === 0) return false;
  return allowed.includes(String(currentFloorName).trim().toLowerCase());
}
