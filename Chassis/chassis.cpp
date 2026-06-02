/**
 * @file    chassis.cpp
 * @author  liqun
 * @brief   通用底盘运动控制库 —— 类实现
 * @version 1.0
 * @date    2026-06-02
 *
 * @copyright Copyright (c) 2026
 *
 * 麦轮、三轮全向、四轮全向、舵轮底盘的正逆运动学与控制实现。
 */

#include "chassis.hpp"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ============================================================
 *   Chassis 基类实现
 * ============================================================ */

/** @brief 出生的时候默认趴着不动 */
Chassis::Chassis() : speed_x_(0.0f), speed_y_(0.0f), speed_w_(0.0f), is_stopped_(true) {}

/** @brief 析构函数：规矩要有 */
Chassis::~Chassis() = default;

/**
 * @brief 只要有一个速度分量不为0，而且没踩刹车，就是在动
 * @return true 表示底盘还在运动
 */
bool Chassis::IsMoving() const {
    return (speed_x_ != 0.0f || speed_y_ != 0.0f || speed_w_ != 0.0f) && !is_stopped_;
}

/**
 * @brief 把当前的目标速度塞给你（通过引用传出去）
 * @param x 输出：前进速度 (m/s)
 * @param y 输出：横移速度 (m/s)
 * @param w 输出：旋转角速度 (rad/s)
 */
void Chassis::GetSpeed(float& x, float& y, float& w) const {
    x = speed_x_;
    y = speed_y_;
    w = speed_w_;
}

/* ============================================================
 *   MecanumChassis 实现 —— 横着走的螃蟹步 🦀
 * ============================================================ */

/**
 * @brief 构造麦轮底盘：给4个电机发身份证，记下轮子大小和轴距
 * @param ids    4个电机ID [FL, FR, BL, BR]
 * @param radius 轮子半径 R (m)
 * @param base_x X方向半轴距 Lx (m)
 * @param base_y Y方向半轴距 Ly (m)
 */
MecanumChassis::MecanumChassis(const int ids[4], float radius, float base_x, float base_y)
    : wheel_radius_(radius), wheel_base_x_(base_x), wheel_base_y_(base_y),
      combined_base_(base_x + base_y) {
    for (int i = 0; i < 4; ++i) {
        motor_[i] = Motor(ids[i]);
    }
}

/**
 * @brief 逆运动学：告诉我想怎么跑（vx, vy, wz），我算出4个电机该转多快
 *
 * 公式来源：麦轮的经典运动学，L = Lx + Ly
 * @code
 * ω0 = (vx - vy - ω·L) / R   // FL
 * ω1 = (vx + vy + ω·L) / R   // FR
 * ω2 = (vx + vy - ω·L) / R   // BL
 * ω3 = (vx - vy + ω·L) / R   // BR
 * @endcode
 *
 * @param vx      输入：前进速度 (m/s)
 * @param vy      输入：横移速度 (m/s)
 * @param wz      输入：旋转角速度 (rad/s)
 * @param out_rpm 输出：4个电机的目标角速度 (rad/s)
 */
void MecanumChassis::InverseKinematics(float vx, float vy, float wz, float out_rpm[4]) const {
    float L = combined_base_;
    float R = wheel_radius_;
    out_rpm[0] = (vx - vy - wz * L) / R;  // FL
    out_rpm[1] = (vx + vy + wz * L) / R;  // FR
    out_rpm[2] = (vx + vy - wz * L) / R;  // BL
    out_rpm[3] = (vx - vy + wz * L) / R;  // BR
}

/**
 * @brief 正运动学：偷看4个电机的当前转速，反推底盘实际在怎么动
 *
 * 这是逆运动学方程组的求解过程，数学推导略（相信你也不想看 😅）
 * @code
 * vx =  R/4 · ( s0 + s1 + s2 + s3)
 * vy =  R/4 · (-s0 + s1 + s2 - s3)
 * ωz = R/4L · (-s0 + s1 - s2 + s3)
 * @endcode
 *
 * @param vx 输出：前进速度 (m/s)
 * @param vy 输出：横移速度 (m/s)
 * @param wz 输出：旋转角速度 (rad/s)
 */
void MecanumChassis::ForwardKinematics(float& vx, float& vy, float& wz) const {
    float R = wheel_radius_;
    float L = combined_base_;
    float s0 = motor_[0].getRpm();
    float s1 = motor_[1].getRpm();
    float s2 = motor_[2].getRpm();
    float s3 = motor_[3].getRpm();

    vx =  R / 4.0f * ( s0 + s1 + s2 + s3);
    vy =  R / 4.0f * (-s0 + s1 + s2 - s3);
    wz =  R / (4.0f * L) * (-s0 + s1 - s2 + s3);
}

