function buildBinLocationIndex(layers) {
  const byFloor = [];

  function buildIndexForLayer(layer) {
    const byPointName = new Map();

    function recordEntry(pointName, instanceName, binTaskString) {
      if (!pointName) return;
      const key = String(pointName).trim().toUpperCase();
      let entry = byPointName.get(key);
      if (!entry) {
        entry = { locationNames: [], binTaskStrings: [], binTaskObjects: [] };
        byPointName.set(key, entry);
      }
      if (instanceName) entry.locationNames.push(String(instanceName).trim());
      if (typeof binTaskString === 'string' && binTaskString.trim() !== '') {
        entry.binTaskStrings.push(binTaskString);
        try {
          const parsed = JSON.parse(binTaskString);
          entry.binTaskObjects.push(parsed);
        } catch (_) {}
      }
    }

    function traverse(obj) {
      if (!obj) return;
      if (Array.isArray(obj)) {
        obj.forEach(traverse);
        return;
      }
      if (typeof obj === 'object') {
        if (Array.isArray(obj.binLocationList)) {
          for (const loc of obj.binLocationList) {
            const pName = String(loc?.pointName || '').trim();
            const iName = String(loc?.instanceName || '').trim();
            const props = Array.isArray(loc?.property) ? loc.property : (Array.isArray(loc?.properties) ? loc.properties : []);
            const taskStrs = [];
            for (const prop of (props || [])) {
              if (prop && String(prop.key || '').trim() === 'binTask' && typeof prop.stringValue === 'string') {
                taskStrs.push(prop.stringValue);
              }
            }
            if (taskStrs.length === 0) {
              recordEntry(pName, iName, undefined);
            } else {
              for (const s of taskStrs) recordEntry(pName, iName, s);
            }
          }
        }
        for (const k in obj) {
          if (!Object.prototype.hasOwnProperty.call(obj, k)) continue;
          const v = obj[k];
          if (v && (typeof v === 'object' || Array.isArray(v))) traverse(v);
        }
      }
    }

    try {
      traverse(layer);
    } catch (_) {}
    return byPointName;
  }

  try {
    for (const layer of (layers || [])) {
      byFloor.push(buildIndexForLayer(layer));
    }
  } catch (_) {}

  window.binLocationIndex = { byFloor };
}

function lookupBinLocationInfo(pointName) {
  const key = String(pointName || '').trim().toUpperCase();
  const idx = (typeof currentFloorIndex === 'number') ? currentFloorIndex : 0;
  const byFloor = window.binLocationIndex?.byFloor;
  const map = Array.isArray(byFloor) ? byFloor[idx] : null;
  return (map && map.get(key)) || null;
}

function formatBinTaskDetails(entry) {
  try {
    const objs = Array.isArray(entry?.binTaskObjects) ? entry.binTaskObjects : [];
    const sections = [];

    for (const obj of objs) {
      const arr = Array.isArray(obj) ? obj : [obj];
      for (const item of arr) {
        if (item && typeof item === 'object') {
          for (const actionName of Object.keys(item)) {
            const params = item[actionName];
            const lines = [];
            lines.push(`${actionName}：`);
            if (params && typeof params === 'object') {
              for (const k of Object.keys(params)) {
                const v = params[k];
                const vs = (typeof v === 'string') ? `"${v}"` : String(v);
                lines.push(` "${k}": ${vs}`);
              }
            } else {
              lines.push(` ${String(params)}`);
            }
            sections.push(lines.join('\n'));
          }
        } else if (typeof item === 'string') {
          sections.push(item);
        } else {
          sections.push(String(item));
        }
      }
    }

    if (sections.length > 0) return sections.join('\n\n');
    const raw = Array.isArray(entry?.binTaskStrings) ? entry.binTaskStrings : [];
    return raw.join('\n');
  } catch (_) {
    const raw = Array.isArray(entry?.binTaskStrings) ? entry.binTaskStrings : [];
    return raw.join('\n');
  }
}

function renderBinTaskDetails(entry) {
  const container = document.getElementById('stStorageDetails');
  if (!container) return false;
  container.innerHTML = '';

  const objs = Array.isArray(entry?.binTaskObjects) ? entry.binTaskObjects : [];
  let made = false;

  function makeCard(title, kv) {
    const card = document.createElement('div');
    card.className = 'bin-card';
    const t = document.createElement('div');
    t.className = 'bin-card-title';
    t.textContent = title || 'Detail';
    card.appendChild(t);
    for (const [k, v] of kv) {
      const row = document.createElement('div');
      row.className = 'bin-kv';
      const kEl = document.createElement('div');
      kEl.className = 'key';
      kEl.textContent = String(k);
      const vEl = document.createElement('div');
      vEl.className = 'val';
      vEl.textContent = (typeof v === 'string') ? v : String(v);
      row.appendChild(kEl);
      row.appendChild(vEl);
      card.appendChild(row);
    }
    container.appendChild(card);
  }

  for (const obj of objs) {
    const list = Array.isArray(obj) ? obj : [obj];
    for (const item of list) {
      if (item && typeof item === 'object' && Object.keys(item).length > 0) {
        const actionName = Object.keys(item)[0];
        const params = item[actionName];
        const kv = [];
        if (params && typeof params === 'object') {
          for (const k of Object.keys(params)) kv.push([k, params[k]]);
        }
        makeCard(actionName, kv);
        made = true;
      } else if (typeof item === 'string') {
        makeCard('Raw', [['string', item]]);
        made = true;
      }
    }
  }

  if (!made) {
    const raws = Array.isArray(entry?.binTaskStrings) ? entry.binTaskStrings : [];
    for (const r of raws) {
      makeCard('Raw', [['string', r]]);
      made = true;
    }
  }

  return made;
}
