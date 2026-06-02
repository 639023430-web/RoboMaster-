/**
 * @file    chassis.hpp
 * @author  liqun
 * @brief   通用底盘运动控制库 —— 类声明与接口
 * @version 1.0
 * @date    2026-06-02
 *
 * @copyright Copyright (c) 2026
 *
 * C++ 面向对象底盘控制库，支持麦轮、三轮全向、四轮全向、舵轮四种构型。
 * 统一基类接口，正逆运动学解算，轮速限幅保护。
 *
 * 无异常、无 RTTI、无虚函数（基类除外）—— 可直接在 STM32 等裸机平台使用。
 */

#ifndef __CHASSIS_HPP
#define __CHASSIS_HPP

#include "../Motor/motor.hpp"
#include <cmath>
#include <algorithm>

/**
 * @brief 底盘抽象基类 —— 所有底盘的“老祖宗” 🏎️
 *
 * 不管你是麦轮、全向轮还是舵轮，都得继承我！
 * 这里只定义了规矩（纯虚函数），具体怎么跑，看各家本事。
 */
class Chassis {
protected:
    float speed_x_;   /**< 当前目标前进速度 (m/s) */
    float speed_y_;   /**< 当前目标横移速度 (m/s) */
    float speed_w_;   /**< 当前目标旋转角速度 (rad/s) */
    bool is_stopped_; /**< 刹车标志：true 表示已停车 */

public:
    /** @brief 默认构造：出生就趴着不动（速度全0，刹车状态） */
    Chassis();

    /** @brief 虚析构：虽然没什么要打扫的，但规矩要有 */
    virtual ~Chassis();

    /**
     * @brief 设置底盘目标速度
     * @param x 前进方向速度 (m/s)
     * @param y 横移方向速度 (m/s)
     * @param w 旋转角速度 (rad/s)
     */
    virtual void SetSpeed(float x, float y, float w) = 0;

    /** @brief 急刹车！所有人停下！🛑 */
    virtual void Stop() = 0;

    /**
     * @brief 查看底盘是否还在运动
     * @return true 表示还在动（有速度分量且未刹车）
     */
    bool IsMoving() const;

    /**
     * @brief 获取当前目标速度
     * @param x 输出：前进速度 (m/s)
     * @param y 输出：横移速度 (m/s)
     * @param w 输出：旋转角速度 (rad/s)
     */
    void GetSpeed(float& x, float& y, float& w) const;
};

/**
 * @brief 麦克纳姆轮底盘 —— 横着走都没问题的漂移王 🦀
 *
 * 4个轮子各怀绝技，靠着辊子的斜向摩擦力，实现全向平移+旋转。
 * 常用于 RoboMaster 步兵机器人。
 *
 * 布局（俯视图）：
 * @code
 *        Y+
 *        ^
 *        |
 *   FL(0)●────────●FR(1)
 *        |  车体   |
 *   BL(2)●────────●BR(3)  --> X+
 * @endcode
 */
class MecanumChassis : public Chassis {
private:
    Motor motor_[4];       /**< 4个电机：前左、前右、后左、后右 */
    float wheel_radius_;   /**< 轮子半径 R (m) */
    float wheel_base_x_;   /**< X方向半轴距 Lx (m) */
    float wheel_base_y_;   /**< Y方向半轴距 Ly (m) */
    float combined_base_;  /**< 组合轴距 L = Lx + Ly，用于旋转补偿 */

public:
    /**
     * @brief 构造麦轮底盘
     * @param ids    4个电机的ID数组 [FL, FR, BL, BR]
     * @param radius 轮子半径 R (m)
     * @param base_x X方向半轴距 Lx (m)
     * @param base_y Y方向半轴距 Ly (m)
     */
    MecanumChassis(const int ids[4], float radius, float base_x, float base_y);

    /**
     * @brief 设置底盘目标速度，自动完成逆解算、限幅并下发给电机
     * @param x 前进速度 (m/s)
     * @param y 横移速度 (m/s)
     * @param w 旋转角速度 (rad/s)
     */
    void SetSpeed(float x, float y, float w) override;

    /** @brief 刹车！目标速度归零，所有电机停转 */
    void Stop() override;

    /**
     * @brief 正运动学：根据4个电机当前转速，反推底盘实际速度
     * @param vx 输出：前进速度 (m/s)
     * @param vy 输出：横移速度 (m/s)
     * @param wz 输出：旋转角速度 (rad/s)
     */
    void ForwardKinematics(float& vx, float& vy, float& wz) const;