/**
 * @brief 轮速限幅：如果某个轮子飙得太快，大家一起等比例降速
 *
 * 这样虽然慢一点，但运动方向不会歪，体验丝滑 🎢
 * @param rpm     输入输出：4个电机的转速数组 (rad/s)
 * @param max_rpm 最大允许转速 (rad/s)
 */
void MecanumChassis::ClampWheelSpeed(float rpm[4], float max_rpm) const {
    float max_val = 0.0f;
    for (int i = 0; i < 4; ++i) {
        float abs_val = std::fabs(rpm[i]);
        if (abs_val > max_val) max_val = abs_val;
    }
    if (max_val > max_rpm && max_val > 0.0f) {
        float scale = max_rpm / max_val;
        for (int i = 0; i < 4; ++i) {
            rpm[i] *= scale;
        }
    }
}

/**
 * @brief 设置底盘速度：先记下来，然后逆解算、限幅、下发给4个电机，一气呵成
 * @param x 前进速度 (m/s)
 * @param y 横移速度 (m/s)
 * @param w 旋转角速度 (rad/s)
 */
void MecanumChassis::SetSpeed(float x, float y, float w) {
    speed_x_ = x;
    speed_y_ = y;
    speed_w_ = w;
    is_stopped_ = false;

    float rpm[4];
    InverseKinematics(x, y, w, rpm);
    ClampWheelSpeed(rpm, 10.0f);  // 默认限速 10 rad/s，别浪

    for (int i = 0; i < 4; ++i) {
        motor_[i].setRpm(rpm[i]);
    }
}

/** @brief 刹车！目标速度归零，所有电机也归零 */
void MecanumChassis::Stop() {
    speed_x_ = speed_y_ = speed_w_ = 0.0f;
    is_stopped_ = true;
    for (int i = 0; i < 4; ++i) {
        motor_[i].setRpm(0.0f);
    }
}

/* ============================================================
 *   Omni3Chassis 实现 —— 三轮全向，小而美 🔺
 * ============================================================ */

/**
 * @brief 构造三轮全向底盘：3个电机、轮子半径、中心到轮距
 * @param ids    3个电机ID
 * @param radius 轮子半径 R (m)
 * @param base   中心到轮子的距离 L (m)
 */
Omni3Chassis::Omni3Chassis(const int ids[3], float radius, float base)
    : wheel_radius_(radius), wheel_base_(base) {
    for (int i = 0; i < 3; ++i) {
        motor_[i] = Motor(ids[i]);
    }
}

/**
 * @brief 逆运动学：给定目标速度，算出3个电机的转速
 *
 * 三个轮子均匀分布在 0°、120°、240°，驱动方向为切向 (θ+90°)。
 * 每个轮子的速度 = (-vx·sinθ + vy·cosθ + ω·L) / R
 *
 * @param vx      输入：前进速度 (m/s)
 * @param vy      输入：横移速度 (m/s)
 * @param wz      输入：旋转角速度 (rad/s)
 * @param out_rpm 输出：3个电机的目标角速度 (rad/s)
 */
void Omni3Chassis::InverseKinematics(float vx, float vy, float wz, float out_rpm[3]) const {
    float L = wheel_base_;
    float R = wheel_radius_;
    const float sqrt3_2 = 0.86602540378f;  /**< √3 / 2 */

    out_rpm[0] = (          vy + wz * L) / R;              // θ=0°
    out_rpm[1] = (-vx * sqrt3_2 - vy * 0.5f + wz * L) / R; // θ=120°
    out_rpm[2] = ( vx * sqrt3_2 - vy * 0.5f + wz * L) / R; // θ=240°
}

/**
 * @brief 正运动学：看3个电机现在的转速，反推底盘实际速度
 *
 * 解个三元一次方程组而已，高中数学 😎
 * @code
 * vx = (s2 - s1) · R / √3
 * vy = (2·s0 - s1 - s2) · R / 3
 * ωz = (s0 + s1 + s2) · R / (3·L)
 * @endcode
 *
 * @param vx 输出：前进速度 (m/s)
 * @param vy 输出：横移速度 (m/s)
 * @param wz 输出：旋转角速度 (rad/s)
 */
