function truncateToOneDecimal(n) {
  const v = Number(n);
  if (!isFinite(v)) return 0;
  return Math.round(v * 10) / 10;
}

function selectRobot(id) {
  selectedRobotId = String(id || '').trim() || null;
  updateRobotList();
  drawMap();
}

function scrollToRobotCard(id) {
  const container = document.getElementById('robotListContainer');
  if (!container) return;
  const robotId = String(id || '').trim();
  if (!robotId) return;
  const card = container.querySelector(`.robot-item[data-robot-id="${CSS.escape(robotId)}"]`);
  if (card) {
    card.scrollIntoView({ behavior: 'smooth', block: 'center' });
  }
}

async function removeRobot(serial) {
  const id = String(serial || '').trim();
  if (!id) return;
  if (!confirm('确定要删除这个机器人吗？')) return;
  try {
    const resp = await fetch(`${API_BASE_URL}/agvs/${encodeURIComponent(id)}`, { method: 'DELETE' });
    if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`);
    await loadRobotList();
    const st = document.getElementById('status');
    if (st) st.textContent = '机器人已删除';
    setTimeout(() => {
      const s2 = document.getElementById('status');
      if (s2 && s2.textContent === '机器人已删除') s2.textContent = '准备就绪';
    }, 2000);
  } catch (err) {
    try { window.printErrorToStatus('删除机器人失败: ' + (err && err.message ? err.message : String(err)), '删除'); } catch (_) {}
  }
}

async function agvPostInstant(serialNumber, payload) {
  const id = String(serialNumber || '').trim();
  if (!id) throw new Error('serialNumber 不能为空');
  const resp = await fetch(`${API_BASE_URL}/agv/${encodeURIComponent(id)}/instant`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`);
  return resp;
}

function buildAgvChangeControlInstant(serialNumber, manufacturer, version, controlMode) {
  const id = String(serialNumber || '').trim();
  const mode = String(controlMode || 'AUTOMATIC').toUpperCase();
  return {
    actions: [{
      actionType: 'changeControl',
      actionId: 'changeControl_' + String(Date.now()),
      actionDescription: 'changeControl',
      blockingType: 'HARD',
      actionParameters: [{ key: 'control', value: mode }]
    }],
    headerId: Date.now(),
    timestamp: new Date().toISOString(),
    version: String(version || '2.0.0'),
    manufacturer: String(manufacturer || ''),
    serialNumber: id
  };
}

async function toggleAgvControl(serialNumber) {
  const id = String(serialNumber || '').trim();
  if (!id) return;
  try {
    const robot = registeredRobots.find(r => r.robot_name === id);
    if (!robot) return;
    const manu = robot.manufacturer || '';
    const ver = robot.vda_version || '2.0.0';
    const cur = String(robot.operatingMode || 'AUTOMATIC').toUpperCase();
    const next = (cur === 'SERVICE') ? 'AUTOMATIC' : 'SERVICE';
    const payload = buildAgvChangeControlInstant(id, manu, ver, next);
    await agvPostInstant(id, payload);
    robot.operatingMode = next;
    updateRobotList();
    try {
      const st = document.getElementById('status');
      if (st) st.textContent = `控制权切换已发送: ${id} -> ${next}`;
    } catch (_) {}
  } catch (err) {
    try { window.printErrorToStatus('控制权切换失败: ' + (err && err.message ? err.message : String(err)), '控制权'); } catch (_) {}
  }
}

