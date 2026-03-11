window.SimViewer = window.SimViewer || {};
window.VDA5050_FILTER_BROKER_IP = '';
window.VDA5050_FILTER_BROKER_PORT = '';

const API_BASE_URL = window.location.origin + '/api';
window.FRONTEND_POLL_INTERVAL_MS = 1000;
window.CENTER_FORWARD_OFFSET_M = 0.1;
window.SIM_TIME_SCALE = 1.0;
window.SIM_SETTINGS_CACHE = null;
window.HOT_SIM_INIT = null;

let perfPollTimer = null;
let CURRENT_MAP_ID = '';

try {
  const q = new URLSearchParams(window.location.search || '');
  let mv = q.get('map');
  if (mv && typeof mv === 'string') {
    mv = mv.replace(/\\/g, '/').replace(/^\/+/, '');
    if (/^(VehicleMap|ViewerMap)\/[^\/]+\.scene$/i.test(mv)) {
      CURRENT_MAP_ID = mv;
    } else if (/^[^\/]+\.scene$/i.test(mv)) {
      CURRENT_MAP_ID = 'ViewerMap/' + mv;
    }
  }
} catch (_) {}

const POINT_TYPE_INFO = { 1: { label: '普通点', color: '#f39c12' }, 2: { label: '等待点', color: '#2980b9' }, 3: { label: '避让点', color: '#8e44ad' }, 4: { label: '临时避让点', color: '#9b59b6' }, 5: { label: '库区点', color: '#16a085' }, 7: { label: '不可避让点', color: '#c0392b' }, 11: { label: '电梯点', color: '#e91e63' }, 12: { label: '自动门点', color: '#00bcd4' }, 13: { label: '充电点', color: '#4caf50' }, 14: { label: '停靠点', color: '#7f8c8d' }, 15: { label: '动作点', color: '#e74c3c' }, 16: { label: '禁行点', color: '#ff3860' } };
const PASS_TYPE_INFO = { 0: { label: '普通路段', color: '#3498db' }, 1: { label: '仅空载可通行', color: '#f1c40f' }, 2: { label: '仅载货可通行', color: '#e67e22' }, 10: { label: '禁行路段', color: '#e74c3c' } };

let mapData = null;
let viewTransform = { x: 0, y: 0, scale: 1 };
let isDragging = false;
let lastMousePos = { x: 0, y: 0 };
let showGrid = true;
let sidebarOpen = false;
window.registeredRobots = [];
let registeredRobots = window.registeredRobots;
let ws = null;
window.selectedRobotId = null;
let selectedRobotId = window.selectedRobotId;
let selectedStationId = null;
let selectedRouteId = null;
let selectedNavStation = null;
let lastRouteCandidates = [];
let lastRouteCandidateIndex = 0;
let mapLayers = [];
let floorNames = [];
let currentFloorIndex = 0;
let palletAnimTimer = null;