void Omni3Chassis::ForwardKinematics(float& vx, float& vy, float& wz) const {
    float R = wheel_radius_;
    float L = wheel_base_;
    float s0 = motor_[0].getRpm();
    float s1 = motor_[1].getRpm();
    float s2 = motor_[2].getRpm();
    const float sqrt3 = 1.73205080757f;

    vx = (s2 - s1) * R / sqrt3;
    vy = (2.0f * s0 - s1 - s2) * R / 3.0f;
    wz = (s0 + s1 + s2) * R / (3.0f * L);
}

/**
 * @brief 轮速限幅：限速保平安，等比例缩放
 * @param rpm     输入输出：3个电机的转速数组 (rad/s)
 * @param max_rpm 最大允许转速 (rad/s)
 */
void Omni3Chassis::ClampWheelSpeed(float rpm[3], float max_rpm) const {
    float max_val = 0.0f;
    for (int i = 0; i < 3; ++i) {
        float abs_val = std::fabs(rpm[i]);
        if (abs_val > max_val) max_val = abs_val;
    }
    if (max_val > max_rpm && max_val > 0.0f) {
        float scale = max_rpm / max_val;
        for (int i = 0; i < 3; ++i) {
            rpm[i] *= scale;
        }
    }
}

/**
 * @brief 设置速度：同样的配方——逆解、限幅、下发
 * @param x 前进速度 (m/s)
 * @param y 横移速度 (m/s)
 * @param w 旋转角速度 (rad/s)
 */
void Omni3Chassis::SetSpeed(float x, float y, float w) {
    speed_x_ = x;
    speed_y_ = y;
    speed_w_ = w;
    is_stopped_ = false;

    float rpm[3];
    InverseKinematics(x, y, w, rpm);
    ClampWheelSpeed(rpm, 10.0f);

    for (int i = 0; i < 3; ++i) {
        motor_[i].setRpm(rpm[i]);
    }
}

/** @brief 停下！三轮全歇 */
void Omni3Chassis::Stop() {
    speed_x_ = speed_y_ = speed_w_ = 0.0f;
    is_stopped_ = true;
    for (int i = 0; i < 3; ++i) {
        motor_[i].setRpm(0.0f);
    }
}

/* ============================================================
 *   Omni4Chassis 实现 —— 四轮全向，稳得一批 🛡️
 * ============================================================ */

/**
 * @brief 构造四轮全向底盘：4个电机、轮子半径、前后/左右半轴距
 * @param ids    4个电机ID [FL, FR, BL, BR]
 * @param radius 轮子半径 R (m)
 * @param base_x X方向半轴距 Lx (m)
 * @param base_y Y方向半轴距 Ly (m)
 */
Omni4Chassis::Omni4Chassis(const int ids[4], float radius, float base_x, float base_y)
    : wheel_radius_(radius), wheel_base_x_(base_x), wheel_base_y_(base_y),
      diagonal_base_(std::sqrt(base_x * base_x + base_y * base_y)) {
    for (int i = 0; i < 4; ++i) {
        motor_[i] = Motor(ids[i]);
    }
}

/**
 * @brief 逆运动学：X型四轮全向的独门秘籍
 *
 * 四个轮子驱动方向分别为 45°、135°、225°、315°。
 * 每个轮子的转速 = (vx·cosα + vy·sinα + ω·L) / R
 *
 * @param vx      输入：前进速度 (m/s)
 * @param vy      输入：横移速度 (m/s)
 * @param wz      输入：旋转角速度 (rad/s)
 * @param out_rpm 输出：4个电机的目标角速度 (rad/s)
 */
void Omni4Chassis::InverseKinematics(float vx, float vy, float wz, float out_rpm[4]) const {
    float L = diagonal_base_;
    float R = wheel_radius_;
    const float inv_sqrt2 = 0.70710678118f;  /**< 1/√2 */

    out_rpm[0] = (( vx + vy) * inv_sqrt2 + wz * L) / R;  // FL: 45°
    out_rpm[1] = ((-vx + vy) * inv_sqrt2 + wz * L) / R;  // FR: 135°
    out_rpm[2] = ((-vx - vy) * inv_sqrt2 + wz * L) / R;  // BL: 225°
    out_rpm[3] = (( vx - vy) * inv_sqrt2 + wz * L) / R;  // BR: 315°
}

/**
 * @brief 正运动学：把4个电机的转速翻译回底盘速度
 *
 * 公式来源：解上面那个4元一次方程组（线性代数出场 ✨）
 * @code
 * vx = (s0 - s1 - s2 + s3) · R / (2·√2)
 * vy = (s0 + s1 - s2 - s3) · R / (2·√2)
 * ωz = (s0 + s1 + s2 + s3) · R / (4·L)
 * @endcode
 *
 * @param vx 输出：前进速度 (m/s)
 * @param vy 输出：横移速度 (m/s)
 * @param wz 输出：旋转角速度 (rad/s)
 */
