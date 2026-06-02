# C++ 面向对象与STL完全指南 —— 面向RoboMaster下位机开发

> **目标读者**：有C语言基础，准备用C++做RoboMaster下位机（STM32/嵌入式）开发的工程师。
> **阅读方式**：像看漫画一样从头到尾读，别跳着看，每一节都在为后面铺垫。

---

## 目录

1. [为什么RoboMaster要用C++？](#1-为什么robomaster要用c)
2. [先忘掉C，重新认识"数据和函数"](#2-先忘掉c重新认识数据和函数)
3. [类与对象 —— 用"底盘"来理解](#3-类与对象--用底盘来理解)
4. [构造函数 —— 对象的"出生说明书"](#4-构造函数--对象的出生说明书)
5. [析构函数 —— 对象的"临终遗嘱"](#5-析构函数--对象的临终遗嘱)
6. [访问控制：public/private/protected](#6-访问控制publicprivateprotected)
7. [继承 —— "我爸爸会的，我都会"](#7-继承--我爸爸会的我都会)
8. [多态 —— 同一个指令，不同的执行方式](#8-多态--同一个指令不同的执行方式)
9. [虚函数 —— 让"儿子"能覆盖"爸爸"的行为](#9-虚函数--让儿子能覆盖爸爸的行为)
10. [命名空间 —— 给代码分文件夹](#10-命名空间--给代码分文件夹)
11. [STL简介 —— C++的"瑞士军刀"](#11-stl简介--c的瑞士军刀)
12. [STL容器详解](#12-stl容器详解)
13. [STL算法详解](#13-stl算法详解)
14. [智能指针 —— 告别内存泄漏](#14-智能指针--告别内存泄漏)
15. [嵌入式C++避坑指南](#15-嵌入式c避坑指南)
16. [RoboMaster实战：底盘模块完整示例](#16-robomaster实战底盘模块完整示例)

---

## 1. 为什么RoboMaster要用C++？

```
你想象一下：
C语言 = 工具箱里只有锤子和螺丝刀，什么都能干，但很累。
C++ = 工具箱里多了电钻、激光尺、3D打印机，干同样的事更快更安全。
```

RoboMaster下位机的典型需求：
- 要管理**底盘、云台、发射器**多个模块，每个模块都有自己的数据和方法
- 多个电机、多个传感器，**同类型的东西很多**
- 代码要**好维护**，不能改一个地方导致其他地方全崩
- 代码要**能复用**，今年写的底盘代码明年还能用

C++的**面向对象**和**STL**正好解决这些问题。

---

## 2. 先忘掉C，重新认识"数据和函数"

### 你在C语言里是怎么写代码的？

```c
// C语言的写法：数据是数据，函数是函数，两者分离

// 数据（全局变量满天飞）
float chassis_speed_x = 0;
float chassis_speed_y = 0;
float chassis_speed_w = 0;  // 角速度
float motor_rpm[4] = {0, 0, 0, 0};  // 4个电机的转速

// 函数（操作这些数据）
void chassis_set_speed(float x, float y, float w) {
    chassis_speed_x = x;
    chassis_speed_y = y;
    chassis_speed_w = w;
}

void chassis_calc_motor_rpm() {
    // 麦轮解算：把底盘速度转成4个电机转速
    motor_rpm[0] = chassis_speed_x - chassis_speed_y - chassis_speed_w;
    motor_rpm[1] = chassis_speed_x + chassis_speed_y - chassis_speed_w;
    motor_rpm[2] = -chassis_speed_x - chassis_speed_y - chassis_speed_w;
    motor_rpm[3] = -chassis_speed_x + chassis_speed_y - chassis_speed_w;
}
```

**问题在哪？**
- `chassis_speed_x` 这个变量，**谁都能改**，写错了编译器不报错
- 如果这个项目有**3种底盘**（麦轮、全向轮、舵轮），你得写 `chassis1_speed_x`、`chassis2_speed_x`... 名字越来越长
- 数据和函数没有"绑定"，你很难一眼看出"这个函数是操作哪个数据的"

### C++的思维方式：把数据和函数"打包"

```cpp
// C++的写法：数据和操作它的函数打包在一起，这就是"类"（Class）

class Chassis {
    // 数据（也叫"成员变量"或"属性"）
    float speed_x;
    float speed_y;
    float speed_w;
    float motor_rpm[4];
    
    // 函数（也叫"成员函数"或"方法"）
    void setSpeed(float x, float y, float w) {
        speed_x = x;
        speed_y = y;
        speed_w = w;
    }
    
    void calcMotorRpm() {
        motor_rpm[0] = speed_x - speed_y - speed_w;
        motor_rpm[1] = speed_x + speed_y - speed_w;
        motor_rpm[2] = -speed_x - speed_y - speed_w;
        motor_rpm[3] = -speed_x + speed_y - speed_w;
    }
};
```

**这就是"面向对象"的第一个核心思想：把相关的数据和函数打包在一起，变成一个"类"（Class）。**

---

## 3. 类与对象 —— 用"底盘"来理解

### 类（Class）是什么？对象（Object）是什么？

这个比喻能让你一辈子忘不掉：

```
类 = 图纸
对象 = 按照图纸造出来的实物

"汽车设计图纸" 是一个类。
停车场里那辆"京A88888的红色比亚迪"是一个对象。
隔壁那辆"沪B66666的蓝色特斯拉"是另一个对象。

同一个图纸（类），可以造出无数辆车（对象）。
每辆车都有自己的颜色、油量、速度——互不影响。
```

### 代码演示

```cpp
// Step1: 定义类（画图纸）
class Motor {
public:  // 公开的，外面能访问（后面会细讲）
    int id;      // 电机ID（1,2,3,4...）
    float angle; // 当前角度
    float rpm;   // 当前转速
    
    void setCurrent(float current) {
        // 通过CAN总线发送电流指令给电机
        CAN_Send(id, current);
    }
    
    void update() {
        // 从CAN总线读取电机反馈的角度和转速
        angle = CAN_ReadAngle(id);
        rpm = CAN_ReadRpm(id);
    }
};

// Step2: 用类创建对象（用图纸造实物）
int main() {
    Motor motor1;  // 创建对象：底盘左前电机
    Motor motor2;  // 创建对象：底盘右前电机
    Motor motor3;  // 创建对象：底盘左后电机
    Motor motor4;  // 创建对象：底盘右后电机
    
    // 每个对象有自己独立的数据！
    motor1.id = 1;
    motor2.id = 2;
    motor3.id = 3;
    motor4.id = 4;
    
    motor1.setCurrent(5000);  // 左前电机给5000mA电流
    motor2.setCurrent(3000);  // 右前电机给3000mA电流
    // motor1和motor2互不影响！
    
    motor1.update();  // 更新motor1的角度和转速
    // 现在 motor1.angle 和 motor1.rpm 有最新值了
}
```

### 关键理解

| 概念 | 比喻 | 代码中的体现 |
|------|------|-------------|
| **类（Class）** | 图纸/模板 | `class Motor { ... };` |
| **对象（Object）** | 实物/实例 | `Motor motor1;` |
| **成员变量/属性** | 车的颜色、油量 | `id`, `angle`, `rpm` |
| **成员函数/方法** | 车的功能（踩油门、刹车） | `setCurrent()`, `update()` |

---

---

### 番外篇：类其实就是"超级结构体"！

如果你从C语言过来，**把类理解成"超级结构体"是最快的入门方式**。

看看C的struct和C++的class有多像：

```c
// ============ C语言的结构体 ============
struct Motor_C {
    int id;             // 数据：电机ID
    float angle;        // 数据：当前角度
    float rpm;          // 数据：当前转速
};
// 数据归数据...
// 函数只能写在外面，和数据是分离的
void Motor_SetCurrent(struct Motor_C* m, float current) {
    CAN_Send(m->id, current);  // 必须传指针才能操作数据
}
```

```cpp
// ============ C++的类 ============
class Motor_CPP {
    int id;             // 数据：电机ID
    float angle;        // 数据：当前角度
    float rpm;          // 数据：当前转速
    
    void setCurrent(float current) {   // 函数：直接塞进结构体里！
        CAN_Send(id, current);          // 不用传指针，直接用！
    }
};
```

**类 = 结构体 + 函数直接塞进去 + 访问控制（public/private）+ 继承**

| 能力 | C语言struct | C++的class |
|------|-----------|-----------|
| 装数据 | ✅ | ✅ |
| 装函数（成员函数） | ❌ | ✅ |
| 构造函数（自动初始化） | ❌ 手动写init() | ✅ 自动调用 |
| 析构函数（自动清理） | ❌ 手动写deinit() | ✅ 自动调用 |
| private保护数据 | ❌ 谁都改就是你来改 | ✅ 编译器帮你挡 |
| 继承（复用代码） | ❌ 只能嵌套struct | ✅ extends |
| 多态 | ❌ | ✅ virtual |

> **一句话总结：C的struct是个"数据筐"，C++的class是个"智能筐"——不仅能装数据，还自带工具箱（函数），会自己初始化（构造函数），会自己打扫（析构函数），还能上锁（private）。**

---

## 4. 构造函数 —— 对象的"出生说明书"

### 问题：对象刚创建时，里面的变量是什么值？

```cpp
Motor motor1;  // 刚创建，motor1.angle 是多少？—— 未知！可能是垃圾值！
```

如果 `motor1.id` 是随机垃圾值，后面一调用 `CAN_Send(id, current)` 就往错误的ID发数据——**炸了**。

### 构造函数：对象在"出生"时自动执行的初始化函数

```cpp
class Motor {
public:
    int id;
    float angle;
    float rpm;
    
    // 这就是构造函数！函数名和类名完全一样，没有返回值
    Motor(int motor_id) {
        id = motor_id;   // 把传入的ID存起来
        angle = 0;       // 初始角度设为0
        rpm = 0;         // 初始转速设为0
        // 你还可以在这里做CAN初始化等操作
        CAN_Init(id);
    }
    
    void setCurrent(float current) {
        CAN_Send(id, current);
    }
};

int main() {
    Motor motor1(1);  // 创建时传入ID=1，构造函数自动执行！
    Motor motor2(2);  // 创建时传入ID=2
    
    // 现在 motor1.id=1, motor1.angle=0, motor1.rpm=0 —— 干干净净！
    // 而不用像C语言那样：
    // motor1.id = 1;  motor1.angle = 0;  motor1.rpm = 0;  // 写3行！
}
```

### 构造函数的重载：可以有多种"出生方式"

```cpp
class Motor {
public:
    int id;
    float angle;
    float rpm;
    
    // 构造函数1：传入ID
    Motor(int motor_id) {
        id = motor_id;
        angle = 0;
        rpm = 0;
    }
    
    // 构造函数2：传入ID和初始角度（比如编码器有当前角度）
    Motor(int motor_id, float init_angle) {
        id = motor_id;
        angle = init_angle;
        rpm = 0;
    }
    
    // 构造函数3：无参数（默认构造函数）
    Motor() {
        id = 0;
        angle = 0;
        rpm = 0;
    }
};

int main() {
    Motor m1(1);           // 用构造函数1
    Motor m2(2, 45.5);     // 用构造函数2
    Motor m3;              // 用构造函数3（无参）
}
```

### RoboMaster中的实际应用

```cpp
class Chassis {
private:
    Motor motors[4];
    float wheel_radius;
    float wheel_base;  // 轴距
    
public:
    // 构造函数：一次性初始化底盘的所有参数
    Chassis(int motor_ids[4], float radius, float base)
        : wheel_radius(radius), wheel_base(base)
        // ↑ 这是"初始化列表"，比在函数体里赋值更高效
    {
        // 批量初始化4个电机
        for (int i = 0; i < 4; i++) {
            motors[i] = Motor(motor_ids[i]);
        }
    }
};

int main() {
    int ids[4] = {1, 2, 3, 4};
    Chassis my_chassis(ids, 0.076, 0.35);  
    // 一行代码就创建了一个完整的底盘对象！4个电机都初始化好了！
    
    // 对比C语言你需要写多少行？
    // float wheel_radius = 0.076;
    // float wheel_base = 0.35;
    // CAN_InitMotor(1);
    // CAN_InitMotor(2);
    // ... 至少十几行
}
```

---

## 5. 析构函数 —— 对象的"临终遗嘱"

构造函数是"出生时做什么"，析构函数是"死亡时做什么"。

```cpp
class Motor {
public:
    int id;
    
    Motor(int motor_id) {
        id = motor_id;
        CAN_Init(id);  // 出生时：初始化CAN通信
    }
    
    // 析构函数：函数名是 ~类名，没有参数，没有返回值
    ~Motor() {
        setCurrent(0);     // 死亡前：电机输出清零！
        CAN_Deinit(id);    // 死亡前：关闭CAN通信
    }
};
```

对于嵌入式开发，析构函数常用于：
- 关闭外设（UART, SPI, I2C, CAN）
- 释放DMA资源
- 把电机电流归零（安全！）

**注意**：在裸机嵌入式开发中，对象通常在全局区（整个程序运行期间一直存在），析构函数很少被调用到。但在FreeRTOS这样的RTOS中，任务结束时会调用析构函数，这时候就用得上了。

---

## 6. 访问控制：public/private/protected

### 为什么需要访问控制？

回到底盘的例子：

```cpp
class Chassis {
public:  // 所有人都能碰
    float target_speed_x;  // 目标速度
    
private:  // 只有我自己能碰
    float motor_rpm[4];    // 电机转速——这是内部计算出来的
    float pid_integral;    // PID积分项——乱改会炸
    
    void calcMotorRpm() {
        // 麦轮解算
        motor_rpm[0] = target_speed_x - target_speed_y - target_speed_w;
        // ...
    }
    
public:
    void setSpeed(float x, float y, float w) {
        target_speed_x = x;
        target_speed_y = y;
        calcMotorRpm();  // 自动计算电机转速
    }
};

int main() {
    Chassis chassis;
    
    chassis.setSpeed(100, 0, 0);     // ✅ 正确做法：通过公开接口设置
    chassis.target_speed_x = 100;    // ⚠️ 虽然能编译，但不会触发解算
    // chassis.motor_rpm[0] = 999;   // ❌ 编译错误！motor_rpm是private的！
    // chassis.pid_integral = 0;     // ❌ 编译错误！
}
```

### 三个级别，用人来比喻

| 访问级别 | 比喻 | 谁能访问 |
|---------|------|---------|
| **public** | 你的微信头像、昵称 | 所有人都能看 |
| **private** | 你的银行密码 | 只有你自己知道 |
| **protected** | 你的家族秘密 | 你和你的子女知道（后面继承会讲） |

### 为什么要这样设计？

```
想象你给队友写了一个电机控制类。
如果你把所有变量都设成 public，队友可能：
  - 不小心把 motor_rpm 改成负数
  - 不小心把 pid_integral 清零
  - 跳过安全校验直接改电流值
  
结果：电机疯转，机器人撞墙。

如果你把关键变量设成 private，只暴露安全的 public 接口：
  - 队友只能调用 setCurrent()，而 setCurrent() 内部做了限幅保护
  - 队友改不了内部状态
  
结果：机器人安全运行。
```

---

## 7. 继承 —— "我爸爸会的，我都会"

### 问题场景

RoboMaster战队可能有三种底盘：
1. **麦轮底盘**（Mecanum）：4个麦轮
2. **全向轮底盘**（Omni）：3个或4个全向轮
3. **舵轮底盘**（Steer）：4个舵轮

它们都有**共同的功能**：`setSpeed()`, `stop()`, `getPosition()`
但它们**实现方式不同**：麦轮解算和舵轮解算完全不一样

**你不用继承的话：** 三个类各写一遍 `setSpeed()`, `stop()`, `getPosition()`——大量重复代码。

### 继承的解决方案

```cpp
// ============ 爸爸类：定义"所有底盘都该有的东西" ============
class Chassis {
protected:  // protected：自己和儿子能访问
    float speed_x, speed_y, speed_w;
    bool is_stopped;
    
public:
    // 这是"纯虚函数"——爸爸不确定怎么做，让儿子去实现
    // 语法：virtual 返回类型 函数名() = 0;
    virtual void setSpeed(float x, float y, float w) = 0;
    virtual void stop() = 0;
    
    // 普通函数——所有底盘都一样，爸爸直接实现
    bool isMoving() {
        return (speed_x != 0 || speed_y != 0 || speed_w != 0);
    }
};

// ============ 儿子1：麦轮底盘 ============
class MecanumChassis : public Chassis {  // ":" 表示继承
private:
    Motor motors[4];
    float wheel_radius;
    
public:
    MecanumChassis(int ids[4], float radius) : wheel_radius(radius) {
        for (int i = 0; i < 4; i++) motors[i] = Motor(ids[i]);
    }
    
    // 实现爸爸要求的方法（override = 覆盖/重写）
    void setSpeed(float x, float y, float w) override {
        speed_x = x; speed_y = y; speed_w = w;
        // 麦轮解算
        motors[0].setRpm(( x - y - w) / wheel_radius);
        motors[1].setRpm(( x + y - w) / wheel_radius);
        motors[2].setRpm((-x - y - w) / wheel_radius);
        motors[3].setRpm((-x + y - w) / wheel_radius);
        is_stopped = false;
    }
    
    void stop() override {
        for (int i = 0; i < 4; i++) motors[i].setRpm(0);
        speed_x = speed_y = speed_w = 0;
        is_stopped = true;
    }
};

// ============ 儿子2：全向轮底盘（3轮） ============
class OmniChassis : public Chassis {
private:
    Motor motors[3];
    
public:
    OmniChassis(int ids[3]) {
        for (int i = 0; i < 3; i++) motors[i] = Motor(ids[i]);
    }
    
    void setSpeed(float x, float y, float w) override {
        // 全向轮解算（三角分布，公式和麦轮不同）
        motors[0].setRpm((-0.5f * x + 0.866f * y + w));
        motors[1].setRpm((-0.5f * x - 0.866f * y + w));
        motors[2].setRpm(( x + w));
        is_stopped = false;
    }
    
    void stop() override {
        for (int i = 0; i < 3; i++) motors[i].setRpm(0);
        is_stopped = false;
    }
};
```

### 继承的好处：写一份代码，操作所有类型的底盘

```cpp
// 这个函数不管传入哪种底盘，都能正常工作！
void emergencyStop(Chassis& chassis) {  // "Chassis&" 是引用，能接收任何子类
    chassis.stop();  // 自动调用对应子类的stop()
    // 如果是MecanumChassis，调用MecanumChassis的stop()
    // 如果是OmniChassis，调用OmniChassis的stop()
}

int main() {
    MecanumChassis hero_chassis((int[]){1,2,3,4}, 0.076);
    OmniChassis guard_chassis((int[]){5,6,7});
    
    emergencyStop(hero_chassis);  // 调用麦轮的stop
    emergencyStop(guard_chassis); // 调用全向轮的stop
    
    // 还可以用基类指针管理
    Chassis* robot;  // 爸爸类型的指针
    robot = &hero_chassis;
    robot->stop();   // 自动调用麦轮的stop()
    robot = &guard_chassis;
    robot->stop();   // 自动调用全向轮的stop()
}
```

### 继承链可以很长

```
Motor（爸爸：最基础的电机控制）
  ↓
Motor3508 : Motor（儿子：大疆M3508电机，增加了温度读取）
  ↓
Motor3508WithGimbal : Motor3508（孙子：装在云台上的M3508，增加了角度限位）
```

---

## 8. 多态 —— 同一个指令，不同的执行方式

**多态** 这个词听着高深，实际就是：

> 你喊一声"攻击！"（同一个指令）
> - 步兵机器人：用枪管射击（一种执行方式）
> - 英雄机器人：发射42mm弹丸（另一种执行方式）
> - 哨兵机器人：自动追踪开火（又一种执行方式）

代码层面：

```cpp
// 基类
class Robot {
public:
    virtual void attack() = 0;  // 每个机器人都要会攻击，但方式不同
};

class Infantry : public Robot {
public:
    void attack() override {
        SHOOT_17mm();  // 17mm弹丸射击
    }
};

class Hero : public Robot {
public:
    void attack() override {
        SHOOT_42mm();  // 42mm弹丸射击
    }
};

class Sentry : public Robot {
public:
    void attack() override {
        AUTO_AIM_AND_SHOOT();  // 自动瞄准射击
    }
};

// 统一的控制函数
void commandAttack(Robot& robot) {
    robot.attack();  // 不管是什么机器人，统一调用attack
}
```

**多态 = 同一个接口（attack），不同对象有不同表现（多态）。**

---

## 9. 虚函数 —— 让"儿子"能覆盖"爸爸"的行为

### 先看一个"如果没有虚函数"的坑

```cpp
class Father {
public:
    void speak() { cout << "我是爸爸" << endl; }
};

class Son : public Father {
public:
    void speak() { cout << "我是儿子" << endl; }  // 想覆盖爸爸的speak
};

int main() {
    Son son;
    Father* ptr = &son;  // 用爸爸类型的指针指向儿子
    ptr->speak();  // 输出什么？
}
```

**输出: "我是爸爸"** —— 为什么？因为 `speak()` 不是虚函数，编译器在编译时就决定了调用 `Father::speak()`，这就是 **静态绑定**。

### 加上 virtual 之后

```cpp
class Father {
public:
    virtual void speak() { cout << "我是爸爸" << endl; }  // 加了 virtual！
};

class Son : public Father {
public:
    void speak() override { cout << "我是儿子" << endl; }
};

int main() {
    Son son;
    Father* ptr = &son;  // 用爸爸类型的指针指向儿子
    ptr->speak();  // 输出 "我是儿子"！ ✓ 正确了！
}
```

### virtual 的工作原理（面试常考，了解即可）

```
没有virtual时：编译器看指针类型（Father*），直接调Father::speak() → 静态绑定
有virtual时：  每个对象里有一个隐藏的"虚函数表指针"，运行时查表 → 动态绑定
```

每个有虚函数的类，在内存中有一个 **虚函数表（vtable）**：
- `Son` 的虚函数表里记录着：`speak -> Son::speak`
- `Father` 的虚函数表里记录着：`speak -> Father::speak`

调用 `ptr->speak()` 时：
1. 通过对象里的指针找到虚函数表
2. 查表找到实际的函数地址
3. 调用对应的函数

**在嵌入式里要注意**：虚函数表占用内存（一个类一张表），且虚函数调用比普通函数略慢（多一次查表）。但这点开销在STM32F4/H7上完全可以忽略（主频几百MHz，查表几纳秒）。

---

## 10. 命名空间 —— 给代码分文件夹

### 问题：名字冲突

```cpp
// 队友A写的底盘控制
class Motor { ... };
void init() { ... };

// 队友B写的云台控制
class Motor { ... };  // ❌ 重名了！
void init() { ... };  // ❌ 重名了！
```

### 解决方案：namespace

```cpp
namespace Chassis {
    class Motor { ... };
    void init() { ... };
}

namespace Gimbal {
    class Motor { ... };
    void init() { ... };
}

int main() {
    Chassis::init();  // 调用底盘的init
    Gimbal::init();   // 调用云台的init
    
    Chassis::Motor chassis_motor;
    Gimbal::Motor gimbal_motor;
}
```

### 实际项目中的用法

```cpp
// 把不同模块放在不同命名空间里
namespace Chassis {
    class MecanumChassis { ... };
    class OmniChassis { ... };
    const float WHEEL_RADIUS = 0.076;
}

namespace Gimbal {
    class YawMotor { ... };
    class PitchMotor { ... };
}

namespace Shooter {
    class FrictionWheel { ... };
    class Trigger { ... };
}

namespace IMU {
    class BMI088 { ... };
    float getYaw() { ... };
    float getPitch() { ... };
}
```

---

## 11. STL简介 —— C++的"瑞士军刀"

**STL = Standard Template Library（标准模板库）**

```
如果你之前用C语言写嵌入式，你一定做过这些事：
- 自己写链表：malloc一个节点，手动链接，忘了free就内存泄漏
- 自己写动态数组：手动realloc，手动拷贝
- 自己写排序：冒泡排序写了一遍又一遍
- 自己写队列：环形缓冲区，满了怎么处理，头尾指针搞半天

STL = 这些全部帮你写好了，经过无数人验证，比你自己写的快、稳、安全。
```

### STL的三大组成部分

| 部分 | 干什么的 | 常用例子 |
|------|---------|---------|
| **容器（Containers）** | 装数据 | `vector`, `array`, `list`, `queue`, `map`, `set` |
| **算法（Algorithms）** | 处理数据 | `sort`, `find`, `reverse`, `min_element`, `for_each` |
| **迭代器（Iterators）** | 连接容器和算法的"桥梁" | `begin()`, `end()` |

---

## 12. STL容器详解

### 12.1 vector —— 动态数组（最常用！）

```cpp
#include <vector>

// C语言：你必须先声明大小
int data_c[10];  // 只能存10个，存第11个就溢出

// C++ vector：自动扩容！
std::vector<int> data;   // 空的，大小是0
data.push_back(10);       // 加一个，自动变大
data.push_back(20);       // 再加一个
data.push_back(30);       // 现在有3个元素

// RoboMaster实战：存储扫描到的敌方机器人
struct Enemy {
    int id;
    float distance;
    float angle;
};

std::vector<Enemy> enemies;

// 雷达扫到一个敌人，就加进去
Enemy e1 = {1, 3.5, 30};
enemies.push_back(e1);

// 遍历所有敌人
for (size_t i = 0; i < enemies.size(); i++) {
    printf("Enemy %d: distance=%.2f, angle=%.2f\n", 
           enemies[i].id, enemies[i].distance, enemies[i].angle);
}

// 更优雅的遍历方式（范围for循环）
for (const Enemy& e : enemies) {
    printf("Enemy %d at %.2f meters\n", e.id, e.distance);
}

// 删除第1个敌人
enemies.erase(enemies.begin() + 0);  // 后面的自动往前移

// 清空所有敌人
enemies.clear();
```

### 12.2 array —— 定长数组（替代C数组）

```cpp
#include <array>

// C语言数组的问题
float motor_speeds_c[4] = {0, 0, 0, 0};
// 1. 不能直接赋值整个数组：motor_speeds2 = motor_speeds_c;  ❌
// 2. 传给函数时会退化成指针，丢失大小信息
// 3. 没有边界检查

// C++ array：像数组，但更好
std::array<float, 4> motor_speeds = {0, 0, 0, 0};
std::array<float, 4> motor_speeds2 = motor_speeds;  // ✅ 可以直接赋值！

// 有 size() 方法
for (size_t i = 0; i < motor_speeds.size(); i++) {
    motor_speeds[i] = i * 100.0f;
}

// 有边界检查（用 at() 而不是 []）
motor_speeds.at(3) = 300;  // 正常
// motor_speeds.at(10) = 999;  // 运行时抛出异常，而不是访问野内存！
```

### 12.3 queue —— 队列（消息缓冲必备）

```cpp
#include <queue>

// RoboMaster实战：遥控器数据缓冲
struct RemoteControlData {
    float joystick_x;
    float joystick_y;
    float joystick_w;
    bool shoot;
    uint32_t timestamp;
};

std::queue<RemoteControlData> rc_queue;

// 中断里收到遥控器数据，放进队列
void onRCDataReceived(RemoteControlData data) {
    rc_queue.push(data);  // 加入队尾
}

// 主循环里处理遥控器数据
void processRCData() {
    while (!rc_queue.empty()) {          // 队列不为空就处理
        RemoteControlData data = rc_queue.front();  // 取队首
        rc_queue.pop();                  // 弹出队首
        
        // 处理遥控数据
        chassis.setSpeed(data.joystick_x, data.joystick_y, data.joystick_w);
        if (data.shoot) shooter.fire();
    }
}
```

### 12.4 deque —— 双端队列

```cpp
#include <deque>

// 像queue，但两端都能进出
std::deque<int> dq;
dq.push_back(1);   // 从尾部加： [1]
dq.push_back(2);   // 从尾部加： [1, 2]
dq.push_front(0);  // 从头部加： [0, 1, 2]
dq.pop_back();     // 从尾部删： [0, 1]
dq.pop_front();    // 从头部删： [1]
```

### 12.5 map —— 键值对（查表、配置管理）

```cpp
#include <map>
#include <string>

// RoboMaster实战：电机ID -> 电机对象的映射
// C语言做法：if (id == 1) motor1; else if (id == 2) motor2; ... 一堆if-else
// C++做法：map，直接通过key查找

std::map<int, Motor> motors;  // key=电机ID, value=电机对象

// 初始化
motors[1] = Motor(1);  // ID=1的电机
motors[2] = Motor(2);  // ID=2的电机
motors[3] = Motor(3);
motors[4] = Motor(4);

// 查找：通过ID直接找到对应电机
int target_id = 2;
if (motors.find(target_id) != motors.end()) {  // 找到了
    motors[target_id].setCurrent(5000);
}

// 遍历所有电机
for (auto& pair : motors) {
    // pair.first 是 key（电机ID）
    // pair.second 是 value（电机对象）
    printf("Motor %d: angle=%.2f\n", pair.first, pair.second.getAngle());
}

// 另一个实战：PID参数配置
std::map<std::string, float> pid_params;
pid_params["chassis_vel_kp"] = 10.0f;
pid_params["chassis_vel_ki"] = 0.5f;
pid_params["chassis_vel_kd"] = 2.0f;
pid_params["gimbal_pos_kp"] = 20.0f;

// 使用时：
float kp = pid_params["chassis_vel_kp"];  // 直接取，不用遍历数组找
```

### 12.6 常用容器速查表

| 容器 | 内部结构 | 适用场景 | RoboMaster用例 |
|------|---------|---------|---------------|
| `vector` | 动态数组 | 需要频繁尾部添加、随机访问 | 存储敌人列表、路径点 |
| `array` | 定长数组 | 已知大小不变 | 4个电机的转速值 |
| `queue` | 队列（FIFO） | 先进先出的缓冲 | CAN消息队列、遥控器数据缓冲 |
| `deque` | 双端队列 | 两端都要操作 | 命令缓冲区（可以插队） |
| `list` | 双向链表 | 频繁插入删除 | 很少在嵌入式用（内存碎片） |
| `map` | 红黑树（有序） | 键值对查找 | 电机ID→对象、配置参数 |
| `set` | 红黑树（有序） | 去重集合 | 已扫描过的敌人ID |
| `unordered_map` | 哈希表（无序） | 快速查找 | 比map更快但内存更大 |

---

## 13. STL算法详解

### 13.1 排序 sort

```cpp
#include <algorithm>
#include <vector>

// 敌人按距离排序，最近的放前面
std::vector<Enemy> enemies = {
    {1, 5.0, 30},
    {2, 2.0, 45},
    {3, 8.0, 10},
    {4, 1.5, 60}
};

// 按距离升序排序（最近的在前面）
std::sort(enemies.begin(), enemies.end(), 
    [](const Enemy& a, const Enemy& b) {
        return a.distance < b.distance;  // a距离 < b距离 → a排前面
    });

// 结果：enemies[0] 是距离1.5m的，enemies[3] 是距离8m的
```

`[](...){...}` 这个叫 **Lambda表达式**（匿名函数），你就把它理解成"当场写的一个小函数"：

```cpp
// 这两段代码完全等价：
// 方式1：传统函数
bool compareByDistance(const Enemy& a, const Enemy& b) {
    return a.distance < b.distance;
}
std::sort(enemies.begin(), enemies.end(), compareByDistance);

// 方式2：Lambda（当场写，不另起函数名）
std::sort(enemies.begin(), enemies.end(), 
    [](const Enemy& a, const Enemy& b) {
        return a.distance < b.distance;
    });
```

### 13.2 查找 find / find_if

```cpp
std::vector<int> motor_ids = {1, 2, 3, 4, 5, 6, 7, 8};

// 查找ID为5的电机是否存在
auto it = std::find(motor_ids.begin(), motor_ids.end(), 5);
if (it != motor_ids.end()) {
    printf("找到啦！在第%d个位置\n", it - motor_ids.begin());
} else {
    printf("没找到\n");
}

// find_if：按条件查找（例如找转速>5000的电机）
std::vector<float> rpms = {1000, 3000, 6000, 2000};
auto it2 = std::find_if(rpms.begin(), rpms.end(), 
    [](float rpm) { return rpm > 5000; });
if (it2 != rpms.end()) {
    printf("找到转速超限的电机：%.0f\n", *it2);
}
```

### 13.3 其他常用算法

```cpp
// min_element / max_element —— 找最大/最小
auto max_rpm = std::max_element(rpms.begin(), rpms.end());
printf("最大转速: %.0f\n", *max_rpm);

// reverse —— 反转
std::vector<int> nums = {1, 2, 3, 4, 5};
std::reverse(nums.begin(), nums.end());  
// nums = {5, 4, 3, 2, 1}

// fill —— 填充
std::array<float, 10> buffer;
std::fill(buffer.begin(), buffer.end(), 0.0f);  // 全部填0

// copy —— 拷贝
std::array<float, 4> src = {1, 2, 3, 4};
std::array<float, 4> dst;
std::copy(src.begin(), src.end(), dst.begin());  // dst = {1, 2, 3, 4}

// count / count_if —— 计数
int count = std::count_if(rpms.begin(), rpms.end(),
    [](float rpm) { return rpm > 5000; });  // 统计转速>5000的电机数量

// for_each —— 对每个元素执行操作
std::for_each(motors.begin(), motors.end(),
    [](Motor& m) { m.setCurrent(0); });  // 所有电机电流归零
```

---

## 14. 智能指针 —— 告别内存泄漏

### 问题：C语言里的 malloc/free 噩梦

```c
// C语言
int* data1 = malloc(100 * sizeof(int));
int* data2 = malloc(100 * sizeof(int));
// ... 中间可能因为某个if条件提前return了
// ... 忘记free了 → 内存泄漏！
free(data1);
free(data2);  // 如果前面return了，这行执行不到
```

### C++智能指针：自动释放

```cpp
#include <memory>

// unique_ptr：独占所有权，不能拷贝，只能移动
{
    std::unique_ptr<int[]> data(new int[100]);
    data[0] = 42;
    // ... 各种操作
}  // 离开作用域，data自动释放！不用写free/delete！

// shared_ptr：共享所有权，引用计数为0时自动释放
std::shared_ptr<Motor> motor = std::make_shared<Motor>(1);
// 可以传给多个函数，最后一个使用者离开时自动释放

// weak_ptr：观察shared_ptr，不增加引用计数，防止循环引用
```

### 嵌入式开发中的注意事项

**在RoboMaster下位机中，智能指针用得不多**，因为：
- 嵌入式通常不用动态内存分配（heap），对象都在栈上或全局区
- 如果用了FreeRTOS，动态分配一般用FreeRTOS的`pvPortMalloc`

但你还是要了解，因为：
- 上位机（视觉/导航）会用很多智能指针
- 了解它是为了防止看到代码不慌

---

## 15. 嵌入式C++避坑指南

### 15.1 不要在不该用的地方用STL

```cpp
// ❌ 不要：在中断里创建vector
void CAN_RX_IRQHandler() {
    std::vector<uint8_t> data;  // 可能在堆上分配内存，中断里不能！
    // ...
}

// ✅ 正确：用全局的定长buffer
uint8_t can_rx_buffer[64];  // 全局，编译时就分配好了
```

### 15.2 注意内存占用

```cpp
// ❌ 如果你只有8KB RAM，别这么玩
std::map<int, std::string> big_map;  // map本身就有开销

// ✅ 嵌入式里用小容器
std::array<Motor, 4> motors;  // 编译时就知道大小，栈上分配
Motor motors_raw[4];           // 甚至直接用C数组也行
```

### 15.3 虚函数有开销但不致命

- 每个带虚函数的类有一个虚函数表（vtable），占少量Flash
- 每个对象多一个指针（4字节），指向vtable
- 虚函数调用比普通函数调用多一次间接跳转（纳秒级）
- **结论**：在STM32F4/H7上放心用，这点开销可以忽略

### 15.4 RTTI和异常：嵌入式里最好关掉

```cpp
// 在编译器选项里关闭：
// -fno-rtti   (关闭运行时类型识别，省Flash)
// -fno-exceptions (关闭C++异常，省Flash和RAM)

// 嵌入式里用返回值判断错误，不用异常：
// ❌ 
void setCurrent(float current) {
    if (current > MAX_CURRENT) throw "Overcurrent!";
}

// ✅
int setCurrent(float current) {
    if (current > MAX_CURRENT) return -1;  // 返回错误码
    CAN_Send(id, current);
    return 0;  // 成功
}
```

### 15.5 能用constexpr就用constexpr

```cpp
// ❌ 运行时计算
const float WHEEL_RADIUS = 0.076;  // 普通常量

// ✅ 编译时计算，省RAM，快
constexpr float WHEEL_RADIUS = 0.076f;
constexpr float WHEEL_CIRCUMFERENCE = 2 * 3.14159f * WHEEL_RADIUS;
// WHEEL_CIRCUMFERENCE 在编译时就计算好了，运行时直接用
```

---

## 16. RoboMaster实战：底盘模块完整示例

下面是一个完整的、可以直接参考的底盘控制代码框架。

```cpp
// ==================== Motor.h ====================
#pragma once
#include <cstdint>

class Motor {
private:
    uint16_t can_id;
    float current_angle;   // 当前角度 (度)
    float current_rpm;     // 当前转速 (转/分)
    float target_current;  // 目标电流 (mA)
    
    static constexpr float MAX_CURRENT = 10000.0f;  // 最大电流10A
    static constexpr float MIN_CURRENT = -10000.0f;
    
public:
    Motor() : can_id(0), current_angle(0), current_rpm(0), target_current(0) {}
    
    explicit Motor(uint16_t id) 
        : can_id(id), current_angle(0), current_rpm(0), target_current(0) {}
    
    // 设置目标电流（自动限幅）
    void setCurrent(float current) {
        if (current > MAX_CURRENT) target_current = MAX_CURRENT;
        else if (current < MIN_CURRENT) target_current = MIN_CURRENT;
        else target_current = current;
        CAN_SendCurrent(can_id, target_current);  // 通过CAN发送
    }
    
    // 从CAN更新电机反馈数据
    void updateFeedback() {
        current_angle = CAN_ReadAngle(can_id);
        current_rpm = CAN_ReadRpm(can_id);
    }
    
    // 停止电机
    void stop() { setCurrent(0); }
    
    // Getters
    float getAngle() const { return current_angle; }
    float getRpm() const { return current_rpm; }
    uint16_t getId() const { return can_id; }
};


// ==================== Chassis.h ====================
#pragma once
#include "Motor.h"
#include <array>
#include <cmath>

class Chassis {
protected:
    float speed_x, speed_y, speed_w;  // 底盘速度（世界坐标系）
    bool emergency_stop;
    
public:
    virtual ~Chassis() = default;
    virtual void setSpeed(float x, float y, float w) = 0;
    virtual void stop() = 0;
    virtual void emergencyStop() {
        emergency_stop = true;
        stop();
    }
    bool isStopped() const { return emergency_stop; }
};

// ==================== MecanumChassis.h ====================
#pragma once
#include "Chassis.h"

class MecanumChassis : public Chassis {
private:
    std::array<Motor, 4> motors;  // 4个麦轮电机
    const float wheel_radius;     // 轮子半径 (m)
    
    // 麦轮运动学解算
    void kinematicsInverse(float vx, float vy, float vw,
                           float& rpm_fl, float& rpm_fr,
                           float& rpm_rl, float& rpm_rr) {
        // 麦轮逆运动学公式
        // fl = 左前, fr = 右前, rl = 左后, rr = 右后
        rpm_fl = ( vx - vy - vw) / wheel_radius;
        rpm_fr = ( vx + vy + vw) / wheel_radius;
        rpm_rl = ( vx + vy - vw) / wheel_radius;
        rpm_rr = ( vx - vy + vw) / wheel_radius;
    }
    
public:
    MecanumChassis(const std::array<uint16_t, 4>& motor_ids, float radius)
        : wheel_radius(radius) {
        for (size_t i = 0; i < 4; ++i) {
            motors[i] = Motor(motor_ids[i]);
        }
        speed_x = speed_y = speed_w = 0;
        emergency_stop = false;
    }
    
    void setSpeed(float x, float y, float w) override {
        if (emergency_stop) return;
        
        speed_x = x;
        speed_y = y;
        speed_w = w;
        
        // 麦轮解算
        float rpm[4];
        kinematicsInverse(x, y, w, rpm[0], rpm[1], rpm[2], rpm[3]);
        
        // 发送给4个电机（这里简化，实际需要PID+电流换算）
        for (size_t i = 0; i < 4; ++i) {
            motors[i].setCurrent(rpm[i] * 1000.0f);  // 简化：转速→电流
        }
    }
    
    void stop() override {
        for (auto& motor : motors) {
            motor.stop();
        }
        speed_x = speed_y = speed_w = 0;
    }
    
    // 更新所有电机反馈
    void updateFeedback() {
        for (auto& motor : motors) {
            motor.updateFeedback();
        }
    }
    
    std::array<float, 4> getMotorRpms() const {
        std::array<float, 4> rpms;
        for (size_t i = 0; i < 4; ++i) {
            rpms[i] = motors[i].getRpm();
        }
        return rpms;
    }
};


// ==================== Gimbal.h (云台示例) ====================
#pragma once
#include "Motor.h"

class Gimbal {
private:
    Motor yaw_motor;    // Yaw轴电机
    Motor pitch_motor;  // Pitch轴电机
    
    float yaw_angle;    // 当前偏航角
    float pitch_angle;  // 当前俯仰角
    
    static constexpr float YAW_LIMIT = 180.0f;
    static constexpr float PITCH_MIN = -20.0f;
    static constexpr float PITCH_MAX = 35.0f;
    
public:
    Gimbal(uint16_t yaw_id, uint16_t pitch_id)
        : yaw_motor(yaw_id), pitch_motor(pitch_id),
          yaw_angle(0), pitch_angle(0) {}
    
    void setAngle(float target_yaw, float target_pitch) {
        // 角度限幅
        if (target_pitch < PITCH_MIN) target_pitch = PITCH_MIN;
        if (target_pitch > PITCH_MAX) target_pitch = PITCH_MAX;
        
        // 这里应该用PID计算电流，简化处理
        yaw_motor.setCurrent(target_yaw * 100);
        pitch_motor.setCurrent(target_pitch * 100);
        
        yaw_angle = target_yaw;
        pitch_angle = target_pitch;
    }
    
    void update() {
        yaw_motor.updateFeedback();
        pitch_motor.updateFeedback();
    }
    
    float getYaw() const { return yaw_angle; }
    float getPitch() const { return pitch_angle; }
};


// ==================== main.cpp ====================
#include "MecanumChassis.h"
#include "Gimbal.h"
#include <queue>

// 遥控器数据结构
struct RCData {
    float left_x, left_y;   // 左摇杆（底盘平移）
    float right_x, right_y; // 右摇杆（底盘旋转 + 云台俯仰）
    bool shoot;
    bool emergency;
};

// 全局对象（嵌入式里常见做法）
std::queue<RCData> rc_queue;  // 遥控器数据队列
MecanumChassis chassis({1, 2, 3, 4}, 0.076f);  // ID 1-4, 轮半径76mm
Gimbal gimbal(5, 6);  // Yaw电机ID=5, Pitch电机ID=6

// 遥控器中断回调
void onRCDataReceived(const RCData& data) {
    rc_queue.push(data);
}

// 主控制循环 (相当于FreeRTOS的一个任务)
void controlTask() {
    while (true) {
        // 1. 处理遥控器数据
        while (!rc_queue.empty()) {
            RCData data = rc_queue.front();
            rc_queue.pop();
            
            if (data.emergency) {
                chassis.emergencyStop();
                continue;
            }
            
            // 底盘控制：左摇杆控制平移
            chassis.setSpeed(data.left_x * 3000, data.left_y * 3000, data.right_x * 500);
            
            // 云台控制：根据右摇杆或其他数据
            gimbal.setAngle(data.right_x * 90, data.right_y * 35);
            
            // 射击控制
            if (data.shoot) {
                // shooter.fire();
            }
        }
        
        // 2. 更新反馈
        chassis.updateFeedback();
        gimbal.update();
        
        // 3. 延时 (FreeRTOS用 vTaskDelay)
        osDelay(1);  // 1ms控制周期 (1kHz)
    }
}
```

---

## 总结：C vs C++ 思维转变

| 方面 | C语言思维 | C++思维 |
|------|----------|---------|
| **组织方式** | 全局变量 + 全局函数 | 类 = 数据 + 方法打包 |
| **模块化** | 一个 .c 文件一堆函数 | 一个类一个 .h 文件，职责清晰 |
| **代码复用** | 复制粘贴，稍作修改 | 继承、模板 |
| **数据安全** | "我保证不改这个变量" | private 强制保护 |
| **扩展性** | 改代码，加 if-else | 继承/多态，不改老代码 |
| **内存管理** | malloc/free，容易忘 | 智能指针、RAII |
| **容器** | 手写链表、数组 | vector, queue, map 拿来就用 |

## 推荐学习路径

1. **先理解类和对象**（本文第3-6节），在你的开发板上写一个简单的 Motor 类，控制一个LED闪烁
2. **再学继承**（本文第7-9节），试着给上面的 Motor 类派生出一个 RGBMotor 类
3. **然后学STL容器**（本文第11-12节），把原来的C数组换成 vector/array
4. **最后看设计模式**（可选），单例模式、观察者模式在RoboMaster里很常见

**记住**：不要试图一次全部掌握。先写代码，遇到问题再回来查。面向对象是一种思维方式，多写自然就习惯了。

> 用C++写RoboMaster代码就像玩乐高——每个模块是一个积木块（类），你可以自由组合（继承、组合），坏了一块换一块就行（封装），不用整个拆了重来。

---

## 17. 嵌入式C++究竟用哪些？不用哪些？

你问的这个问题，本质上是：**"C++那么大，我学不完，嵌入式里到底哪些是真要用的？"**

答案很简单：

### C++ 在嵌入式里的真相：C with Classes

```
C++ for Embedded ≈ C语言 + 挑着用的C++特性

C语言的一切你照用：
  - 指针、数组、位操作、寄存器操作、中断、DMA... 全部和C一样
  - volatile、const、static... 全部照旧
  - malloc/free（虽然嵌入式里尽量不用）
  - 所有外设库的C API 完全兼容

C++只是在上面"加点料"：
  - 类（class）= 更聪明的struct
  - 继承 = 代码复用，少写重复代码
  - 命名空间 = 防止名字打架
  - STL里几个轻量容器 = 告别手写链表/队列
```

### 一张表说清楚：嵌入式里用哪些C++特性

| C++特性 | 嵌入式里用吗？ | 理由 |
|----------|:---:|------|
| **class 类** | ✅ 核心 | 把数据和函数打包，就是超级struct |
| **构造函数/析构函数** | ✅ 常用 | 初始化外设、释放资源 |
| **public/private** | ✅ 常用 | 保护关键变量，防止队友误改 |
| **继承** | ✅ 有用 | 底盘→麦轮/全向轮，不写重复代码 |
| **虚函数/多态** | ✅ 可以 | 开销极小（几纳秒），STM32H7随便用 |
| **namespace** | ✅ 推荐 | Chassis::Motor 和 Gimbal::Motor 不打架 |
| **constexpr** | ✅ 强推 | 编译时计算，省RAM零开销 |
| **enum class** | ✅ 强推 | 比C的enum安全，不会隐式转int |
| **引用 &** | ✅ 方便 | 传参不用检查NULL，比指针安全 |
| **std::array** | ✅ 强推 | 和安全检查，零开销 |
| **std::queue** | ✅ 有用 | 消息缓冲，替代手写环形队列 |
| **for (auto& x : arr)** | ✅ 方便 | 遍历数组更简洁 |
| **模板 template** | ⚠️ 适度 | 别写复杂模板，TinyTemplate可以 |
| **std::vector** | ⚠️ 谨慎 | 会在堆上分配，嵌入式尽量别用 |
| **std::map** | ⚠️ 谨慎 | 红黑树开销大，慎用 |
| **std::string** | ❌ 别用 | 堆分配 + 动态扩容，嵌入式噩梦 |
| **std::shared_ptr** | ❌ 别用 | 堆分配 + 引用计数，嵌入式别碰 |
| **C++异常 try/catch** | ❌ 关掉 | -fno-exceptions，省Flash+RAM |
| **RTTI dynamic_cast** | ❌ 关掉 | -fno-rtti，省Flash |
| **iostream cout/cin** | ❌ 别用 | 巨大无比，printf就够了 |
| **STL算法 sort/find** | ⚠️ 很少 | 嵌入式数据量小，手写循环更可控 |
| **Lambda表达式** | ⚠️ 偶尔 | sort时用一下可以，别写嵌套lambda |

### 嵌入式C++的精髓：挑着用

```
你真正要学的C++，就这么多：

必学（每天用）：
  class、构造函数、public/private、namespace、constexpr、enum class、引用&、std::array

选学（需要时用）：
  继承、虚函数、std::queue、简单模板、Lambda

不学（嵌入式里别碰）：
  iostream、std::string、智能指针、异常、RTTI、复杂STL容器
```

### 和C语言一样的部分（放心，没变）

```cpp
// 这些东西在C++里和C完全一样，照写就行：

// 寄存器操作
GPIOA->ODR |= (1 << 5);           // 和C一模一样
TIM1->CCR1 = 500;                  // 和C一模一样

// 中断
extern "C" void TIM2_IRQHandler() {  // 加个 extern "C" 就行
    // 和C一模一样
}

// volatile
volatile uint32_t* reg = (uint32_t*)0x40021000;  // 和C一模一样

// 位操作
uint8_t flags = 0;
flags |= (1 << 3);                 // 和C一模一样

// 指针运算
uint8_t* buf = &data[0];           // 和C一模一样
*buf++ = 0x42;                     // 和C一模一样
```

### 实际项目中的C++使用比例

```
一个典型的RoboMaster下位机项目：

C语言基础语法：  ████████████████████ 60%  （循环、判断、指针、位操作、寄存器操作）
class/封装：     ████████             25%  （Motor类、Chassis类、Gimbal类...）
namespace：      ██                   5%   （Chassis::、Gimbal::...）
STL轻量容器：    ██                   5%   （std::array、std::queue）
继承/多态：      █                    3%   （Chassis基类→MecanumChassis子类）
其他C++特性：    █                    2%   （constexpr、enum class、auto...）
```

**看到没？60%还是C语言的老本行，C++只是在上面加了一层"组织代码"的外壳。**

### 一句话总结

> 嵌入式C++ = 用C语言干活 + 用class把活干漂亮。

你不用成为C++专家，你只需要：
1. C语言的底子（你已经有了）
2. 会用class打包数据和函数（像超级struct那样用）
3. 会用namespace分模块
4. 知道STL里哪几个容器是安全的（array ✅, queue ✅, vector ❌）
5. 知道哪些特性在嵌入式里不能碰（异常、RTTI、iostream、string）

**剩下的，全是C语言。**