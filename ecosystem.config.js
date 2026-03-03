module.exports = {
  apps: [
    {
      name: "AMB-01",
      cwd: "/home/ubuntu/SimAGV4.0/Simulation_vehicle_example/AMB-01",
      script: "./AMB-01",
      exec_interpreter: "none",
      namespace: "simagv",
      autorestart: true,
      watch: false,
      max_restarts: 10,
      restart_delay: 2000,
      error_file: "/home/ubuntu/SimAGV4.0/Simulation_vehicle_example/AMB-01/error.log",
      out_file: "/home/ubuntu/SimAGV4.0/Simulation_vehicle_example/AMB-01/out.log",
      max_size: "20M"
    },
    {
      name: "equip-CallerSim_01",
      cwd: "/home/ubuntu/SimAGV4.0",
      script: "/home/ubuntu/.virtualenvs/main/bin/python3",
      args: "-m SimEquipment.CallerSim_01",
      namespace: "equip",
      env: { PYTHONUNBUFFERED: "1" },
      autorestart: true,
      watch: false,
      max_restarts: 10,
      restart_delay: 2000
    },
    {
      name: "equip-DoorSim_01",
      cwd: "/home/ubuntu/SimAGV4.0",
      script: "/home/ubuntu/.virtualenvs/main/bin/python3",
      args: "-m SimEquipment.DoorSim_01",
      namespace: "equip",
      env: { PYTHONUNBUFFERED: "1" },
      autorestart: true,
      watch: false,
      max_restarts: 10,
      restart_delay: 2000
    },
    {
      name: "equip-ElevatorSim_01",
      cwd: "/home/ubuntu/SimAGV4.0",
      script: "/home/ubuntu/.virtualenvs/main/bin/python3",
      args: "-m SimEquipment.ElevatorSim_01",
      namespace: "equip",
      env: { PYTHONUNBUFFERED: "1" },
      autorestart: true,
      watch: false,
      max_restarts: 10,
      restart_delay: 2000
    },
    {
      name: "equip-LightSim_01",
      cwd: "/home/ubuntu/SimAGV4.0",
      script: "/home/ubuntu/.virtualenvs/main/bin/python3",
      args: "-m SimEquipment.LightSim_01",
      namespace: "equip",
      env: { PYTHONUNBUFFERED: "1" },
      autorestart: true,
      watch: false,
      max_restarts: 10,
      restart_delay: 2000
    }
  ]
};
