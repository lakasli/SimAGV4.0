# 方案评价
## 优点
- 计算量可控：把多车的“近邻筛选/碰撞判定”集中在一个线程做，可用空间索引把 O(N²) 压到接近 O(N)（均匀分布时）。
- 逻辑解耦：每台车只负责发布自身几何与状态（当前 visualization 已包含 safety/radar），世界模型负责全局交互判定。
- 可观测性好：世界模型能输出“谁被谁挡住/谁与谁碰撞”的判据与证据，便于回放与调参。

## 风险与需要补强的点
- “触发->停机->errors上报”必须落在仿真车进程内：现在 `state.errors` 由车内 `SimErrorManager` 生成；后端仅靠订阅 visualization 无法直接改写 errors，需要新增一条“安全事件注入”控制面。
- 时序一致性：visualization 默认 5Hz（可被 stateHz 与 maxPublishHz 钳制），如果以 visualization 作为唯一输入，可能漏掉高速擦碰；建议世界模型 tick 频率 ≥10Hz，并用时间戳去抖/插值或改用 state(位置)+静态配置(尺寸/雷达)组合。
- 抖动/拉扯：如果“阻挡解除就立刻 startPause”，会与人工 stopPause 冲突；需要“只撤销自己造成的停机”与滞回策略。
- 单点故障：世界模型挂掉，车不再自动避碰；需要 fail-safe 策略（例如：世界模型离线时不改变车辆状态）。

# 具体实施方案（落地到当前代码结构）
## A. 输入与判定（世界模型线程 / 后端）
- **输入源**：订阅 `uagv/+/+/+/visualization`（当前 payload 里包含 state + safety + radar）：
  - safety：center/length/width/theta
  - radar：origin/theta/fovDeg/radius
  - 同时使用 payload 内的 `mapId`/`agvPosition` 做同图过滤
- **空间索引（Broad-phase）**：用“均匀网格/哈希栅格”按安全矩形 AABB 分桶；每车只与同桶及周边桶车辆做精检。
- **精检（Narrow-phase）**：
  - 碰撞：安全矩形(OBB) 与 安全矩形(OBB) SAT 判定。
  - 阻挡：车A雷达扇形 vs 车B安全矩形：用“角度+距离”快速筛选，再做线段-多边形相交/角点在扇形内判定（可先实现近似：角点命中+最近点距离）。
- **策略与滞回**：
  - blocked：连续命中≥200ms 触发；连续清空≥500ms 解除。
  - collision：一旦命中可“锁存”（需显式 reset），或配置为“清空≥1s 自动解除”。默认建议锁存，避免擦边反复启动。

## B. 触发与上报（控制面）
### 关键点：让仿真车进程能被外部“注入安全事件”并在 state.errors 出现
- **新增 instantActions 扩展动作**（不破坏现有 VDA 流）：
  - `actionType = "simSafetyStop"`，参数：
    - `reason`: `"radarBlocked" | "collision"`
    - `active`: `1/0`
    - `peerSerial`: 对方车辆序列号（可选）
  - `actionType = "simSafetyReset"`：清除 collision 锁存（可选）
- **世界模型发布**：通过现有 `MqttPublisher.publish_instant_actions()` 向对应 `.../instantActions` topic 发布上述动作。

## C. 仿真车侧改造（C++）
### 1) 在引擎里接收安全事件并停机
- 文件：`SimAGV/entrance_layer/sim_instance_startup.cpp`
- 变更点：
  - 在 `applyActionLocked()` 增加 `simSafetyStop/simSafetyReset` 分支，解析 `actionParameters`。
  - 增加状态位：`safetyStopBlocked_`、`safetyStopCollision_`、`safetyStopPeer_`、`manualPaused_`（把现有 `paused_` 语义收敛为人工暂停位，另加 `safetyPaused_` 或直接用上述两个 stop 位）。
  - 在 `updateMotionLocked()` 开头把 `paused_` 判断扩展为 `manualPaused_ || safetyStopBlocked_ || safetyStopCollision_`，确保立即速度清零并停止推进。
  - 在 `makeStateObjectLocked()` 将 `paused` 字段反映为 `manualPaused_ || safetyStopBlocked_ || safetyStopCollision_`，并让 `driving` 的判定也受其影响（与现有 `navigationBlocked_` 规则一致）。

### 2) 在 state.errors 中上报
- 文件：`SimAGV/atom_functions/error_management_atoms.cpp/.hpp`
- 变更点：新增错误码定义并由引擎状态位驱动 set/clear：
  - 例如：
    - `54232 Navigation WARNING blocked by other vehicle`
    - `54233 Collision FATAL collision detected`
- 文件：`SimAGV/entrance_layer/sim_instance_startup.cpp`
  - 在 `updateState()` 中：
    - `updateMovementBlocked(navigationBlocked_ || safetyStopBlocked_)`
    - 若 `safetyStopCollision_`：`errorManager_.setErrorByCode(54233)`；否则 `clearErrorByCode(54233)`。

## D. 后端世界模型线程集成（Python）
- 新增模块：`backend/world_model.py`（或类似命名）
  - 维护 `last_visualization_by_serial`、`stop_state_by_serial`、`debounce_timers`
  - 周期 tick 计算碰撞/阻挡并调用 `app.state.mqtt_publisher.publish_instant_actions()`
- 修改启动：`backend/main.py` 的 startup 事件中启动该线程，并在 shutdown 时停止（仿照已有 `config_persist_thread` 的 stop_event 结构）。

# 流程地图（L2 指挥官视角）
- stepId: ingestVisualization
  action: 解析 MQTT topic/payload -> {serial,mapId,safety,radar,timestamp}
  onSuccess: updateWorldState
  onError: drop
- stepId: updateWorldState
  action: 更新车辆快照缓存（按 serial 覆盖）
  onSuccess: detectInteractions
- stepId: detectInteractions
  action: 建栅格索引 + 近邻精检 -> blockedPairs/collisionPairs
  onSuccess: decidePerVehicle
- stepId: decidePerVehicle
  action: 去抖/锁存/同图过滤 -> 每车 {blockedActive, collisionActive, peer}
  onSuccess: dispatchActions
- stepId: dispatchActions
  action: 对状态变化车辆发布 instantActions(simSafetyStop/simSafetyReset)
  onSuccess: done
  onError: retryNextTick

# 验证方案
- 几何单测：Python 下对 OBB-SAT、扇形-矩形判定做 10+ 组 case。
- 集成验证：
  - 人工构造两车 visualization（同 mapId）让 safety 重叠，确认两车收到 simSafetyStop 后速度为 0，state.errors 出现 54233。
  - 构造“雷达命中但不重叠”，确认错误为 WARNING（54232/54231）且解除后自动恢复（仅恢复世界模型触发的停机）。

# 代码影响范围（预期修改/新增文件）
- 修改：`SimAGV/entrance_layer/sim_instance_startup.cpp`
- 修改：`SimAGV/atom_functions/error_management_atoms.cpp`、`error_management_atoms.hpp`
- 新增：`backend/world_model.py`
- 修改：`backend/main.py`（启动/停止 world model 线程）

如果确认该计划，我会按以上步骤直接落地实现，并在最后补上可复现的联调脚本与最小化测试用例。