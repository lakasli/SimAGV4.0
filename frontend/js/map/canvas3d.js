import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';

window.THREE = THREE;
window.RoundedBoxGeometry = RoundedBoxGeometry;

const AGV_STATUS_CONFIG = {
  running: { label: '运行中', color: 0x22c55e, emissive: 0x22c55e },
  idle: { label: '空闲', color: 0x3b82f6, emissive: 0x3b82f6 },
  error: { label: '故障', color: 0xef4444, emissive: 0xef4444 },
  charging: { label: '充电中', color: 0xeab308, emissive: 0xeab308 }
};

let scene = null;
let camera = null;
let renderer = null;
let controls = null;
let ground = null;
let gridHelper = null;
let materials = {};
let agvModels = new Map();
let stationMeshes = new Map();
let routeMeshes = [];
let selectedObject = null;
let raycaster = null;
let mouse = null;

const SCALE_FACTOR = 20;
const CAMERA_HEIGHT = 15;
const CAMERA_DISTANCE = 20;

let cachedMapData = null;
let cachedFloorName = null;
let cachedSelectedStationId = null;
let cachedSelectedRouteId = null;
let cachedPointsKey = null;
let cachedRoutesKey = null;
let cachedRobotPositions = new Map();

function initScene() {
  const container = document.getElementById('canvas-container');
  if (!container) {
    console.error('Canvas container not found');
    return false;
  }

  scene = new THREE.Scene();
  scene.background = new THREE.Color(0x1a1a2e);

  const aspect = container.clientWidth / container.clientHeight;
  camera = new THREE.PerspectiveCamera(45, aspect, 0.1, 1000);
  camera.position.set(0, CAMERA_HEIGHT, CAMERA_DISTANCE);

  renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setSize(container.clientWidth, container.clientHeight);
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  container.appendChild(renderer.domElement);

  controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  controls.dampingFactor = 0.05;
  controls.minDistance = 5;
  controls.maxDistance = 100;
  controls.maxPolarAngle = Math.PI / 2 - 0.1;

  raycaster = new THREE.Raycaster();
  mouse = new THREE.Vector2();

  setupLights();
  createGround();

  window.addEventListener('resize', onWindowResize);

  return true;
}

function setupLights() {
  const ambientLight = new THREE.AmbientLight(0xffffff, 0.4);
  scene.add(ambientLight);

  const mainLight = new THREE.DirectionalLight(0xffffff, 1);
  mainLight.position.set(10, 20, 10);
  mainLight.castShadow = true;
  mainLight.shadow.mapSize.width = 2048;
  mainLight.shadow.mapSize.height = 2048;
  mainLight.shadow.camera.near = 0.5;
  mainLight.shadow.camera.far = 100;
  mainLight.shadow.camera.left = -50;
  mainLight.shadow.camera.right = 50;
  mainLight.shadow.camera.top = 50;
  mainLight.shadow.camera.bottom = -50;
  scene.add(mainLight);

  const fillLight = new THREE.DirectionalLight(0x6366f1, 0.3);
  fillLight.position.set(-10, 10, -10);
  scene.add(fillLight);

  const rimLight = new THREE.DirectionalLight(0xf97316, 0.2);
  rimLight.position.set(0, 10, -20);
  scene.add(rimLight);
}

function createGround() {
  const groundGeometry = new THREE.PlaneGeometry(100, 100);
  const groundMaterial = new THREE.MeshStandardMaterial({
    color: 0x1e293b,
    roughness: 0.8,
    metalness: 0.2
  });
  ground = new THREE.Mesh(groundGeometry, groundMaterial);
  ground.rotation.x = -Math.PI / 2;
  ground.position.y = -0.01;
  ground.receiveShadow = true;
  scene.add(ground);

  gridHelper = new THREE.GridHelper(100, 100, 0x334155, 0x1e293b);
  gridHelper.position.y = 0.01;
  scene.add(gridHelper);
}

function createMaterial(name, color, options = {}) {
  materials[name] = new THREE.MeshStandardMaterial({
    color: color,
    roughness: options.roughness || 0.4,
    metalness: options.metalness || 0.3,
    ...options
  });
  return materials[name];
}

