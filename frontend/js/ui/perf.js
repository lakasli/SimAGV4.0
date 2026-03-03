function formatBytes(bytes) {
  const n = Number(bytes);
  if (!isFinite(n) || n < 0) return '-';
  if (n === 0) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let v = n;
  let idx = 0;
  while (v >= 1024 && idx < units.length - 1) {
    v /= 1024;
    idx++;
  }
  const digits = (idx <= 1) ? 0 : 1;
  return `${v.toFixed(digits)} ${units[idx]}`;
}

function formatCpuPercent(v) {
  const n = Number(v);
  if (!isFinite(n)) return '-';
  return `${n.toFixed(1)}%`;
}

function formatPercent(v) {
  const n = Number(v);
  if (!isFinite(n)) return '-';
  return `${n.toFixed(1)}%`;
}

function escapeHtml(s) {
  return String(s ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function safePm2Name(raw) {
  return String(raw ?? '').replace(/[^A-Za-z0-9_-]/g, '-');
}

function applyAgvProcessStatusToRobots(processes) {
  try {
    if (!Array.isArray(processes)) return;
    if (!Array.isArray(registeredRobots) || registeredRobots.length === 0) return;
    const names = new Set(processes.map(p => String(p && p.name ? p.name : '').trim()).filter(Boolean));
    let changed = false;
    for (const r of registeredRobots) {
      if (!r || r._pm2Busy) continue;
      const sn = String(r.robot_name || '').trim();
      if (!sn) continue;
      const safe = safePm2Name(sn);
      const running = names.has(safe) || names.has('agv-' + safe);
      const next = running ? '启动' : '停止';
      if (r.instanceStatus !== next) {
        r.instanceStatus = next;
        if (!running) {
          r.commConnected = false;
          r.lastUpdateTs = 0;
        }
        changed = true;
      }
    }
    if (changed && typeof requestUpdateRobotList === 'function') requestUpdateRobotList();
  } catch (_) {}
}

function renderPerfProcessList(containerId, processes) {
  const el = document.getElementById(containerId);
  if (!el) return;
  if (!Array.isArray(processes) || processes.length === 0) {
    el.innerHTML = '<div class="perf-table-empty">-</div>';
    return;
  }
  const rows = processes
    .slice()
    .sort((a, b) => Number(b && b.cpu_percent) - Number(a && a.cpu_percent))
    .map(p => {
      const name = escapeHtml(p && p.name ? p.name : '-');
      const pid = Number(p && p.pid) || 0;
      const cpu = formatCpuPercent(p && p.cpu_percent);
      const memPct = formatPercent(p && p.memory_percent);
      const mem = formatBytes(p && p.memory_bytes);
      const title = pid ? `${name} (pid ${pid})` : name;
      return `<div class="perf-table-row"><div>${title}</div><div class="perf-table-cell-muted">${cpu}</div><div class="perf-table-cell-muted">${memPct}</div><div class="perf-table-cell-muted">${mem}</div></div>`;
    })
    .join('');

  el.innerHTML = `<div class="perf-table-row perf-table-header"><div>实例</div><div>CPU</div><div>内存%</div><div>内存</div></div>${rows}`;
}

async function refreshPerfPanel() {
  try {
    const resp = await fetch(`${API_BASE_URL}/perf/summary`);
    if (!resp.ok) return;
    const data = await resp.json();
    const agv = data && data.simagv ? data.simagv : {};
    const equip = data && data.equip ? data.equip : {};

    const agvCpuEl = document.getElementById('perfAgvCpu');
    const agvMemEl = document.getElementById('perfAgvMem');
    const equipCpuEl = document.getElementById('perfEquipCpu');
    const equipMemEl = document.getElementById('perfEquipMem');

    if (agvCpuEl) agvCpuEl.textContent = formatCpuPercent(agv.cpu_percent);
    if (agvMemEl) agvMemEl.textContent = formatBytes(agv.memory_bytes);
    if (equipCpuEl) equipCpuEl.textContent = formatCpuPercent(equip.cpu_percent);
    if (equipMemEl) equipMemEl.textContent = formatBytes(equip.memory_bytes);

    renderPerfProcessList('perfAgvList', agv && agv.processes ? agv.processes : []);
    renderPerfProcessList('perfEquipList', equip && equip.processes ? equip.processes : []);
    applyAgvProcessStatusToRobots(agv && agv.processes ? agv.processes : []);
  } catch (_) {}
}

function startPerfMonitor() {
  if (perfPollTimer) {
    try {
      clearInterval(perfPollTimer);
    } catch (_) {}
    perfPollTimer = null;
  }
  refreshPerfPanel();
  perfPollTimer = setInterval(refreshPerfPanel, 1000);
}

function openPerfModal() {
  const modal = document.getElementById('perfModal');
  if (!modal) return;
  modal.style.display = 'flex';
  requestAnimationFrame(() => {
    const content = modal.querySelector('.modal-content');
    if (content && !content.dataset.fixedHeight) {
      const rect = content.getBoundingClientRect();
      content.style.height = rect.height + 'px';
      content.style.maxHeight = rect.height + 'px';
      content.dataset.fixedHeight = '1';
    }
  });
  try {
    refreshPerfPanel();
  } catch (_) {}
}

function closePerfModal() {
  const modal = document.getElementById('perfModal');
  if (!modal) return;
  modal.style.display = 'none';
}
