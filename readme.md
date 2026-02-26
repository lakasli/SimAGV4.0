# SimAGV 3.0

SimAGV 3.0 是一个高性能 AGV（自动导引车）仿真平台，支持 VDA5050 协议。项目采用分层架构设计，结合 C++ 高性能仿真核心与 Python/Web 易用的管理界面，提供完整的 AGV 仿真、调度测试与可视化功能。

## 目录

- [项目架构](#项目架构)
- [环境要求](#环境要求)
- [安装与部署](#安装与部署)
- [快速开始](#快速开始)
- [功能特性](#功能特性)
- [配置说明](#配置说明)
- [开发指南](#开发指南)

## 项目架构

本项目主要由以下三部分组成：

1.  **仿真核心 (SimAGV)**:
    -   基于 C++17 开发，采用 L1-L4 分层架构。
    -   **L1 入口层**: 处理 MQTT 通信、HTTP 请求及配置加载。
    -   **L2 协调层**: 负责实例内部流程协调。
    -   **L3 分子层**: 实现导航、电池管理、状态监控、VDA 协议处理等功能模块。
    -   **L4 原子层**: 提供基础算法与功能单元。
    -   支持 VDA5050 2.0.0 协议。

2.  **管理后台 (Backend)**:
    -   基于 Python FastAPI 开发。
    -   负责 AGV 实例的生命周期管理（注册、启动、停止）。
    -   提供 WebSocket 实时数据推送。
    -   集成 PM2 进行进程管理。

3.  **Web 前端 (Frontend)**:
    -   提供可视化的 AGV 注册、状态监控与地图展示界面。
    -   通过 WebSocket 与后台交互，实时展示 AGV 位姿与状态。

## 环境要求

-   **操作系统**: Linux (推荐 Ubuntu 20.04/22.04)
-   **C++ 环境**: GCC/G++ (需支持 C++17)
-   **Python 环境**: Python 3.10+
-   **Node.js 环境**: Node.js & PM2 (用于进程管理)
-   **消息中间件**: MQTT Broker (推荐 Mosquitto)

## 安装与部署

### 1. 基础依赖安装

```bash
# 安装系统依赖
sudo apt update
sudo apt install -y g++ python3 python3-pip mosquitto nodejs npm

# 安装 PM2
sudo npm install -g pm2
```

### 2. Python 依赖安装

建议使用虚拟环境：

```bash
# 创建并激活虚拟环境 (可选)
python3 -m venv venv
source venv/bin/activate

# 安装依赖 (根据代码推断的依赖列表)
pip install fastapi uvicorn paho-mqtt requests websockets
```

### 3. 编译仿真核心

虽然管理后台会在启动实例时自动尝试编译，但建议首次手动编译以确保环境正常：

```bash
cd SimAGV
./build.sh
```

编译成功后，将在 `SimAGV/build/` 目录下生成 `SimAGV` 可执行文件。

## 快速开始

### 1. 启动管理服务

项目使用 PM2 托管管理服务（包含 Web 前端与后端 API）：
需要先使用which python3查看python3的路径
/home/vwedadmin/miniconda3/bin/python3
修改ecosystem.frontend.config.js中 process.env.SIMAGV_PYTHON_BIN 为 /home/vwedadmin/miniconda3/bin/python3

```bash
# 在项目根目录下执行
pm2 start ecosystem.frontend.config.js
```

### 2. 访问 Web 界面

服务启动后，默认监听 `7073` 端口。请在浏览器中访问：

http://localhost:7073

### 3. 注册与运行 AGV

1.  在 Web 界面点击“注册 AGV”。
2.  输入 AGV 序列号（例如 `AMB-02`）并确认。
3.  后台将自动为新 AGV 创建实例目录（`Simulation_vehicle_example/<序列号>`），生成配置文件，并启动仿真进程。
4.  在列表中可以看到 AGV 状态变为 Online。

## 功能特性

-   **VDA5050 协议支持**: 完整支持 VDA5050 2.0.0 标准，包括 Order 下发、Instant Actions、State 上报及 Visualization 数据。
-   **多实例仿真**: 支持同时运行多个独立的 AGV 仿真实例，互不干扰。
-   **热加载配置**: 支持 `hot_config.yaml` 机制，可在运行时动态调整部分仿真参数。
-   **地图支持**: 兼容 `.pgm` 栅格地图与 `topo.json` 拓扑地图。
-   **故障模拟**: 内置错误管理器，可模拟各类 AGV 故障与恢复场景。

## 配置说明

### 仿真配置文件 (`config.yaml`)

每个 AGV 实例目录下均有独立的 `config.yaml`，主要配置项包括：

-   **mqtt_broker**: MQTT 连接信息。
-   **vehicle**: 车辆基础信息（厂商、序列号、协议版本）。
-   **sim_config**: 仿真参数（时间倍率、上报频率、地图 ID 等）。
-   **initial_pose**: 初始位姿 (x, y, theta)。
-   **battery**: 电池充放电模型参数。

### 热加载配置 (`hot_config.yaml`)

在实例运行时，修改实例目录下的 `hot_config.yaml` 可即时生效（无需重启），支持调整仿真速度、发布频率等。

### 前端配置

#### 限制 MQTT 消息来源 IP

默认情况下，前端界面会接收并显示后端转发的所有 MQTT 消息。如果需要仅显示来自特定 MQTT Broker 的消息（例如在多 Broker 环境下），可以在前端代码中进行配置。

修改文件：`frontend/js/index.js`

```javascript
// 配置：指定只接收来自特定IP地址MQTT Broker的消息（例如 '192.168.1.100'）
// 默认为 null，表示接收所有来源
window.VDA5050_FILTER_BROKER_IP = '192.168.1.100'; 
```

修改后刷新浏览器页面即可生效。

## 开发指南

### 目录结构

```text
SimAGV3.0/
├── SimAGV/                     # C++ 仿真核心源码
│   ├── atom_functions/         # L4 原子层
│   ├── molecular_functions/    # L3 分子层
│   ├── coordination_layer/     # L2 协调层
│   ├── entrance_layer/         # L1 入口层
│   └── build.sh                # 编译脚本
├── backend/                    # Python 管理后台源码
│   ├── main.py                 # FastAPI 入口
│   └── agv_manager.py          # AGV 管理逻辑
├── frontend/                   # Web 前端资源
├── maps/                       # 地图文件 (VehicleMap/ViewerMap)
├── Simulation_vehicle_example/ # AGV 运行实例目录
├── ecosystem.config.js         # AGV 实例 PM2 配置文件
└── ecosystem.frontend.config.js# 前端/后台 PM2 配置文件
```

### API 文档

启动服务后，可访问 `http://localhost:7073/docs` 查看完整的 Swagger API 文档。

前端frontend_3D启动命令
cd frontend_3D 
npm run dev -- --host 0.0.0.0