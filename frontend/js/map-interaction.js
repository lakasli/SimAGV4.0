// Map interaction functions - mouse events, click detection, coordinate conversion

function setupEventListeners() { 
  canvas.addEventListener('mousedown', onMouseDown); 
  canvas.addEventListener('mousemove', onMouseMove); 
  canvas.addEventListener('mouseup', onMouseUp); 
  canvas.addEventListener('wheel', onWheel); 
  canvas.addEventListener('contextmenu', e => e.preventDefault()); 
  document.addEventListener('click', (ev) => { 
    const robotMenu = document.getElementById('robotContextMenu'); 
    const stationMenu = document.getElementById('stationContextMenu'); 
    const tgt = ev.target; 
    if (robotMenu && robotMenu.style.display === 'block' && !robotMenu.contains(tgt)) { hideRobotContextMenu(); } 
    if (stationMenu && stationMenu.style.display === 'block' && !stationMenu.contains(tgt)) { hideStationContextMenu(); } 
  }); 
  const regMapSel = document.getElementById('registerMapSelect'); 
  if (regMapSel) { 
    regMapSel.addEventListener('change', () => { 
      const lbl = document.getElementById('registerMapSelectedLabel'); 
      if (lbl) lbl.textContent = regMapSel.value || '未设置'; 
      updateRegisterInitialPositionOptions(); 
    }); 
  } 
}

async function onMouseDown(e) { 
  const p = getMousePos(e); 
  if (e.button === 2) { 
    const robot = findRobotAtScreenPoint(p.x, p.y); 
    if (robot) { 
      selectedRobotId = robot.robot_name; 
      showRobotContextMenu(p.x, p.y, robot); 
    } else { 
      const hitPoint = findPointAtScreenPos(p.x, p.y); 
      if (hitPoint) { 
        showStationContextMenu(p.x, p.y, hitPoint); 
      } else { 
        hideRobotContextMenu(); 
        hideStationContextMenu(); 
      } 
    } 
    return; 
  } 
  const hitPoint = findPointAtScreenPos(p.x, p.y); 
  if (hitPoint) { 
    selectedStationId = hitPoint.id; 
    const info = getStationDetails(hitPoint); 
    updateStationInfoPanel(info); 
    selectedRouteId = null; 
    lastRouteCandidates = []; 
    lastRouteCandidateIndex = 0; 
    updateRouteInfoPanel(null); 
    drawMap(); 
    hideRobotContextMenu(); 
    return; 
  } 
  const routeCandidates = findRoutesAtScreenPos(p.x, p.y); 
  if (routeCandidates.length > 0) { 
    const candidateIds = routeCandidates.map(rt => String(rt.id ?? (`${rt.from}->${rt.to}`))); 
    const sameSet = (lastRouteCandidates.length === candidateIds.length) && lastRouteCandidates.every((id, idx) => id === candidateIds[idx]); 
    if (!sameSet) { 
      lastRouteCandidates = candidateIds; 
      lastRouteCandidateIndex = 0; 
    } else { 
      lastRouteCandidateIndex = (lastRouteCandidateIndex + 1) % lastRouteCandidates.length; 
    } 
    const targetRoute = routeCandidates[lastRouteCandidateIndex]; 
    selectedRouteId = lastRouteCandidates[lastRouteCandidateIndex]; 
    const rinfo = getRouteDetails(targetRoute); 
    updateRouteInfoPanel(rinfo); 
    selectedStationId = null; 
    updateStationInfoPanel(null); 
    drawMap(); 
    hideRobotContextMenu(); 
    return; 
  } 
  isDragging = true; 
  lastMousePos = p; 
  canvas.style.cursor = 'grabbing'; 
  const mi = document.getElementById('mapInfoSection'); 
  if (mi) mi.style.display = 'block'; 
  const rh = document.getElementById('routeHeader'); 
  if (rh) rh.style.display = 'none'; 
  const sh = document.getElementById('stationHeader'); 
  if (sh) sh.style.display = 'none'; 
  const rd = document.getElementById('routeDetails'); 
  if (rd) rd.style.display = 'none'; 
  const sd = document.getElementById('stationDetails'); 
  if (sd) sd.style.display = 'none'; 
  hideRobotContextMenu(); 
  hideStationContextMenu(); 
}

function onMouseMove(e) { 
  const p = getMousePos(e); 
  const wp = screenToWorld(p.x, p.y); 
  document.getElementById('mousePos').textContent = `${wp.x.toFixed(2)}, ${wp.y.toFixed(2)}`; 
  if (isDragging) { 
    viewTransform.x += p.x - lastMousePos.x; 
    viewTransform.y += p.y - lastMousePos.y; 
    drawMap(); 
    lastMousePos = p; 
  } 
}

