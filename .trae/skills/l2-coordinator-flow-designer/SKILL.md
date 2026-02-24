---
name: l2-coordinator-flow-designer
description: 设计并实现L2协调层：把控制流集中到“指挥官流程表/状态机”，并用“外交官”管理跨模块通信、“数据员”管理数据路由与转换；输出可读的流程地图与可Debug的执行轨迹设计。
---

# 目标
让Debug路径从“追调用栈”变成“看流程表定位步骤”，实现Flow与Logic分离。

# 角色划分
- 指挥官：定义步骤、分支、重试、失败处理（工作流/状态机）
- 外交官：封装模块间通信/事件/消息/回调（横切关注点）
- 数据员：负责输入输出数据的装配、转换、路由（横切关注点）

# 工作流
1. 把需求拆成步骤图：Step0..StepN，每步对应一个L3/L4能力调用
2. 为每步定义：
   - preconditions（前置条件）
   - action（调用哪个分子/原子）
   - onSuccess -> nextStep
   - onError -> errorStep/rollbackStep
3. 设计“执行轨迹”：
   - 每步记录：stepId、输入摘要、输出摘要、错误码（不泄露敏感信息）
4. 输出“流程地图”（可读表格或YAML/JSON）

# 流程表模板（示例）
- stepId: validateInput
  action: molecule.validateOrder
  onSuccess: computePrice
  onError: fail
- stepId: computePrice
  action: atom.computePrice
  onSuccess: persist
  onError: fail
- stepId: persist
  action: molecule.saveOrder
  onSuccess: done
  onError: fail

# Debug承诺（输出中要明确）
- 任何运行异常，都能映射到某个stepId
- 定位路径：L1入口 -> L2流程表 -> 具体atom/molecule