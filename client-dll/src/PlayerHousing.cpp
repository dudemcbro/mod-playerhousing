// PlayerHousing.dll: tells the Player Housing addon where the mouse points in the world, so a
// piece being placed can follow the cursor and a click can set it down. World of Warcraft
// 3.3.5a (build 12340) has no way to ask that from an addon; this adds one:
//
//   PlayerHousing_CursorWorld([fx, fy [, flags [, reach]]])
//       -> x, y, z [, nx, ny, nz]  (nothing when the cursor points at the sky)
//     fx, fy: the cursor as a fraction of WorldFrame from its bottom-left corner (without
//     them: of the game window). x, y, z: the first thing the ray from the camera hits
//     (ground, buildings, furniture with collision; not creatures or players). nx, ny, nz:
//     which way that surface faces, when it can tell.
//   PlayerHousing_DLLInfo() -> a line saying what the DLL is doing (for bug reports)
//   PlayerHousingDLL: its version, a number (nil without the DLL)
//
// Nothing else: it reads the camera and asks the game the same question the game asks for its
// own line of sight checks. It only works with Wow.exe 3.3.5a build 12340 and does nothing
// with any other (the addresses below are that build's). PlayerHousingLauncher.exe loads it.

#include "CursorRay.h"

#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MinHook.h"

using namespace PlayerHousingDll;

namespace
{
    constexpr int VERSION = 1;
    constexpr uint32_t DEFAULT_FLAGS = 0x100111;  // the game's line of sight: ground, buildings, doodads, objects
    constexpr float DEFAULT_REACH = 200.0f;       // yards
    constexpr float NORMAL_STEP = 0.004f;         // of the view: the points beside the cursor for a surface's facing

    // Wow.exe 3.3.5a (12340).
    struct lua_State;
    using lua_CFunction = int (*)(lua_State*);
    constexpr int LUA_GLOBALSINDEX = -10002;
    constexpr int LUA_TNIL = 0;
    constexpr int LUA_TNUMBER = 3;

    auto const GetLuaState = reinterpret_cast<lua_State* (*)()>(0x00817DB0);
    auto const lua_gettop = reinterpret_cast<int (*)(lua_State*)>(0x0084DBD0);
    auto const lua_settop = reinterpret_cast<void (*)(lua_State*, int)>(0x0084DBF0);
    auto const lua_type = reinterpret_cast<int (*)(lua_State*, int)>(0x0084DEB0);
    auto const lua_tonumber = reinterpret_cast<double (*)(lua_State*, int)>(0x0084E030);
    auto const lua_pushnumber = reinterpret_cast<void (*)(lua_State*, double)>(0x0084E2A0);
    auto const lua_pushstring = reinterpret_cast<void (*)(lua_State*, char const*)>(0x0084E350);
    auto const lua_pushcclosure = reinterpret_cast<void (*)(lua_State*, lua_CFunction, int)>(0x0084E400);
    auto const lua_rawget = reinterpret_cast<void (*)(lua_State*, int)>(0x0084E600);
    auto const lua_setfield = reinterpret_cast<void (*)(lua_State*, int, char const*)>(0x0084E900);

    // Lua only calls C functions inside this range: widened so it calls ours.
    auto const FUNCTION_POINTER_MIN = reinterpret_cast<uint32_t volatile*>(0x00D415B8);
    auto const FUNCTION_POINTER_MAX = reinterpret_cast<uint32_t volatile*>(0x00D415BC);