function onMouseUp() { 
  isDragging = false; 
  canvas.style.cursor = 'grab'; 
}

function onWheel(e) { 
  e.preventDefault(); 
  const p = getMousePos(e); 
  const wp = screenToWorld(p.x, p.y); 
  const sf = e.deltaY > 0 ? 0.9 : 1.1; 
  const ns = Math.max(0.1, Math.min(5, viewTransform.scale * sf)); 
  if (ns !== viewTransform.scale) { 
    viewTransform.scale = ns; 
    viewTransform.x = p.x - (wp.x * 20) * viewTransform.scale; 
    viewTransform.y = p.y + (wp.y * 20) * viewTransform.scale; 
    updateZoomDisplay(); 
    drawMap(); 
  } 
}

function findPointAtScreenPos(sx, sy) { 
  if (!mapData || !mapData.points) return null; 
  const radius = pointDrawSize() + 2; 
  const currentFloorName = floorNames[currentFloorIndex] || null; 
  for (const p of mapData.points) { 
    const pfloor = extractFloorNameFromLayer(p.layer || ''); 
    if (currentFloorName && pfloor && String(pfloor).trim().toLowerCase() !== String(currentFloorName).trim().toLowerCase()) continue; 
    const sp = worldToScreen(p.x, p.y); 
    const dx = sx - sp.x, dy = sy - sp.y; 
    if (dx * dx + dy * dy <= radius * radius) { 
      return { 
        id: String(p.id || p.name || `${p.x},${p.y}`), 
        name: p.name || p.id || '未知', 
        type: p.type, 
        properties: (Array.isArray(p.properties) ? p.properties : (Array.isArray(p.property) ? p.property : [])), 
        associatedStorageLocations: (Array.isArray(p.associatedStorageLocations) ? p.associatedStorageLocations : []), 
        layer: p.layer || '', 
        pos: { x: p.x, y: p.y }, 
        dir: p.dir || 0 
      }; 
    } 
  } 
  return null; 
}

function findRoutesAtScreenPos(sx, sy) { 
  if (!mapData || !Array.isArray(mapData.routes)) return []; 
  const pmap = {}; 
  (mapData.points || []).forEach(p => { pmap[p.id] = p; }); 
  const threshold = Math.max(6, 8 * (viewTransform.scale)); 
  const candidates = []; 
  for (let i = (mapData.routes.length - 1); i >= 0; i--) { 
    const route = mapData.routes[i]; 
    const fp = pmap[route.from], tp = pmap[route.to]; 
    if (!fp || !tp) continue; 
    if (route.type === 'bezier3' && route.c1 && route.c2) { 
      const N = 24; 
      let minD = Infinity; 
      let prev = worldToScreen(fp.x, fp.y); 
      for (let j = 1; j <= N; j++) { 
        const t = j / N; 
        const bt = bezierPoint({ x: fp.x, y: fp.y }, { x: route.c1.x, y: route.c1.y }, { x: route.c2.x, y: route.c2.y }, { x: tp.x, y: tp.y }, t); 
        const cur = worldToScreen(bt.x, bt.y); 
        const d = pointToSegmentDistance(sx, sy, prev.x, prev.y, cur.x, cur.y); 
        if (d < minD) minD = d; 
        prev = cur; 
      } 
      if (minD <= threshold) candidates.push(route); 
    } else { 
      const s1 = worldToScreen(fp.x, fp.y); 
      const s2 = worldToScreen(tp.x, tp.y); 
      const d = pointToSegmentDistance(sx, sy, s1.x, s1.y, s2.x, s2.y); 
      if (d <= threshold) candidates.push(route); 
    } 
  } 
  return candidates; 
}

function findRobotAtScreenPoint(sx, sy) { 
  for (const robot of registeredRobots) { 
    if (robot.currentMap) { 
      const robotFloor = extractFloorFromMapId(robot.currentMap); 
      const currentFloorName = floorNames[currentFloorIndex] || null; 
      if (robotFloor && currentFloorName && robotFloor !== currentFloorName) continue; 
    } 
    const pos = robot.currentPosition || robot.initialPosition || null; 
    if (!pos) continue; 
    const sp = worldToScreen(pos.x, pos.y); 
    const dx = sx - sp.x; 
    const dy = sy - sp.y; 
    const ang = (pos.theta ?? pos.orientation ?? 0); 
    const cosA = Math.cos(ang + Math.PI / 2), sinA = Math.sin(ang + Math.PI / 2); 
    const lx = dx * cosA - dy * sinA; 
    const ly = dx * sinA + dy * cosA; 
    const s = Math.max(0.5, Math.min(2, viewTransform.scale)); 
    const half = 12 * s; 
    if (lx >= -half && lx <= half && ly >= -half && ly <= half) { return robot; } 
  } 
  return null; 
}

