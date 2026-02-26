function pointToSegmentDistance(px, py, x1, y1, x2, y2) {
  const dx = x2 - x1, dy = y2 - y1;
  if (dx === 0 && dy === 0) return Math.hypot(px - x1, py - y1);
  const t = ((px - x1) * dx + (py - y1) * dy) / (dx * dx + dy * dy);
  const tt = Math.max(0, Math.min(1, t));
  const cx = x1 + tt * dx, cy = y1 + tt * dy;
  return Math.hypot(px - cx, py - cy);
}

function findPointAtScreenPos(sx, sy) {
  if (window.SimViewer3D && window.SimViewer3D.findStationAtPoint) {
    const worldPos = screenToWorld(sx, sy);
    return window.SimViewer3D.findStationAtPoint(worldPos.x, worldPos.y);
  }

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
  if (window.SimViewer3D && window.SimViewer3D.findRouteAtPoint) {
    const worldPos = screenToWorld(sx, sy);
    const route = window.SimViewer3D.findRouteAtPoint(worldPos.x, worldPos.y);
    return route ? [route] : [];
  }

  if (!mapData || !Array.isArray(mapData.routes)) return [];
  const pmap = {};
  (mapData.points || []).forEach(p => {
    pmap[p.id] = p;
  });
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
        const bt = bezierPoint(
          { x: fp.x, y: fp.y },
          { x: route.c1.x, y: route.c1.y },
          { x: route.c2.x, y: route.c2.y },
          { x: tp.x, y: tp.y },
          t
        );
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
  if (window.SimViewer3D && window.SimViewer3D.findAGVAtPoint) {
    const worldPos = screenToWorld(sx, sy);
    return window.SimViewer3D.findAGVAtPoint(worldPos.x, worldPos.y);
  }

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
    if (lx >= -half && lx <= half && ly >= -half && ly <= half) {
      return robot;
    }
  }
  return null;
}

window.pointToSegmentDistance = pointToSegmentDistance;
window.findPointAtScreenPos = findPointAtScreenPos;
window.findRoutesAtScreenPos = findRoutesAtScreenPos;
window.findRobotAtScreenPoint = findRobotAtScreenPoint;