    auto const IN_WORLD = reinterpret_cast<char const volatile*>(0x00BD0792);
    auto const GAME_WINDOW = reinterpret_cast<HWND const volatile*>(0x00D41620);
    auto const WORLD_FRAME = reinterpret_cast<void* const volatile*>(0x00B7436C);
    auto const GetActiveCamera = reinterpret_cast<uint8_t* (*)()>(0x004F5960);
    // CGWorldFrame::GetScreenCoordinates (a thiscall: fastcall with edx unused).
    auto const WorldToScreen = reinterpret_cast<int(__fastcall*)(void*, void*, Vec3*, Vec3*, uint32_t*)>(0x004F6D20);
    // TraceLine(start, end, hit, fraction of the way, flags, 0): true when something is in the way.
    auto const TraceLine = reinterpret_cast<uint8_t (*)(Vec3*, Vec3*, Vec3*, float*, uint32_t, uint32_t)>(0x007A3B70);
    // FrameScript_FireOnUpdate: every frame, on the game's own thread.
    constexpr uintptr_t FIRE_ON_UPDATE = 0x00495810;
    using FireOnUpdate_t = int (*)(int, int, int, int);
    FireOnUpdate_t OriginalFireOnUpdate = nullptr;

    // The camera: where it is (+0x08), then its forward, right and up axes (+0x14, +0x20, +0x2C).
    constexpr size_t CAMERA_POSITION = 0x08;
    constexpr size_t CAMERA_FORWARD = 0x14;
    constexpr size_t CAMERA_RIGHT = 0x20;
    constexpr size_t CAMERA_UP = 0x2C;

    char g_status[160] = "not started";
    char g_logPath[MAX_PATH] = "";
    unsigned g_registrations = 0;
    ScreenAxes g_axes;

    void Log(char const* format, ...)
    {
        if (!g_logPath[0])
            return;
        FILE* file = std::fopen(g_logPath, "a");
        if (!file)
            return;
        SYSTEMTIME now;
        GetLocalTime(&now);
        std::fprintf(file, "%02d:%02d:%02d ", now.wHour, now.wMinute, now.wSecond);
        va_list args;
        va_start(args, format);
        std::vfprintf(file, format, args);
        va_end(args);
        std::fputc('\n', file);
        std::fclose(file);
    }

    void SetStatus(char const* text)
    {
        std::snprintf(g_status, sizeof(g_status), "%s", text);
        Log("%s", text);
    }

    // Wow.exe's own version, from its file: 3.3.5.12340 or nothing is touched.
    bool IsBuild12340(char* found, size_t size)
    {
        char path[MAX_PATH];
        if (!GetModuleFileNameA(nullptr, path, MAX_PATH))
            return false;
        DWORD handle = 0;
        DWORD length = GetFileVersionInfoSizeA(path, &handle);
        if (!length)
        {
            std::snprintf(found, size, "no version information");
            return false;
        }
        char* data = new char[length];
        bool ok = false;
        VS_FIXEDFILEINFO* info = nullptr;
        UINT infoLength = 0;
        if (GetFileVersionInfoA(path, 0, length, data) && VerQueryValueA(data, "\\", reinterpret_cast<void**>(&info), &infoLength) && info)
        {
            unsigned major = HIWORD(info->dwFileVersionMS);
            unsigned minor = LOWORD(info->dwFileVersionMS);
            unsigned patch = HIWORD(info->dwFileVersionLS);
            unsigned build = LOWORD(info->dwFileVersionLS);
            std::snprintf(found, size, "%u.%u.%u.%u", major, minor, patch, build);
            ok = major == 3 && minor == 3 && patch == 5 && build == 12340;
        }
        delete[] data;
        return ok;
    }

    Vec3 ReadVec(uint8_t const* base, size_t offset)
    {
        Vec3 v;
        std::memcpy(&v, base + offset, sizeof(v));
        return v;
    }

    bool Project(void* worldFrame, Vec3 point, float& sx, float& sy)
    {
        Vec3 screen = { 0.0f, 0.0f, 0.0f };
        uint32_t mask = 0;
        WorldToScreen(worldFrame, nullptr, &point, &screen, &mask);
        if (!std::isfinite(screen.x) || !std::isfinite(screen.y))
            return false;
        sx = screen.x;
        sy = screen.y;
        return true;
    }