function worldTo3D(x, y) {
  return { x: x, z: -y };
}

function worldTo3DPos(x, y) {
  return new THREE.Vector3(x, 0, -y);
}

function pos3DToWorld(pos) {
  return { x: pos.x, y: -pos.z };
}

function clearMapObjects() {
  stationMeshes.forEach(mesh => {
    scene.remove(mesh);
    if (mesh.geometry) mesh.geometry.dispose();
    if (mesh.material) mesh.material.dispose();
  });
  stationMeshes.clear();

  routeMeshes.forEach(mesh => {
    scene.remove(mesh);
    if (mesh.geometry) mesh.geometry.dispose();
    if (mesh.material) mesh.material.dispose();
  });
  routeMeshes = [];
}

function drawPoints3D(points, selectedStationId) {
  if (!scene || !points) return;

  const currentFloorName = (typeof floorNames !== 'undefined' && floorNames[currentFloorIndex]) || null;

  const cacheKey = JSON.stringify({
    points: points.map(p => p.id),
    floor: currentFloorName,
    selectedId: selectedStationId
  });

  if (cachedPointsKey === cacheKey && cachedSelectedStationId === selectedStationId) {
    updateStationSelection(selectedStationId);
    return;
  }

  cachedPointsKey = cacheKey;
  cachedFloorName = currentFloorName;
  cachedSelectedStationId = selectedStationId;

  stationMeshes.forEach(mesh => {
    scene.remove(mesh);
    if (mesh.geometry) mesh.geometry.dispose();
    if (mesh.material) {
      if (mesh.material.map) mesh.material.map.dispose();
      mesh.material.dispose();
    }
  });
  stationMeshes.clear();

  points.forEach(point => {
    const pfloor = extractFloorNameFromLayer(point.layer || '');
    if (currentFloorName && pfloor && String(pfloor).trim().toLowerCase() !== String(currentFloorName).trim().toLowerCase()) {
      return;
    }

    let color = 0x95a5a6;
    const t = Number(point.type);
    if (!Number.isNaN(t) && typeof POINT_TYPE_INFO !== 'undefined' && POINT_TYPE_INFO[t]) {
      const colorStr = POINT_TYPE_INFO[t].color;
      color = parseInt(colorStr.replace('#', '0x'));
    } else {
      const nm = point.name || '';
      if (nm.startsWith('AP')) color = 0xe74c3c;
      else if (nm.startsWith('LM')) color = 0xf39c12;
      else if (nm.startsWith('CP')) color = 0x27ae60;
    }

    const geometry = new THREE.CylinderGeometry(0.2, 0.2, 0.01, 10);
    const material = new THREE.MeshStandardMaterial({
      color: color,
      roughness: 0.5,
      metalness: 0.3
    });
    const mesh = new THREE.Mesh(geometry, material);

    const pos3D = worldTo3D(point.x, point.y);
    mesh.position.set(pos3D.x, 0.01, pos3D.z);
    mesh.castShadow = true;
    mesh.receiveShadow = true;

    mesh.userData = {
      type: 'station',
      id: point.id,
      name: point.name || '',
      pointType: point.type,
      pointData: point
    };

    const key = (point.name || '') || point.id;
    const isSelected = !!selectedStationId && key === selectedStationId;
    if (isSelected) {
      mesh.material.emissive = new THREE.Color(0xf1c40f);
      mesh.material.emissiveIntensity = 0.5;
    }

    scene.add(mesh);
    stationMeshes.set(point.id, mesh);

    if (point.name) {
      const canvas = document.createElement('canvas');
      const context = canvas.getContext('2d');
      canvas.width = 256;
      canvas.height = 64;
      context.fillStyle = '#ecf0f1';
      context.font = 'bold 24px Arial';
      context.textAlign = 'center';
      context.fillText(point.name, 128, 40);

      const texture = new THREE.CanvasTexture(canvas);
      const spriteMaterial = new THREE.SpriteMaterial({ map: texture });
      const sprite = new THREE.Sprite(spriteMaterial);
      sprite.position.set(pos3D.x, 0.5, pos3D.z);
      sprite.scale.set(2, 0.5, 1);
      sprite.userData = { type: 'label', parentId: point.id };
      scene.add(sprite);
      stationMeshes.set('label_' + point.id, sprite);
    }
  });
}

