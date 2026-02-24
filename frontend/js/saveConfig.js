
// Helper: Process AGV physical config updates
function updateAgvPhysicalConfig(robotName, width, length) {
  const w = Number(width);
  const l = Number(length);
  
  const robot = registeredRobots.find(r => r.robot_name === robotName);
  if (robot) {
     if (!robot.collisionParams) robot.collisionParams = {};
     if (w > 0) robot.collisionParams.width = w;
     if (l > 0) robot.collisionParams.length = l;
  }

  const el = document.getElementById('sim-agv-' + robotName);
  if (el && el.setConfig) {
    el.setConfig({ width: w, length: l });
  }
}

async function saveConfig() {
  if (!window.currentConfigRobotId) return;
  const id = window.currentConfigRobotId;
  const robot = registeredRobots.find(r => r.robot_name === id);
  if (!robot) return;

  const newName = document.getElementById('configRobotName').value;
  const newType = document.getElementById('configRobotType').value;
  const newIP = document.getElementById('configRobotIP').value;
  const newManu = document.getElementById('configRobotManufacturer').value;
  const newVer = document.getElementById('configRobotVersion').value;
  const newBat = document.getElementById('configBattery').value;
  const newMap = document.getElementById('configMapSelect').value;
  const newPos = document.getElementById('configInitialPosition').value;
  const newOri = document.getElementById('configOrientation').value;

  // Physical params
  const fSpeedMin = document.getElementById('factsheetSpeedMin').value;
  const fSpeedMax = document.getElementById('factsheetSpeedMax').value;
  const fAccelMax = document.getElementById('factsheetAccelMax').value;
  const fDecelMax = document.getElementById('factsheetDecelMax').value;
  const fHeightMin = document.getElementById('factsheetHeightMin').value;
  const fHeightMax = document.getElementById('factsheetHeightMax').value;
  const fWidth = document.getElementById('factsheetWidth').value;
  const fLength = document.getElementById('factsheetLength').value;
  const fRadarFov = document.getElementById('factsheetRadarFov').value;
  const fRadarRadius = document.getElementById('factsheetRadarRadius').value;
  const fSafetyScale = document.getElementById('factsheetSafetyScale').value;

  // Update local robot object first for immediate feedback
  if (newName) robot.robot_name = newName;
  if (newType) robot.type = newType;
  if (newIP) robot.ip = newIP;
  if (newManu) robot.manufacturer = newManu;
  if (newVer) robot.vda_version = newVer;
  if (newBat) robot.battery = Number(newBat);
  
  // Handle map and position updates
  if (newMap && newMap !== '') {
     // Map changed logic
  }
  
  // Apply physical config to AGV model
  if (fWidth && fLength) {
    updateAgvPhysicalConfig(id, fWidth, fLength);
  }

  try {
    // Prepare payload for backend update
    const payload = {
      serial_number: newName || id,
      type: newType,
      ip: newIP,
      manufacturer: newManu,
      vda_version: newVer,
      battery: Number(newBat),
      factsheet: {
        speedMin: Number(fSpeedMin),
        speedMax: Number(fSpeedMax),
        accelMax: Number(fAccelMax),
        decelMax: Number(fDecelMax),
        heightMin: Number(fHeightMin),
        heightMax: Number(fHeightMax),
        width: Number(fWidth),
        length: Number(fLength),
        radarFov: Number(fRadarFov),
        radarRadius: Number(fRadarRadius),
        safetyScale: Number(fSafetyScale)
      }
    };

    if (newMap) payload.mapId = newMap;
    if (newPos) {
       // Parse position string "x,y"
       const parts = newPos.split(',');
       if (parts.length === 2) {
         payload.initialPosition = { x: Number(parts[0]), y: Number(parts[1]), theta: Number(newOri || 0) };
       }
    } else if (newOri) {
       // Only orientation changed
       if (robot.currentPosition) {
         payload.initialPosition = { ...robot.currentPosition, theta: Number(newOri) };
       }
    }

    const resp = await fetch(`${API_BASE_URL}/agv/${id}/config`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });

    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    
    closeConfigModal();
    loadRobotList(); // Refresh list
    
  } catch (e) {
    alert('保存配置失败: ' + e.message);
  }
}