    // The first thing along the ray from the camera through (fx, fy).
    bool Trace(uint8_t const* camera, void* worldFrame, float fx, float fy, uint32_t flags, float reach, Vec3& hit, Vec3& way)
    {
        Vec3 position = ReadVec(camera, CAMERA_POSITION);
        auto project = [worldFrame](Vec3 p, float& sx, float& sy) { return Project(worldFrame, p, sx, sy); };
        if (!CursorRay(position, ReadVec(camera, CAMERA_FORWARD), ReadVec(camera, CAMERA_RIGHT), ReadVec(camera, CAMERA_UP), fx, fy, project, g_axes, way))
            return false;
        Vec3 start = Add(position, Scale(way, 0.1f));
        Vec3 end = Add(position, Scale(way, reach));
        Vec3 point = { 0.0f, 0.0f, 0.0f };
        float fraction = 1.0f;
        if (!TraceLine(&start, &end, &point, &fraction, flags, 0))
            return false;
        // Its hit point, or the fraction of the way when it didn't fill one in.
        if (point.x == 0.0f && point.y == 0.0f && point.z == 0.0f)
            point = Add(start, Scale(Sub(end, start), fraction));
        hit = point;
        return true;
    }

    bool CursorFromWindow(float& fx, float& fy)
    {
        HWND window = *GAME_WINDOW;
        POINT cursor;
        RECT client;
        if (!window || !GetCursorPos(&cursor) || !ScreenToClient(window, &cursor) || !GetClientRect(window, &client))
            return false;
        if (client.right <= 0 || client.bottom <= 0)
            return false;
        fx = float(cursor.x) / float(client.right);
        fy = 1.0f - float(cursor.y) / float(client.bottom);
        return true;
    }

    int Lua_CursorWorld(lua_State* L)
    {
        if (!*IN_WORLD)
            return 0;
        uint8_t const* camera = GetActiveCamera();
        void* worldFrame = *WORLD_FRAME;
        if (!camera || !worldFrame)
            return 0;

        float fx;
        float fy;
        if (lua_type(L, 1) == LUA_TNUMBER && lua_type(L, 2) == LUA_TNUMBER)
        {
            fx = float(lua_tonumber(L, 1));
            fy = float(lua_tonumber(L, 2));
        }
        else if (!CursorFromWindow(fx, fy))
            return 0;
        if (!(fx >= 0.0f && fx <= 1.0f && fy >= 0.0f && fy <= 1.0f))
            return 0;
        uint32_t flags = lua_type(L, 3) == LUA_TNUMBER ? uint32_t(lua_tonumber(L, 3)) : DEFAULT_FLAGS;
        float reach = lua_type(L, 4) == LUA_TNUMBER ? float(lua_tonumber(L, 4)) : DEFAULT_REACH;
        if (!(reach > 1.0f && reach <= 1000.0f))
            reach = DEFAULT_REACH;

        Vec3 hit;
        Vec3 way;
        if (!Trace(camera, worldFrame, fx, fy, flags, reach, hit, way))
            return 0;
        lua_pushnumber(L, hit.x);
        lua_pushnumber(L, hit.y);
        lua_pushnumber(L, hit.z);

        // Which way it faces, from two points just beside it.
        Vec3 side;
        Vec3 above;
        Vec3 unused;
        Vec3 normal;
        float sideX = fx + (fx < 0.5f ? NORMAL_STEP : -NORMAL_STEP);
        float aboveY = fy + (fy < 0.5f ? NORMAL_STEP : -NORMAL_STEP);
        if (Trace(camera, worldFrame, sideX, fy, flags, reach, side, unused) && Trace(camera, worldFrame, fx, aboveY, flags, reach, above, unused)
            && SurfaceNormal(hit, side, above, Scale(way, -1.0f), normal))
        {
            lua_pushnumber(L, normal.x);
            lua_pushnumber(L, normal.y);
            lua_pushnumber(L, normal.z);
            return 6;
        }
        return 3;
    }

