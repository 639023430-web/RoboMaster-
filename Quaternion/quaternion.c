/**
 * @file    quaternion.c
 * @author  liqun
 * @brief   四元数运算库 —— C 语言实现
 * @version 1.0
 * @date    2026-06-02
 *
 * @copyright Copyright (c) 2026
 *
 * 实现了四元数的基本代数运算（加/减/乘/标量乘）、归一化/共轭/逆、
 * 欧拉角↔四元数转换、旋转矩阵生成、三维向量旋转、SLERP 球面线性插值
 * 等姿态解算常用函数。
 *
 * 适用于嵌入式实时系统（如 STM32 + Mahony/Madgwick 滤波器），
 * 无堆分配、无递归、仅依赖标准 math.h。
 */

#include "quaternion.h"
#include <math.h>

/**
 * @brief 将值 clamp 到 [-1, 1] 范围内
 *
 * 用于 asinf() 等函数在浮点运算误差导致参数略微超出定义域时保护。
 */
static inline float Clamp(float val, float lo, float hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

/**
 * @brief 将四元数设置为单位四元数 (1, 0, 0, 0)，表示无旋转
 *
 * 单位四元数对应的旋转角度为 0，通常在姿态初始化时调用。
 */
void Quaternion_SetIdentity(Quaternion_t *q)
{
    q->w = 1.0f;
    q->x = 0.0f;
    q->y = 0.0f;
    q->z = 0.0f;
}

/**
 * @brief 四元数加法: r = q1 + q2（逐分量相加）
 *
 * 加法在物理上没有直接的旋转意义，主要用于插值计算中。
 */
Quaternion_t Quaternion_Add(const Quaternion_t *q1, const Quaternion_t *q2)
{
    Quaternion_t result;
    result.w = q1->w + q2->w;
    result.x = q1->x + q2->x;
    result.y = q1->y + q2->y;
    result.z = q1->z + q2->z;
    return result;
}

/**
 * @brief 四元数减法: r = q1 - q2（逐分量相减）
 */
Quaternion_t Quaternion_Subtract(const Quaternion_t *q1, const Quaternion_t *q2)
{
    Quaternion_t result;
    result.w = q1->w - q2->w;
    result.x = q1->x - q2->x;
    result.y = q1->y - q2->y;
    result.z = q1->z - q2->z;
    return result;
}

/**
 * @brief 四元数乘法 —— Hamilton 乘积: r = q1 ⊗ q2
 *
 * 含义：先执行 q2 表示的旋转，再执行 q1 表示的旋转。
 *
 * Hamilton 乘积公式:
 *   r.w = w1*w2 - x1*x2 - y1*y2 - z1*z2
 *   r.x = w1*x2 + x1*w2 + y1*z2 - z1*y2
 *   r.y = w1*y2 - x1*z2 + y1*w2 + z1*x2
 *   r.z = w1*z2 + x1*y2 - y1*x2 + z1*w2
 *
 * @note 四元数乘法**不满足交换律**，q1 ⊗ q2 ≠ q2 ⊗ q1。
 */
Quaternion_t Quaternion_Multiply(const Quaternion_t *q1, const Quaternion_t *q2)
{
    Quaternion_t result;

    result.w = q1->w * q2->w - q1->x * q2->x - q1->y * q2->y - q1->z * q2->z;
    result.x = q1->w * q2->x + q1->x * q2->w + q1->y * q2->z - q1->z * q2->y;
    result.y = q1->w * q2->y - q1->x * q2->z + q1->y * q2->w + q1->z * q2->x;
    result.z = q1->w * q2->z + q1->x * q2->y - q1->y * q2->x + q1->z * q2->w;

    return result;
}

/**
 * @brief 标量乘法: r = q * scale（逐分量乘标量）
 */
Quaternion_t Quaternion_Scale(const Quaternion_t *q, float scale)
{
    Quaternion_t result;
    result.w = q->w * scale;
    result.x = q->x * scale;
    result.y = q->y * scale;
    result.z = q->z * scale;
    return result;
}

/**
 * @brief 计算四元数的模长 |q| = sqrt(w^2 + x^2 + y^2 + z^2)
 */
float Quaternion_Norm(const Quaternion_t *q)
{
    return sqrtf(q->w * q->w + q->x * q->x + q->y * q->y + q->z * q->z);
}

/**
 * @brief 四元数归一化（原地修改）
 *
 * 使四元数模长为 1。姿态更新算法（如 Mahony/Madgwick）中
 * 每步积分后通常都需要归一化，以消除数值误差。
 *
 * @note 调用频率通常与姿态更新频率一致，如 IMU 数据 1 kHz。
 *       若 norm 为 0，则不做任何操作（避免除零）。
 */
void Quaternion_Normalize(Quaternion_t *q)
{
    float norm = sqrtf(q->w * q->w + q->x * q->x + q->y * q->y + q->z * q->z);

    if (norm > 1e-12f) {   /* 使用较小阈值，避免极微小值时除零 */
        float inv_norm = 1.0f / norm;
        q->w *= inv_norm;
        q->x *= inv_norm;
        q->y *= inv_norm;
        q->z *= inv_norm;
    }
}

/**
 * @brief 判断四元数是否为单位四元数
 *
 * @param epsilon 容差，如 1e-6f。当 |norm - 1.0| < epsilon 时视为已归一化。
 */
bool Quaternion_IsNormalized(const Quaternion_t *q, float epsilon)
{
    float norm = Quaternion_Norm(q);
    float diff = norm - 1.0f;
    if (diff < 0.0f) diff = -diff;   /* fabsf 等效 */
    return diff < epsilon;
}

/**
 * @brief 四元数共轭: q* = (w, -x, -y, -z)
 *
 * 若 q 表示一个旋转，则 q* 表示相反方向的旋转。
 */
Quaternion_t Quaternion_Conjugate(const Quaternion_t *q)
{
    Quaternion_t result;
    result.w =  q->w;
    result.x = -q->x;
    result.y = -q->y;
    result.z = -q->z;
    return result;
}

/**
 * @brief 四元数的逆: q^{-1} = q* / |q|^2
 *
 * 对于单位四元数，逆等于共轭。
 * 满足 q ⊗ q^{-1} = q^{-1} ⊗ q = (1, 0, 0, 0)。
 */
Quaternion_t Quaternion_Inverse(const Quaternion_t *q)
{
    Quaternion_t conj = Quaternion_Conjugate(q);
    float norm_sq = q->w * q->w + q->x * q->x + q->y * q->y + q->z * q->z;

    if (norm_sq > 1e-12f) {
        float inv_norm_sq = 1.0f / norm_sq;
        conj.w *= inv_norm_sq;
        conj.x *= inv_norm_sq;
        conj.y *= inv_norm_sq;
        conj.z *= inv_norm_sq;
    }
    /* else: 零四元数无逆，返回共轭（即零四元数本身） */

    return conj;
}

/**
 * @brief 四元数点积（内积）: q1·q2 = w1*w2 + x1*x2 + y1*y2 + z1*z2
 *
 * 点积在 SLERP 插值中用于计算两个四元数之间的夹角，
 * 也可用于判断两个姿态的接近程度（cos 值越接近 1 越接近）。
 */
float Quaternion_Dot(const Quaternion_t *q1, const Quaternion_t *q2)
{
    return q1->w * q2->w + q1->x * q2->x + q1->y * q2->y + q1->z * q2->z;
}

/**
 * @brief 将 ZYX 欧拉角转换为四元数
 *
 * 旋转顺序（从世界坐标系到机体坐标系）:
 *   1. 绕世界 Z 轴旋转 yaw   (偏航角)
 *   2. 绕新   Y'轴旋转 pitch (俯仰角)
 *   3. 绕新   X''轴旋转 roll (滚转角)
 *
 * 即: q = q_roll(x) ⊗ q_pitch(y) ⊗ q_yaw(z)
 *
 * @note roll/pitch/yaw 单位均为弧度(rad)。
 */
void Quaternion_FromEulerZYX(Quaternion_t *q, float roll, float pitch, float yaw)
{
    float cr = cosf(roll  * 0.5f);
    float sr = sinf(roll  * 0.5f);
    float cp = cosf(pitch * 0.5f);
    float sp = sinf(pitch * 0.5f);
    float cy = cosf(yaw   * 0.5f);
    float sy = sinf(yaw   * 0.5f);

    /* q = q_roll ⊗ q_pitch ⊗ q_yaw
     * 展开 Hamilton 乘积得: */
    q->w = cr * cp * cy + sr * sp * sy;
    q->x = sr * cp * cy - cr * sp * sy;
    q->y = cr * sp * cy + sr * cp * sy;
    q->z = cr * cp * sy - sr * sp * cy;
}

/**
 * @brief 将四元数转换为 ZYX 欧拉角（rad）
 *
 * 对于归一化四元数:
 *   roll  = atan2( 2*(w*x + y*z), 1 - 2*(x^2 + y^2) )
 *   pitch = asin(  2*(w*y - z*x) )
 *   yaw   = atan2( 2*(w*z + x*y), 1 - 2*(y^2 + z^2) )
 *
 * @note pitch 角定义域为 [-pi/2, pi/2]，当 pitch 接近 ±90° 时会出现万向节死锁。
 */
void Quaternion_ToEulerZYX(const Quaternion_t *q, float *roll, float *pitch, float *yaw)
{
    float w = q->w, x = q->x, y = q->y, z = q->z;

    float xy = x * y;
    float wz = w * z;
    float wy = w * y;
    float xz = x * z;
    float wx = w * x;
    float yz = y * z;

    float xx = x * x;
    float yy = y * y;
    float zz = z * z;

    /* pitch: 注意 asinf 的参数需要 clamp 到 [-1, 1] 避免浮点误差超界 */
    float sin_pitch = 2.0f * (wy - xz);
    *pitch = asinf(Clamp(sin_pitch, -1.0f, 1.0f));

    /* 处理万向节死锁：当 |sin_pitch| 接近 1 时，roll 和 yaw 无法单独确定 */
    if (fabsf(sin_pitch) > 0.9999f) {
        /* 死锁情况，令 yaw = 0 */
        *yaw = 0.0f;
        *roll = atan2f(2.0f * (wx - yz), 1.0f - 2.0f * (xx + zz));
    } else {
        *roll = atan2f(2.0f * (wx + yz), 1.0f - 2.0f * (xx + yy));
        *yaw  = atan2f(2.0f * (wz + xy), 1.0f - 2.0f * (yy + zz));
    }
}

/**
 * @brief 将四元数转换为 3x3 旋转矩阵
 *
 * 对于归一化四元数 q = (w, x, y, z):
 *
 *       [ 1-2(y^2+z^2)    2(xy-wz)      2(xz+wy)   ]
 *   R = [  2(xy+wz)    1-2(x^2+z^2)    2(yz-wx)   ]
 *       [  2(xz-wy)      2(yz+wx)    1-2(x^2+y^2) ]
 *
 * 以行主序存储，R[i][j] 表示旋转矩阵第 i 行第 j 列。
 *
 * @note 输入必须是归一化四元数，否则矩阵不是正交的。
 */
void Quaternion_ToRotationMatrix(const Quaternion_t *q, float m[3][3])
{
    float w = q->w, x = q->x, y = q->y, z = q->z;

    float x2 = x * x, y2 = y * y, z2 = z * z;
    float wx = w * x, wy = w * y, wz = w * z;
    float xy = x * y, xz = x * z, yz = y * z;

    /* 第 0 行 */
    m[0][0] = 1.0f - 2.0f * (y2 + z2);
    m[0][1] = 2.0f * (xy - wz);
    m[0][2] = 2.0f * (xz + wy);

    /* 第 1 行 */
    m[1][0] = 2.0f * (xy + wz);
    m[1][1] = 1.0f - 2.0f * (x2 + z2);
    m[1][2] = 2.0f * (yz - wx);

    /* 第 2 行 */
    m[2][0] = 2.0f * (xz - wy);
    m[2][1] = 2.0f * (yz + wx);
    m[2][2] = 1.0f - 2.0f * (x2 + y2);
}

/**
 * @brief 使用四元数旋转一个三维向量: v' = q ⊗ v ⊗ q*
 *
 * 这是将四元数表示的旋转应用到三维空间向量的标准方法。
 * 等价于先将 v 扩展为纯四元数 (0, vx, vy, vz)，然后计算:
 *   v' = q * (0, v) * q*
 *
 * @note q 必须是归一化四元数，否则会对向量进行缩放。
 */
void Quaternion_RotateVector(const Quaternion_t *q, const float v[3], float result[3])
{
    float w = q->w, x = q->x, y = q->y, z = q->z;
    float vx = v[0], vy = v[1], vz = v[2];

    /* 展开 v' = q ⊗ (0, v) ⊗ q* 的代数形式，比两次四元数乘法更高效 */
    float wx = w * x, wy = w * y, wz = w * z;
    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, xz = x * z, yz = y * z;

    result[0] = vx * (1.0f - 2.0f * (yy + zz))
              + vy * (2.0f * (xy - wz))
              + vz * (2.0f * (xz + wy));

    result[1] = vx * (2.0f * (xy + wz))
              + vy * (1.0f - 2.0f * (xx + zz))
              + vz * (2.0f * (yz - wx));

    result[2] = vx * (2.0f * (xz - wy))
              + vy * (2.0f * (yz + wx))
              + vz * (1.0f - 2.0f * (xx + yy));
}

/**
 * @brief SLERP（Spherical Linear Interpolation）球面线性插值
 *
 * 在两个单位四元数 q1、q2 之间做球面线性插值，保证:
 *   - 插值路径是四维球面上的"大圆弧"（旋转角度最短路径）
 *   - 角速度恒定
 *
 * 公式:
 *   omega = acos(q1·q2)
 *   q(t)  = (sin((1-t)*omega) / sin(omega)) * q1
 *         + (sin(t*omega)     / sin(omega)) * q2
 *
 * @param q1 起始四元数（必须是单位四元数）
 * @param q2 终点四元数（必须是单位四元数）
 * @param t  插值参数 [0, 1]: 0 → 返回 q1，1 → 返回 q2
 * @return 插值结果（单位四元数）
 *
 * @note 当 q1 和 q2 非常接近时（dot ≈ 1），退化为线性插值（Lerp）再做归一化，
 *       避免 sin(omega) ≈ 0 导致的数值不稳定。
 */
Quaternion_t Quaternion_Slerp(const Quaternion_t *q1, const Quaternion_t *q2, float t)
{
    float dot = Quaternion_Dot(q1, q2);

    /* 若点积为负，说明两个四元数在 4D 球面上位于"对面"，
     * 此时取 q2 的相反数，以保证走的是最短弧。 */
    Quaternion_t q2_mod;
    if (dot < 0.0f) {
        dot = -dot;
        q2_mod.w = -q2->w;
        q2_mod.x = -q2->x;
        q2_mod.y = -q2->y;
        q2_mod.z = -q2->z;
    } else {
        q2_mod = *q2;
    }

    /* 夹角 omega = acos(dot) */
    const float SLERP_THRESHOLD = 0.9995f;  /* 约 1.8°，小于此角退化为 Lerp */
    float omega, sin_omega;

    if (dot > SLERP_THRESHOLD) {
        /* 点积接近 1: 夹角极小，退化为线性插值 + 归一化 */
        Quaternion_t result;
        float one_minus_t = 1.0f - t;
        result.w = one_minus_t * q1->w + t * q2_mod.w;
        result.x = one_minus_t * q1->x + t * q2_mod.x;
        result.y = one_minus_t * q1->y + t * q2_mod.y;
        result.z = one_minus_t * q1->z + t * q2_mod.z;
        Quaternion_Normalize(&result);
        return result;
    }

    /* 标准 SLERP */
    omega     = acosf(dot);
    sin_omega = sinf(omega);

    float s0 = sinf((1.0f - t) * omega) / sin_omega;  /* q1 的系数 */
    float s1 = sinf(t * omega)           / sin_omega;  /* q2 的系数 */

    Quaternion_t result;
    result.w = s0 * q1->w + s1 * q2_mod.w;
    result.x = s0 * q1->x + s1 * q2_mod.x;
    result.y = s0 * q1->y + s1 * q2_mod.y;
    result.z = s0 * q1->z + s1 * q2_mod.z;

    return result;
}