function updateStationSelection(selectedStationId) {
  stationMeshes.forEach((mesh, key) => {
    if (!key.startsWith('label_') && mesh.userData && mesh.userData.type === 'station') {
      const keyStr = (mesh.userData.name || '') || mesh.userData.id;
      const isSelected = !!selectedStationId && keyStr === selectedStationId;
      if (isSelected) {
        if (!mesh.material.emissive) mesh.material.emissive = new THREE.Color();
        mesh.material.emissive.setHex(0xf1c40f);
        mesh.material.emissiveIntensity = 0.5;
      } else if (mesh.material.emissive) {
        mesh.material.emissive.setHex(0x000000);
        mesh.material.emissiveIntensity = 0;
      }
    }
  });
  cachedSelectedStationId = selectedStationId;
}

function updateRouteSelection(selectedRouteId) {
  routeMeshes.forEach(mesh => {
    if (mesh.userData && mesh.userData.type === 'route') {
      const rid = mesh.userData.id;
      const isSelected = !!selectedRouteId && String(selectedRouteId) === rid;
      if (mesh.material) {
        mesh.material.opacity = isSelected ? 1 : 0.8;
      }
    }
  });
  cachedSelectedRouteId = selectedRouteId;
}

function drawRoutes3D(routes, points, selectedRouteId) {
  if (!scene || !routes || !points) return;

  const currentFloorName = (typeof floorNames !== 'undefined' && floorNames[currentFloorIndex]) || null;

  const routeCacheKey = JSON.stringify({
    routes: routes.map(r => `${r.from}->${r.to}`),
    floor: currentFloorName,
    selectedId: selectedRouteId
  });

  if (cachedRoutesKey === routeCacheKey && cachedSelectedRouteId === selectedRouteId) {
    updateRouteSelection(selectedRouteId);
    return;
  }

  cachedRoutesKey = routeCacheKey;
  cachedFloorName = currentFloorName;
  cachedSelectedRouteId = selectedRouteId;

  routeMeshes.forEach(mesh => {
    scene.remove(mesh);
    if (mesh.geometry) mesh.geometry.dispose();
    if (mesh.material) mesh.material.dispose();
  });
  routeMeshes = [];

  const pmap = {};
  points.forEach(p => { pmap[p.id] = p; });

  routes.forEach(route => {
    const fp = pmap[route.from], tp = pmap[route.to];
    if (!fp || !tp) return;

    if (currentFloorName) {
      const nf = String(currentFloorName).trim().toLowerCase();
      const ff = extractFloorNameFromLayer(fp.layer || '');
      const tf = extractFloorNameFromLayer(tp.layer || '');
      const ffn = ff ? String(ff).trim().toLowerCase() : '';
      const tfn = tf ? String(tf).trim().toLowerCase() : '';
      if (ffn && ffn !== nf) return;
      if (tfn && tfn !== nf) return;
    }

    let dir = 1;
    if (typeof route.direction !== 'undefined') {
      dir = route.direction;
      if (dir === 0) dir = 0;
      else dir = dir > 0 ? 1 : -1;
    }

    const passCode = (route.pass !== undefined) ? Number(route.pass) : 0;
    let color = 0x3498db;
    if (typeof PASS_TYPE_INFO !== 'undefined' && PASS_TYPE_INFO[passCode]) {
      const colorStr = PASS_TYPE_INFO[passCode].color;
      color = parseInt(colorStr.replace('#', '0x'));
    }

    const rid = String(route.id ?? (`${route.from}->${route.to}`));
    const isSelected = !!selectedRouteId && String(selectedRouteId) === rid;

    const from3D = worldTo3D(fp.x, fp.y);
    const to3D = worldTo3D(tp.x, tp.y);

    let lineGeometry;
    let curve;

    if (route.type === 'bezier3' && route.c1 && route.c2) {
      const c1_3D = worldTo3D(route.c1.x, route.c1.y);
      const c2_3D = worldTo3D(route.c2.x, route.c2.y);

      curve = new THREE.CubicBezierCurve3(
        new THREE.Vector3(from3D.x, 0.05, from3D.z),
        new THREE.Vector3(c1_3D.x, 0.05, c1_3D.z),
        new THREE.Vector3(c2_3D.x, 0.05, c2_3D.z),
        new THREE.Vector3(to3D.x, 0.05, to3D.z)
      );
      lineGeometry = new THREE.BufferGeometry().setFromPoints(curve.getPoints(50));
    } else {
      const points3D = [
        new THREE.Vector3(from3D.x, 0.05, from3D.z),
        new THREE.Vector3(to3D.x, 0.05, to3D.z)
      ];
      lineGeometry = new THREE.BufferGeometry().setFromPoints(points3D);
    }

    const lineMaterial = new THREE.LineBasicMaterial({
      color: color,
      linewidth: 2,
      transparent: true,
      opacity: isSelected ? 1 : 0.8
    });

    const line = new THREE.Line(lineGeometry, lineMaterial);
    line.userData = {
      type: 'route',
      id: rid,
      routeData: route,
      from: route.from,
      to: route.to
    };

    if (isSelected) {
      lineMaterial.opacity = 1;
    }

    if (passCode === 10) {
      lineMaterial.opacity = 0.5;
    }

    scene.add(line);
    routeMeshes.push(line);

    if (dir !== 0 && curve) {
      const t = 0.7;
      const point = curve.getPoint(t);
      const tangent = curve.getTangent(t);

      const arrowLen = 0.01;
      const arrowDir = dir > 0 ? tangent : tangent.clone().negate();

      const angle = Math.atan2(arrowDir.x, arrowDir.z);
      const shape = new THREE.Shape();
      const size = 0.14;
      shape.moveTo(size * Math.sin(angle), size * Math.cos(angle));
      shape.lineTo(size * Math.sin(angle + Math.PI * 2 / 3), size * Math.cos(angle + Math.PI * 2 / 3));
      shape.lineTo(size * Math.sin(angle + Math.PI * 4 / 3), size * Math.cos(angle + Math.PI * 4 / 3));
      shape.closePath();

      const extrudeSettings = { depth: arrowLen, bevelEnabled: false };
      const arrowGeo = new THREE.ExtrudeGeometry(shape, extrudeSettings);
      arrowGeo.rotateX(-Math.PI / 2);
      const arrowMat = new THREE.MeshBasicMaterial({ color: color });
      const arrow = new THREE.Mesh(arrowGeo, arrowMat);

      arrow.position.copy(point);
      arrow.position.y = 0.05;

      scene.add(arrow);
      routeMeshes.push(arrow);
    } else if (dir !== 0) {
      const arrowLen = 0.01;
      const direction = new THREE.Vector3(to3D.x - from3D.x, 0, to3D.z - from3D.z).normalize();
      if (dir < 0) direction.negate();

      const angle = Math.atan2(direction.x, direction.z);
      const shape = new THREE.Shape();
      const size = 0.14;
      shape.moveTo(size * Math.sin(angle), size * Math.cos(angle));
      shape.lineTo(size * Math.sin(angle + Math.PI * 2 / 3), size * Math.cos(angle + Math.PI * 2 / 3));
      shape.lineTo(size * Math.sin(angle + Math.PI * 4 / 3), size * Math.cos(angle + Math.PI * 4 / 3));
      shape.closePath();

      const extrudeSettings = { depth: arrowLen, bevelEnabled: false };
      const arrowGeo = new THREE.ExtrudeGeometry(shape, extrudeSettings);
      arrowGeo.rotateX(-Math.PI / 2);
      const arrowMat = new THREE.MeshBasicMaterial({ color: color });
      const arrow = new THREE.Mesh(arrowGeo, arrowMat);

      const midPoint = new THREE.Vector3(
        (from3D.x + to3D.x) / 2,
        0.05,
        (from3D.z + to3D.z) / 2
      );
      arrow.position.copy(midPoint);

      scene.add(arrow);
      routeMeshes.push(arrow);
    }
  });
}

