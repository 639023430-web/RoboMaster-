/**
 * @file kinematics.c
 * @author liqun
 * @brief 麦轮运动学解算
 * @version 0.1
 * @date 2026-05-31
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "kinematics.h"
#include <string.h>
#include <math.h>

/**
 * @brief 逆运动学解算：将底盘目标速度转换为四个麦轮的转速
 * @param cmd     输入：底盘目标速度指令 (vx, vy, wz)
 * @param state   输出：四个轮子的目标角速度 (rad/s)
 */
void backward_kinematics(const chassis_cmd_t *cmd, chassis_state_t *state)
{
    float vx = cmd->vx;
    float vy = cmd->vy;
    float wz = cmd->wz;
    float L  = COMBINED_BASE;
    float inv_R = 1.0f / WHEEL_RADIUS;

    state->speed[0] = (vx - vy - wz * L) * inv_R;  /* 前左 FL */
    state->speed[1] = (vx + vy + wz * L) * inv_R;  /* 前右 FR */
    state->speed[2] = (vx + vy - wz * L) * inv_R;  /* 后左 BL */
    state->speed[3] = (vx - vy + wz * L) * inv_R;  /* 后右 BR */
}

/**
 * @brief 正运动学解算：由四个麦轮的实际转速推算底盘速度
 * @param state   输入：四个轮子的当前角速度 (rad/s)
 * @param odom    输出：底盘的实际速度 (vx, vy, wz)
 */
void forward_kinematics(const chassis_state_t *state, chassis_odom_t *odom)
{
    float s0 = state->speed[0];
    float s1 = state->speed[1];
    float s2 = state->speed[2];
    float s3 = state->speed[3];
    float R  = WHEEL_RADIUS;
    float L  = COMBINED_BASE;

    odom->vx = (R * 0.25f)  * ( s0 + s1 + s2 + s3);
    odom->vy = (R * 0.25f)  * (-s0 + s1 + s2 - s3);
    odom->wz = (R / (4.0f * L)) * (-s0 + s1 - s2 + s3);
}

/**
 * @brief 轮速限幅：等比例缩放，保持运动方向不变
 * @param state      输入/输出：轮速状态，原地修改
 * @param max_speed  最大允许轮速 (rad/s)
 */
void clamp_wheel_speed(chassis_state_t *state, float max_speed)
{
    float max_abs = 0.0f;
    int i;

    for (i = 0; i < 4; i++) {
        float abs_val = fabsf(state->speed[i]);
        if (abs_val > max_abs) {
            max_abs = abs_val;
        }
    }

    if (max_abs > max_speed) {
        float scale = max_speed / max_abs;
        for (i = 0; i < 4; i++) {
            state->speed[i] *= scale;
        }
    }
}

/**
 * @brief 底层电机控速接口
 * @param motor_id  电机编号 (0=FL, 1=FR, 2=BL, 3=BR)
 * @param speed     目标角速度 (rad/s)
 */
void motor_set_speed(uint8_t motor_id, float speed)
{
    (void)motor_id;
    (void)speed;
}

/**
 * @brief 批量下发轮速到全部四个电机
 * @param state  待下发的轮速状态
 */
void motor_set_all_speeds(const chassis_state_t *state)
{
    uint8_t i;
    for (i = 0; i < 4; i++) {
        motor_set_speed(i, state->speed[i]);
    }
}

/**
 * @brief 底盘控制演示：获取指令 -> 逆解算 -> 限幅 -> 下发电机
 */
void chassis_control(void)
{
    chassis_cmd_t   cmd;
    chassis_state_t state;

    cmd.vx =  0.5f;
    cmd.vy =  0.0f;
    cmd.wz =  0.2f;

    backward_kinematics(&cmd, &state);
    clamp_wheel_speed(&state, MAX_WHEEL_SPEED);
    motor_set_all_speeds(&state);
}
