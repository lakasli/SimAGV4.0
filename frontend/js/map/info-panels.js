function deriveClassFromName(name) {
  if (!name) return 'Station';
  if (/^CP/i.test(name)) return 'CP';
  if (/^DP/i.test(name)) return 'DP';
  if (/^WP/i.test(name)) return 'WP';
  if (/^Pallet/i.test(name)) return 'Pallet';
  return 'Station';
}

function getStationDetails(pt) {
  const id = String(pt.id || pt.name || '');
  const name = String(pt.name || pt.id || '');
  const typeCode = (pt.type !== undefined) ? Number(pt.type) : NaN;
  const className = (POINT_TYPE_INFO[typeCode]?.label) || '未知类型';
  const dir = (typeof pt.dir === 'number') ? pt.dir : undefined;
  const ignoreDir = false;
  let spin = false;
  const props = Array.isArray(pt.properties) ? pt.properties : (Array.isArray(pt.property) ? pt.property : []);
  for (const prop of props) {
    if (prop && prop.key === 'spin' && prop.boolValue === true) {
      spin = true;
      break;
    }
  }
  const stor = Array.isArray(pt.associatedStorageLocations) ? pt.associatedStorageLocations : [];
  return { id, className, instanceName: name, pos: pt.pos, dir, ignoreDir, spin, associatedStorageLocations: stor };
}

function updateStationInfoPanel(info) {
  const sd = document.getElementById('stationDetails');
  const sh = document.getElementById('stationHeader');
  const mi = document.getElementById('mapInfoSection');
  const rh = document.getElementById('routeHeader');
  const rd = document.getElementById('routeDetails');

  if (!info) {
    document.getElementById('stId').textContent = '-';
    document.getElementById('stType').textContent = '-';
    document.getElementById('stName').textContent = '-';
    document.getElementById('stPos').textContent = '-';
    document.getElementById('stDir').textContent = '-';
    document.getElementById('stSpin').textContent = '-';
    document.getElementById('stStorage').textContent = '-';
    const sr = document.getElementById('stStorageRow');
    if (sr) sr.style.display = 'none';
    const sdr = document.getElementById('stStorageDetailsRow');
    if (sdr) sdr.style.display = 'none';
    const sdt = document.getElementById('stStorageDetails');
    if (sdt) sdt.textContent = '-';
    if (sd) sd.style.display = 'none';
    if (sh) sh.style.display = 'none';
    if (mi) mi.style.display = 'block';
    if (rh) rh.style.display = 'none';
    if (rd) rd.style.display = 'none';
    return;
  }

  document.getElementById('stId').textContent = info.id || '-';
  document.getElementById('stType').textContent = info.className || '-';
  document.getElementById('stName').textContent = info.instanceName || '-';
  const posStr = info.pos ? `(${Number(info.pos.x).toFixed(2)}, ${Number(info.pos.y).toFixed(2)})` : '-';
  document.getElementById('stPos').textContent = posStr;
  const orientationText = (info.ignoreDir === true) ? '任意' : (typeof info.dir === 'number' ? `${Number(info.dir * 180 / Math.PI).toFixed(2)}°` : '-');
  document.getElementById('stDir').textContent = orientationText;
  document.getElementById('stSpin').textContent = (info.spin === true) ? 'true' : 'false';

  const bin = lookupBinLocationInfo(info.instanceName || info.id);
  const storNames = Array.isArray(bin?.locationNames) ? bin.locationNames : [];
  const hasDetails = renderBinTaskDetails(bin);

  const storEl = document.getElementById('stStorage');
  if (storEl) storEl.textContent = storNames.length > 0 ? storNames.join(', ') : '-';

  const sr = document.getElementById('stStorageRow');
  const sdr = document.getElementById('stStorageDetailsRow');
  if (sr) sr.style.display = (storNames.length > 0) ? 'block' : 'none';
  if (sdr) sdr.style.display = hasDetails ? 'block' : 'none';

  if (sd) sd.style.display = 'block';
  if (sh) sh.style.display = 'block';
  if (mi) mi.style.display = 'none';
  if (rh) rh.style.display = 'none';
  if (rd) rd.style.display = 'none';
}

function getRoutePassLabel(passCode) {
  const code = Number(passCode);
  const info = Object.prototype.hasOwnProperty.call(PASS_TYPE_INFO, code) ? PASS_TYPE_INFO[code] : PASS_TYPE_INFO[0];
  return info.label;
}

function getRouteColorByPass(passCode) {
  const code = Number(passCode);
  const info = Object.prototype.hasOwnProperty.call(PASS_TYPE_INFO, code) ? PASS_TYPE_INFO[code] : PASS_TYPE_INFO[0];
  return info.color;
}

function getRouteDetails(route) {
  if (!route) return null;
  const id = String(route.id ?? (`${route.from}->${route.to}`));
  const desc = String(route.desc || route.name || '-');
  const type = String(route.type || '-');
  const pass = (route.pass !== undefined) ? Number(route.pass) : 0;
  return { id, desc, type, passLabel: getRoutePassLabel(pass), passCode: pass };
}

function updateRouteInfoPanel(info) {
  const panel = document.getElementById('routeDetails');
  if (!panel) return;

  const rh = document.getElementById('routeHeader');
  const sd = document.getElementById('stationDetails');
  const sh = document.getElementById('stationHeader');
  const mi = document.getElementById('mapInfoSection');

  if (!info) {
    document.getElementById('rtId').textContent = '-';
    document.getElementById('rtDesc').textContent = '-';
    document.getElementById('rtType').textContent = '-';
    document.getElementById('rtPass').textContent = '-';
    panel.style.display = 'none';
    if (rh) rh.style.display = 'none';
    if (mi) mi.style.display = 'block';
    return;
  }

  document.getElementById('rtId').textContent = info.id || '-';
  document.getElementById('rtDesc').textContent = info.desc || '-';
  document.getElementById('rtType').textContent = info.type || '-';
  document.getElementById('rtPass').textContent = info.passLabel || '-';
  panel.style.display = 'block';
  if (rh) rh.style.display = 'block';
  if (sd) sd.style.display = 'none';
  if (sh) sh.style.display = 'none';
  if (mi) mi.style.display = 'none';
}
