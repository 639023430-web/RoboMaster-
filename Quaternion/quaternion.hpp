/**
 * @file    quaternion.hpp
 * @author  liqun
 * @brief   四元数 C++ 类 —— 类声明与接口
 * @version 1.0
 * @date    2026-06-02
 *
 * @copyright Copyright (c) 2026
 *
 * C++ 轻量级四元数类封装，底层仍依赖 C 结构体 Quaternion_t。
 * 提供运算符重载、工厂方法、链式调用等 C++ 惯用接口。
 *
 * 无异常、无 RTTI、无虚函数 —— 可直接在 STM32 等裸机平台上使用。
 */

#ifndef __QUATERNION_HPP
#define __QUATERNION_HPP

#include <cmath>

#ifdef __cplusplus
extern "C" {
#endif
#include "quaternion.h"
#ifdef __cplusplus
}
#endif

/**
 * @brief 四元数 C++ 类
 *
 * 封装了底层 C 结构体 Quaternion_t，提供：
 *   - 构造函数（默认/参数/从C结构体）
 *   - 运算符重载（+ - * += -= *= == !=）
 *   - 成员方法（归一化/共轭/逆/欧拉角转换/旋转矩阵/向量旋转/SLERP）
 *   - 与 C API 的互操作接口（toC / operator=）
 *
 * @note 适用于嵌入式平台（无异常/无RTTI），可直接在 STM32 上使用。
 */
class Quaternion
{
public:
    float w; /**< 实部（标量部分） */
    float x; /**< 虚部 i 分量 */
    float y; /**< 虚部 j 分量 */
    float z; /**< 虚部 k 分量 */

    /* ======================= 构造函数 ======================= */

    /// 默认构造函数 —— 初始化为单位四元数 (1, 0, 0, 0)
    Quaternion();

    /// 参数构造函数
    Quaternion(float w_, float x_, float y_, float z_);

    /// 从 C 结构体构造
    explicit Quaternion(const Quaternion_t &q);

    /* ======================= C 互操作 ======================= */

    /// 转换为 C 结构体
    Quaternion_t toC() const;

    /// 从 C 结构体赋值
    Quaternion &operator=(const Quaternion_t &q);

    /* ======================= 静态工厂方法 ======================= */

    /// 创建单位四元数（无旋转）
    static Quaternion identity();

    /// 从 ZYX 欧拉角创建四元数
    static Quaternion fromEulerZYX(float roll, float pitch, float yaw);

    /* ======================= 基本运算 ======================= */

    /// 模长 |q| = sqrt(w^2 + x^2 + y^2 + z^2)
    float norm() const;

    /// norm() 的别名
    float magnitude() const;

    /// 归一化（原地修改），返回自身引用以支持链式调用
    Quaternion &normalize();

    /// 返回归一化后的副本（不修改原对象）
    Quaternion normalized() const;

    /// 判断是否为单位四元数
    bool isNormalized(float epsilon = 1e-6f) const;

    /* ======================= 共轭 & 逆 ======================= */

    /// 共轭: q* = (w, -x, -y, -z)
    Quaternion conjugate() const;

    /// 逆: q^{-1} = q* / |q|^2
    Quaternion inverse() const;

    /* ======================= 点积 ======================= */

    /// 点积（内积），用于衡量两个姿态的接近程度
    float dot(const Quaternion &other) const;

    /* ======================= 欧拉角转换 ======================= */

    /// 转换为 ZYX 欧拉角（rad）
    void toEulerZYX(float &roll, float &pitch, float &yaw) const;

    /* ======================= 旋转矩阵 ======================= */

    /// 转换为 3x3 旋转矩阵（行主序）
    void toRotationMatrix(float m[3][3]) const;

    /* ======================= 向量旋转 ======================= */

    /// 使用四元数旋转三维向量: v' = q * v * q*
    void rotateVector(const float v[3], float result[3]) const;

    /* ======================= SLERP 插值 ======================= */

    /// 球面线性插值（SLERP），在两个姿态间恒角速度插值
    Quaternion slerp(const Quaternion &to, float t) const;

    /* ======================= 运算符重载 ======================= */

    Quaternion operator+(const Quaternion &o) const;
    Quaternion operator-(const Quaternion &o) const;

    /// Hamilton 乘积: q1 * q2
    Quaternion operator*(const Quaternion &o) const;

    /// 标量乘法: q * s
    Quaternion operator*(float s) const;

    Quaternion &operator+=(const Quaternion &o);
    Quaternion &operator-=(const Quaternion &o);
    Quaternion &operator*=(const Quaternion &o);

    bool operator==(const Quaternion &o) const;
    bool operator!=(const Quaternion &o) const;
};

/* ======================= 非成员运算符 ======================= */

/// 标量左乘: s * q
Quaternion operator*(float s, const Quaternion &q);

#endif // __QUATERNION_HPP