function createAGVModel(agv, statusKey = 'idle') {
  const group = new THREE.Group();
  group.userData = { type: 'agv', agvId: agv.robot_name, agvData: agv };

  const statusConfig = AGV_STATUS_CONFIG[statusKey] || AGV_STATUS_CONFIG.idle;

  const chassis = createChassis(statusConfig);
  chassis.position.y = 0.05;
  group.add(chassis);

  const head = createHead(statusConfig);
  head.position.set(0, 0.20, 0.77);
  group.add(head);

  const lidar = createLidar(statusConfig);
  lidar.position.set(0, 0.27, 0.65);
  group.add(lidar);

  const pallet = createPallet();
  pallet.position.set(0, 0.33, -0.1);
  group.add(pallet);

  const wheels = createWheels();
  group.add(wheels);

  const chargingModule = createChargingModule(statusConfig);
  chargingModule.position.set(0, 0.2, -0.77);
  group.add(chargingModule);

  return group;
}

function createChassis(statusConfig) {
  const group = new THREE.Group();

  const shape = new THREE.Shape();
  const width = 1.0;
  const depth = 1.5;
  const chamfer = 0.2;

  shape.moveTo(-width/2 + chamfer, -depth/2);
  shape.lineTo(width/2 - chamfer, -depth/2);
  shape.lineTo(width/2, -depth/2 + chamfer);
  shape.lineTo(width/2, depth/2 - chamfer);
  shape.lineTo(width/2 - chamfer, depth/2);
  shape.lineTo(-width/2 + chamfer, depth/2);
  shape.lineTo(-width/2, depth/2 - chamfer);
  shape.lineTo(-width/2, -depth/2 + chamfer);
  shape.closePath();

  const extrudeSettings = {
    depth: 0.2,
    bevelEnabled: true,
    bevelThickness: 0.02,
    bevelSize: 0.02,
    bevelSegments: 3
  };

  const mainBodyGeo = new THREE.ExtrudeGeometry(shape, extrudeSettings);
  mainBodyGeo.rotateX(-Math.PI / 2);
  const mainBody = new THREE.Mesh(
    mainBodyGeo,
    new THREE.MeshStandardMaterial({
      color: statusConfig.color,
      roughness: 0.6,
      metalness: 0.4
    })
  );
  mainBody.position.y = 0;
  mainBody.castShadow = true;
  mainBody.receiveShadow = true;
  group.add(mainBody);

  return group;
}

