function loadPallet(id) {
  const robot = registeredRobots.find(r => r.robot_name === id);
  if (!robot) return;
  robot.hasPallet = true;
  drawMap();
}

function unloadPallet(id) {
  const robot = registeredRobots.find(r => r.robot_name === id);
  if (!robot) return;
  robot.hasPallet = false;
  drawMap();
}