void Omni4Chassis::ForwardKinematics(float& vx, float& vy, float& wz) const {
    float R = wheel_radius_;
    float L = diagonal_base_;
    float s0 = motor_[0].getRpm();
    float s1 = motor_[1].getRpm();
    float s2 = motor_[2].getRpm();
    float s3 = motor_[3].getRpm();
    const float sqrt2 = 1.41421356237f;

    vx = ( s0 - s1 - s2 + s3) * R / (2.0f * sqrt2);
    vy = ( s0 + s1 - s2 - s3) * R / (2.0f * sqrt2);
    wz = ( s0 + s1 + s2 + s3) * R / (4.0f * L);
}

/**
 * @brief 老规矩：谁最快就按谁来等比例降速
 * @param rpm     输入输出：4个电机的转速数组 (rad/s)
 * @param max_rpm 最大允许转速 (rad/s)
 */
void Omni4Chassis::ClampWheelSpeed(float rpm[4], float max_rpm) const {
    float max_val = 0.0f;
    for (int i = 0; i < 4; ++i) {
        float abs_val = std::fabs(rpm[i]);
        if (abs_val > max_val) max_val = abs_val;
    }
    if (max_val > max_rpm && max_val > 0.0f) {
        float scale = max_rpm / max_val;
        for (int i = 0; i < 4; ++i) {
            rpm[i] *= scale;
        }
    }
}

/**
 * @brief 设置速度：熟悉的流程，四轮版
 * @param x 前进速度 (m/s)
 * @param y 横移速度 (m/s)
 * @param w 旋转角速度 (rad/s)
 */
void Omni4Chassis::SetSpeed(float x, float y, float w) {
    speed_x_ = x;
    speed_y_ = y;
    speed_w_ = w;
    is_stopped_ = false;

    float rpm[4];
    InverseKinematics(x, y, w, rpm);
    ClampWheelSpeed(rpm, 10.0f);

    for (int i = 0; i < 4; ++i) {
        motor_[i].setRpm(rpm[i]);
    }
}

/** @brief 停下！四轮全停 */
void Omni4Chassis::Stop() {
    speed_x_ = speed_y_ = speed_w_ = 0.0f;
    is_stopped_ = true;
    for (int i = 0; i < 4; ++i) {
        motor_[i].setRpm(0.0f);
    }
}

/* ============================================================
 *   SteeringModule 实现 —— 每个轮子都是独立的司机 🚗
 * ============================================================ */

/**
 * @brief 出生：领个身份证（电机ID），方向默认朝前（0弧度）
 * @param id 驱动电机的ID
 */
SteeringModule::SteeringModule(int id)
    : drive_motor_(id), target_angle_(0.0f), current_angle_(0.0f) {}

/** @brief 踩油门！设置驱动转速 */
void SteeringModule::setSpeed(float rpm) { drive_motor_.setRpm(rpm); }

/** @brief 打方向盘！设置目标转向角 */
void SteeringModule::setAngle(float angle_rad) { target_angle_ = angle_rad; }

/** @return 当前油门踩了多少 (rad/s) */
float SteeringModule::getSpeed() const { return drive_motor_.getRpm(); }

/** @return 方向盘想打到哪里 (rad) */
float SteeringModule::getTargetAngle() const { return target_angle_; }

/** @return 方向盘实际打到哪里了 (rad) */
float SteeringModule::getCurrentAngle() const { return current_angle_; }

/**
 * @brief 外部编码器来报数："报告！我现在转到 xxx 弧度了"
 * @param angle_rad 实际角度 (rad)
 */
void SteeringModule::updateCurrentAngle(float angle_rad) { current_angle_ = angle_rad; }

/** @return 查身份证（电机ID） */
int SteeringModule::getId() const { return drive_motor_.getId(); }

/* ============================================================
 *   SwerveChassis 实现 —— 4个独立司机的豪华车队 🏎️🏎️🏎️🏎️
 * ============================================================ */

/**
 * @brief 构造舵轮底盘：4个模块、轮子半径、前后/左右半轴距
 * @param ids    4个模块的电机ID [FL, FR, BL, BR]
 * @param radius 轮子半径 R (m)
 * @param base_x X方向半轴距 Lx (m)
 * @param base_y Y方向半轴距 Ly (m)
 */
