# 2.5D AGV调度展示界面 - 技术栈规划

## 项目概述

构建一个2.5D风格的AGV（自动导引车）调度展示界面，支持：
- 多AGV同时运动渲染
- 多角度视角切换
- 实时调度状态展示

---

## 推荐技术栈

### Three.js + React（推荐）

**优势：**
- Three.js 是最成熟的 WebGL 3D 库，社区庞大
- 2.5D 场景可使用 OrthographicCamera（正交相机）实现
- React Three Fiber (@react-three/fiber) 提供声明式 3D 开发体验
- 性能优异，支持大规模物体渲染

**技术组合：**
```
前端框架：React 18 + TypeScript
3D渲染：  Three.js + React Three Fiber (@react-three/fiber)
状态管理：Zustand（轻量级，适合实时状态）
UI组件：  Tailwind CSS + shadcn/ui
地图数据：支持 SVG/JSON 导入仓库布局
```

**核心实现要点：**
1. **2.5D视角**：使用 `OrthographicCamera` 设置等距视角（Isometric）
2. **多AGV渲染**：使用 `InstancedMesh` 优化大量相同模型渲染
3. **平滑运动**：使用 `useFrame` hook 或 `requestAnimationFrame` 更新位置
4. **多角度切换**：预设多个相机位置，使用 `GSAP` 或 `TWEEN` 实现平滑过渡

---

## 推荐方案详细架构（Three.js + React）

### 项目结构
```
agv-dispatch-system/
├── src/
│   ├── components/
│   │   ├── Scene/           # 3D场景组件
│   │   │   ├── AGV.tsx      # AGV模型
│   │   │   ├── Warehouse.tsx # 仓库地面/路径
│   │   │   └── CameraController.tsx # 相机控制
│   │   ├── UI/              # 2D UI覆盖层
│   │   │   ├── Sidebar.tsx  # 侧边栏控制面板
│   │   │   ├── AGVList.tsx  # AGV列表
│   │   │   └── StatusPanel.tsx # 状态面板
│   │   └── Dashboard.tsx    # 主界面布局
│   ├── hooks/
│   │   ├── useAGVData.ts    # AGV数据订阅
│   │   └── useCameraViews.ts # 多视角管理
│   ├── stores/
│   │   └── agvStore.ts      # Zustand状态管理
│   ├── types/
│   │   └── agv.ts           # TypeScript类型定义
│   └── utils/
│       └── pathUtils.ts     # 路径计算工具
├── public/
│   └── models/              # 3D模型文件
└── package.json
```

### 核心依赖
```json
{
  "dependencies": {
    "react": "^18.2.0",
    "three": "^0.160.0",
    "@react-three/fiber": "^8.15.0",
    "@react-three/drei": "^9.92.0",
    "zustand": "^4.4.0",
    "tailwindcss": "^3.4.0",
    "gsap": "^3.12.0"
  },
  "devDependencies": {
    "typescript": "^5.3.0",
    "@types/three": "^0.160.0",
    "vite": "^5.0.0"
  }
}
```

### 2.5D视角实现

```typescript
// 使用正交相机实现2.5D等距视角
import { OrthographicCamera } from '@react-three/drei'

function IsometricCamera() {
  return (
    <OrthographicCamera
      makeDefault
      position={[10, 10, 10]}
      zoom={50}
      near={0.1}
      far={1000}
    />
  )
}
```

### 多AGV渲染优化

```typescript
// 使用 InstancedMesh 优化多AGV渲染
import { useRef, useMemo } from 'react'
import { useFrame } from '@react-three/fiber'
import * as THREE from 'three'

function AGVFleet({ agvData }) {
  const meshRef = useRef()
  const count = agvData.length
  
  useFrame(() => {
    if (!meshRef.current) return
    agvData.forEach((agv, i) => {
      const matrix = new THREE.Matrix4()
      matrix.setPosition(agv.x, 0, agv.z)
      meshRef.current.setMatrixAt(i, matrix)
    })
    meshRef.current.instanceMatrix.needsUpdate = true
  })
  
  return (
    <instancedMesh ref={meshRef} args={[null, null, count]}>
      <boxGeometry args={[1, 0.5, 1]} />
      <meshStandardMaterial color="#F97316" />
    </instancedMesh>
  )
}
```

---

## 设计系统

### 配色方案
| 角色 | 颜色 | 用途 |
|------|------|------|
| Primary | #64748B | 工业灰，主要UI元素 |
| Secondary | #94A3B8 | 次要元素、边框 |
| CTA/Accent | #F97316 | 安全橙，AGV高亮、警告 |
| Background | #F8FAFC | 浅灰背景 |
| Text | #334155 | 主要文字 |
| Success | #22C55E | 正常状态 |
| Warning | #EAB308 | 警告状态 |
| Error | #EF4444 | 故障状态 |

### 字体
- **标题/数据**: Fira Code（等宽字体，适合数据展示）
- **正文**: Fira Sans

### 视觉风格
- **风格**: Data-Dense Dashboard
- **特点**: 多图表/组件、数据表格、KPI卡片、最小边距、网格布局
- **效果**: 悬停提示、点击缩放、行高亮、平滑过滤动画

---

## 功能模块规划

### 1. 3D场景模块
- [ ] 仓库地面网格渲染
- [ ] 路径/轨道可视化
- [ ] AGV模型（可使用简单几何体或GLTF模型）
- [ ] 多视角预设（俯视、等距、跟随AGV）
- [ ] 相机平滑过渡动画

### 2. AGV调度模块
- [ ] AGV实时位置更新
- [ ] 运动轨迹预测/显示
- [ ] 状态指示（运行/空闲/故障）
- [ ] 任务路径可视化

### 3. UI控制面板
- [ ] AGV列表与状态
- [ ] 视角切换按钮
- [ ] 搜索/筛选功能
- [ ] 实时统计面板

### 4. 数据通信
- [ ] WebSocket 实时数据订阅
- [ ] REST API 历史数据查询
- [ ] 模拟数据生成（开发阶段）

---

## 性能优化建议

1. **实例化渲染**：使用 `InstancedMesh` 渲染多个相同AGV模型
2. **LOD（细节层次）**：远距离使用简化模型
3. **视锥剔除**：Three.js 自动处理，确保相机 far 值合理
4. **状态更新节流**：高频数据使用 `requestAnimationFrame` 批量更新
5. **React优化**：使用 `useMemo`、`useCallback` 避免不必要重渲染

---

## 开发步骤建议

1. **第一阶段**：搭建基础框架，实现单AGV渲染与运动
2. **第二阶段**：添加多AGV支持，实现实例化渲染
3. **第三阶段**：实现多视角切换与相机动画
4. **第四阶段**：集成实时数据，完善UI面板
5. **第五阶段**：性能优化与细节打磨

---

## 总结

**推荐使用 Three.js + React + React Three Fiber 方案**，原因：
1. 2.5D/3D 渲染能力强大且灵活
2. 社区成熟，文档丰富
3. React 生态集成良好
4. 性能优化手段完善
5. 可扩展性强，未来可升级为完整3D系统
