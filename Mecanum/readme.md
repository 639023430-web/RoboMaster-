# 麦轮运动学解算库 (Mecanum Wheel Kinematics)

纯 C 实现的麦克纳姆轮运动学解算库，同时提供 C++ 类封装，适用于 RoboMaster 机器人底盘的全向运动控制。

## 文件结构

```
Mecanum/
├── kinematics.h      C 头文件（类型定义 + 函数声明）
├── kinematics.c      C 实现文件
├── kinematics.hpp    C++ 头文件（MecanumKinematics 类）
├── kinematics.cpp    C++ 实现文件
└── README.md         本文件
```

---

## 1. 麦轮布局

俯视图，X 前进，Y 左移：

```
       Y+
       ^
       |
  FL(0)●────────●FR(1)
       |  车体   |
  BL(2)●────────●BR(3)  --> X+
```

轮子辊子方向（从上方看）：

| 轮子 | 编号 | 辊子轴向 | 效果（正转时） |
|------|------|----------|----------------|
| FL 前左 | 0 | `\` | 产生 +Vy 分量 |
| FR 前右 | 1 | `/` | 产生 -Vy 分量 |
| BL 后左 | 2 | `/` | 产生 -Vy 分量 |
| BR 后右 | 3 | `\` | 产生 +Vy 分量 |

## 2. 机械参数

| 参数 | 宏/变量 | 默认值 | 单位 | 说明 |
|------|---------|--------|------|------|
| 轮子半径 | `WHEEL_RADIUS` | 0.05 | m | |
| X 方向半轴距 | `WHEEL_BASE_X` | 0.125 | m | 中心到轮子 X 向距离 |
| Y 方向半轴距 | `WHEEL_BASE_Y` | 0.125 | m | 中心到轮子 Y 向距离 |
| 组合轴距 | `COMBINED_BASE` | 0.25 | m | L = Lx + Ly |
| 最大轮速 | `MAX_WHEEL_SPEED` | 10.0 | rad/s | 限幅保护 |

## 3. 逆运动学（backward kinematics）

> 输入底盘速度 (vx, vy, ωz) → 输出四个麦轮角速度 (ω0, ω1, ω2, ω3)

```
ω0 = (vx - vy - ωz·L) / R    // 前左 FL
ω1 = (vx + vy + ωz·L) / R    // 前右 FR
ω2 = (vx + vy - ωz·L) / R    // 后左 BL
ω3 = (vx - vy + ωz·L) / R    // 后右 BR
```

其中 **L = Lx + Ly**，**R** 为轮子半径。

## 4. 正运动学（forward kinematics）

> 输入四个麦轮角速度 (s0, s1, s2, s3) → 输出底盘速度 (vx, vy, ωz)

逆解算方程组：

```
s0 = (vx - vy - ωz·L) / R
s1 = (vx + vy + ωz·L) / R
s2 = (vx + vy - ωz·L) / R
s3 = (vx - vy + ωz·L) / R
```

求解 vx, vy, ωz：

```
vx =  R/4  · ( s0 + s1 + s2 + s3)
vy =  R/4  · (-s0 + s1 + s2 - s3)
ωz = R/4L · (-s0 + s1 - s2 + s3)
```

## 5. 轮速限幅

当任意轮速超过 `max_speed` 时，等比例缩放全部四个轮速，保证运动方向不变。

```
scale = max_speed / max(|ω0|, |ω1|, |ω2|, |ω3|)
ωi' = ωi × scale
```

## 6. 电机控制接口

### C 版本

`motor_set_speed()` 声明为 `__attribute__((weak))`，用户在自己的 `.c` 文件中定义同名函数即可覆盖。

```c
// 用户的 motor.c
#include "kinematics.h"

void motor_set_speed(uint8_t motor_id, float speed)
{
    switch (motor_id) {
    case 0: can_send(CAN1, 0x201, speed_to_current(speed)); break;
    case 1: can_send(CAN1, 0x202, speed_to_current(speed)); break;
    case 2: can_send(CAN1, 0x203, speed_to_current(speed)); break;
    case 3: can_send(CAN1, 0x204, speed_to_current(speed)); break;
    }
}
```

### C++ 版本

通过 `setMotorCallback()` 注册回调，解耦解算与驱动：

```cpp
MecanumKinematics kin(0.05f, 0.125f, 0.125f, 10.0f);

kin.setMotorCallback([](uint8_t id, float spd) {
    uint16_t can_id = 0x200 + id;
    float current = spd * 0.1f;  // 转速 -> 电流
    can_send(can_id, &current, sizeof(current));
});

MecanumKinematics::Command cmd{0.5f, 0.0f, 0.2f};
kin.execute(cmd);  // 一步完成：逆解算 -> 限幅 -> 下发电机
```

## 7. API 速查

### C API (`kinematics.h`)

| 函数 | 说明 |
|------|------|
| `backward_kinematics(cmd, state)` | 逆解算：底盘速度 → 轮速 |
| `forward_kinematics(state, odom)` | 正解算：轮速 → 底盘速度 |
| `clamp_wheel_speed(state, max)` | 轮速等比例限幅 |
| `motor_set_speed(id, speed)` | 单电机控速（weak，用户覆盖） |
| `motor_set_all_speeds(state)` | 批量下发四轮转速 |
| `chassis_control()` | 演示流程 |

### C++ API (`kinematics.hpp`)

| 方法 | 说明 |
|------|------|
| `backward(cmd)` / `backward(cmd, out)` | 逆解算 |
| `forward(state)` / `forward(state, out)` | 正解算 |
| `clamp(state)` | 轮速限幅，返回是否发生裁剪 |
| `setMotorCallback(cb)` | 注册电机控制回调 |
| `setMotorSpeed(id, speed)` | 单电机控速 |
| `setAllMotorSpeeds(state)` | 批量下发电机 |
| `execute(cmd)` | 一步到位：逆解算→限幅→下发 |

## 8. 文件结构

```
├── kinematics.h      C 头文件（类型定义 + 函数声明，兼容 C++）
├── kinematics.c      C 实现
├── kinematics.hpp    C++ 头文件（MecanumKinematics 类）
├── kinematics.cpp    C++ 实现
└── readme.md         本文件
```
