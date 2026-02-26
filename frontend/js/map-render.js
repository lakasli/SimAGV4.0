// Map rendering functions - drawMap, drawGrid, drawPoints, drawRoutes

function drawMap() { 
  if (!mapData) return; 
  ctx.clearRect(0, 0, canvas.width, canvas.height); 
  if (showGrid) drawGrid(); 
  if (mapData.routes) drawRoutes(); 
  if (mapData.points) drawPoints(); 
  if (registeredRobots.length > 0) drawRobots(); 
  try { updateDoorOverlayPositions(); } catch (e) { } 
}

function drawGrid() { 
  ctx.save(); 
  ctx.strokeStyle = '#34495e'; 
  ctx.lineWidth = 1; 
  const g = 50 * viewTransform.scale; 
  const ox = viewTransform.x % g, oy = viewTransform.y % g; 
  for (let x = ox; x < canvas.width; x += g) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, canvas.height); ctx.stroke(); } 
  for (let y = oy; y < canvas.height; y += g) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(canvas.width, y); ctx.stroke(); } 
  ctx.restore(); 
}

function drawPoints() { 
  (mapData.points || []).forEach(point => { 
    const currentFloorName = floorNames[currentFloorIndex] || null; 
    const pfloor = extractFloorNameFromLayer(point.layer || ''); 
    if (currentFloorName && pfloor && String(pfloor).trim().toLowerCase() !== String(currentFloorName).trim().toLowerCase()) { return; } 
    const sp = worldToScreen(point.x, point.y); 
    if (sp.x < -20 || sp.x > canvas.width + 20 || sp.y < -20 || sp.y > canvas.height + 20) return; 
    ctx.save(); 
    let color = '#95a5a6'; 
    let size = pointDrawSize(); 
    const t = Number(point.type); 
    if (!Number.isNaN(t) && POINT_TYPE_INFO[t]) { color = POINT_TYPE_INFO[t].color; } 
    else { 
      const nm = point.name || ''; 
      if (nm.startsWith('AP')) { color = '#e74c3c'; } 
      else if (nm.startsWith('LM')) { color = '#f39c12'; } 
      else if (nm.startsWith('CP')) { color = '#27ae60'; } 
    } 
    ctx.fillStyle = color; 
    ctx.beginPath(); 
    ctx.arc(sp.x, sp.y, size, 0, 2 * Math.PI); 
    ctx.fill(); 
    ctx.strokeStyle = '#2c3e50'; 
    ctx.lineWidth = 2; 
    ctx.stroke(); 
    const name = point.name || ''; 
    const key = name || point.id; 
    let isSelected = !!selectedStationId && key === selectedStationId; 
    if (!isSelected && selectedNavStation && selectedNavStation.pos) { 
      const hitRadius = pointDrawSize() + 2; 
      const spSel = worldToScreen(selectedNavStation.pos.x, selectedNavStation.pos.y); 
      const dxs = sp.x - spSel.x, dys = sp.y - spSel.y; 
      if (dxs * dxs + dys * dys <= (hitRadius + 4) * (hitRadius + 4)) { isSelected = true; } 
    } 
    if (isSelected) { 
      ctx.strokeStyle = '#f1c40f'; 
      ctx.lineWidth = 3; 
      ctx.beginPath(); 
      ctx.arc(sp.x, sp.y, size + 4, 0, 2 * Math.PI); 
      ctx.stroke(); 
    } 
    if (viewTransform.scale > 0.5 && name) { 
      ctx.fillStyle = '#ecf0f1'; 
      ctx.font = '12px Arial'; 
      ctx.textAlign = 'center'; 
      ctx.fillText(name, sp.x, sp.y - size - 6); 
    } 
    ctx.restore(); 
  }); 
}

