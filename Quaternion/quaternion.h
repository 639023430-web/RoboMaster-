/**
 * @file    quaternion.h
 * @author  liqun
 * @brief   四元数运算库 —— C 语言 API 声明
 * @version 1.0
 * @date    2026-06-02
 *
 * @copyright Copyright (c) 2026
 *
 * 本头文件声明了四元数运算的完整 C 语言 API，覆盖基本代数（加减法、
 * Hamilton 乘积、标量乘）、归一化/共轭/逆、欧拉角与四元数相互转换、
 * 旋转矩阵生成、三维向量旋转、SLERP 球面线性插值等函数，广泛适用于
 * Mahony/Madgwick 姿态滤波器及嵌入式运动控制场景。
 *
 * @note 所有角度统一使用 **弧度 (rad)**。
 *       欧拉角约定为 ZYX 内旋顺序（先绕 Z 偏航，再绕 Y' 俯仰，最后绕 X'' 滚转）。
 */

#ifndef __QUATERNION_H
#define __QUATERNION_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 四元数结构体
 *
 * 数学形式：q = w + x*i + y*j + z*k
 * - w: 实部（标量部分）
 * - (x, y, z): 虚部（矢量部分）
 */
typedef struct {
    float w; /**< 实部（标量部分） */
    float x; /**< 虚部 i-分量 */
    float y; /**< 虚部 j-分量 */
    float z; /**< 虚部 k-分量 */
} Quaternion_t;

/**
 * @brief 初始化为单位四元数 (1, 0, 0, 0)，表示零旋转（无姿态变化）
 *
 * 通常在系统上电初始化时调用一次，为后续姿态估计提供初始状态。
 *
 * @param[out] q  待初始化的四元数指针
 */
void Quaternion_SetIdentity(Quaternion_t *q);

/**
 * @brief 加法（逐分量相加）：r = q1 + q2
 *
 * 加法本身没有直接的旋转几何意义，主要用于 SLERP/NLerp 插值中的构造步骤。
 *
 * @param[in]  q1  第一个四元数
 * @param[in]  q2  第二个四元数
 * @return     q1 + q2（逐分量）
 */
Quaternion_t Quaternion_Add(const Quaternion_t *q1, const Quaternion_t *q2);

/**
 * @brief 减法（逐分量相减）：r = q1 - q2
 *
 * @param[in]  q1  第一个四元数
 * @param[in]  q2  第二个四元数
 * @return     q1 - q2（逐分量）
 */
Quaternion_t Quaternion_Subtract(const Quaternion_t *q1, const Quaternion_t *q2);

/**
 * @brief Hamilton 乘积：r = q1 * q2
 *
 * 语义：先执行 q2 表示的旋转，再执行 q1 表示的旋转（右乘优先）。
 *
 * @attention 四元数乘法 **不满足交换律**，一般情况下 q1*q2 != q2*q1。
 *
 * @param[in]  q1  左乘四元数
 * @param[in]  q2  右乘四元数
 * @return     q1 * q2（Hamilton 乘积）
 */
Quaternion_t Quaternion_Multiply(const Quaternion_t *q1, const Quaternion_t *q2);

/**
 * @brief 标量乘法（逐分量乘标量）：r = q * scale
 *
 * @param[in]  q      四元数
 * @param[in]  scale  标量乘子
 * @return     q * scale
 */
Quaternion_t Quaternion_Scale(const Quaternion_t *q, float scale);

/**
 * @brief 欧氏范数（模长）：||q|| = sqrt(w^2 + x^2 + y^2 + z^2)
 *
 * @param[in]  q  四元数
 * @return     ||q||
 */
float Quaternion_Norm(const Quaternion_t *q);

/**
 * @brief 原地归一化，使 ||q|| = 1
 *
 * 在 Mahony/Madgwick 等姿态滤波器的每一步积分后，四元数会因浮点误差
 * 逐渐偏离单位长度。建议以与滤波器相同的频率（通常 ~1 kHz）调用此函数，
 * 将四元数拉回单位 3-球面上，保证旋转矩阵的正交性。
 *
 * @note 若 ||q|| 约为 0，函数不做任何操作（防止除零产生 NaN）。
 *
 * @param[in,out] q  待归一化的四元数指针
 */
void Quaternion_Normalize(Quaternion_t *q);

/**
 * @brief 判断四元数是否已归一化（单位模长）
 *
 * @param[in]  q        待检测的四元数
 * @param[in]  epsilon  容差（如 1e-6f）
 * @return     true 若 | ||q|| - 1 | < epsilon
 */
bool Quaternion_IsNormalized(const Quaternion_t *q, float epsilon);

/**
 * @brief 四元数共轭：q* = (w, -x, -y, -z)
 *
 * 对单位四元数而言，共轭即反向旋转（逆旋转）。
 *
 * @param[in]  q  四元数
 * @return     q*
 */
Quaternion_t Quaternion_Conjugate(const Quaternion_t *q);

/**
 * @brief 四元数的逆：q^{-1} = q* / ||q||^2
 *
 * 满足 q * q^{-1} = q^{-1} * q = (1, 0, 0, 0)。
 * 若 q 已归一化，则逆等于共轭。
 *
 * @param[in]  q  四元数
 * @return     q^{-1}
 */
