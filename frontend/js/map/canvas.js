import * as THREE from 'three';
import SimViewer3D from './canvas3d.js';

window.THREE = THREE;
window.SimViewer3D = SimViewer3D;

let isDragging = false;
let lastMousePos = { x: 0, y: 0 };

async function initCanvas() {
  const initialized = SimViewer3D.init();
  if (!initialized) {
    console.error('Failed to initialize 3D scene');
    return;
  }

  const canvas = SimViewer3D.getCanvas();
  if (canvas) {
    canvas.addEventListener('mousedown', onMouseDown);
    canvas.addEventListener('mousemove', onMouseMove);
    canvas.addEventListener('mouseup', onMouseUp);
    canvas.addEventListener('contextmenu', e => e.preventDefault());
  }

  document.addEventListener('click', (ev) => {
    const robotMenu = document.getElementById('robotContextMenu');
    const stationMenu = document.getElementById('stationContextMenu');
    const tgt = ev.target;
    if (robotMenu && robotMenu.style.display === 'block' && !robotMenu.contains(tgt)) {
      hideRobotContextMenu();
    }
    if (stationMenu && stationMenu.style.display === 'block' && !stationMenu.contains(tgt)) {
      hideStationContextMenu();
    }
  });

  const regMapSel = document.getElementById('registerMapSelect');
  if (regMapSel) {
    regMapSel.addEventListener('change', () => {
      const lbl = document.getElementById('registerMapSelectedLabel');
      if (lbl) lbl.textContent = regMapSel.value || '未设置';
      updateRegisterInitialPositionOptions();
    });
  }

  SimViewer3D.animate();
}

function resizeCanvas() {
  const container = document.querySelector('.canvas-container');
  if (container && SimViewer3D.getCamera()) {
    const camera = SimViewer3D.getCamera();
    const renderer = SimViewer3D.getCanvas()?.parentElement;
    if (renderer) {
      camera.aspect = container.clientWidth / container.clientHeight;
      camera.updateProjectionMatrix();
    }
  }
  if (typeof mapData !== 'undefined' && mapData) {
    SimViewer3D.drawMap();
  }
}

