/**
 * @file kinematics.cpp
 * @author liqun
 * @brief 麦轮运动学解算
 * @version 0.1
 * @date 2026-05-31
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "kinematics.hpp"
#include <cmath>
#include <algorithm>

/**
 * @brief 构造函数
 * @param wheel_radius   轮子半径 (m)
 * @param wheel_base_x   X方向半轴距 (m)
 * @param wheel_base_y   Y方向半轴距 (m)
 * @param max_speed      轮速上限 (rad/s)
 */
MecanumKinematics::MecanumKinematics(float wheel_radius,
                                     float wheel_base_x,
                                     float wheel_base_y,
                                     float max_speed)
    : wheel_radius_(wheel_radius)
    , wheel_base_x_(wheel_base_x)
    , wheel_base_y_(wheel_base_y)
    , max_speed_(max_speed)
    , motor_callback_(nullptr)
{
}

void MecanumKinematics::setWheelRadius(float r)
{
    wheel_radius_ = r;
}

void MecanumKinematics::setWheelBase(float lx, float ly)
{
    wheel_base_x_ = lx;
    wheel_base_y_ = ly;
}

void MecanumKinematics::setMaxSpeed(float max_speed)
{
    max_speed_ = max_speed;
}

/**
 * @brief 逆运动学解算：底盘速度 -> 四个麦轮角速度（返回值版本）
 * @param cmd  底盘目标速度指令
 * @return     四个轮子的目标角速度
 */
MecanumKinematics::WheelState
MecanumKinematics::backward(const Command &cmd) const
{
    WheelState out;
    backward(cmd, out);
    return out;
}

/**
 * @brief 逆运动学解算：底盘速度 -> 四个麦轮角速度（引用输出版本）
 * @param cmd  底盘目标速度指令
 * @param out  输出：四个轮子的目标角速度
 */
void MecanumKinematics::backward(const Command &cmd, WheelState &out) const
{
    float vx = cmd.vx;
    float vy = cmd.vy;
    float wz = cmd.wz;
    float L  = combinedBase();
    float inv_R = 1.0f / wheel_radius_;

    out.speed[FRONT_LEFT]  = (vx - vy - wz * L) * inv_R;
    out.speed[FRONT_RIGHT] = (vx + vy + wz * L) * inv_R;
    out.speed[REAR_LEFT]   = (vx + vy - wz * L) * inv_R;
    out.speed[REAR_RIGHT]  = (vx - vy + wz * L) * inv_R;
}

/**
 * @brief 正运动学解算：四个麦轮角速度 -> 底盘速度（返回值版本）
 * @param state  四个轮子的当前角速度
 * @return       底盘的实际速度
 */
MecanumKinematics::Odometry
MecanumKinematics::forward(const WheelState &state) const
{
    Odometry out;
    forward(state, out);
    return out;
}

/**
 * @brief 正运动学解算：四个麦轮角速度 -> 底盘速度（引用输出版本）
 * @param state  四个轮子的当前角速度
 * @param out    输出：底盘的实际速度
 */
void MecanumKinematics::forward(const WheelState &state, Odometry &out) const
{
    float s0 = state.speed[0];
    float s1 = state.speed[1];
    float s2 = state.speed[2];
    float s3 = state.speed[3];
    float R  = wheel_radius_;
    float L  = combinedBase();

    out.vx = (R * 0.25f)  * ( s0 + s1 + s2 + s3);
    out.vy = (R * 0.25f)  * (-s0 + s1 + s2 - s3);
    out.wz = (R / (4.0f * L)) * (-s0 + s1 - s2 + s3);
}

/**
 * @brief 轮速限幅：等比例缩放，保持运动方向不变
 * @param state  输入/输出：轮速状态，原地修改
 * @return       true 表示发生了限幅裁剪
 */
bool MecanumKinematics::clamp(WheelState &state) const
{
    float max_abs = 0.0f;

    for (int i = 0; i < 4; i++) {
        float abs_val = std::fabs(state.speed[i]);
        if (abs_val > max_abs) {
            max_abs = abs_val;
        }
    }

    if (max_abs > max_speed_) {
        float scale = max_speed_ / max_abs;
        for (int i = 0; i < 4; i++) {
            state.speed[i] *= scale;
        }
        return true;
    }
    return false;
}

/**
 * @brief 注册底层电机控制回调
 * @param cb  回调函数，签名 void(uint8_t motor_id, float speed_rad_per_s)
 */
void MecanumKinematics::setMotorCallback(MotorCallback cb)
{
    motor_callback_ = std::move(cb);
}

/**
 * @brief 设置单个电机转速（通过回调下发）
 * @param motor_id  电机编号 (0=FL, 1=FR, 2=BL, 3=BR)
 * @param speed     目标角速度 (rad/s)
 */
void MecanumKinematics::setMotorSpeed(uint8_t motor_id, float speed) const
{
    if (motor_callback_) {
        motor_callback_(motor_id, speed);
    }
}

/**
 * @brief 批量下发全部电机转速
 * @param state  待下发的轮速状态
 */
void MecanumKinematics::setAllMotorSpeeds(const WheelState &state) const
{
    if (motor_callback_) {
        for (uint8_t i = 0; i < 4; i++) {
            motor_callback_(i, state.speed[i]);
        }
    }
}

/**
 * @brief 一步到位：逆解算 -> 限幅 -> 下发电机
 * @param cmd  底盘目标速度指令
 */
void MecanumKinematics::execute(const Command &cmd)
{
    WheelState state = backward(cmd);
    clamp(state);
    setAllMotorSpeeds(state);
}