Quaternion_t Quaternion_Inverse(const Quaternion_t *q);

/**
 * @brief 四维点积：q1·q2 = w1*w2 + x1*x2 + y1*y2 + z1*z2
 *
 * 点积的几何意义：
 *  - dot = 1  → 相同旋转（夹角 0°）
 *  - dot = 0  → 正交（夹角 90°）
 *  - dot = -1 → 相反旋转（夹角 180°）
 *
 * 同时也是两四元数间夹角一半的余弦值，SLERP 内部依赖此值判断夹角大小。
 *
 * @param[in]  q1  第一个四元数
 * @param[in]  q2  第二个四元数
 * @return     q1·q2
 */
float Quaternion_Dot(const Quaternion_t *q1, const Quaternion_t *q2);

/**
 * @brief 将 ZYX 欧拉角（弧度）转换为四元数
 *
 * 旋转顺序（世界坐标系 → 机体坐标系，内旋）：
 *  1. Yaw   （偏航）—— 绕世界 Z 轴旋转
 *  2. Pitch （俯仰）—— 绕新的 Y' 轴旋转
 *  3. Roll  （滚转）—— 绕新的 X'' 轴旋转
 *
 * 数学上等价于三次 Hamilton 乘积的级联：
 *  q = q_roll(x) * q_pitch(y) * q_yaw(z)
 *
 * @param[out] q      输出四元数
 * @param[in]  roll   绕 X 轴的滚转角 (rad)
 * @param[in]  pitch  绕 Y 轴的俯仰角 (rad)
 * @param[in]  yaw    绕 Z 轴的偏航角 (rad)
 */
void Quaternion_FromEulerZYX(Quaternion_t *q, float roll, float pitch, float yaw);

/**
 * @brief 将四元数转换为 ZYX 欧拉角（弧度）
 *
 * 对归一化四元数 q = (w, x, y, z)：
 *   roll  = atan2( 2(w*x + y*z),  1 - 2(x^2 + y^2) )
 *   pitch = asin(  2(w*y - z*x)                     )
 *   yaw   = atan2( 2(w*z + x*y),  1 - 2(y^2 + z^2) )
 *
 * @note pitch 定义域为 [-pi/2, pi/2]。
 *       当 pitch 接近 ±90°（万向节死锁）时，内部强制设 yaw = 0 并由剩余自由度计算 roll。
 *
 * @param[in]  q      输入四元数（建议先归一化）
 * @param[out] roll   滚转角 (rad)
 * @param[out] pitch  俯仰角 (rad)
 * @param[out] yaw    偏航角 (rad)
 */
void Quaternion_ToEulerZYX(const Quaternion_t *q, float *roll, float *pitch, float *yaw);

/**
 * @brief 将四元数转换为 3x3 旋转矩阵
 *
 * 对归一化四元数 q = (w, x, y, z)：
 *
 *       [ 1-2(y^2+z^2)    2(xy - wz)     2(xz + wy)  ]
 *   R = [   2(xy + wz)  1-2(x^2+z^2)     2(yz - wx)  ]
 *       [   2(xz - wy)    2(yz + wx)   1-2(x^2+y^2)  ]
 *
 * 矩阵以 **行主序** 存储，即 m[i][j] 表示第 i 行第 j 列。
 *
 * @warning 输入必须归一化，否则矩阵非正交。
 *
 * @param[in]  q  输入四元数
 * @param[out] m  输出 3x3 旋转矩阵
 */
void Quaternion_ToRotationMatrix(const Quaternion_t *q, float m[3][3]);

/**
 * @brief 用四元数旋转三维向量：v' = q * (0, v) * q*
 *
 * 内部将代数式展开为矩阵-向量乘法的直接形式，避免构造中间四元数对象，效率更高。
 *
 * @param[in]  q       归一化旋转四元数
 * @param[in]  v       输入三维向量
 * @param[out] result  旋转后的三维向量
 */
void Quaternion_RotateVector(const Quaternion_t *q, const float v[3], float result[3]);

/**
 * @brief 在两个单位四元数之间做球面线性插值（SLERP）
 *
 * SLERP 保证：
 *  - 插值路径位于四维单位球面的 **大圆弧**（最短旋转路径）
 *  - 整个插值过程中 **角速度恒定**，无加速/减速感
 *
 * 记 omega = arccos(q1·q2)，有：
 *   slerp(q1, q2, t) = sin((1-t)*omega)/sin(omega) * q1
 *                     + sin(t*omega)/sin(omega)     * q2
 *
 * 边界情况处理：
 *  - 若 q1·q2 < 0，翻转 q2 以保证走 **最短弧**。
 *  - 若 |dot| > 0.9995（夹角 < ~1.8°），退化为 **NLerp**（线性插值+归一化），
 *    避免 sin(omega) ≈ 0 导致的数值不稳定。
 *
 * @param[in]  q1  起始四元数（须归一化）
 * @param[in]  q2  终点四元数（须归一化）
 * @param[in]  t   插值参数 [0, 1]：0 → q1，1 → q2
 * @return     插值后的单位四元数
 */
Quaternion_t Quaternion_Slerp(const Quaternion_t *q1, const Quaternion_t *q2, float t);

#ifdef __cplusplus
}
#endif

#endif /* __QUATERNION_H */