function drawRoutes() { 
  if (!mapData || !mapData.points) return; 
  const pmap = {}; 
  mapData.points.forEach(p => pmap[p.id] = p); 
  (mapData.routes || []).forEach(route => { 
    const fp = pmap[route.from], tp = pmap[route.to]; 
    if (!fp || !tp) return; 
    const dir = getRouteDirection(route); 
    const currentFloorName = floorNames[currentFloorIndex] || null; 
    if (currentFloorName) { 
      const nf = String(currentFloorName).trim().toLowerCase(); 
      const ff = extractFloorNameFromLayer(fp.layer || ''); 
      const tf = extractFloorNameFromLayer(tp.layer || ''); 
      const ffn = ff ? String(ff).trim().toLowerCase() : ''; 
      const tfn = tf ? String(tf).trim().toLowerCase() : ''; 
      if (ffn && ffn !== nf) return; 
      if (tfn && tfn !== nf) return; 
    } 
    ctx.save(); 
    const passCode = (route.pass !== undefined) ? Number(route.pass) : 0; 
    const baseColor = getRouteColorByPass(passCode); 
    ctx.strokeStyle = baseColor; 
    ctx.lineWidth = 2; 
    ctx.lineCap = 'round'; 
    if (passCode === 10) { ctx.setLineDash([6, 4]); } else { ctx.setLineDash([]); } 
    const rid = String(route.id ?? (`${route.from}->${route.to}`)); 
    const isSelected = !!selectedRouteId && String(selectedRouteId) === rid; 
    if (isSelected) { ctx.lineWidth = 3; } 
    if (route.type === 'bezier3' && route.c1 && route.c2) { 
      const p0 = { x: fp.x, y: fp.y }; 
      const p1 = { x: route.c1.x, y: route.c1.y }; 
      const p2 = { x: route.c2.x, y: route.c2.y }; 
      const p3 = { x: tp.x, y: tp.y }; 
      const fromScreen = worldToScreen(p0.x, p0.y); 
      const c1Screen = worldToScreen(p1.x, p1.y); 
      const c2Screen = worldToScreen(p2.x, p2.y); 
      const toScreen = worldToScreen(p3.x, p3.y); 
      ctx.beginPath(); 
      ctx.moveTo(fromScreen.x, fromScreen.y); 
      ctx.bezierCurveTo(c1Screen.x, c1Screen.y, c2Screen.x, c2Screen.y, toScreen.x, toScreen.y); 
      ctx.stroke(); 
      if (viewTransform.scale > 0.3) { 
        if (dir !== 0) { 
          const t = 0.7; 
          const q0 = dir > 0 ? p0 : p3; 
          const q1 = dir > 0 ? p1 : p2; 
          const q2 = dir > 0 ? p2 : p1; 
          const q3 = dir > 0 ? p3 : p0; 
          const pt = bezierPoint(q0, q1, q2, q3, t); 
          const tg = bezierTangent(q0, q1, q2, q3, t); 
          const ptScreen = worldToScreen(pt.x, pt.y); 
          const tangentAngle = Math.atan2(-tg.y, tg.x) * (dir > 0 ? 1 : -1); 
          drawArrowAt(ptScreen.x, ptScreen.y, tangentAngle); 
        } 
      } 
    } else { 
      const s1 = worldToScreen(fp.x, fp.y); 
      const s2 = worldToScreen(tp.x, tp.y); 
      ctx.beginPath(); 
      ctx.moveTo(s1.x, s1.y); 
      ctx.lineTo(s2.x, s2.y); 
      ctx.stroke(); 
      if (viewTransform.scale > 0.3 && dir !== 0) { 
        const angle = Math.atan2(s2.y - s1.y, s2.x - s1.x); 
        const arrowX = dir > 0 ? s2.x : s1.x; 
        const arrowY = dir > 0 ? s2.y : s1.y; 
        const arrowAngle = dir > 0 ? angle : angle + Math.PI; 
        drawArrowAt(arrowX, arrowY, arrowAngle); 
      } 
    } 
    ctx.restore(); 
  }); 
}

function drawArrowAt(x, y, angle) { 
  const arrowLength = 10; 
  const arrowAngle = Math.PI / 6; 
  ctx.save(); 
  ctx.fillStyle = ctx.strokeStyle; 
  ctx.beginPath(); 
  ctx.moveTo(x, y); 
  ctx.lineTo(x - arrowLength * Math.cos(angle - arrowAngle), y - arrowLength * Math.sin(angle - arrowAngle)); 
  ctx.lineTo(x - arrowLength * Math.cos(angle + arrowAngle), y - arrowLength * Math.sin(angle + arrowAngle)); 
  ctx.closePath(); 
  ctx.fill(); 
  ctx.restore(); 
}

function drawArrow(start, end) { 
  const angle = Math.atan2(end.y - start.y, end.x - start.x); 
  drawArrowAt(end.x, end.y, angle); 
}

function buildBinLocationIndex(layers) { 
  const byFloor = []; 
  function buildIndexForLayer(layer) { 
    const byPointName = new Map(); 
    function recordEntry(pointName, instanceName, binTaskString) { 
      if (!pointName) return; 
      const key = String(pointName).trim().toUpperCase(); 
      let entry = byPointName.get(key); 
      if (!entry) { entry = { locationNames: [], binTaskStrings: [], binTaskObjects: [] }; byPointName.set(key, entry); } 
      if (instanceName) entry.locationNames.push(String(instanceName).trim()); 
      if (typeof binTaskString === 'string' && binTaskString.trim() !== '') { 
        entry.binTaskStrings.push(binTaskString); 
        try { const parsed = JSON.parse(binTaskString); entry.binTaskObjects.push(parsed); } catch (_) { } 
      } 
    } 
    function traverse(obj) { 
      if (!obj) return; 
      if (Array.isArray(obj)) { obj.forEach(traverse); return; } 
      if (typeof obj === 'object') { 
        if (Array.isArray(obj.binLocationList)) { 
          for (const loc of obj.binLocationList) { 
            const pName = String(loc?.pointName || '').trim(); 
            const iName = String(loc?.instanceName || '').trim(); 
            const props = Array.isArray(loc?.property) ? loc.property : (Array.isArray(loc?.properties) ? loc.properties : []); 
            let taskStrs = []; 
            for (const prop of (props || [])) { 
              if (prop && String(prop.key || '').trim() === 'binTask' && typeof prop.stringValue === 'string') { taskStrs.push(prop.stringValue); } 
            } 
            if (taskStrs.length === 0) { recordEntry(pName, iName, undefined); } 
            else { for (const s of taskStrs) recordEntry(pName, iName, s); } 
          } 
        } 
        for (const k in obj) { 
          if (!Object.prototype.hasOwnProperty.call(obj, k)) continue; 
          const v = obj[k]; 
          if (v && (typeof v === 'object' || Array.isArray(v))) traverse(v); 
        } 
      } 
    } 
    try { traverse(layer); } catch (_) { } 
    return byPointName; 
  } 
  try { for (const layer of (layers || [])) { byFloor.push(buildIndexForLayer(layer)); } } catch (_) { } 
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
            } else { lines.push(` ${String(params)}`); } 
            sections.push(lines.join('\n')); 
          } 
        } else if (typeof item === 'string') { sections.push(item); } 
        else { sections.push(String(item)); } 
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
        if (params && typeof params === 'object') { for (const k of Object.keys(params)) kv.push([k, params[k]]); } 
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
    for (const r of raws) { makeCard('Raw', [['string', r]]); made = true; } 
  } 
  return made; 
}
