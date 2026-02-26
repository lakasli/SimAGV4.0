function initWebSocket() {
  try {
    const url = (location.protocol === 'https:' ? 'wss' : 'ws') + '://' + location.host + '/ws';
    ws = new WebSocket(url);
    ws.onopen = () => {
      const st = document.getElementById('status');
      if (st) st.textContent = 'WS已连接';
      setTimeout(() => {
        const s2 = document.getElementById('status');
        if (s2 && s2.textContent === 'WS已连接') s2.textContent = '准备就绪';
      }, 2000);
    };
    ws.onmessage = (ev) => {
      try {
        const msg = JSON.parse(ev.data);
        if (window.VDA5050_FILTER_BROKER_IP || window.VDA5050_FILTER_BROKER_PORT) {
          const brokerInfo = String(msg.broker || '');
          const parts = brokerInfo.split(':');
          const brokerIp = String(parts[0] || '').trim();
          const brokerPort = String(parts[1] || '').trim();
          const filterIp = String(window.VDA5050_FILTER_BROKER_IP || '').trim();
          const filterPort = String(window.VDA5050_FILTER_BROKER_PORT || '').trim();
          if (filterIp && brokerIp && brokerIp !== filterIp) return;
          if (filterPort && brokerPort && brokerPort !== filterPort) return;
        }
        if (msg && msg.type === 'mqtt_state' && msg.payload) {
          applyWSState(msg.payload);
        }
      } catch (_) {}
    };
    ws.onclose = () => {
      const st = document.getElementById('status');
      if (st) st.textContent = 'WS断开，重连中...';
      try {
        registeredRobots.forEach(r => {
          r.commConnected = false;
        });
        updateRobotList();
      } catch (_) {}
      setTimeout(initWebSocket, 1000);
    };
    ws.onerror = () => {
      try {
        ws.close();
      } catch (_) {}
    };
  } catch (_) {}
}

function applyWSState(stateMsg) {
  try {
    const payload = stateMsg || {};
    const sn = String(payload.serial_number || payload.serialNumber || '').trim();
    if (!sn) return;
    const robot = registeredRobots.find(r => r.robot_name === sn);
    if (!robot) return;

    const agvPos = payload.agv_position || payload.agvPosition || {};
    const batt = payload.battery_state || payload.batteryState || {};
    const opMode = String(payload.operating_mode || payload.operatingMode || '').trim();
    if (opMode) robot.operatingMode = String(opMode).toUpperCase();

    const mapId = (agvPos && (agvPos.map_id || agvPos.mapId)) || null;
    const px = Number(agvPos && agvPos.x);
    const py = Number(agvPos && agvPos.y);
    const th = Number(agvPos && (agvPos.theta ?? agvPos.orientation));
    const bc = Number(batt && (batt.battery_charge || batt.batteryCharge));

    if (isFinite(bc)) robot.battery = truncateToOneDecimal(bc);
    if (mapId) robot.currentMap = String(mapId);
    if (isFinite(px) && isFinite(py)) {
      robot.currentPosition = { x: px, y: py, theta: isFinite(th) ? th : (robot.currentPosition?.theta || 0) };
    }
    robot.instanceStatus = '启动';
    robot.commConnected = true;
    robot.lastUpdateTs = Date.now();
    updateRobotList();

    if (!window._wsDrawMapThrottle) {
      window._wsDrawMapThrottle = { timer: null, pending: false };
    }
    if (!window._wsDrawMapThrottle.pending) {
      window._wsDrawMapThrottle.pending = true;
      window._wsDrawMapThrottle.timer = setTimeout(() => {
        window._wsDrawMapThrottle.pending = false;
        if (window._wsDrawMapThrottle.timer) clearTimeout(window._wsDrawMapThrottle.timer);
        drawMap();
      }, 100);
    }
  } catch (_) {}
}

function startCommStatusGuard() {
  setInterval(() => {
    const now = Date.now();
    let changed = false;
    for (const r of registeredRobots) {
      const ts = Number(r.lastUpdateTs || 0);
      if (r.commConnected && (!ts || (now - ts) > 5000)) {
        r.commConnected = false;
        changed = true;
      }
    }
    if (changed) {
      try { updateRobotList(); } catch (_) {}
    }
  }, 1000);
}
