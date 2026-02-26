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

async function getSceneStationsForMap(mapName) {
  try {
    const path = String(mapName || '').trim();
    if (!path) return [];
    const resp = await fetch('/maps/' + path);
    if (!resp.ok) return [];
    const raw = await resp.json();
    const root = Array.isArray(raw) ? raw[0] : raw;
    const points = (root && Array.isArray(root.points)) ? root.points : [];
    const allowedPrefixes = ['AP', 'CP', 'PP', 'LM', 'WP'];
    const startsAllowed = (txt) => allowedPrefixes.some(pref => String(txt || '').trim().toUpperCase().startsWith(pref));
    const out = [];
    for (const p of points) {
      const nm = String(p.name || p.id || '').trim();
      if (!nm || !startsAllowed(nm)) continue;
      out.push({ id: String(p.id || nm), instanceName: nm, pointName: nm, className: deriveClassFromName(nm), pos: { x: Number(p.x), y: Number(p.y) } });
    }
    return out;
  } catch (err) {
    console.error('getSceneStationsForMap error:', err);
    return [];
  }
}

function findNearestSceneStation(stations, worldPos) {
  const allowedPrefixes = ['AP', 'CP', 'PP', 'LM', 'WP'];
  const allowedClasses = new Set(['ActionPoint', 'LocationMark', 'ChargingPoint', 'ParkingPoint', 'WayPoint', 'Waypoint']);
  let best = null;
  let bestD = Infinity;
  (stations || []).forEach(s => {
    const id = String(s.id || '').trim();
    const iName = String(s.instanceName || '').trim();
    const pName = String(s.pointName || '').trim();
    const cls = String(s.className || '').trim();
    const startsAllowed = (txt) => allowedPrefixes.some(pref => txt.startsWith(pref));
    const isAllowed = allowedClasses.has(cls) || startsAllowed(id) || startsAllowed(iName) || startsAllowed(pName);
    if (!isAllowed) return;
    const sx = (s && s.pos && typeof s.pos.x !== 'undefined') ? Number(s.pos.x) : Number(s.x);
    const sy = (s && s.pos && typeof s.pos.y !== 'undefined') ? Number(s.pos.y) : Number(s.y);
    const d = Math.hypot(Number(worldPos.x) - sx, Number(worldPos.y) - sy);
    if (d < bestD) { bestD = d; best = s; }
  });
  return best;
}

function findStationIdForPoint(stations, pt) {
  const key1 = (pt?.name || '').trim();
  const key2 = (pt?.id || '').trim();
  const allowedPrefixes = ['AP', 'CP', 'PP', 'LM', 'WP'];
  const allowedClasses = new Set(['ActionPoint', 'LocationMark', 'ChargingPoint', 'ChargePoint', 'ParkingPoint', 'ParkPoint', 'WayPoint', 'Waypoint']);
  const startsAllowed = (txt) => allowedPrefixes.some(pref => String(txt || '').trim().startsWith(pref));
  const isAllowedStation = (s) => {
    const id = String(s.id || '').trim();
    const iName = String(s.instanceName || '').trim();
    const pName = String(s.pointName || '').trim();
    const cls = String(s.className || '').trim();
    return allowedClasses.has(cls) || startsAllowed(id) || startsAllowed(iName) || startsAllowed(pName);
  };
  for (const s of (stations || [])) {
    const iName = (s.instanceName || '').trim();
    const pName = (s.pointName || '').trim();
    if (!isAllowedStation(s)) continue;
    if (key1 && (iName === key1 || pName === key1)) return s.instanceName || s.id || s.pointName;
    if (key2 && (iName === key2 || pName === key2)) return s.instanceName || s.id || s.pointName;
  }
  const nearest = findNearestSceneStation(stations, pt.pos || { x: 0, y: 0 });
  return nearest ? (nearest.instanceName || nearest.id || nearest.pointName) : null;
}

