/**
 * @file    quaternion.cpp
 * @author  liqun
 * @brief   四元数 C++ 类 —— 方法实现
 * @version 1.0
 * @date    2026-06-02
 *
 * @copyright Copyright (c) 2026
 *
 * Quaternion 类的所有非平凡方法实现（构造函数、运算符、工厂方法、
 * 归一化、共轭/逆、欧拉角转换、旋转矩阵、向量旋转、SLERP）。
 * 详细 API 文档见 quaternion.hpp。
 */

#include "quaternion.hpp"
#include <cmath>

Quaternion::Quaternion()
    : w(1.0f), x(0.0f), y(0.0f), z(0.0f)
{
}

Quaternion::Quaternion(float w_, float x_, float y_, float z_)
    : w(w_), x(x_), y(y_), z(z_)
{
}

Quaternion::Quaternion(const Quaternion_t &q)
    : w(q.w), x(q.x), y(q.y), z(q.z)
{
}

Quaternion_t Quaternion::toC() const
{
    Quaternion_t q;
    q.w = w; q.x = x; q.y = y; q.z = z;
    return q;
}

Quaternion &Quaternion::operator=(const Quaternion_t &q)
{
    w = q.w; x = q.x; y = q.y; z = q.z;
    return *this;
}

Quaternion Quaternion::identity()
{
    return Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
}

Quaternion Quaternion::fromEulerZYX(float roll, float pitch, float yaw)
{
    float cr = std::cos(roll  * 0.5f);
    float sr = std::sin(roll  * 0.5f);
    float cp = std::cos(pitch * 0.5f);
    float sp = std::sin(pitch * 0.5f);
    float cy = std::cos(yaw   * 0.5f);
    float sy = std::sin(yaw   * 0.5f);

    /*
     * q = q_roll(x) * q_pitch(y) * q_yaw(z)
     * 展开 Hamilton 乘积得到闭合形式:
     */
    return Quaternion(
        cr * cp * cy + sr * sp * sy,   /* w */
        sr * cp * cy - cr * sp * sy,   /* x */
        cr * sp * cy + sr * cp * sy,   /* y */
        cr * cp * sy - sr * sp * cy    /* z */
    );
}

float Quaternion::norm() const
{
    return std::sqrt(w * w + x * x + y * y + z * z);
}

float Quaternion::magnitude() const
{
    return norm();
}

Quaternion &Quaternion::normalize()
{
    float n2 = w * w + x * x + y * y + z * z;
    if (n2 > 1e-12f) {
        float inv = 1.0f / std::sqrt(n2);
        w *= inv; x *= inv; y *= inv; z *= inv;
    }
    return *this;
}

Quaternion Quaternion::normalized() const
{
    Quaternion result(*this);
    result.normalize();
    return result;
}

bool Quaternion::isNormalized(float epsilon) const
{
    float diff = norm() - 1.0f;
    if (diff < 0.0f) diff = -diff;
    return diff < epsilon;
}

Quaternion Quaternion::conjugate() const
{
    return Quaternion(w, -x, -y, -z);
}

Quaternion Quaternion::inverse() const
{
    float n2 = w * w + x * x + y * y + z * z;
    if (n2 > 1e-12f) {
        float inv = 1.0f / n2;
        return Quaternion(w * inv, -x * inv, -y * inv, -z * inv);
    }
    /* 零四元数无逆，返回共轭（即自身） */
    return conjugate();
}

float Quaternion::dot(const Quaternion &other) const
{
    return w * other.w + x * other.x + y * other.y + z * other.z;
}

void Quaternion::toEulerZYX(float &roll, float &pitch, float &yaw) const
{
    float xy = x * y, wz = w * z;
    float wy = w * y, xz = x * z;
    float wx = w * x, yz = y * z;
    float xx = x * x, yy = y * y, zz = z * z;

    float sin_pitch = 2.0f * (wy - xz);

    /* Clamp 到 [-1, 1]，防止 asinf 参数因浮点误差越界 */
    if (sin_pitch >  1.0f) { sin_pitch =  1.0f; }
    if (sin_pitch < -1.0f) { sin_pitch = -1.0f; }

    pitch = std::asin(sin_pitch);

    if (std::fabs(sin_pitch) > 0.9999f) {
        /* 万向节死锁：令 yaw = 0 */
        yaw = 0.0f;
        roll = std::atan2(2.0f * (wx - yz), 1.0f - 2.0f * (xx + zz));
    } else {
        roll = std::atan2(2.0f * (wx + yz), 1.0f - 2.0f * (xx + yy));
        yaw  = std::atan2(2.0f * (wz + xy), 1.0f - 2.0f * (yy + zz));
    }
}