function setupEventListeners() {
  const canvas = SimViewer3D.getCanvas();
  if (canvas) {
    canvas.addEventListener('mousedown', onMouseDown);
    canvas.addEventListener('mousemove', onMouseMove);
    canvas.addEventListener('mouseup', onMouseUp);
    canvas.addEventListener('contextmenu', e => e.preventDefault());
  }

  document.addEventListener('click', (ev) => {
    const robotMenu = document.getElementById('robotContextMenu');
    const stationMenu = document.getElementById('stationContextMenu');
    const tgt = ev.target;
    if (robotMenu && robotMenu.style.display === 'block' && !robotMenu.contains(tgt)) {
      hideRobotContextMenu();
    }
    if (stationMenu && stationMenu.style.display === 'block' && !stationMenu.contains(tgt)) {
      hideStationContextMenu();
    }
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
  const canvas = SimViewer3D.getCanvas();
  if (!canvas) return;

  const rect = canvas.getBoundingClientRect();
  const p = { x: e.clientX - rect.left, y: e.clientY - rect.top };

  if (e.button === 2) {
    const intersects = SimViewer3D.getIntersectedObjects(e);

    for (const intersect of intersects) {
      let obj = intersect.object;
      while (obj.parent && !obj.userData.type) {
        obj = obj.parent;
      }

      if (obj.userData.type === 'agv') {
        if (typeof selectedRobotId !== 'undefined') {
          selectedRobotId = obj.userData.agvId;
        }
        showRobotContextMenu(p.x, p.y, obj.userData.agvData);
        return;
      }

      if (obj.userData.type === 'station') {
        showStationContextMenu(p.x, p.y, obj.userData.pointData);
        return;
      }
    }

    hideRobotContextMenu();
    hideStationContextMenu();
    return;
  }

  const intersects = SimViewer3D.getIntersectedObjects(e);

  for (const intersect of intersects) {
    let obj = intersect.object;
    while (obj.parent && !obj.userData.type) {
      obj = obj.parent;
    }

    if (obj.userData.type === 'station') {
      if (typeof selectedStationId !== 'undefined') {
        selectedStationId = obj.userData.id;
      }
      const info = getStationDetails(obj.userData.pointData);
      updateStationInfoPanel(info);
      if (typeof selectedRouteId !== 'undefined') selectedRouteId = null;
      if (typeof lastRouteCandidates !== 'undefined') {
        lastRouteCandidates = [];
        lastRouteCandidateIndex = 0;
      }
      updateRouteInfoPanel(null);
      SimViewer3D.drawMap();
      hideRobotContextMenu();
      return;
    }

    if (obj.userData.type === 'route') {
      const rid = obj.userData.id;
      if (typeof selectedRouteId !== 'undefined') {
        selectedRouteId = rid;
      }
      const rinfo = getRouteDetails(obj.userData.routeData);
      updateRouteInfoPanel(rinfo);
      if (typeof selectedStationId !== 'undefined') selectedStationId = null;
      updateStationInfoPanel(null);
      SimViewer3D.drawMap();
      hideRobotContextMenu();
      return;
    }

    if (obj.userData.type === 'agv') {
      if (typeof selectedRobotId !== 'undefined') {
        selectedRobotId = obj.userData.agvId;
      }
      SimViewer3D.drawMap();
      hideRobotContextMenu();
      return;
    }
  }

  isDragging = true;
  lastMousePos = p;

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
}

function onMouseMove(e) {
  const canvas = SimViewer3D.getCanvas();
  if (!canvas) return;

  const rect = canvas.getBoundingClientRect();
  const p = { x: e.clientX - rect.left, y: e.clientY - rect.top };

  const camera = SimViewer3D.getCamera();
  const controls = SimViewer3D.getControls();

  if (camera && controls) {
    const wp = screenToWorld3D(p.x, p.y, camera, controls);
    const mousePosEl = document.getElementById('mousePos');
    if (mousePosEl) {
      mousePosEl.textContent = `${wp.x.toFixed(2)}, ${wp.y.toFixed(2)}`;
    }
  }

  if (isDragging) {
    lastMousePos = p;
  }
}

function onMouseUp() {
  isDragging = false;
}

function screenToWorld3D(sx, sy, camera, controls) {
  if (!camera || !controls) return { x: 0, y: 0 };

  const container = document.getElementById('canvas-container');
  if (!container) return { x: 0, y: 0 };

  const mouse = new THREE.Vector2(
    (sx / container.clientWidth) * 2 - 1,
    -(sy / container.clientHeight) * 2 + 1
  );

  const raycaster = new THREE.Raycaster();
  raycaster.setFromCamera(mouse, camera);

  const plane = new THREE.Plane(new THREE.Vector3(0, 1, 0), 0);
  const intersection = new THREE.Vector3();
  raycaster.ray.intersectPlane(plane, intersection);

  return { x: intersection.x, y: -intersection.z };
}

function worldToScreen(wx, wy) {
  const camera = SimViewer3D.getCamera();
  const container = document.getElementById('canvas-container');
  if (!camera || !container) return { x: 0, y: 0 };

  const pos3D = new THREE.Vector3(wx, 0, -wy);
  pos3D.project(camera);

  return {
    x: (pos3D.x + 1) / 2 * container.clientWidth,
    y: (-pos3D.y + 1) / 2 * container.clientHeight
  };
}

function screenToWorld(sx, sy) {
  const camera = SimViewer3D.getCamera();
  const controls = SimViewer3D.getControls();
  return screenToWorld3D(sx, sy, camera, controls);
}

function getMousePos(e) {
  const canvas = SimViewer3D.getCanvas();
  if (!canvas) return { x: 0, y: 0 };
  const r = canvas.getBoundingClientRect();
  return { x: e.clientX - r.left, y: e.clientY - r.top };
}

function findPointAtScreenPos(sx, sy) {
  const camera = SimViewer3D.getCamera();
  const container = document.getElementById('canvas-container');
  if (!camera || !container) return null;

  const mouse = new THREE.Vector2(
    (sx / container.clientWidth) * 2 - 1,
    -(sy / container.clientHeight) * 2 + 1
  );

  const raycaster = new THREE.Raycaster();
  raycaster.setFromCamera(mouse, camera);

  const stationObjects = [];
  if (typeof stationMeshes !== 'undefined') {
    stationMeshes.forEach(mesh => {
      if (mesh.userData.type === 'station') stationObjects.push(mesh);
    });
  }

  const intersects = raycaster.intersectObjects(stationObjects);
  if (intersects.length > 0) {
    return intersects[0].object.userData.pointData;
  }

  return null;
}

function findRoutesAtScreenPos(sx, sy) {
  const camera = SimViewer3D.getCamera();
  const container = document.getElementById('canvas-container');
  if (!camera || !container) return [];

  const mouse = new THREE.Vector2(
    (sx / container.clientWidth) * 2 - 1,
    -(sy / container.clientHeight) * 2 + 1
  );

  const raycaster = new THREE.Raycaster();
  raycaster.setFromCamera(mouse, camera);

  const routeObjects = [];
  if (typeof routeMeshes !== 'undefined') {
    routeMeshes.forEach(mesh => {
      if (mesh.userData.type === 'route') routeObjects.push(mesh);
    });
  }

  const intersects = raycaster.intersectObjects(routeObjects);
  return intersects.map(i => i.object.userData.routeData);
}

function findRobotAtScreenPoint(sx, sy) {
  const camera = SimViewer3D.getCamera();
  const container = document.getElementById('canvas-container');
  if (!camera || !container) return null;

  const mouse = new THREE.Vector2(
    (sx / container.clientWidth) * 2 - 1,
    -(sy / container.clientHeight) * 2 + 1
  );

  const raycaster = new THREE.Raycaster();
  raycaster.setFromCamera(mouse, camera);

  const agvObjects = [];
  if (typeof agvModels !== 'undefined') {
    agvModels.forEach(model => agvObjects.push(model));
  }

  const intersects = raycaster.intersectObjects(agvObjects, true);
  if (intersects.length > 0) {
    let obj = intersects[0].object;
    while (obj.parent && !obj.userData.agvId) {
      obj = obj.parent;
    }
    if (obj.userData.agvData) {
      return obj.userData.agvData;
    }
  }

  return null;
}

function pointDrawSize() {
  return 3;
}

function drawMap() {
  SimViewer3D.drawMap();
}

window.initCanvas = initCanvas;
window.resizeCanvas = resizeCanvas;
window.setupEventListeners = setupEventListeners;
window.drawMap = drawMap;
window.worldToScreen = worldToScreen;
window.screenToWorld = screenToWorld;
window.getMousePos = getMousePos;
window.findPointAtScreenPos = findPointAtScreenPos;
window.findRoutesAtScreenPos = findRoutesAtScreenPos;
window.findRobotAtScreenPoint = findRobotAtScreenPoint;
window.pointDrawSize = pointDrawSize;