async function navigateRobotToStation(point) {
  if (!selectedRobotId) {
    try { window.printErrorToStatus('请先在机器人列表中选中机器人', '导航'); } catch (_) {}
    return;
  }
  const robot = registeredRobots.find(r => r.robot_name === selectedRobotId);
  if (!robot) {
    try { window.printErrorToStatus('未找到选中机器人', '导航'); } catch (_) {}
    return;
  }
  const opMode = String(robot.operatingMode || 'AUTOMATIC').toUpperCase();
  if (opMode !== 'SERVICE') {
    try { window.printErrorToStatus(`当前仿真车无控制权（operatingMode=${opMode}），不可下发导航订单`, '导航'); } catch (_) {}
    return;
  }
  const pos = robot.currentPosition || robot.initialPosition;
  if (!pos) {
    try { window.printErrorToStatus('机器人没有当前位置或初始位置', '导航'); } catch (_) {}
    return;
  }
  const mapName = resolveMapNameForNav(robot, point);
  if (!mapName) {
    try { window.printErrorToStatus('无法解析地图文件名', '导航'); } catch (_) {}
    return;
  }
  const st = document.getElementById('status');
  if (st) st.textContent = '正在读取地图并规划路径...';
  const resp = await fetch('/maps/' + mapName);
  if (!resp.ok) {
    try { window.printErrorToStatus(`读取地图失败: HTTP ${resp.status}`, '导航'); } catch (_) {}
    return;
  }
  const sceneData = await resp.json();
  const topo = parseSceneTopology(sceneData);
  if (!Array.isArray(topo.stations) || topo.stations.length === 0 || !Array.isArray(topo.paths) || topo.paths.length === 0) {
    try { window.printErrorToStatus('地图缺少站点或路段（routes），无法规划', '导航'); } catch (_) {}
    return;
  }
  const stations = await getSceneStationsForMap(mapName);
  const targetId = findStationIdForPoint(stations, point);
  if (!targetId) {
    try { window.printErrorToStatus('无法匹配到对应的站点', '导航'); } catch (_) {}
    return;
  }
  const targetStation = (stations || []).find(s => String(s.instanceName || s.id || s.pointName) === String(targetId));
  if (!targetStation || !targetStation.pos) {
    try { window.printErrorToStatus('无法获取目标站点坐标', '导航'); } catch (_) {}
    return;
  }
  const startStationId = findNearestStation({ x: Number(pos.x), y: Number(pos.y) }, topo.stations);
  const endStationId = findNearestStation({ x: Number(targetStation.pos.x), y: Number(targetStation.pos.y) }, topo.stations);
  if (!startStationId || !endStationId) {
    try { window.printErrorToStatus('无法选取起止站点', '导航'); } catch (_) {}
    return;
  }
  const nodePath = aStar(String(startStationId), String(endStationId), topo.stations, topo.paths);
  if (!nodePath || nodePath.length < 2) {
    try { window.printErrorToStatus('未找到可达路径，请检查地图 routes', '导航'); } catch (_) {}
    return;
  }
  const agvInfo = {
    serial_number: selectedRobotId,
    manufacturer: robot.manufacturer || 'SEER',
    version: (robot.version || '2.0.0')
  };
  const mapIdShort = extractFloorFromMapId(mapName);
  const order = generateVdaOrder(nodePath, agvInfo, topo, mapIdShort, { allowedDeviationXY: 0, allowedDeviationTheta: 0 });
  try {
    const pubResp = await fetch(`${API_BASE_URL}/agv/${encodeURIComponent(selectedRobotId)}/order`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(order)
    });
    if (!pubResp.ok) throw new Error(`HTTP ${pubResp.status}: ${pubResp.statusText}`);
    await pubResp.json();
    const stationNameById = new Map((topo.stations || []).map(s => [String(s.id), s.name || String(s.id)]));
    const displayPath = nodePath.map(id => stationNameById.get(String(id)) || String(id));
    if (st) st.textContent = `订单已发布，路径: ${displayPath.join(' -> ')}`;
  } catch (e) {
    console.error('订单发布失败:', e);
    try { window.printErrorToStatus(e, '订单发布失败'); } catch (_) {}
  }
}