async function toggleAgvPm2(serialNumber) {
  const id = String(serialNumber || '').trim();
  if (!id) return;
  const robot = registeredRobots.find(r => r.robot_name === id);
  if (robot && robot._pm2Busy) return;
  const currentStatus = String(robot?.instanceStatus || '停止');
  const shouldStop = currentStatus === '启动';
  const action = shouldStop ? 'stop' : 'start';
  try {
    if (robot) robot._pm2Busy = true;
    try { updateRobotList(); } catch (_) {}
    const resp = await fetch(`${API_BASE_URL}/agvs/${encodeURIComponent(id)}/pm2/${action}`, { method: 'POST' });
    if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`);
    if (robot) {
      robot.instanceStatus = shouldStop ? '停止' : '启动';
      if (shouldStop) {
        robot.commConnected = false;
        robot.lastUpdateTs = 0;
      }
    }
    try {
      const st = document.getElementById('status');
      if (st) st.textContent = `${id} 实例${shouldStop ? '停止' : '启动'}完成`;
      setTimeout(() => {
        const s2 = document.getElementById('status');
        if (s2 && String(s2.textContent || '').includes('完成')) s2.textContent = '准备就绪';
      }, 2000);
    } catch (_) {}
  } catch (err) {
    try { window.printErrorToStatus('实例操作失败: ' + (err && err.message ? err.message : String(err)), 'PM2'); } catch (_) {}
  } finally {
    if (robot) robot._pm2Busy = false;
    try { updateRobotList(); } catch (_) {}
  }
}

async function loadRobotList() {
  try {
    const resp = await fetch(`${API_BASE_URL}/agvs`);
    if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`);
    const agvs = await resp.json();
    const robots = (agvs || []).map(info => ({
      robot_name: info.serial_number,
      type: info.type,
      ip: info.IP,
      manufacturer: info.manufacturer,
      vda_version: info.vda_version,
      battery: 100,
      initialPosition: null,
      currentPosition: null,
      currentMap: null,
      commConnected: false,
      instanceStatus: '停止',
      lastUpdateTs: 0,
      hasPallet: false,
      forkHeight: 0,
      operatingMode: 'AUTOMATIC'
    }));
    registeredRobots.length = 0;
    robots.forEach(r => registeredRobots.push(r));
    if (window.registeredRobots !== registeredRobots) {
      window.registeredRobots = registeredRobots;
    }
    updateRobotList();
    drawMap();
  } catch (err) {
    console.error('加载机器人列表失败:', err);
    const st = document.getElementById('status');
    if (st) st.textContent = '加载机器人列表失败: ' + err.message;
  }
}

let _robotListRafId = null;
let _robotListDataVersion = new Map();

function requestUpdateRobotList() {
  if (_robotListRafId) return;
  _robotListRafId = requestAnimationFrame(() => {
    _robotListRafId = null;
    updateRobotListIncremental();
  });
}

function getRobotItemKey(robot) {
  return String(robot.robot_name || '').trim();
}

function getRobotDataHash(robot) {
  return [
    robot.battery,
    robot.commConnected,
    robot.instanceStatus,
    robot.currentMap,
    robot.forkHeight,
    robot.operatingMode,
    robot.currentPosition?.x,
    robot.currentPosition?.y,
    robot.currentPosition?.theta,
    robot._pm2Busy,
    selectedRobotId === getRobotItemKey(robot) ? '1' : '0'
  ].join('|');
}