function createHead(statusConfig) {
  const group = new THREE.Group();

  const displayGeo = new THREE.BoxGeometry(0.4, 0.06, 0.02);
  const displayMat = new THREE.MeshStandardMaterial({
    color: 0x1e293b,
    emissive: statusConfig.emissive,
    emissiveIntensity: 0.5
  });
  const display = new THREE.Mesh(displayGeo, displayMat);
  group.add(display);

  for (let i = 0; i < 3; i++) {
    const statusLight = new THREE.Mesh(
      new THREE.CylinderGeometry(0.02, 0.02, 0.015, 16),
      new THREE.MeshStandardMaterial({
        color: statusConfig.color,
        emissive: statusConfig.emissive,
        emissiveIntensity: 1
      })
    );
    statusLight.rotation.x = Math.PI / 2;
    statusLight.position.set(-0.15 + i * 0.15, 0, 0.01);
    group.add(statusLight);
  }

  return group;
}

function createLidar(statusConfig) {
  const group = new THREE.Group();

  const lidarMat = new THREE.MeshStandardMaterial({
    color: 0x1e293b,
    metalness: 0.7,
    roughness: 0.3
  });

  const lidarHousing = new THREE.Mesh(
    new THREE.CylinderGeometry(0.06, 0.08, 0.05, 32),
    lidarMat
  );
  group.add(lidarHousing);

  const lidarTop = new THREE.Mesh(
    new THREE.CylinderGeometry(0.05, 0.06, 0.03, 32),
    lidarMat
  );
  lidarTop.position.y = 0.04;
  group.add(lidarTop);

  const lidarSensor = new THREE.Mesh(
    new THREE.CylinderGeometry(0.04, 0.04, 0.015, 32),
    new THREE.MeshStandardMaterial({
      color: statusConfig.color,
      emissive: statusConfig.emissive,
      emissiveIntensity: 0.8
    })
  );
  lidarSensor.position.y = 0.06;
  group.add(lidarSensor);

  return group;
}