    /**
     * @brief 逆运动学：根据目标底盘速度，计算4个电机的目标转速
     * @param vx      输入：前进速度 (m/s)
     * @param vy      输入：横移速度 (m/s)
     * @param wz      输入：旋转角速度 (rad/s)
     * @param out_rpm 输出：4个电机的目标角速度 (rad/s)
     */
    void InverseKinematics(float vx, float vy, float wz, float out_rpm[4]) const;

    /**
     * @brief 轮速等比例限幅：保证方向不变，只降速度
     * @param rpm     输入输出：4个电机的转速数组 (rad/s)
     * @param max_rpm 最大允许转速 (rad/s)
     */
    void ClampWheelSpeed(float rpm[4], float max_rpm) const;
};

/**
 * @brief 三轮全向轮底盘 —— 小巧灵活的三角战士 🔺
 *
 * 3个全向轮呈120°均匀分布，结构简单、重量轻，适合小型机器人。
 * 虽然比四轮少一条腿，但全向移动照样溜得很！
 *
 * 布局：三个轮子分别在 0°、120°、240°，驱动方向为切向。
 */
class Omni3Chassis : public Chassis {
private:
    Motor motor_[3];     /**< 3个电机，均匀分布在圆周上 */
    float wheel_radius_; /**< 轮子半径 R (m) */
    float wheel_base_;   /**< 中心到每个轮子的距离 L (m) */

public:
    /**
     * @brief 构造三轮全向底盘
     * @param ids    3个电机的ID数组
     * @param radius 轮子半径 R (m)
     * @param base   中心到轮子的距离 L (m)
     */
    Omni3Chassis(const int ids[3], float radius, float base);

    /**
     * @brief 设置底盘目标速度，自动完成逆解算、限幅并下发
     * @param x 前进速度 (m/s)
     * @param y 横移速度 (m/s)
     * @param w 旋转角速度 (rad/s)
     */
    void SetSpeed(float x, float y, float w) override;

    /** @brief 刹车！三轮全歇 */
    void Stop() override;

    /**
     * @brief 正运动学：根据3个电机转速，反推底盘实际速度
     * @param vx 输出：前进速度 (m/s)
     * @param vy 输出：横移速度 (m/s)
     * @param wz 输出：旋转角速度 (rad/s)
     */
    void ForwardKinematics(float& vx, float& vy, float& wz) const;

    /**
     * @brief 逆运动学：根据目标底盘速度，计算3个电机的目标转速
     * @param vx      输入：前进速度 (m/s)
     * @param vy      输入：横移速度 (m/s)
     * @param wz      输入：旋转角速度 (rad/s)
     * @param out_rpm 输出：3个电机的目标角速度 (rad/s)
     */
    void InverseKinematics(float vx, float vy, float wz, float out_rpm[3]) const;

    /**
     * @brief 轮速等比例限幅：别飙车，等比例减速
     * @param rpm     输入输出：3个电机的转速数组 (rad/s)
     * @param max_rpm 最大允许转速 (rad/s)
     */
    void ClampWheelSpeed(float rpm[3], float max_rpm) const;
};

/**
 * @brief 四轮全向轮底盘 —— 稳如老狗的全面手 🛡️
 *
 * 4个全向轮呈X型（菱形）布局，四角分布。
 * 比三轮更稳、承载更强，而且同样能横着走！
 *
 * 布局：四个轮子驱动方向分别为 45°、135°、225°、315°。
 */
class Omni4Chassis : public Chassis {
private:
    Motor motor_[4];       /**< 4个电机，分别守在四个角 */
    float wheel_radius_;   /**< 轮子半径 R (m) */
    float wheel_base_x_;   /**< X方向半轴距 Lx (m) */
    float wheel_base_y_;   /**< Y方向半轴距 Ly (m) */
    float diagonal_base_;  /**< 对角线半长 L = sqrt(Lx² + Ly²) (m) */

public:
    /**
     * @brief 构造四轮全向底盘
     * @param ids    4个电机的ID数组 [FL, FR, BL, BR]
     * @param radius 轮子半径 R (m)
     * @param base_x X方向半轴距 Lx (m)
     * @param base_y Y方向半轴距 Ly (m)
     */
    Omni4Chassis(const int ids[4], float radius, float base_x, float base_y);

    /**
     * @brief 设置底盘目标速度，自动完成逆解算、限幅并下发
     * @param x 前进速度 (m/s)
     * @param y 横移速度 (m/s)
     * @param w 旋转角速度 (rad/s)
     */
    void SetSpeed(float x, float y, float w) override;

    /** @brief 刹车！四轮全停 */
    void Stop() override;

