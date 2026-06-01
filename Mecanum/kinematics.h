/**
 * @file kinematics.h
 * @author liqun
 * @brief 麦轮运动学解算
 * @version 0.1
 * @date 2026-05-31
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef __KINEMATICS_H__
#define __KINEMATICS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 机械参数 */
#define WHEEL_RADIUS        0.05f    /* 轮子半径 (m) */
#define WHEEL_BASE_X        0.125f   /* 轮距X方向半长 (m) */
#define WHEEL_BASE_Y        0.125f   /* 轮距Y方向半长 (m) */
#define COMBINED_BASE       (WHEEL_BASE_X + WHEEL_BASE_Y)  /* L = Lx + Ly */
#define MAX_WHEEL_SPEED     10.0f    /* 轮子最大转速 (rad/s) */

/* 底盘目标速度指令（车体坐标系） */
typedef struct {
    float vx;   /* X方向线速度 (m/s)，前进为正 */
    float vy;   /* Y方向线速度 (m/s)，左移为正 */
    float wz;   /* Z轴角速度 (rad/s)，逆时针为正 */
} chassis_cmd_t;

/* 底盘实际状态（轮子转速） */
typedef struct {
    float speed[4];  /* 四个麦轮的角速度 (rad/s)
                         索引0: 前左 (Front-Left)
                         索引1: 前右 (Front-Right)
                         索引2: 后左 (Rear-Left)
                         索引3: 后右 (Rear-Right) */
} chassis_state_t;

/* 底盘实际速度（车体坐标系，由轮速解算得到） */
typedef struct {
    float vx;
    float vy;
    float wz;
} chassis_odom_t;

/**
 * @brief 逆运动学解算：将底盘目标速度转换为四个麦轮的转速
 * @param cmd     输入：底盘目标速度指令 (vx, vy, wz)
 * @param state   输出：四个轮子的目标角速度 (rad/s)
 */
void backward_kinematics(const chassis_cmd_t *cmd, chassis_state_t *state);

/**
 * @brief 正运动学解算：由四个麦轮的实际转速推算底盘速度
 * @param state   输入：四个轮子的当前角速度 (rad/s)
 * @param odom    输出：底盘的实际速度 (vx, vy, wz)
 */
void forward_kinematics(const chassis_state_t *state, chassis_odom_t *odom);

/**
 * @brief 轮速限幅：确保所有轮速不超过最大限幅
 *        当任一电机达到限幅时，等比例缩放所有轮速以保持运动方向
 * @param state   输入/输出：轮速状态，原地修改
 * @param max_speed 最大允许轮速 (rad/s)
 */
void clamp_wheel_speed(chassis_state_t *state, float max_speed);

/**
 * @brief 底盘控制演示函数：读取目标速度 -> 逆解算 -> 限幅 -> 输出轮速
 */
void chassis_control(void);

#ifdef __cplusplus
}
#endif

#endif /* __KINEMATICS_H__ */