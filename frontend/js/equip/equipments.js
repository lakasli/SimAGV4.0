let equipments = [];

async function loadEquipmentList() {
  try {
    const resp = await fetch(`${API_BASE_URL}/equipments`);
    if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${resp.statusText}`);
    equipments = await resp.json();
    updateEquipmentList();
  } catch (err) {
    console.error('加载设备列表失败:', err);
    const el = document.getElementById('status');
    if (el) el.textContent = '加载设备列表失败: ' + err.message;
  }
}

function updateEquipmentList() {
  const c = document.getElementById('equipListContainer');
  if (!c) return;
  const list = Array.isArray(equipments) ? equipments : [];
  if (list.length === 0) {
    c.innerHTML = '<p style="color:#bdc3c7;font-style:italic;">暂无设备</p>';
    return;
  }
  c.innerHTML = list.map(eq => {
    const title = String(eq.serial_number || eq.dir_name || '设备');
    const type = String(eq.type || '-');
    const site = eq.site || [];
    const mapId = eq.map_id || '-';
    const siteStr = Array.isArray(site) && site.length > 0 
      ? site.map(s => `(${(s.x || 0).toFixed(0)},${(s.y || 0).toFixed(0)})`).join(', ')
      : '-';
    const floorStr = Array.isArray(site) && site.length > 0
      ? site.map(s => s.floor_name || '-').join(', ')
      : '-';
    return `<div class="robot-item">
      <div class="robot-header"><div class="robot-name">${escapeHtml(title)}</div></div>
      <div class="robot-info">类型: ${escapeHtml(type)} | 位姿: ${escapeHtml(siteStr)} | 地图: ${escapeHtml(mapId)} | 楼层: ${escapeHtml(floorStr)}</div>
    </div>`;
  }).join('');
}

function updateDoorOverlayPositions() {}
