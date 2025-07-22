#pragma once

struct Vec2 {
    float x, y;
};

struct Vec3 {
    float x, y, z;
    Vec3 operator+(const Vec3& rhs) const {
        return { x + rhs.x, y + rhs.y, z + rhs.z };
    }
};

struct QAngle {
    float pitch, yaw, roll;
    QAngle operator+(const QAngle& rhs) const {
        return { pitch + rhs.pitch, yaw + rhs.yaw, roll + rhs.roll };
    }
};