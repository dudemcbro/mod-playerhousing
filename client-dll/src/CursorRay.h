// The ray from the camera through a point on the screen, worked out with the game's own
// world-to-screen projection, so there is no field of view or aspect ratio to get wrong.
//
// Points on a flat sheet square to the camera's view land on the screen in a straight-line
// (affine) way: where three points of the sheet land gives where every other one does. The
// sheet twenty yards ahead of the camera, its middle and a step right and up from it, tells
// the whole mapping; the cursor's point on the sheet follows, and the ray goes through it. One
// more projection checks the answer and corrects any rounding. (Far out and big steps: world
// positions are large numbers, around 16000 on GM Island, and a float rounds them to about a
// thousandth of a yard.)
//
// No Windows or game code here: the test builds it on any machine with a made-up camera.

#pragma once

#include <cmath>

namespace PlayerHousingDll
{
    struct Vec3
    {
        float x;
        float y;
        float z;
    };

    inline Vec3 Add(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline Vec3 Sub(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline Vec3 Scale(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
    inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline Vec3 Cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    inline float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }

    inline Vec3 Normalize(Vec3 a)
    {
        float length = Length(a);
        return length > 1e-6f ? Scale(a, 1.0f / length) : a;
    }

    // Which way the game's screen units run (they are measured from a corner of the view,
    // whose middle is half its size). Worked out while the camera looks level enough to tell,
    // and kept for when it looks straight down.
    struct ScreenAxes
    {
        bool known = false;
        bool xRight = true;  // x grows to the right
        bool yUp = true;     // y grows upward
    };

    // camera: where it is; forward, right, up: the camera's axes (right and up may point the
    //   other way: the mapping takes care of that).
    // fx, fy: the cursor as a fraction of the view, from its bottom-left corner.
    // project(world, sx, sy): the game's world to screen; false when it can't say.
    // direction: the ray's way, one yard long.
    template <typename Project>
    bool CursorRay(Vec3 camera, Vec3 forward, Vec3 right, Vec3 up, float fx, float fy, Project&& project, ScreenAxes& axes, Vec3& direction)
    {
        constexpr float DEPTH = 20.0f;
        constexpr float STEP = 2.0f;
        forward = Normalize(forward);
        right = Normalize(right);
        up = Normalize(up);
        Vec3 middle = Add(camera, Scale(forward, DEPTH));
        float mx;
        float my;
        float rx;
        float ry;
        float ux;
        float uy;
        if (!project(middle, mx, my) || !project(Add(middle, Scale(right, STEP)), rx, ry) || !project(Add(middle, Scale(up, STEP)), ux, uy))
            return false;
        if (!(mx > 0.0f) || !(my > 0.0f))
            return false;

        // Screen units per yard on the sheet, along the camera's right and up.
        float j00 = (rx - mx) / STEP;
        float j01 = (ux - mx) / STEP;
        float j10 = (ry - my) / STEP;
        float j11 = (uy - my) / STEP;
        float det = j00 * j11 - j01 * j10;
        if (!std::isfinite(det) || std::fabs(det) < 1e-6f * (mx * my + 1e-6f))
            return false;

        // Which way the screen units run: the world's up shows up the screen, and the way
        // the camera faces, turned right (x north, y west, z up), shows to the right.
        Vec3 worldUp = { 0.0f, 0.0f, 1.0f };
        Vec3 worldRight = Cross(forward, worldUp);
        if (Length(worldRight) > 0.2f)
        {
            float upA = Dot(worldUp, right);
            float upB = Dot(worldUp, up);
            float rightA = Dot(worldRight, right);
            float rightB = Dot(worldRight, up);
            axes.yUp = j10 * upA + j11 * upB > 0.0f;
            axes.xRight = j00 * rightA + j01 * rightB > 0.0f;
            axes.known = true;
        }

        // The cursor in screen units: the view is twice its middle.
        float tx = (axes.xRight ? fx : 1.0f - fx) * 2.0f * mx;
        float ty = (axes.yUp ? fy : 1.0f - fy) * 2.0f * my;

        auto solve = [&](float ex, float ey, float& a, float& b)
        {
            a = (j11 * ex - j01 * ey) / det;
            b = (-j10 * ex + j00 * ey) / det;
        };
        float a;
        float b;
        solve(tx - mx, ty - my, a, b);
        Vec3 way = Add(Scale(forward, DEPTH), Add(Scale(right, a), Scale(up, b)));

        // Check: where does that land? Correct what's left over (twice: rounding).
        for (int pass = 0; pass < 2; ++pass)
        {
            float cx;
            float cy;
            if (!project(Add(camera, way), cx, cy))
                break;
            float da;
            float db;
            solve(tx - cx, ty - cy, da, db);
            if (!std::isfinite(da) || !std::isfinite(db) || std::fabs(da) >= DEPTH || std::fabs(db) >= DEPTH)
                break;
            way = Add(way, Add(Scale(right, da), Scale(up, db)));
        }

        if (!std::isfinite(way.x) || !std::isfinite(way.y) || !std::isfinite(way.z) || Dot(way, forward) <= 0.0f)
            return false;
        direction = Normalize(way);
        return true;
    }

    // The surface's facing from three nearby points on it (the cursor's and two just beside),
    // turned toward the camera. False when they aren't on one surface.
    inline bool SurfaceNormal(Vec3 hit, Vec3 side, Vec3 above, Vec3 toward, Vec3& normal)
    {
        Vec3 a = Sub(side, hit);
        Vec3 b = Sub(above, hit);
        if (Length(a) > 2.0f || Length(b) > 2.0f)
            return false;
        Vec3 n = Cross(a, b);
        if (Length(n) < 1e-6f)
            return false;
        n = Normalize(n);
        if (Dot(n, toward) < 0.0f)
            n = Scale(n, -1.0f);
        normal = n;
        return true;
    }
}