function updateRobotItemElement(el, robot) {
  const id = getRobotItemKey(robot);
  const batteryText = truncateToOneDecimal(Number(robot.battery || 0)).toFixed(1);
  const pos = robot.currentPosition || robot.initialPosition;
  const posText = pos ? `当前位置: (${Number(pos.x).toFixed(2)}, ${Number(pos.y).toFixed(2)}) | 方向: ${Number(pos.theta || 0).toFixed(2)}` : '位置未知';
  const commText = robot.commConnected ? '连接' : '断开';
  const commColor = robot.commConnected ? '#00ff00' : '#ff0000';
  const instStatus = robot.instanceStatus || '停止';
  const instColor = instStatus === '启动' ? '#00ff00' : '#ff0000';
  const mapText = robot.currentMap || '未设置';
  const fhText = (typeof robot.forkHeight !== 'undefined' && robot.forkHeight !== null) ? `${truncateToOneDecimal(Number(robot.forkHeight)).toFixed(1)} m` : '-';
  const opMode = String(robot.operatingMode || 'AUTOMATIC').toUpperCase();
  const hasControl = opMode === 'SERVICE';
  const ctrlText = hasControl ? '手动控制' : '调度控制';
  const ctrlColor = hasControl ? '#2ecc71' : '#e74c3c';
  const isSelected = selectedRobotId === id;
  const isInstRunning = instStatus === '启动';
  const isPm2Busy = !!robot._pm2Busy;

  el.style.borderLeft = isSelected ? '4px solid #2ecc71' : '';
  el.style.background = isSelected ? '#3b4f63' : '';
  el.dataset.robotId = id;

  const nameEl = el.querySelector('.robot-name');
  if (nameEl) {
    nameEl.innerHTML = `${escapeHtml(id)}<span style="margin-left:8px;color:${ctrlColor};font-weight:700;">${ctrlText}</span>`;
  }

  const powerBtn = el.querySelector('[data-action="toggle-pm2"]');
  if (powerBtn) {
    powerBtn.className = `btn-power-small ${(isInstRunning ? 'on' : 'off')}${isPm2Busy ? ' busy' : ''}`;
    powerBtn.title = isPm2Busy ? '操作中...' : (isInstRunning ? '关机' : '开机');
    powerBtn.textContent = isPm2Busy ? '...' : '⏻';
    powerBtn.disabled = isPm2Busy;
    powerBtn.dataset.robotId = id;
  }

  const infoEl = el.querySelector('.robot-info');
  if (infoEl) {
    infoEl.innerHTML = `类型: ${escapeHtml(robot.type || '-')} | IP: ${escapeHtml(robot.ip || '-')} | 厂商: ${escapeHtml(robot.manufacturer || '未知')}<br>地图: ${escapeHtml(mapText)}<br>${escapeHtml(posText)} | 叉高: ${escapeHtml(fhText)}<br><span style="color:${commColor}">电量: ${escapeHtml(batteryText)}%</span> | <span style="color:${commColor}">通信状态: ${escapeHtml(commText)}</span> | <span style="color:${instColor}">实例状态: ${escapeHtml(instStatus)}</span>`;
  }

  el.querySelectorAll('[data-robot-id]').forEach(btn => {
    btn.dataset.robotId = id;
  });
}

