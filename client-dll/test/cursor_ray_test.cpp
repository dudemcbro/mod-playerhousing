// Checks CursorRay against made-up cameras: every way of looking, field of view, aspect ratio,
// screen units running either way and camera axes given either way round. The ray it finds
// must land back on the cursor through the same projection.
//
//     g++ -std=c++17 -O2 -I../src cursor_ray_test.cpp -o cursor_ray_test && ./cursor_ray_test

#include "CursorRay.h"

#include <cstdio>
#include <random>

using namespace PlayerHousingDll;

namespace
{
    struct Camera
    {
        Vec3 position;
        Vec3 forward;
        Vec3 right;
        Vec3 up;
        float verticalFov;
        float aspect;
        float width;   // screen units
        float height;
        bool xRight;
        bool yUp;

        bool Project(Vec3 point, float& sx, float& sy) const
        {
            Vec3 d = Sub(point, position);
            float depth = Dot(d, forward);
            if (depth < 0.01f)
                return false;
            float t = std::tan(verticalFov / 2.0f);
            float x = Dot(d, right) / depth / (t * aspect);  // -1 .. 1 across the view
            float y = Dot(d, up) / depth / t;
            if (!xRight)
                x = -x;
            if (!yUp)
                y = -y;
            sx = width / 2.0f * (1.0f + x);
            sy = height / 2.0f * (1.0f + y);
            return true;
        }
    };

    Camera Make(float yaw, float pitch, float verticalFov, float aspect, bool xRight, bool yUp)
    {
        Camera camera;
        camera.position = { 16222.0f, 16252.0f, 13.0f };
        camera.forward = { std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch) };
        // Right: the way faced, turned a quarter to the right (x north, y west, z up).
        Vec3 flatRight = { std::sin(yaw), -std::cos(yaw), 0.0f };
        camera.right = flatRight;
        camera.up = Cross(camera.right, camera.forward);
        camera.verticalFov = verticalFov;
        camera.aspect = aspect;
        camera.height = 0.75f;
        camera.width = 0.75f * aspect;
        camera.xRight = xRight;
        camera.yUp = yUp;
        return camera;
    }
}

// A quarter of a pixel on a 768 pixel high view (0.75 units high): floats at GM Island's
// coordinates (around 16000) round to about that.
constexpr float TOLERANCE = 0.75f / 768.0f / 4.0f;

int main()
{
    std::mt19937 random(12340);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    int checks = 0;
    int failures = 0;
    for (int round = 0; round < 20000; ++round)
    {
        float yaw = unit(random) * 6.2831853f;
        float pitch = (unit(random) * 2.0f - 1.0f) * 1.45f;
        float fov = 0.6f + unit(random) * 0.8f;
        float aspect = 1.0f + unit(random) * 1.5f;
        bool xRight = unit(random) < 0.8f;
        bool yUp = unit(random) < 0.5f;
        Camera camera = Make(yaw, pitch, fov, aspect, xRight, yUp);

        // The axes as the DLL reads them may point the other way.
        Vec3 givenRight = unit(random) < 0.5f ? camera.right : Scale(camera.right, -1.0f);
        Vec3 givenUp = unit(random) < 0.5f ? camera.up : Scale(camera.up, -1.0f);

        float fx = unit(random);
        float fy = unit(random);
        Vec3 direction;
        // The screen's ways are the game's own: learned once, from a look that isn't straight
        // up or down, and kept (as the DLL keeps them).
        ScreenAxes axes;
        Camera level = Make(yaw, 0.3f, fov, aspect, xRight, yUp);
        auto projectLevel = [&](Vec3 p, float& qx, float& qy) { return level.Project(p, qx, qy); };
        CursorRay(level.position, level.forward, level.right, level.up, 0.5f, 0.5f, projectLevel, axes, direction);
        if (!axes.known || axes.xRight != xRight || axes.yUp != yUp)
        {
            ++failures;
            std::printf("screen ways wrong: %d %d want %d %d\n", axes.xRight, axes.yUp, xRight, yUp);
        }

        auto project = [&](Vec3 p, float& sx, float& sy) { return camera.Project(p, sx, sy); };
        ++checks;
        if (!CursorRay(camera.position, camera.forward, givenRight, givenUp, fx, fy, project, axes, direction))
        {
            ++failures;
            std::printf("no ray: yaw %.2f pitch %.2f\n", yaw, pitch);
            continue;
        }
        float sx = 0.0f;
        float sy = 0.0f;
        camera.Project(Add(camera.position, Scale(direction, 30.0f)), sx, sy);
        float wantX = (xRight ? fx : 1.0f - fx) * camera.width;
        float wantY = (yUp ? fy : 1.0f - fy) * camera.height;
        if (std::fabs(sx - wantX) > TOLERANCE || std::fabs(sy - wantY) > TOLERANCE || std::fabs(Length(direction) - 1.0f) > 1e-4f)
        {
            ++failures;
            if (failures < 10)
                std::printf("off: yaw %.2f pitch %.2f fov %.2f aspect %.2f -> %.5f %.5f want %.5f %.5f\n", yaw, pitch, fov, aspect, sx, sy, wantX, wantY);
        }

        // Looking straight down, the screen's ways can't be told from the world: the ones
        // worked out before are kept.
        Camera down = Make(yaw, -1.5707963f, fov, aspect, xRight, yUp);
        down.up = Cross(down.right, down.forward);
        auto projectDown = [&](Vec3 p, float& qx, float& qy) { return down.Project(p, qx, qy); };
        ++checks;
        if (!CursorRay(down.position, down.forward, down.right, down.up, fx, fy, projectDown, axes, direction))
        {
            ++failures;
            continue;
        }
        down.Project(Add(down.position, Scale(direction, 30.0f)), sx, sy);
        wantX = (xRight ? fx : 1.0f - fx) * down.width;
        wantY = (yUp ? fy : 1.0f - fy) * down.height;
        if (std::fabs(sx - wantX) > TOLERANCE || std::fabs(sy - wantY) > TOLERANCE)
        {
            ++failures;
            if (failures < 10)
                std::printf("off looking down: %.5f %.5f want %.5f %.5f\n", sx, sy, wantX, wantY);
        }
    }

    // A wall facing the camera: its facing comes back toward the camera.
    Vec3 normal;
    Vec3 hit = { 10.0f, 0.0f, 1.0f };
    ++checks;
    if (!SurfaceNormal(hit, { 10.0f, -0.05f, 1.0f }, { 10.0f, 0.0f, 1.05f }, { -1.0f, 0.0f, 0.0f }, normal) || normal.x > -0.999f)
        ++failures;
    // Floor: up.
    ++checks;
    if (!SurfaceNormal({ 0.0f, 0.0f, 0.0f }, { 0.05f, 0.0f, 0.0f }, { 0.0f, 0.05f, 0.0f }, { 0.0f, 0.0f, 1.0f }, normal) || normal.z < 0.999f)
        ++failures;
    // Points on different surfaces: none.
    ++checks;
    if (SurfaceNormal({ 0.0f, 0.0f, 0.0f }, { 5.0f, 0.0f, 0.0f }, { 0.0f, 0.05f, 0.0f }, { 0.0f, 0.0f, 1.0f }, normal))
        ++failures;

    std::printf("%d/%d cursor ray checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
