# 底盘运动控制库 (Chassis Control) 🏎️

C++ 面向对象实现的通用底盘运动学库，支持 **麦克纳姆轮（Mecanum）**、**三轮全向轮（Omni-3）**、**四轮全向轮（Omni-4）** 和 **舵轮（Swerve Drive）** 四种常见底盘构型。

> 不管你是想横着走的螃蟹步 🦀，还是指哪打哪的豪华车队 🏎️，这里都有你的菜。

## 文件结构

```
Chassis/
├── chassis.hpp    C++ 头文件（类定义 + Doxygen 注释）
├── chassis.cpp    C++ 实现文件
└── README.md      本文件
```

---

## 1. 类体系

```
Chassis (抽象基类)
├── MecanumChassis    麦克纳姆轮底盘（4轮）
├── Omni3Chassis      三轮全向轮底盘（120°分布）
├── Omni4Chassis      四轮全向轮底盘（X型布局）
└── SwerveChassis     舵轮底盘（4轮独立转向）
```

### 基类 `Chassis`

| 方法 | 说明 |
|------|------|
| `SetSpeed(x, y, w)` | 设置底盘目标速度（m/s, m/s, rad/s） |
| `Stop()` | 停止底盘，所有电机转速归零 |
| `IsMoving()` | 判断底盘是否处于运动状态 |
| `GetSpeed(x, y, w)` | 获取当前目标速度 |

---

## 2. 麦克纳姆轮底盘 `MecanumChassis` 🦀

### 2.1 布局

```
       Y+
       ^
       |
  FL(0)●────────●FR(1)
       |  车体   |
  BL(2)●────────●BR(3)  --> X+
```

### 2.2 机械参数

| 参数 | 类型 | 说明 |
|------|------|------|
| `wheel_radius` | float | 轮子半径 R (m) |
| `wheel_base_x` | float | X 方向半轴距 Lx (m) |
| `wheel_base_y` | float | Y 方向半轴距 Ly (m) |

### 2.3 API 示例

```cpp
const int ids[4] = {1, 2, 3, 4};
MecanumChassis chassis(ids, 0.05f, 0.125f, 0.125f);
chassis.SetSpeed(1.0f, 0.0f, 0.5f); // 前进 + 旋转
chassis.Stop();
```

### 2.4 核心方法

| 方法 | 说明 |
|------|------|
| `InverseKinematics(vx, vy, wz, out[4])` | 逆运动学：底盘速度 → 四轮转速 |
| `ForwardKinematics(vx, vy, wz)` | 正运动学：四轮转速 → 底盘速度 |
| `ClampWheelSpeed(rpm[4], max_rpm)` | 等比例限幅，保持运动方向不变 |

---

## 3. 三轮全向轮底盘 `Omni3Chassis` 🔺

### 3.1 布局

三个全向轮均匀分布在圆周上，位置角度分别为 `0°`、`120°`、`240°`，驱动方向为各自切向。

```
           Y+
           ^
           |
      轮1(120°)
         / \
        /   \
       /     \
      /       \
  轮2(240°)────轮0(0°) --> X+
```

### 3.2 机械参数

| 参数 | 类型 | 说明 |
|------|------|------|
| `wheel_radius` | float | 轮子半径 R (m) |
| `wheel_base` | float | 中心到轮子的距离 L (m) |

### 3.3 API 示例

```cpp
const int ids[3] = {1, 2, 3};
Omni3Chassis chassis(ids, 0.05f, 0.15f);
chassis.SetSpeed(0.5f, 0.3f, 0.2f);
chassis.Stop();
```

---

## 4. 四轮全向轮底盘 `Omni4Chassis` 🛡️

### 4.1 布局

四个全向轮呈 **X型（菱形）** 布局，分别守在四个角：

| 轮子 | 位置 | 驱动方向 |
|------|------|----------|
| FL(0) | 前左 | 45° |
| FR(1) | 前右 | 135° |
| BL(2) | 后左 | 225° |
| BR(3) | 后右 | 315° |

### 4.2 机械参数

| 参数 | 类型 | 说明 |
|------|------|------|
| `wheel_radius` | float | 轮子半径 R (m) |
| `wheel_base_x` | float | X 方向半轴距 Lx (m) |
| `wheel_base_y` | float | Y 方向半轴距 Ly (m) |

### 4.3 API 示例

```cpp
const int ids[4] = {1, 2, 3, 4};
Omni4Chassis chassis(ids, 0.05f, 0.125f, 0.125f);
chassis.SetSpeed(0.5f, 0.3f, 0.2f);
chassis.Stop();
```

---

## 5. 舵轮底盘 `SwerveChassis` 🎯

### 5.1 布局

每个轮子独立控制**驱动转速**和**转向角度**，共 4 个模块。

```
       Y+
       ^
       |
  FL(0)●────────●FR(1)
       |  车体   |
  BL(2)●────────●BR(3)  --> X+
```

### 5.2 机械参数

| 参数 | 类型 | 说明 |
|------|------|------|
| `wheel_radius` | float | 轮子半径 R (m) |
| `wheel_base_x` | float | X 方向半轴距 Lx (m) |
| `wheel_base_y` | float | Y 方向半轴距 Ly (m) |

### 5.3 模块 `SteeringModule`

每个舵轮模块包含：
- `drive_motor`：驱动电机，控制转速
- `target_angle`：目标转向角度（rad）
- `current_angle`：当前转向角度（rad，外部反馈更新）

### 5.4 API 示例

```cpp
const int ids[4] = {1, 2, 3, 4};
SwerveChassis chassis(ids, 0.05f, 0.125f, 0.125f);
chassis.SetSpeed(1.0f, 0.0f, 0.0f); // 前进

// 获取各模块目标状态（下发给电机驱动）
float speed[4], angle[4];
chassis.GetModuleStates(speed, angle);
```

---

## 6. 轮速限幅机制

所有底盘类型都支持轮速等比例限幅：

```cpp
void ClampWheelSpeed(float rpm[], float max_rpm);
```

当任意轮子转速超过 `max_rpm` 时，**全部轮子等比例缩放**，保证：
- 运动方向不变 ✅
- 没有轮子单独爆表 ✅
- 体验丝滑不突兀 ✅

---

## 7. 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0 | 2026-06-02 | 重构底盘库：麦轮 + 三轮全向 + 四轮全向 + 舵轮，C++ 类封装，人性化 Doxygen 注释 |