function createRobotItemElement(robot) {
  const id = getRobotItemKey(robot);
  const batteryText = truncateToOneDecimal(Number(robot.battery || 0)).toFixed(1);
  const pos = robot.currentPosition || robot.initialPosition;
  const posText = pos ? `当前位置: (${Number(pos.x).toFixed(2)}, ${Number(pos.y).toFixed(2)}) | 方向: ${Number(pos.theta || 0).toFixed(2)}` : '位置未知';
  const commText = robot.commConnected ? '连接' : '断开';
  const commColor = robot.commConnected ? '#00ff00' : '#ff0000';
  const instStatus = robot.instanceStatus || '停止';
  const instColor = instStatus === '启动' ? '#00ff00' : '#ff0000';
  const mapText = robot.currentMap ? robot.currentMap : '未设置';
  const fhText = (typeof robot.forkHeight !== 'undefined' && robot.forkHeight !== null) ? `${truncateToOneDecimal(Number(robot.forkHeight)).toFixed(1)} m` : '-';
  const opMode = String(robot.operatingMode || 'AUTOMATIC').toUpperCase();
  const hasControl = opMode === 'SERVICE';
  const ctrlText = hasControl ? '手动控制' : '调度控制';
  const ctrlColor = hasControl ? '#2ecc71' : '#e74c3c';
  const selectedStyle = (selectedRobotId === id) ? 'border-left:4px solid #2ecc71;background:#3b4f63;' : '';
  const isInstRunning = instStatus === '启动';
  const isPm2Busy = !!robot._pm2Busy;
  const powerClass = (isInstRunning ? 'on' : 'off') + (isPm2Busy ? ' busy' : '');
  const powerTitle = isPm2Busy ? '操作中...' : (isInstRunning ? '关机' : '开机');
  const powerLabel = isPm2Busy ? '...' : '⏻';
  
  const div = document.createElement('div');
  div.className = 'robot-item';
  div.style.cssText = selectedStyle;
  div.dataset.robotId = id;
  div.dataset.action = 'select-robot';
  div.innerHTML = `<div class="robot-header">
    <div class="robot-name">${escapeHtml(id)}<span style="margin-left:8px;color:${ctrlColor};font-weight:700;">${ctrlText}</span></div>
    <button class="btn-power-small ${powerClass}" title="${escapeHtml(powerTitle)}" ${isPm2Busy ? 'disabled' : ''} data-action="toggle-pm2" data-robot-id="${escapeHtml(id)}">${escapeHtml(powerLabel)}</button>
  </div>
  <div class="robot-info" style="word-break:break-all;">
    类型: ${escapeHtml(robot.type || '-')} | IP: ${escapeHtml(robot.ip || '-')} | 厂商: ${escapeHtml(robot.manufacturer || '未知')}<br>
    地图: ${escapeHtml(mapText)}<br>
    ${escapeHtml(posText)} | 叉高: ${escapeHtml(fhText)}<br>
    <span style="color:${commColor}">电量: ${escapeHtml(batteryText)}%</span> |
    <span style="color:${commColor}">通信状态: ${escapeHtml(commText)}</span> |
    <span style="color:${instColor}">实例状态: ${escapeHtml(instStatus)}</span>
  </div>
  <div class="robot-actions">
    <button class="btn-small btn-danger" data-action="remove-robot" data-robot-id="${escapeHtml(id)}">删除</button>
    <button class="btn-small btn-config-small" data-action="open-config" data-robot-id="${escapeHtml(id)}">配置</button>
    <button class="btn-small btn-control-small" data-action="toggle-control" data-robot-id="${escapeHtml(id)}">切换控制权</button>
  </div>`;
  return div;
}

function updateRobotListIncremental() {
  const c = document.getElementById('robotListContainer');
  if (!c) return;
  
  if (!Array.isArray(registeredRobots) || registeredRobots.length === 0) {
    c.innerHTML = '<p style="color:#bdc3c7;font-style:italic;">暂无注册的机器人</p>';
    _robotListDataVersion.clear();
    return;
  }

  const currentKeys = new Set(registeredRobots.map(getRobotItemKey));
  const existingItems = c.querySelectorAll('.robot-item');
  const existingMap = new Map();
  
  existingItems.forEach(item => {
    const key = item.dataset.robotId;
    if (key) existingMap.set(key, item);
  });

  existingItems.forEach(item => {
    const key = item.dataset.robotId;
    if (key && !currentKeys.has(key)) {
      item.remove();
      _robotListDataVersion.delete(key);
    }
  });

  let prevEl = null;
  registeredRobots.forEach((robot, index) => {
    const key = getRobotItemKey(robot);
    const newHash = getRobotDataHash(robot);
    const oldHash = _robotListDataVersion.get(key);
    
    let itemEl = existingMap.get(key);
    
    if (!itemEl) {
      itemEl = createRobotItemElement(robot);
      _robotListDataVersion.set(key, newHash);
    } else if (oldHash !== newHash) {
      updateRobotItemElement(itemEl, robot);
      _robotListDataVersion.set(key, newHash);
    }
    
    if (itemEl) {
      if (prevEl) {
        if (itemEl.previousSibling !== prevEl) {
          prevEl.after(itemEl);
        }
      } else {
        if (c.firstChild !== itemEl) {
          c.prepend(itemEl);
        }
      }
      prevEl = itemEl;
    }
  });
}

function updateRobotList() {
  updateRobotListIncremental();
}