    int Lua_DLLInfo(lua_State* L)
    {
        char line[400];
        int length = std::snprintf(line, sizeof(line), "PlayerHousing.dll %d: %s (added to Lua %u times; screen %s, %s)", VERSION, g_status,
            g_registrations, g_axes.xRight ? "x right" : "x left", g_axes.yUp ? "y up" : "y down");
        // Where the camera is and which way it looks, for when a point comes out wrong.
        uint8_t const* camera = *IN_WORLD ? GetActiveCamera() : nullptr;
        if (camera && length > 0 && size_t(length) < sizeof(line))
        {
            Vec3 at = ReadVec(camera, CAMERA_POSITION);
            Vec3 forward = ReadVec(camera, CAMERA_FORWARD);
            std::snprintf(line + length, sizeof(line) - length, "; camera %.1f %.1f %.1f looking %.2f %.2f %.2f", at.x, at.y, at.z,
                forward.x, forward.y, forward.z);
        }
        lua_pushstring(L, line);
        return 1;
    }

    // Each new Lua (logging in, /reload) gets the functions on its first frame.
    void RegisterIfNeeded()
    {
        lua_State* L = GetLuaState();
        if (!L)
            return;
        int top = lua_gettop(L);
        lua_pushstring(L, "PlayerHousingDLL");
        lua_rawget(L, LUA_GLOBALSINDEX);
        bool present = lua_type(L, -1) != LUA_TNIL;
        lua_settop(L, top);
        if (present)
            return;

        if (*FUNCTION_POINTER_MIN > 1)
            *FUNCTION_POINTER_MIN = 1;
        if (*FUNCTION_POINTER_MAX < 0x7FFFFFFF)
            *FUNCTION_POINTER_MAX = 0x7FFFFFFF;
        lua_pushcclosure(L, Lua_CursorWorld, 0);
        lua_setfield(L, LUA_GLOBALSINDEX, "PlayerHousing_CursorWorld");
        lua_pushcclosure(L, Lua_DLLInfo, 0);
        lua_setfield(L, LUA_GLOBALSINDEX, "PlayerHousing_DLLInfo");
        lua_pushnumber(L, VERSION);
        lua_setfield(L, LUA_GLOBALSINDEX, "PlayerHousingDLL");
        ++g_registrations;
        if (g_registrations <= 20)
            Log("added the functions to Lua (%u)", g_registrations);
    }

    int FireOnUpdateHook(int a1, int a2, int a3, int a4)
    {
        RegisterIfNeeded();
        return OriginalFireOnUpdate(a1, a2, a3, a4);
    }

    void Start(HMODULE self)
    {
        // The log goes next to the DLL: PlayerHousing.log.
        if (GetModuleFileNameA(self, g_logPath, MAX_PATH))
        {
            char* dot = std::strrchr(g_logPath, '.');
            if (dot)
                std::snprintf(dot, g_logPath + MAX_PATH - dot, ".log");
            if (FILE* file = std::fopen(g_logPath, "w"))
                std::fclose(file);
        }

        char version[64] = "unknown";
        if (!IsBuild12340(version, sizeof(version)))
        {
            char text[160];
            std::snprintf(text, sizeof(text), "off: this game is %s, not 3.3.5a (12340)", version);
            SetStatus(text);
            return;
        }
        if (MH_Initialize() != MH_OK)
        {
            SetStatus("off: MinHook could not start");
            return;
        }
        MH_STATUS created = MH_CreateHook(reinterpret_cast<void*>(FIRE_ON_UPDATE), reinterpret_cast<void*>(&FireOnUpdateHook),
            reinterpret_cast<void**>(&OriginalFireOnUpdate));
        MH_STATUS enabled = created == MH_OK ? MH_EnableHook(reinterpret_cast<void*>(FIRE_ON_UPDATE)) : created;
        if (enabled != MH_OK)
        {
            char text[160];
            std::snprintf(text, sizeof(text), "off: could not hook the frame update (%s)", MH_StatusToString(enabled));
            SetStatus(text);
            return;
        }
        SetStatus("ready (Wow.exe 3.3.5.12340)");
    }
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        Start(instance);
    }
    return TRUE;
}