async function navigateRobotToStationWithAction(point, actionTitle, actionParams) {
  if (!selectedRobotId) {
    try { window.printErrorToStatus('请先在机器人列表中选中机器人', '执行动作'); } catch (_) {}
    return;
  }
  const robot = registeredRobots.find(r => r.robot_name === selectedRobotId);
  if (!robot) {
    try { window.printErrorToStatus('未找到选中机器人', '执行动作'); } catch (_) {}
    return;
  }
  const opMode = String(robot.operatingMode || 'AUTOMATIC').toUpperCase();
  if (opMode !== 'SERVICE') {
    try { window.printErrorToStatus(`当前仿真车无控制权（operatingMode=${opMode}），不可下发导航订单`, '执行动作'); } catch (_) {}
    return;
  }
  const pos = robot.currentPosition || robot.initialPosition;
  if (!pos) {
    try { window.printErrorToStatus('机器人没有当前位置或初始位置', '执行动作'); } catch (_) {}
    return;
  }
  const mapName = resolveMapNameForNav(robot, point);
  if (!mapName) {
    try { window.printErrorToStatus('无法解析地图文件名', '执行动作'); } catch (_) {}
    return;
  }
  const st = document.getElementById('status');
  if (st) st.textContent = '正在读取地图并规划路径...';
  const resp = await fetch('/maps/' + mapName);
  if (!resp.ok) {
    try { window.printErrorToStatus(`读取地图失败: HTTP ${resp.status}`, '执行动作'); } catch (_) {}
    return;
  }
  const sceneData = await resp.json();
  const topo = parseSceneTopology(sceneData);
  if (!Array.isArray(topo.stations) || topo.stations.length === 0 || !Array.isArray(topo.paths) || topo.paths.length === 0) {
    try { window.printErrorToStatus('地图缺少站点或路段（routes），无法规划', '执行动作'); } catch (_) {}
    return;
  }
  const stations = await getSceneStationsForMap(mapName);
  const targetId = findStationIdForPoint(stations, point);
  if (!targetId) {
    try { window.printErrorToStatus('无法匹配到对应的站点', '执行动作'); } catch (_) {}
    return;
  }
  const targetStation = (stations || []).find(s => String(s.instanceName || s.id || s.pointName) === String(targetId));
  if (!targetStation || !targetStation.pos) {
    try { window.printErrorToStatus('无法获取目标站点坐标', '执行动作'); } catch (_) {}
    return;
  }
  const startStationId = findNearestStation({ x: Number(pos.x), y: Number(pos.y) }, topo.stations);
  const endStationId = findNearestStation({ x: Number(targetStation.pos.x), y: Number(targetStation.pos.y) }, topo.stations);
  if (!startStationId || !endStationId) {
    try { window.printErrorToStatus('无法选取起止站点', '执行动作'); } catch (_) {}
    return;
  }
  const nodePath = aStar(String(startStationId), String(endStationId), topo.stations, topo.paths);
  if (!nodePath || nodePath.length < 2) {
    try { window.printErrorToStatus('未找到可达路径，请检查地图 routes', '执行动作'); } catch (_) {}
    return;
  }
  const agvInfo = {
    serial_number: selectedRobotId,
    manufacturer: robot.manufacturer || 'SEER',
    version: (robot.version || '2.0.0')
  };
  const mapIdShort = extractFloorFromMapId(mapName);
  const order = generateVdaOrder(nodePath, agvInfo, topo, mapIdShort, { allowedDeviationXY: 0, allowedDeviationTheta: 0 });
  try {
    const lastIdx = (order.nodes || []).length - 1;
    if (lastIdx >= 0) {
      const paramsList = [];
      if (actionParams && typeof actionParams === 'object') {
        for (const k of Object.keys(actionParams)) {
          paramsList.push({ key: String(k), value: actionParams[k] });
        }
      }
      const actionObj = {
        actionType: String(actionTitle || ''),
        actionId: 'act-' + String(Date.now()),
        blockingType: 'HARD',
        actionDescription: String(actionTitle || ''),
        actionParameters: paramsList
      };
      if (!Array.isArray(order.nodes[lastIdx].actions)) order.nodes[lastIdx].actions = [];
      order.nodes[lastIdx].actions.push(actionObj);
    }
    const pubResp = await fetch(`${API_BASE_URL}/agv/${encodeURIComponent(selectedRobotId)}/order`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(order)
    });
    if (!pubResp.ok) throw new Error(`HTTP ${pubResp.status}: ${pubResp.statusText}`);
    await pubResp.json();
    const stationNameById = new Map((topo.stations || []).map(s => [String(s.id), s.name || String(s.id)]));
    const displayPath = nodePath.map(id => stationNameById.get(String(id)) || String(id));
    if (st) st.textContent = `订单已发布，路径: ${displayPath.join(' -> ')}，动作: ${String(actionTitle)}`;
  } catch (e) {
    console.error('订单发布失败:', e);
    try { window.printErrorToStatus(e, '订单发布失败'); } catch (_) {}
  }
}
