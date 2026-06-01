# 四元数运算库 (Quaternion Library)

纯 C 实现的四元数运算库，同时提供 C++ 类封装，适用于 RoboMaster 机器人竞赛中的姿态解算、IMU 数据融合（Mahony/Madgwick 滤波器）、云台控制等场景。

## 文件结构

```
Quaternion/
├── quaternion.h      C 头文件（结构体 + 函数声明 + Doxygen 注释）
├── quaternion.c      C 实现文件（所有函数的完整实现）
├── quaternion.hpp    C++ 头文件（类声明 + 运算符重载声明）
├── quaternion.cpp    C++ 实现文件（类方法实现）
└── README.md         本文件
```

## C API 速查

### 数据结构

```c
typedef struct {
    float w;   // 实部（标量）
    float x;   // 虚部 i-分量
    float y;   // 虚部 j-分量
    float z;   // 虚部 k-分量
} Quaternion_t;
```

### 函数列表

| 分类 | 函数 | 说明 |
|------|------|------|
| 初始化 | `Quaternion_SetIdentity(q)` | 设为单位四元数 (1,0,0,0) |
| 加法 | `Quaternion_Add(q1, q2)` | 逐分量相加 |
| 减法 | `Quaternion_Subtract(q1, q2)` | 逐分量相减 |
| 乘法 | `Quaternion_Multiply(q1, q2)` | Hamilton 乘积（旋转组合） |
| 标量乘 | `Quaternion_Scale(q, s)` | q * s |
| 模长 | `Quaternion_Norm(q)` | 欧氏范数 |
| 归一化 | `Quaternion_Normalize(q)` | 原地归一化，使模长为 1 |
| 归一化判断 | `Quaternion_IsNormalized(q, eps)` | 检查是否单位四元数 |
| 共轭 | `Quaternion_Conjugate(q)` | q* = (w, -x, -y, -z) |
| 逆 | `Quaternion_Inverse(q)` | q^{-1} = q* / \|q\|^2 |
| 点积 | `Quaternion_Dot(q1, q2)` | w1*w2 + x1*x2 + y1*y2 + z1*z2 |
| 欧拉角→四元数 | `Quaternion_FromEulerZYX(q, r, p, y)` | ZYX 顺序，弧度 |
| 四元数→欧拉角 | `Quaternion_ToEulerZYX(q, &r, &p, &y)` | ZYX 顺序，含死锁保护 |
| 旋转矩阵 | `Quaternion_ToRotationMatrix(q, m)` | 3x3 行主序矩阵 |
| 向量旋转 | `Quaternion_RotateVector(q, v, out)` | v' = q * v * q* |
| 插值 | `Quaternion_Slerp(q1, q2, t)` | 球面线性插值 |

### 典型用法

```c
#include "quaternion.h"

Quaternion_t attitude;
Quaternion_SetIdentity(&attitude);

// 从陀螺仪角速度更新姿态（1 kHz）
float gx = 0.01f, gy = 0.02f, gz = 0.0f;   // rad/s
float dt = 0.001f;
Quaternion_t dq = {1.0f, 0.5f*gx*dt, 0.5f*gy*dt, 0.5f*gz*dt};
Quaternion_Normalize(&dq);
attitude = Quaternion_Multiply(&attitude, &dq);
Quaternion_Normalize(&attitude);              // 每步归一化

// 读取欧拉角
float roll, pitch, yaw;
Quaternion_ToEulerZYX(&attitude, &roll, &pitch, &yaw);

// 旋转重力向量到机体坐标系
float grav[3] = {0, 0, 1}, grav_body[3];
Quaternion_RotateVector(&attitude, grav, grav_body);
```

## C++ API 速查

### 类方法

| 分类 | 方法 / 运算符 | 说明 |
|------|--------------|------|
| 构造 | `Quaternion()` | 默认单位四元数 |
| 构造 | `Quaternion(w, x, y, z)` | 指定各分量 |
| 构造 | `Quaternion(Quaternion_t)` | 从 C 结构体构造 |
| 工厂 | `Quaternion::identity()` | 静态工厂：单位四元数 |
| 工厂 | `Quaternion::fromEulerZYX(r,p,y)` | 静态工厂：欧拉角→四元数 |
| 运算符 | `+` `-` `*` `+=` `-=` `*=` | 加/减/Hamilton乘积 |
| 运算符 | `q * 2.0f` / `2.0f * q` | 标量乘法 |
| 归一化 | `q.normalize()` | 原地归一化，返回 *this（链式调用）|
| 归一化 | `q.normalized()` | 返回归一化副本 |
| 共轭/逆 | `q.conjugate()` / `q.inverse()` | |
| 欧拉角 | `q.toEulerZYX(r,p,y)` | 四元数→欧拉角 |
| 旋转矩阵 | `q.toRotationMatrix(m)` | 四元数→3x3 矩阵 |
| 向量旋转 | `q.rotateVector(v, out)` | |
| 插值 | `q.slerp(target, t)` | SLERP |
| C互操作 | `q.toC()` / `operator=` | 双向转换 Quaternion_t |

### 典型用法

```cpp
#include "quaternion.hpp"

Quaternion attitude;                                      // 默认 (1,0,0,0)
Quaternion dq(1.0f, 0.005f, 0.01f, 0.0f);
dq.normalize();
attitude = attitude * dq;                                 // Hamilton 乘积
attitude.normalize().rotateVector(grav, grav_body);       // 链式调用

float r, p, y;
attitude.toEulerZYX(r, p, y);

// 与 C API 互操作
Quaternion_t c_q = attitude.toC();
Quaternion_Normalize(&c_q);
attitude = c_q;                                           // 自动转换
```

## 坐标系与欧拉角约定

- **坐标系**：右手坐标系
  - X 轴：前（Forward）
  - Y 轴：左（Left）
  - Z 轴：上（Up）
- **欧拉角顺序**：ZYX 内旋（Yaw → Pitch → Roll）
  - Yaw：绕 Z 轴（偏航）
  - Pitch：绕 Y' 轴（俯仰）
  - Roll：绕 X'' 轴（滚转）
- **旋转正方向**：右手定则

## 性能建议

| 建议 | 说明 |
|------|------|
| 归一化频率 = IMU 更新频率 | 每次积分后调用 Normalize，典型 ~1 kHz |
| 启用 FPU | ARM Cortex-M4/M7 开启硬件浮点 + `-ffast-math` |
| SLERP 用于离线/低频 | 实时控制可改用 NLerp（线性插值+归一化，更快） |
| 注意万向节死锁 | pitch ≈ ±90° 时 yaw 被强制置零，见 toEulerZYX 注释 |

## 参考资料

1. Ken Shoemake, *Animating Rotation with Quaternion Curves*, SIGGRAPH 1985
2. J. Diebel, *Representing Attitude: Euler Angles, Unit Quaternions, and Rotation Vectors*, Stanford 2006
3. R. Mahony et al., *Nonlinear Complementary Filters on the Special Orthogonal Group*, IEEE TAC 2008
4. S. Madgwick, *An efficient orientation filter for IMU and MARG sensor arrays*, 2010