function getStationDetails(pt) { 
  const id = String(pt.id || pt.name || ''); 
  const name = String(pt.name || pt.id || ''); 
  const typeCode = (pt.type !== undefined) ? Number(pt.type) : NaN; 
  const className = (POINT_TYPE_INFO[typeCode]?.label) || '未知类型'; 
  const dir = (typeof pt.dir === 'number') ? pt.dir : undefined; 
  const ignoreDir = false; 
  let spin = false; 
  const props = Array.isArray(pt.properties) ? pt.properties : (Array.isArray(pt.property) ? pt.property : []); 
  for (const prop of props) { 
    if (prop && prop.key === 'spin' && prop.boolValue === true) { spin = true; break; } 
  } 
  const stor = Array.isArray(pt.associatedStorageLocations) ? pt.associatedStorageLocations : []; 
  return { id, className, instanceName: name, pos: pt.pos, dir, ignoreDir, spin, associatedStorageLocations: stor }; 
}

function getRouteDetails(route) { 
  if (!route) return null; 
  const id = String(route.id ?? (`${route.from}->${route.to}`)); 
  const desc = String(route.desc || route.name || '-'); 
  const type = String(route.type || '-'); 
  const pass = (route.pass !== undefined) ? Number(route.pass) : 0; 
  return { id, desc, type, passLabel: getRoutePassLabel(pass), passCode: pass }; 
}

function updateStationInfoPanel(info) { 
  if (!info) { 
    document.getElementById('stId').textContent = '-'; 
    document.getElementById('stType').textContent = '-'; 
    document.getElementById('stName').textContent = '-'; 
    document.getElementById('stPos').textContent = '-'; 
    document.getElementById('stDir').textContent = '-'; 
    document.getElementById('stSpin').textContent = '-'; 
    document.getElementById('stStorage').textContent = '-'; 
    const sr = document.getElementById('stStorageRow'); 
    if (sr) sr.style.display = 'none'; 
    const sdr = document.getElementById('stStorageDetailsRow'); 
    if (sdr) sdr.style.display = 'none'; 
    const sdt = document.getElementById('stStorageDetails'); 
    if (sdt) sdt.textContent = '-'; 
    const sd = document.getElementById('stationDetails'); 
    if (sd) sd.style.display = 'none'; 
    const sh = document.getElementById('stationHeader'); 
    if (sh) sh.style.display = 'none'; 
    return; 
  } 
  document.getElementById('stId').textContent = info.id || '-'; 
  document.getElementById('stType').textContent = info.className || '-'; 
  document.getElementById('stName').textContent = info.instanceName || '-'; 
  const posStr = info.pos ? `(${Number(info.pos.x).toFixed(2)}, ${Number(info.pos.y).toFixed(2)})` : '-'; 
  document.getElementById('stPos').textContent = posStr; 
  const orientationText = (info.ignoreDir === true) ? '任意' : (typeof info.dir === 'number' ? `${Number(info.dir * 180 / Math.PI).toFixed(2)}°` : '-'); 
  document.getElementById('stDir').textContent = orientationText; 
  document.getElementById('stSpin').textContent = (info.spin === true) ? 'true' : 'false'; 
  const bin = lookupBinLocationInfo(info.instanceName || info.id); 
  const storNames = Array.isArray(bin?.locationNames) ? bin.locationNames : []; 
  const hasDetails = renderBinTaskDetails(bin); 
  const sr = document.getElementById('stStorageRow'); 
  const sdr = document.getElementById('stStorageDetailsRow'); 
  if (sr) sr.style.display = (storNames.length > 0) ? 'block' : 'none'; 
  if (sdr) sdr.style.display = hasDetails ? 'block' : 'none'; 
  const storEl = document.getElementById('stStorage'); 
  if (storEl) storEl.textContent = storNames.join(', ') || '-'; 
  const sd = document.getElementById('stationDetails'); 
  if (sd) sd.style.display = 'block'; 
  const sh = document.getElementById('stationHeader'); 
  if (sh) sh.style.display = 'block'; 
  const mi = document.getElementById('mapInfoSection'); 
  if (mi) mi.style.display = 'block'; 
  const rh = document.getElementById('routeHeader'); 
  if (rh) rh.style.display = 'none'; 
  const rd = document.getElementById('routeDetails'); 
  if (rd) rd.style.display = 'none'; 
}

