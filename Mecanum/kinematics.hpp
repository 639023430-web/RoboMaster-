/**
 * @file kinematics.hpp
 * @author liqun
 * @brief 麦轮运动学解算
 * @version 0.1
 * @date 2026-05-31
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef __KINEMATICS_HPP__
#define __KINEMATICS_HPP__

#include <cstdint>
#include <array>
#include <functional>
class MecanumKinematics {
public:
    /* 底盘目标速度 */
    struct Command {
        float vx = 0.0f;  /* X方向线速度 (m/s) */
        float vy = 0.0f;  /* Y方向线速度 (m/s) */
        float wz = 0.0f;  /* Z轴角速度 (rad/s) */
    };

    /* 底盘实际速度（里程计） */
    struct Odometry {
        float vx = 0.0f;
        float vy = 0.0f;
        float wz = 0.0f;
    };

    /* 轮速状态 */
    struct WheelState {
        float speed[4] = {0};  /* 四个麦轮角速度 (rad/s) */
    };

    /* 电机编号 */
    enum MotorID : uint8_t {
        FRONT_LEFT  = 0,
        FRONT_RIGHT = 1,
        REAR_LEFT   = 2,
        REAR_RIGHT  = 3
    };

    /* 电机控制回调类型：void(uint8_t motor_id, float speed_rad_per_s) */
    using MotorCallback = std::function<void(uint8_t, float)>;

    /**
     * @brief 构造函数
     * @param wheel_radius   轮子半径 (m)
     * @param wheel_base_x   X方向半轴距 (m)
     * @param wheel_base_y   Y方向半轴距 (m)
     * @param max_speed      轮速上限 (rad/s)
     */
    MecanumKinematics(float wheel_radius  = 0.05f,
                      float wheel_base_x = 0.125f,
                      float wheel_base_y = 0.125f,
                      float max_speed    = 10.0f);

    /* ============== 参数设置 ============== */
    void setWheelRadius(float r);
    void setWheelBase(float lx, float ly);
    void setMaxSpeed(float max_speed);
    float wheelRadius()   const { return wheel_radius_; }
    float combinedBase()  const { return wheel_base_x_ + wheel_base_y_; }
    float maxSpeed()      const { return max_speed_; }

    /* ============== 运动学解算 ============== */

    /**
     * @brief 逆运动学：底盘速度 -> 四个麦轮角速度
     */
    WheelState backward(const Command &cmd) const;
    void backward(const Command &cmd, WheelState &out) const;

    /**
     * @brief 正运动学：四个麦轮角速度 -> 底盘速度
     */
    Odometry forward(const WheelState &state) const;
    void forward(const WheelState &state, Odometry &out) const;

    /* ============== 轮速限幅 ============== */

    /**
     * @brief 等比例限幅，保持运动方向不变
     * @return true 表示发生了限幅裁剪
     */
    bool clamp(WheelState &state) const;

    /* ============== 电机控制 ============== */

    /**
     * @brief 注册底层电机速度控制回调
     *
     * 用户需要实现一个函数/可调用对象，接受 (motor_id, speed_rad_per_s)，
     * 内部完成 CAN/PWM/SPI 等底层通信。
     *
     * 示例：
     *   kinematics.setMotorCallback([](uint8_t id, float spd) {
     *       can_send(0x200 + id, spd);
     *   });
     */
    void setMotorCallback(MotorCallback cb);

    /**
     * @brief 设置单个电机转速（通过回调下发）
     */
    void setMotorSpeed(uint8_t motor_id, float speed) const;

    /**
     * @brief 批量下发全部电机转速
     */
    void setAllMotorSpeeds(const WheelState &state) const;

    /* ============== 高层控制接口 ============== */

    /**
     * @brief 一步到位：输入底盘速度指令，自动完成
     *        逆解算 -> 限幅 -> 下发电机
     */
    void execute(const Command &cmd);

private:
    float wheel_radius_;
    float wheel_base_x_;
    float wheel_base_y_;
    float max_speed_;
    MotorCallback motor_callback_;
};

#endif /* __KINEMATICS_HPP__ */
