function setupKeyboardControl() {
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' || e.keyCode === 27) {
      if (selectedRobotId !== null) {
        selectedRobotId = null;
        if (typeof updateRobotList === 'function') {
          updateRobotList();
        }
        if (typeof drawMap === 'function') {
          drawMap();
        }
      }
    }
  });
}