function updateRouteInfoPanel(info) { 
  const panel = document.getElementById('routeDetails'); 
  if (!panel) return; 
  if (!info) { 
    document.getElementById('rtId').textContent = '-'; 
    document.getElementById('rtDesc').textContent = '-'; 
    document.getElementById('rtType').textContent = '-'; 
    document.getElementById('rtPass').textContent = '-'; 
    panel.style.display = 'none'; 
    const rh = document.getElementById('routeHeader'); 
    if (rh) rh.style.display = 'none'; 
    return; 
  } 
  document.getElementById('rtId').textContent = info.id || '-'; 
  document.getElementById('rtDesc').textContent = info.desc || '-'; 
  document.getElementById('rtType').textContent = info.type || '-'; 
  document.getElementById('rtPass').textContent = info.passLabel || '-'; 
  panel.style.display = 'block'; 
  const rh = document.getElementById('routeHeader'); 
  if (rh) rh.style.display = 'block'; 
  const sd = document.getElementById('stationDetails'); 
  if (sd) sd.style.display = 'none'; 
  const sh = document.getElementById('stationHeader'); 
  if (sh) sh.style.display = 'none'; 
  const mi = document.getElementById('mapInfoSection'); 
  if (mi) mi.style.display = 'none'; 
}

function showRobotContextMenu(sx, sy, robot) { 
  const menu = document.getElementById('robotContextMenu'); 
  if (!menu) return; 
  const hasPallet = !!robot.hasPallet; 
  menu.innerHTML = ''; 
  const item = document.createElement('div'); 
  item.style.padding = '8px 12px'; 
  item.style.cursor = 'pointer'; 
  item.textContent = hasPallet ? '卸载托盘' : '加载托盘'; 
  item.onclick = function () { 
    if (hasPallet) unloadPallet(robot.robot_name); 
    else loadPallet(robot.robot_name); 
    hideRobotContextMenu(); 
  }; 
  menu.appendChild(item); 
  const container = document.getElementById('canvasContainer'); 
  const rect = container.getBoundingClientRect(); 
  menu.style.left = (sx + rect.left - rect.left) + 'px'; 
  menu.style.top = (sy + rect.top - rect.top) + 'px'; 
  menu.style.display = 'block'; 
}

function hideRobotContextMenu() { 
  const menu = document.getElementById('robotContextMenu'); 
  if (menu) menu.style.display = 'none'; 
}

