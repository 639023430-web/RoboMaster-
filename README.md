# RoboMaster 算法库

嵌入式 C/C++ 实现的基础数学与运动学算法库，适用于 RoboMaster 机器人竞赛中的姿态解算、运动控制等场景。

移植时可以选择换上相应的数学加速库。

---

## 目录结构

```
RoboMaster/
├── Quaternion/                # 四元数运算库
│   ├── quaternion.h           # C 版本头文件
│   ├── quaternion.c           # C 版本实现
│   ├── quaternion.hpp         # C++ 版本头文件
│   ├── quaternion.cpp         # C++ 版本实现
│   └── README.md              # 四元数库文档
├── Mecanum/                   # 麦轮运动学解算库
│   ├── kinematics.h           # C 版本头文件
│   ├── kinematics.c           # C 版本实现
│   ├── kinematics.hpp         # C++ 版本头文件
│   ├── kinematics.cpp         # C++ 版本实现
│   └── README.md              # 麦轮库文档
├── LICENSE
└── README.md                  # 本文件
```

---

## 模块概览

### 1. Quaternion —— 四元数运算库

三维空间旋转的数学工具，用于姿态表示与解算。

**核心功能：**
- 四元数基本代数（加减、Hamilton 乘积、标量乘）
- 归一化 / 共轭 / 逆
- 欧拉角（ZYX 顺序）↔ 四元数 相互转换
- 四元数 → 旋转矩阵
- 三维向量旋转
- SLERP 球面线性插值（最短路径 + 恒角速度）

**C 语言示例：**
```c
#include "quaternion.h"

Quaternion_t q;
Quaternion_SetIdentity(&q);
Quaternion_FromEulerZYX(&q, 0.0f, 0.5f, 1.0f);
Quaternion_Normalize(&q);
```

**C++ 示例：**
```cpp
#include "quaternion.hpp"

Quaternion q = Quaternion::fromEulerZYX(0, 0.5f, 1.0f);
q.normalize();
float r, p, y;
q.toEulerZYX(r, p, y);
```

> 详细文档见 [Quaternion/README.md](Quaternion/README.md)

### 2. Mecanum —— 麦轮运动学解算库

麦克纳姆轮底盘的正逆运动学解算，实现全向移动控制。

**核心功能：**
- 逆运动学：底盘速度 (vx, vy, ωz) → 四个麦轮角速度
- 正运动学：四个麦轮角速度 → 底盘里程计速度
- 轮速等比例限幅（保持运动方向不变）
- 电机控制回调接口（C 版 weak 函数 / C++ 版 std::function）

**C 语言示例：**
```c
#include "kinematics.h"

chassis_cmd_t   cmd  = {0.5f, 0.0f, 0.2f};  // vx, vy, wz
chassis_state_t state;
backward_kinematics(&cmd, &state);
clamp_wheel_speed(&state, MAX_WHEEL_SPEED);
motor_set_all_speeds(&state);
```

**C++ 示例：**
```cpp
#include "kinematics.hpp"

MecanumKinematics kin(0.05f, 0.125f, 0.125f, 10.0f);
kin.setMotorCallback([](uint8_t id, float spd) {
    can_send(0x200 + id, spd);
});
MecanumKinematics::Command cmd{0.5f, 0.0f, 0.2f};
kin.execute(cmd);  // 一步完成：逆解算 → 限幅 → 下发电机
```

> 详细文档见 [Mecanum/README.md](Mecanum/README.md)

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 0.1 | 2026-05-31 | 麦轮运动学初版（C + C++） |
| 0.1 | 2026-06-01 | 四元数初版（结构体 + 乘法 + 归一化） |
| 1.0 | 2026-06-02 | 四元数完善：16 个函数 + C++ 类封装 + 完整中文 Doxygen 注释 |

## License

Copyright (c) 2026 liqun. All rights reserved.
