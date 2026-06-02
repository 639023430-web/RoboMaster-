#ifndef __MOTOR_HPP
#define __MOTOR_HPP

#include <cstdint>

class Motor {
  private:
    int id_;
    float rpm_;

  public:
    Motor() : id_(0), rpm_(0.0f) {}
    explicit Motor(int id) : id_(id), rpm_(0.0f) {}

    void setRpm(float rpm) { rpm_ = rpm; }
    float getRpm() const { return rpm_; }
    int getId() const { return id_; }
};

#endif // __MOTOR_HPP
