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
      name: "equip-elevator",
      cwd: "/home/ubuntu/SimAGV4.0",
      script: "/usr/bin/python3",
      args: "-m SimEQU.devices.elevator.device",
      namespace: "equip",
      env: { PYTHONUNBUFFERED: "1" },
      autorestart: true,
      watch: false,
      max_restarts: 10,
      restart_delay: 2000
    }
  ]
};
