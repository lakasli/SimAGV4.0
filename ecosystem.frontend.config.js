const webHost = String(process.env.SIMAGV_WEB_HOST || process.env.SIMAGV_BACKEND_HOST || '0.0.0.0');
const webPort = String(process.env.SIMAGV_WEB_PORT || process.env.SIMAGV_BACKEND_PORT || '7073');
const appCwd = String(process.env.SIMAGV_PM2_APP_CWD || __dirname);
const appScript = String(process.env.SIMAGV_PM2_APP_SCRIPT || process.env.SIMAGV_PYTHON_BIN || '/home/ubuntu/.virtualenvs/main/bin/python3');

module.exports = {
    apps: [
        {
            name: "web-frontend",
            cwd: appCwd,
            script: appScript,
            args: `-m uvicorn backend.main:app --host ${webHost} --port ${webPort} --log-level info`,
            namespace: "web",
            env: { PYTHONUNBUFFERED: "1", SIMAGV_BACKEND_HOST: webHost, SIMAGV_BACKEND_PORT: webPort },
            autorestart: true,
            watch: false,
            max_restarts: 10,
            restart_delay: 2000
        }
    ]
}