function showStationContextMenu(sx, sy, point) { 
  const menu = document.getElementById('stationContextMenu'); 
  if (!menu) return; 
  menu.innerHTML = ''; 
  const itemNav = document.createElement('div'); 
  itemNav.style.padding = '8px 12px'; 
  itemNav.style.cursor = 'pointer'; 
  itemNav.textContent = '导航至该站点'; 
  itemNav.onclick = async function (ev) { 
    if (ev && ev.stopPropagation) ev.stopPropagation(); 
    try { await navigateRobotToStation(point); } 
    catch (err) { console.error('导航失败:', err); try { window.printErrorToStatus(err, '导航失败'); } catch (_) { } } 
    finally { hideStationContextMenu(); } 
  }; 
  if (!selectedRobotId) { 
    itemNav.style.opacity = '0.6'; 
    itemNav.style.pointerEvents = 'none'; 
    const tip = document.createElement('div'); 
    tip.style.padding = '6px 12px'; 
    tip.style.fontSize = '12px'; 
    tip.style.color = '#bdc3c7'; 
    tip.textContent = '请先在列表中选中机器人'; 
    menu.appendChild(tip); 
  } 
  menu.appendChild(itemNav); 
  const isCharge = isChargingStationPoint(point); 
  if (isCharge) { 
    const itemCharge = document.createElement('div'); 
    itemCharge.style.padding = '8px 12px'; 
    itemCharge.style.cursor = 'pointer'; 
    itemCharge.textContent = '执行充电任务'; 
    itemCharge.onclick = async function (ev) { 
      if (ev && ev.stopPropagation) ev.stopPropagation(); 
      try { await navigateRobotToStationWithAction(point, 'StartCharging', { source: 'CPMenu' }); } 
      catch (err) { console.error('充电任务发布失败:', err); try { window.printErrorToStatus(err, '充电任务发布失败'); } catch (_) { } } 
      finally { hideStationContextMenu(); } 
    }; 
    if (!selectedRobotId) { itemCharge.style.opacity = '0.6'; itemCharge.style.pointerEvents = 'none'; } 
    menu.appendChild(itemCharge); 
  } 
  const options = extractBinTaskOptionsForPoint(point); 
  const isWork = isWorkStationPoint(point); 
  if (isWork && options.length > 0) { 
    const actionItem = document.createElement('div'); 
    actionItem.style.padding = '8px 12px'; 
    actionItem.style.cursor = 'pointer'; 
    actionItem.textContent = '至站点执行动作'; 
    const sub = document.createElement('div'); 
    sub.style.display = 'none'; 
    sub.style.borderTop = '1px solid #34495e'; 
    options.forEach(opt => { 
      const subItem = document.createElement('div'); 
      subItem.style.padding = '6px 12px 6px 24px'; 
      subItem.style.cursor = 'pointer'; 
      subItem.style.fontSize = '12px'; 
      subItem.textContent = opt.title; 
      subItem.onclick = async function (ev) { 
        if (ev && ev.stopPropagation) ev.stopPropagation(); 
        try { await navigateRobotToStationWithAction(point, opt.title, opt.params); } 
        catch (err) { console.error('动作任务发布失败:', err); try { window.printErrorToStatus(err, '动作任务发布失败'); } catch (_) { } } 
        finally { hideStationContextMenu(); } 
      }; 
      sub.appendChild(subItem); 
    }); 
    actionItem.onmouseenter = function () { sub.style.display = 'block'; }; 
    actionItem.onmouseleave = function () { sub.style.display = 'none'; }; 
    actionItem.appendChild(sub); 
    menu.appendChild(actionItem); 
  } 
  const container = document.getElementById('canvasContainer'); 
  const rect = container.getBoundingClientRect(); 
  menu.style.left = (sx + rect.left - rect.left) + 'px'; 
  menu.style.top = (sy + rect.top - rect.top) + 'px'; 
  menu.style.display = 'block'; 
}

function hideStationContextMenu() { 
  const menu = document.getElementById('stationContextMenu'); 
  if (menu) menu.style.display = 'none'; 
}

function extractBinTaskOptionsForPoint(point) { 
  try { 
    const key = String(point?.name || point?.id || '').trim(); 
    if (!key) return []; 
    const entry = lookupBinLocationInfo(key); 
    const objs = Array.isArray(entry?.binTaskObjects) ? entry.binTaskObjects : []; 
    const options = []; 
    for (const obj of objs) { 
      const list = Array.isArray(obj) ? obj : [obj]; 
      for (const item of list) { 
        if (item && typeof item === 'object' && Object.keys(item).length > 0) { 
          const actionName = Object.keys(item)[0]; 
          const params = item[actionName]; 
          options.push({ title: String(actionName), params: (params && typeof params === 'object') ? params : {} }); 
        } 
      } 
    } 
    if (options.length === 0) { 
      const raws = Array.isArray(entry?.binTaskStrings) ? entry.binTaskStrings : []; 
      for (const s of raws) { 
        let title = String(s).trim(); 
        title = title.replace(/\s+/g, ' ').slice(0, 50) || 'RawAction'; 
        options.push({ title, params: { content: s } }); 
      } 
    } 
    return options; 
  } catch (e) { return []; } 
}

function toggleSidebar() { 
  const s = document.getElementById('sidebar'), t = document.getElementById('sidebarToggle'), c = document.getElementById('canvasContainer'); 
  sidebarOpen = !sidebarOpen; 
  if (sidebarOpen) { 
    s.classList.add('open'); c.classList.add('sidebar-open'); t.classList.add('open'); t.textContent = '收起'; 
  } else { 
    s.classList.remove('open'); c.classList.remove('sidebar-open'); t.classList.remove('open'); t.textContent = '打开'; 
  } 
  setTimeout(resizeCanvas, 400); 
}

function switchTab(name) { 
  document.querySelectorAll('.tab-button').forEach(b => b.classList.remove('active')); 
  document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active')); 
  if (name === 'equip') { 
    document.querySelector('.tab-button[onclick="switchTab(\'equip\')"]').classList.add('active'); 
    document.getElementById('equipTab').classList.add('active'); 
    loadEquipmentList(); 
  } else { 
    document.querySelector('.tab-button[onclick="switchTab(\'list\')"]').classList.add('active'); 
    document.getElementById('listTab').classList.add('active'); 
  } 
}
