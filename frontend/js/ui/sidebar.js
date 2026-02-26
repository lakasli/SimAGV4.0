function toggleSidebar() {
  const s = document.getElementById('sidebar');
  const t = document.getElementById('sidebarToggle');
  const c = document.getElementById('canvasContainer');
  if (!s || !t || !c) return;

  sidebarOpen = !sidebarOpen;
  if (sidebarOpen) {
    s.classList.add('open');
    c.classList.add('sidebar-open');
    t.classList.add('open');
    t.textContent = '收起';
  } else {
    s.classList.remove('open');
    c.classList.remove('sidebar-open');
    t.classList.remove('open');
    t.textContent = '打开';
  }
  setTimeout(() => { if (typeof window.resizeCanvas === 'function') window.resizeCanvas(); }, 400);
}

function switchTab(name) {
  const tabName = String(name || '').trim() || 'list';
  document.querySelectorAll('.tab-button').forEach(b => b.classList.remove('active'));
  document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));

  const listBtn = document.querySelector('.tab-button[data-tab="list"]') || document.querySelector('.tab-button');
  const equipBtn = document.querySelector('.tab-button[data-tab="equip"]');

  if (tabName === 'equip') {
    if (equipBtn) equipBtn.classList.add('active');
    const tab = document.getElementById('equipTab');
    if (tab) tab.classList.add('active');
    try { loadEquipmentList(); } catch (_) {}
  } else {
    if (listBtn) listBtn.classList.add('active');
    const tab = document.getElementById('listTab');
    if (tab) tab.classList.add('active');
  }
}