SwerveChassis::SwerveChassis(const int ids[4], float radius, float base_x, float base_y)
    : wheel_radius_(radius), wheel_base_x_(base_x), wheel_base_y_(base_y) {
    for (int i = 0; i < 4; ++i) {
        module_[i] = SteeringModule(ids[i]);
    }
}

/**
 * @brief 把每个模块的目标状态（速度和角度）打包给你
 * @param out_speed 输出：4个模块的驱动转速 (rad/s)
 * @param out_angle 输出：4个模块的目标转向角 (rad)
 */
void SwerveChassis::GetModuleStates(float out_speed[4], float out_angle[4]) const {
    for (int i = 0; i < 4; ++i) {
        out_speed[i] = module_[i].getSpeed();
        out_angle[i] = module_[i].getTargetAngle();
    }
}

/**
 * @brief 正运动学：已知每个轮子的实际转向角，反推底盘速度
 *
 * 用最小二乘的思路：把4个轮子的贡献平均一下。
 * 每个轮子的线速度 = 角速度 × 半径，再分解到X/Y方向，
 * 旋转分量用 r × v / |r|² 来算。
 *
 * @param angle 输入：4个轮子的实际转向角 (rad)
 * @param vx    输出：前进速度 (m/s)
 * @param vy    输出：横移速度 (m/s)
 * @param wz    输出：旋转角速度 (rad/s)
 */
void SwerveChassis::ForwardKinematics(const float angle[4], float& vx, float& vy, float& wz) const {
    float R = wheel_radius_;
    float Lx = wheel_base_x_;
    float Ly = wheel_base_y_;

    float vx_sum = 0.0f;
    float vy_sum = 0.0f;
    float w_sum = 0.0f;

    /**< 四个模块的站位坐标（从底盘中心看） */
    const float pos_x[4] = {-Lx, Lx, -Lx, Lx};
    const float pos_y[4] = { Ly, Ly, -Ly, -Ly};

    for (int i = 0; i < 4; ++i) {
        float v = module_[i].getSpeed() * R;  /**< 线速度 = 角速度 × 半径 */
        float theta = angle[i];
        float vxi = v * std::cos(theta);      /**< X方向分量 */
        float vyi = v * std::sin(theta);      /**< Y方向分量 */

        vx_sum += vxi;
        vy_sum += vyi;
        /**< 旋转角速度贡献：r × v / |r|²，四个求和后取平均 */
        w_sum += (vyi * pos_x[i] - vxi * pos_y[i]) / (Lx * Lx + Ly * Ly);
    }

    vx = vx_sum / 4.0f;
    vy = vy_sum / 4.0f;
    wz = w_sum / 4.0f;
}

/**
 * @brief 逆运动学+下发：给定底盘目标速度，算出每个模块该干嘛
 *
 * 核心思路：把底盘速度拆到每个轮子所在位置的速度矢量，
 * 然后算出该轮应该朝哪转、转多快。
 *
 * @param x 前进速度 (m/s)
 * @param y 横移速度 (m/s)
 * @param w 旋转角速度 (rad/s)
 */
void SwerveChassis::SetSpeed(float x, float y, float w) {
    speed_x_ = x;
    speed_y_ = y;
    speed_w_ = w;
    is_stopped_ = false;

    float Lx = wheel_base_x_;
    float Ly = wheel_base_y_;
    /**< 四个模块的站位坐标 */
    const float pos_x[4] = {-Lx, Lx, -Lx, Lx};
    const float pos_y[4] = { Ly, Ly, -Ly, -Ly};

    for (int i = 0; i < 4; ++i) {
        /**< 第i个轮子位置应有的速度矢量 = 平移 + 旋转切向速度 */
        float v_xi = x - w * pos_y[i];  /**< X方向：前进 + 旋转贡献 */
        float v_yi = y + w * pos_x[i];  /**< Y方向：横移 + 旋转贡献 */

        float v_mag = std::sqrt(v_xi * v_xi + v_yi * v_yi);  /**< 速度大小 → 驱动转速 */
        float v_angle = std::atan2(v_yi, v_xi);              /**< 速度方向 → 转向角度 */

        module_[i].setSpeed(v_mag / wheel_radius_);
        module_[i].setAngle(v_angle);
    }
}

/**
 * @brief 刹车！驱动电机归零，但转向角度保持不动
 *
 * 突然回正可能会晃，所以转向角度保持原样，只停驱动。
 */
void SwerveChassis::Stop() {
    speed_x_ = speed_y_ = speed_w_ = 0.0f;
    is_stopped_ = true;
    for (int i = 0; i < 4; ++i) {
        module_[i].setSpeed(0.0f);
    }
}