function createPallet() {
  const group = new THREE.Group();

  const platformGeo = new RoundedBoxGeometry(0.8, 0.05, 1.0, 4, 0.02);
  const platform = new THREE.Mesh(
    platformGeo,
    new THREE.MeshStandardMaterial({
      color: 0x64748b,
      roughness: 0.7,
      metalness: 0.2
    })
  );
  platform.castShadow = true;
  platform.receiveShadow = true;
  group.add(platform);

  const liftMechanismGeo = new THREE.CylinderGeometry(0.06, 0.06, 0.12, 16);
  const liftMechanismMat = new THREE.MeshStandardMaterial({
    color: 0x334155,
    metalness: 0.6,
    roughness: 0.4
  });

  const positions = [
    [-0.28, -0.06, 0.25],
    [0.28, -0.06, 0.25],
    [-0.28, -0.06, -0.35],
    [0.28, -0.06, -0.35]
  ];

  positions.forEach(pos => {
    const lift = new THREE.Mesh(liftMechanismGeo, liftMechanismMat);
    lift.position.set(pos[0], pos[1], pos[2]);
    lift.castShadow = true;
    group.add(lift);
  });

  return group;
}

function createWheels() {
  const group = new THREE.Group();

  const wheelRadius = 0.1;
  const wheelWidth = 0.08;
  const wheelGeo = new THREE.CylinderGeometry(wheelRadius, wheelRadius, wheelWidth, 32);
  const wheelMat = new THREE.MeshStandardMaterial({
    color: 0x1e293b,
    roughness: 0.8,
    metalness: 0.1
  });

  const wheelPositions = [
    { x: -0.4, z: 0.45 },
    { x: 0.4, z: 0.45 },
    { x: -0.4, z: -0.45 },
    { x: 0.4, z: -0.45 }
  ];

  wheelPositions.forEach(pos => {
    const wheelGroup = new THREE.Group();

    const wheel = new THREE.Mesh(wheelGeo, wheelMat);
    wheel.rotation.z = Math.PI / 2;
    wheel.castShadow = true;
    wheelGroup.add(wheel);

    wheelGroup.position.set(pos.x, wheelRadius - 0.05, pos.z);
    group.add(wheelGroup);
  });

  return group;
}

function createChargingModule(statusConfig) {
  const group = new THREE.Group();

  const barGeo = new THREE.BoxGeometry(0.3, 0.04, 0.015);

  const isCharging = statusConfig.label === '充电中';
  const chargingMat = new THREE.MeshStandardMaterial({
    color: isCharging ? 0xeab308 : 0x475569,
    emissive: isCharging ? 0xeab308 : 0x000000,
    emissiveIntensity: isCharging ? 0.8 : 0
  });

  const topBar = new THREE.Mesh(barGeo, chargingMat);
  group.add(topBar);

  const bottomBar = new THREE.Mesh(barGeo, chargingMat);
  bottomBar.position.y = -0.06;
  group.add(bottomBar);

  return group;
}

