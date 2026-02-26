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

function normalizeEquipMapIdList(mapId) {
  try {
    const list = Array.isArray(mapId) ? mapId : (typeof mapId !== 'undefined' && mapId !== null ? [mapId] : []);
    const out = [];
    for (const v of list) {
      const floor = extractFloorFromMapId(v);
      if (floor) out.push(String(floor).trim().toLowerCase());
    }
    return Array.from(new Set(out)).filter(Boolean);
  } catch (_) {
    return [];
  }
}

function isEquipVisibleOnCurrentMap(eq) {
  const currentFloorName = floorNames[currentFloorIndex] || null;
  if (!currentFloorName) return true;
  const allowed = normalizeEquipMapIdList(eq && eq.map_id);
  if (!Array.isArray(allowed) || allowed.length === 0) return true;
  return allowed.includes(String(currentFloorName).trim().toLowerCase());
}