    /**
     * @brief 正运动学：根据4个电机转速，反推底盘实际速度
     * @param vx 输出：前进速度 (m/s)
     * @param vy 输出：横移速度 (m/s)
     * @param wz 输出：旋转角速度 (rad/s)
     */
    void ForwardKinematics(float& vx, float& vy, float& wz) const;

    /**
     * @brief 逆运动学：根据目标底盘速度，计算4个电机的目标转速
     * @param vx      输入：前进速度 (m/s)
     * @param vy      输入：横移速度 (m/s)
     * @param wz      输入：旋转角速度 (rad/s)
     * @param out_rpm 输出：4个电机的目标角速度 (rad/s)
     */
    void InverseKinematics(float vx, float vy, float wz, float out_rpm[4]) const;

    /**
     * @brief 轮速等比例限幅：安全第一，等比例减速
     * @param rpm     输入输出：4个电机的转速数组 (rad/s)
     * @param max_rpm 最大允许转速 (rad/s)
     */
    void ClampWheelSpeed(float rpm[4], float max_rpm) const;
};

/**
 * @brief 舵轮模块 —— 每个轮子都有自己的小方向盘 🚗
 *
 * 一个舵轮 = 1个驱动电机（油门）+ 1个转向角度（方向盘）。
 * 这是 SwerveChassis 的基本组成单位。
 */
class SteeringModule {
private:
    Motor drive_motor_;   /**< 驱动电机：负责转多快 */
    float target_angle_;  /**< 目标转向角 (rad)，范围 [-π, +π] */
    float current_angle_; /**< 当前实际转向角 (rad)，由外部编码器更新 */

public:
    /**
     * @brief 构造舵轮模块
     * @param id 驱动电机的ID
     */
    explicit SteeringModule(int id);

    /**
     * @brief 设置驱动电机转速（踩油门）
     * @param rpm 目标角速度 (rad/s)
     */
    void setSpeed(float rpm);

    /**
     * @brief 设置目标转向角（打方向盘）
     * @param angle_rad 目标角度 (rad)
     */
    void setAngle(float angle_rad);

    /** @return 当前驱动转速 (rad/s) */
    float getSpeed() const;

    /** @return 目标转向角 (rad) */
    float getTargetAngle() const;

    /** @return 当前实际转向角 (rad) */
    float getCurrentAngle() const;

    /**
     * @brief 更新当前实际转向角（外部编码器调用）
     * @param angle_rad 实际角度 (rad)
     */
    void updateCurrentAngle(float angle_rad);

    /** @return 电机ID */
    int getId() const;
};

/**
 * @brief 舵轮底盘 —— 指哪打哪的终极形态 🎯
 *
 * 4个舵轮模块，每个都能独立控制速度和方向。
 * 理论上效率最高的全向底盘，但控制也最复杂（要管8个电机）。
 * 适合追求极致灵活性的高端机器人。
 */
class SwerveChassis : public Chassis {
private:
    SteeringModule module_[4]; /**< 4个舵轮模块，四角站位 */
    float wheel_radius_;       /**< 轮子半径 R (m) */
    float wheel_base_x_;       /**< X方向半轴距 Lx (m) */
    float wheel_base_y_;       /**< Y方向半轴距 Ly (m) */

public:
    /**
     * @brief 构造舵轮底盘
     * @param ids    4个模块的电机ID数组 [FL, FR, BL, BR]
     * @param radius 轮子半径 R (m)
     * @param base_x X方向半轴距 Lx (m)
     * @param base_y Y方向半轴距 Ly (m)
     */
    SwerveChassis(const int ids[4], float radius, float base_x, float base_y);

    /**
     * @brief 设置底盘目标速度，自动计算每个模块的转速和转向角
     * @param x 前进速度 (m/s)
     * @param y 横移速度 (m/s)
     * @param w 旋转角速度 (rad/s)
     */
    void SetSpeed(float x, float y, float w) override;

    /** @brief 刹车！驱动电机归零，转向角度保持不动 */
    void Stop() override;

    /**
     * @brief 获取各模块的目标状态
     * @param out_speed 输出：4个模块的驱动转速 (rad/s)
     * @param out_angle 输出：4个模块的目标转向角 (rad)
     */
    void GetModuleStates(float out_speed[4], float out_angle[4]) const;

    /**
     * @brief 正运动学：已知各轮实际转向角，反推底盘速度
     * @param angle 输入：4个轮子的实际转向角 (rad)
     * @param vx    输出：前进速度 (m/s)
     * @param vy    输出：横移速度 (m/s)
     * @param wz    输出：旋转角速度 (rad/s)
     */
    void ForwardKinematics(const float angle[4], float& vx, float& vy, float& wz) const;
};

#endif // __CHASSIS_HPP