void Quaternion::toRotationMatrix(float m[3][3]) const
{
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

void Quaternion::rotateVector(const float v[3], float result[3]) const
{
    float wx = w * x, wy = w * y, wz = w * z;
    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, xz = x * z, yz = y * z;
    float vx = v[0], vy = v[1], vz = v[2];

    /*
     * v' = q * (0, v) * q*  的展开代数形式
     * 等价于用旋转矩阵乘向量，但避免了构建完整矩阵
     */
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

Quaternion Quaternion::slerp(const Quaternion &to, float t) const
{
    const float THRESHOLD = 0.9995f;  /* 约 1.8 度，小于此角退化为 Lerp */

    float d = dot(to);

    /* 取最短路径（若点积为负则翻转 q2） */
    Quaternion q2 = to;
    if (d < 0.0f) {
        d = -d;
        q2 = Quaternion(-to.w, -to.x, -to.y, -to.z);
    }

    /* 夹角极小：退化为线性插值 + 归一化 */
    if (d > THRESHOLD) {
        float t1 = 1.0f - t;
        Quaternion result(
            t1 * w + t * q2.w,
            t1 * x + t * q2.x,
            t1 * y + t * q2.y,
            t1 * z + t * q2.z
        );
        result.normalize();
        return result;
    }

    /* 标准 SLERP */
    float omega     = std::acos(d);
    float sin_omega = std::sin(omega);
    float s0 = std::sin((1.0f - t) * omega) / sin_omega;  /* q1 的系数 */
    float s1 = std::sin(t * omega)           / sin_omega;  /* q2 的系数 */

    return Quaternion(
        s0 * w + s1 * q2.w,
        s0 * x + s1 * q2.x,
        s0 * y + s1 * q2.y,
        s0 * z + s1 * q2.z
    );
}

Quaternion Quaternion::operator+(const Quaternion &o) const
{
    return Quaternion(w + o.w, x + o.x, y + o.y, z + o.z);
}

Quaternion Quaternion::operator-(const Quaternion &o) const
{
    return Quaternion(w - o.w, x - o.x, y - o.y, z - o.z);
}

Quaternion Quaternion::operator*(const Quaternion &o) const
{
    /*
     * Hamilton 乘积:
     *   r.w = w1*w2 - x1*x2 - y1*y2 - z1*z2
     *   r.x = w1*x2 + x1*w2 + y1*z2 - z1*y2
     *   r.y = w1*y2 - x1*z2 + y1*w2 + z1*x2
     *   r.z = w1*z2 + x1*y2 - y1*x2 + z1*w2
     */
    return Quaternion(
        w * o.w - x * o.x - y * o.y - z * o.z,
        w * o.x + x * o.w + y * o.z - z * o.y,
        w * o.y - x * o.z + y * o.w + z * o.x,
        w * o.z + x * o.y - y * o.x + z * o.w
    );
}

Quaternion Quaternion::operator*(float s) const
{
    return Quaternion(w * s, x * s, y * s, z * s);
}

Quaternion &Quaternion::operator+=(const Quaternion &o)
{
    w += o.w; x += o.x; y += o.y; z += o.z;
    return *this;
}

Quaternion &Quaternion::operator-=(const Quaternion &o)
{
    w -= o.w; x -= o.x; y -= o.y; z -= o.z;
    return *this;
}

Quaternion &Quaternion::operator*=(const Quaternion &o)
{
    *this = *this * o;
    return *this;
}

bool Quaternion::operator==(const Quaternion &o) const
{
    return w == o.w && x == o.x && y == o.y && z == o.z;
}

bool Quaternion::operator!=(const Quaternion &o) const
{
    return !(*this == o);
}

Quaternion operator*(float s, const Quaternion &q)
{
    return q * s;
}
