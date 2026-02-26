let renderLoopRafId = null;

function startRenderLoop() {
  if (renderLoopRafId) return;

  let lastDrawTime = 0;
  const minDrawInterval = 100;

  const tick = (timestamp) => {
    try {
      if (window.SimViewer3D && window.SimViewer3D.drawMap) {
        if (timestamp - lastDrawTime >= minDrawInterval) {
          window.SimViewer3D.drawMap();
          lastDrawTime = timestamp;
        }
      } else if (typeof drawMap === 'function') {
        if (timestamp - lastDrawTime >= minDrawInterval) {
          drawMap();
          lastDrawTime = timestamp;
        }
      }
    } catch (_) {}
    renderLoopRafId = requestAnimationFrame(tick);
  };
  renderLoopRafId = requestAnimationFrame(tick);
}

function stopRenderLoop() {
  if (renderLoopRafId) {
    cancelAnimationFrame(renderLoopRafId);
    renderLoopRafId = null;
  }
}

window.startRenderLoop = startRenderLoop;
window.stopRenderLoop = stopRenderLoop;