function drawRobots3D(robots, selectedRobotId) {
  if (!scene) return;

  const currentFloorName = (typeof floorNames !== 'undefined' && floorNames[currentFloorIndex]) || null;

  const currentRobotIds = new Set();

  robots.forEach(robot => {
    if (robot.currentMap && currentFloorName) {
      const robotFloor = extractFloorFromMapId(robot.currentMap);
      if (robotFloor && String(robotFloor).trim().toLowerCase() !== String(currentFloorName).trim().toLowerCase()) {
        return;
      }
    }

    const pos = robot.currentPosition || robot.initialPosition || null;
    if (!pos) return;

    const robotId = robot.robot_name;
    currentRobotIds.add(robotId);

    let model = agvModels.get(robotId);
    const pos3D = worldTo3D(pos.x, pos.y);
    const theta = Number(pos.theta ?? pos.orientation ?? 0);

    if (!model) {
      const statusKey = robot.status || 'idle';
      model = createAGVModel(robot, statusKey);
      scene.add(model);
      agvModels.set(robotId, model);
    } else {
      const cachedPos = cachedRobotPositions.get(robotId);
      if (cachedPos && cachedPos.x === pos3D.x && cachedPos.z === pos3D.z && cachedPos.theta === theta - Math.PI / 2) {
      } else {
        model.position.set(pos3D.x, 0, pos3D.z);
        model.rotation.y = theta - Math.PI / 2;
        cachedRobotPositions.set(robotId, { x: pos3D.x, z: pos3D.z, theta: theta - Math.PI / 2 });
      }
    }

    const statusKey = robot.status || 'idle';
    updateAGVStatus(robotId, statusKey);

    if (selectedRobotId && selectedRobotId === robotId) {
      model.traverse(child => {
        if (child.isMesh && child.material) {
          child.material.emissive = new THREE.Color(0xf97316);
          child.material.emissiveIntensity = 0.3;
        }
      });
    } else {
      model.traverse(child => {
        if (child.isMesh && child.material && child.material.emissive) {
          child.material.emissiveIntensity = 0;
        }
      });
    }
  });

  agvModels.forEach((model, robotId) => {
    if (!currentRobotIds.has(robotId)) {
      scene.remove(model);
      model.traverse(child => {
        if (child.geometry) child.geometry.dispose();
        if (child.material) child.material.dispose();
      });
      agvModels.delete(robotId);
      cachedRobotPositions.delete(robotId);
    }
  });
}

function updateAGVPosition(robotName, position, theta) {
  const model = agvModels.get(robotName);
  if (!model) return;

  const pos3D = worldTo3D(position.x, position.y);
  model.position.set(pos3D.x, 0, pos3D.z);
  model.rotation.y = theta - Math.PI / 2;
}

function updateAGVStatus(robotName, statusKey) {
  const model = agvModels.get(robotName);
  if (!model) return;

  const statusConfig = AGV_STATUS_CONFIG[statusKey] || AGV_STATUS_CONFIG.idle;

  model.traverse(child => {
    if (child.isMesh && child.material && child.material.emissive) {
      const currentColor = child.material.color.getHex();
      if (Object.values(AGV_STATUS_CONFIG).some(c => c.color === currentColor)) {
        child.material.color.setHex(statusConfig.color);
        child.material.emissive.setHex(statusConfig.emissive);
      }
    }
  });
}

function getIntersectedObjects(event) {
  if (!renderer || !camera || !scene) return [];

  const rect = renderer.domElement.getBoundingClientRect();
  mouse.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
  mouse.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;

  raycaster.setFromCamera(mouse, camera);

  const allObjects = [];
  stationMeshes.forEach(mesh => {
    if (mesh.userData.type === 'station') allObjects.push(mesh);
  });
  agvModels.forEach(model => allObjects.push(model));
  routeMeshes.forEach(mesh => {
    if (mesh.userData.type === 'route') allObjects.push(mesh);
  });

  return raycaster.intersectObjects(allObjects, true);
}

function findStationAtPoint(x, y) {
  const pos3D = worldTo3D(x, y);
  const threshold = 0.5;

  for (const [id, mesh] of stationMeshes) {
    if (mesh.userData.type === 'station') {
      const dist = Math.sqrt(
        Math.pow(mesh.position.x - pos3D.x, 2) +
        Math.pow(mesh.position.z - pos3D.z, 2)
      );
      if (dist < threshold) {
        return mesh.userData.pointData;
      }
    }
  }
  return null;
}

