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

function getRouteColorByPass(passCode) {
  const info = PASS_TYPE_INFO[passCode];
  return info ? info.color : '#3498db';
}

function bezierPoint(p0, p1, p2, p3, t) {
  const u = 1 - t;
  const tt = t * t;
  const uu = u * u;
  const uuu = uu * u;
  const ttt = tt * t;
  return {
    x: uuu * p0.x + 3 * uu * t * p1.x + 3 * u * tt * p2.x + ttt * p3.x,
    y: uuu * p0.y + 3 * uu * t * p1.y + 3 * u * tt * p2.y + ttt * p3.y
  };
}

function bezierTangent(p0, p1, p2, p3, t) {
  const u = 1 - t;
  return {
    x: 3 * u * u * (p1.x - p0.x) + 6 * u * t * (p2.x - p1.x) + 3 * t * t * (p3.x - p2.x),
    y: 3 * u * u * (p1.y - p0.y) + 6 * u * t * (p2.y - p1.y) + 3 * t * t * (p3.y - p2.y)
  };
}

function drawMap() {
  if (window.SimViewer3D && window.SimViewer3D.drawMap) {
    window.SimViewer3D.drawMap();
  }
}

window.getRouteDirection = getRouteDirection;
window.getRouteColorByPass = getRouteColorByPass;
window.bezierPoint = bezierPoint;
window.bezierTangent = bezierTangent;
window.drawMap = drawMap;
