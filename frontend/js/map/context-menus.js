function showRobotContextMenu(sx, sy, robot) {
  const menu = document.getElementById('robotContextMenu');
  if (!menu) return;
  const hasPallet = !!robot.hasPallet;
  menu.innerHTML = '';
  const item = document.createElement('div');
  item.style.padding = '8px 12px';
  item.style.cursor = 'pointer';
  item.textContent = hasPallet ? '卸载托盘' : '加载托盘';
  item.onclick = function () {
    if (hasPallet) unloadPallet(robot.robot_name);
    else loadPallet(robot.robot_name);
    hideRobotContextMenu();
  };
  menu.appendChild(item);
  menu.style.left = sx + 'px';
  menu.style.top = sy + 'px';
  menu.style.display = 'block';
}

function hideRobotContextMenu() {
  const menu = document.getElementById('robotContextMenu');
  if (menu) menu.style.display = 'none';
}

function hideStationContextMenu() {
  const menu = document.getElementById('stationContextMenu');
  if (menu) menu.style.display = 'none';
}

function isWorkStationPoint(point) {
  const nm = String(point?.name || point?.id || '').trim();
  return /^AP/i.test(nm);
}

function isChargingStationPoint(point) {
  const nm = String(point?.name || point?.id || '').trim();
  const typeCode = (point && typeof point.type !== 'undefined') ? Number(point.type) : NaN;
  if (typeCode === 13) return true;
  return /^CP/i.test(nm);
}

function extractBinTaskOptionsForPoint(point) {
  try {
    const key = String(point?.name || point?.id || '').trim();
    if (!key) return [];
    const entry = lookupBinLocationInfo(key);
    const objs = Array.isArray(entry?.binTaskObjects) ? entry.binTaskObjects : [];
    const options = [];
    for (const obj of objs) {
      const list = Array.isArray(obj) ? obj : [obj];
      for (const item of list) {
        if (item && typeof item === 'object' && Object.keys(item).length > 0) {
          const actionName = Object.keys(item)[0];
          const params = item[actionName];
          options.push({ title: String(actionName), params: (params && typeof params === 'object') ? params : {} });
        }
      }
    }
    if (options.length === 0) {
      const raws = Array.isArray(entry?.binTaskStrings) ? entry.binTaskStrings : [];
      for (const s of raws) {
        let title = String(s).trim();
        title = title.replace(/\s+/g, ' ').slice(0, 50) || 'RawAction';
        options.push({ title, params: { content: s } });
      }
    }
    return options;
  } catch (_) {
    return [];
  }
}

function showStationContextMenu(sx, sy, point) {
  const menu = document.getElementById('stationContextMenu');
  if (!menu) return;
  menu.innerHTML = '';

  const addItem = (title, enabled, onClick) => {
    const it = document.createElement('div');
    it.style.padding = '8px 12px';
    it.style.cursor = enabled ? 'pointer' : 'default';
    it.style.opacity = enabled ? '1' : '0.6';
    it.style.pointerEvents = enabled ? 'auto' : 'none';
    it.textContent = title;
    if (enabled && typeof onClick === 'function') {
      it.onclick = onClick;
    }
    menu.appendChild(it);
  };

  const canControl = !!selectedRobotId;
  if (!canControl) {
    const tip = document.createElement('div');
    tip.style.padding = '6px 12px';
    tip.style.fontSize = '12px';
    tip.style.color = '#bdc3c7';
    tip.textContent = '请先在列表中选中机器人';
    menu.appendChild(tip);
  }

  addItem('导航至该站点', canControl, async (ev) => {
    if (ev && ev.stopPropagation) ev.stopPropagation();
    try {
      await navigateRobotToStation(point);
    } catch (err) {
      try { window.printErrorToStatus(err, '导航失败'); } catch (_) {}
    } finally {
      hideStationContextMenu();
    }
  });

  if (isChargingStationPoint(point)) {
    addItem('执行充电任务', canControl, async (ev) => {
      if (ev && ev.stopPropagation) ev.stopPropagation();
      try {
        await navigateRobotToStationWithAction(point, 'StartCharging', { source: 'CPMenu' });
      } catch (err) {
        try { window.printErrorToStatus(err, '充电任务发布失败'); } catch (_) {}
      } finally {
        hideStationContextMenu();
      }
    });
  }

  const options = extractBinTaskOptionsForPoint(point);
  if (isWorkStationPoint(point) && options.length > 0) {
    for (const opt of options.slice(0, 10)) {
      const title = '执行动作: ' + String(opt.title || '').slice(0, 30);
      addItem(title, canControl, async (ev) => {
        if (ev && ev.stopPropagation) ev.stopPropagation();
        try {
          await navigateRobotToStationWithAction(point, opt.title, opt.params);
        } catch (err) {
          try { window.printErrorToStatus(err, '动作发布失败'); } catch (_) {}
        } finally {
          hideStationContextMenu();
        }
      });
    }
  }

  menu.style.left = sx + 'px';
  menu.style.top = sy + 'px';
  menu.style.display = 'block';
}