function findRouteAtPoint(x, y) {
  const pos3D = worldTo3D(x, y);
  const threshold = 0.5;

  for (const mesh of routeMeshes) {
    if (mesh.userData.type === 'route') {
      const box = new THREE.Box3().setFromObject(mesh);
      const center = box.getCenter(new THREE.Vector3());
      const dist = Math.sqrt(
        Math.pow(center.x - pos3D.x, 2) +
        Math.pow(center.z - pos3D.z, 2)
      );
      if (dist < threshold) {
        return mesh.userData.routeData;
      }
    }
  }
  return null;
}

function findAGVAtPoint(x, y) {
  const pos3D = worldTo3D(x, y);
  const threshold = 1.0;

  for (const [name, model] of agvModels) {
    const dist = Math.sqrt(
      Math.pow(model.position.x - pos3D.x, 2) +
      Math.pow(model.position.z - pos3D.z, 2)
    );
    if (dist < threshold) {
      return model.userData.agvData;
    }
  }
  return null;
}

function focusOnPosition(x, y) {
  const pos3D = worldTo3D(x, y);
  controls.target.set(pos3D.x, 0, pos3D.z);
}

function resetCamera() {
  camera.position.set(0, CAMERA_HEIGHT, CAMERA_DISTANCE);
  controls.target.set(0, 0, 0);
  controls.update();
}

function onWindowResize() {
  const container = document.getElementById('canvas-container');
  if (!container || !camera || !renderer) return;

  camera.aspect = container.clientWidth / container.clientHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(container.clientWidth, container.clientHeight);
}

function animate() {
  if (!renderer || !scene || !camera) return;

  requestAnimationFrame(animate);

  controls.update();

  const time = Date.now() * 0.001;

  agvModels.forEach((model, agvId) => {
    const agvData = model.userData.agvData;
    if (agvData && agvData.status === 'charging') {
      model.traverse(child => {
        if (child.isMesh && child.material && child.material.emissive) {
          child.material.emissiveIntensity = 0.5 + Math.sin(time * 3) * 0.3;
        }
      });
    }

    if (agvData && agvData.status === 'error') {
      model.traverse(child => {
        if (child.isMesh && child.material && child.material.emissive) {
          child.material.emissiveIntensity = 0.3 + Math.sin(time * 5) * 0.5;
        }
      });
    }
  });

  renderer.render(scene, camera);
}

function drawMap3D() {
  if (typeof mapData === 'undefined' || !mapData) return;

  drawPoints3D(mapData.points, typeof selectedStationId !== 'undefined' ? selectedStationId : null);
  drawRoutes3D(mapData.routes, mapData.points, typeof selectedRouteId !== 'undefined' ? selectedRouteId : null);
  drawRobots3D(typeof registeredRobots !== 'undefined' ? registeredRobots : [], typeof selectedRobotId !== 'undefined' ? selectedRobotId : null);
}

function getCanvasElement() {
  return renderer ? renderer.domElement : null;
}

function getCamera() {
  return camera;
}

function getScene() {
  return scene;
}

function getControls() {
  return controls;
}

function forceRefreshMap() {
  cachedMapData = null;
  cachedPointsKey = null;
  cachedRoutesKey = null;
  cachedFloorName = null;
  cachedSelectedStationId = null;
  cachedSelectedRouteId = null;
  cachedRobotPositions.clear();
}

window.SimViewer3D = {
  init: initScene,
  drawMap: drawMap3D,
  drawPoints: drawPoints3D,
  drawRoutes: drawRoutes3D,
  drawRobots: drawRobots3D,
  updateAGVPosition,
  updateAGVStatus,
  forceRefreshMap,
  getIntersectedObjects,
  findStationAtPoint,
  findRouteAtPoint,
  findAGVAtPoint,
  focusOnPosition,
  resetCamera,
  getCanvas: getCanvasElement,
  getCamera,
  getScene,
  getControls,
  animate,
  worldTo3D,
  pos3DToWorld,
  clearMapObjects
};

export default window.SimViewer3D;
