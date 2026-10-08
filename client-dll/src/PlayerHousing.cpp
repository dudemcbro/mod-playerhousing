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
//   PlayerHousing_CursorRay([fx, fy]) -> x, y, z, dx, dy, dz  (nothing outside the world)
//     The ray itself: where the camera is, and the way through the cursor (length 1). The addon
//     meets it with the floor a held piece stands on, when the first thing it hits is the piece.
//   PlayerHousing_PlaceUnit(guidHigh, guidLow, x, y, z, facing) -> 1, or nothing
//     Shows a creature (the see-through ghost of a piece being placed) at that spot on this
//     screen, every frame if need be, without waiting for the server to move it there: the
//     unit's own position and facing, as the game keeps them, are set. guidHigh and guidLow are
//     the two halves of its guid (a Lua number can't hold all 64 bits). Nothing when there's no
//     such unit, or the world isn't loaded.
//   PlayerHousing_DLLInfo() -> a line saying what the DLL is doing (for bug reports)
//   PlayerHousingDLL: its version, a number (nil without the DLL)
//
// Nothing else: it reads the camera and asks the game the same question the game asks for its
// own line of sight checks, and writes a unit's position where the game keeps it (the same
// place the game reads it from to draw it). It only works with Wow.exe 3.3.5a build 12340 and
// does nothing with any other (the addresses below are that build's). Nothing is traced or
// moved during a loading screen. PlayerHousingLauncher.exe loads it.

#include "CursorRay.h"

#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MinHook.h"

using namespace PlayerHousingDll;

namespace
{
    constexpr int VERSION = 3;
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
    // Non-zero while a loading screen (or the connection to the realm) is up.
    auto const LOADING = reinterpret_cast<uint32_t const volatile*>(0x00B6AA38);
    auto const GAME_WINDOW = reinterpret_cast<HWND const volatile*>(0x00D41620);
    auto const WORLD_FRAME = reinterpret_cast<void* const volatile*>(0x00B7436C);
    auto const GetActiveCamera = reinterpret_cast<uint8_t* (*)()>(0x004F5960);
    // CGWorldFrame::GetScreenCoordinates (a thiscall: fastcall with edx unused).
    auto const WorldToScreen = reinterpret_cast<int(__fastcall*)(void*, void*, Vec3*, Vec3*, uint32_t*)>(0x004F6D20);
    // TraceLine(start, end, hit, fraction of the way, flags, 0): true when something is in the way.
    auto const TraceLine = reinterpret_cast<uint8_t (*)(Vec3*, Vec3*, Vec3*, float*, uint32_t, uint32_t)>(0x007A3B70);
    // ClntObjMgrObjectPtr(guid, type mask): the object with that guid, if it's of that type.
    auto const ObjectPtr = reinterpret_cast<uint8_t* (__cdecl*)(uint64_t, uint32_t)>(0x004D4DB0);
    constexpr uint32_t TYPEMASK_UNIT = 0x0008;
    // A unit's position (x, y, z) and facing, where the game keeps them.
    constexpr size_t UNIT_POSITION = 0x798;
    constexpr size_t UNIT_FACING = 0x7A8;
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

    // In the world, with nothing loading: the world is there to trace through and to show units in.
    bool WorldReady()
    {
        return *IN_WORLD && *LOADING == 0;
    }

    int Lua_CursorWorld(lua_State* L)
    {
        // The world frame first: the camera belongs to it.
        void* worldFrame = WorldReady() ? *WORLD_FRAME : nullptr;
        uint8_t const* camera = worldFrame ? GetActiveCamera() : nullptr;
        if (!camera)
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

    int Lua_CursorRay(lua_State* L)
    {
        void* worldFrame = WorldReady() ? *WORLD_FRAME : nullptr;
        uint8_t const* camera = worldFrame ? GetActiveCamera() : nullptr;
        if (!camera)
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

        Vec3 position = ReadVec(camera, CAMERA_POSITION);
        Vec3 way;
        auto project = [worldFrame](Vec3 p, float& sx, float& sy) { return Project(worldFrame, p, sx, sy); };
        if (!CursorRay(position, ReadVec(camera, CAMERA_FORWARD), ReadVec(camera, CAMERA_RIGHT), ReadVec(camera, CAMERA_UP), fx, fy, project, g_axes, way))
            return 0;
        lua_pushnumber(L, position.x);
        lua_pushnumber(L, position.y);
        lua_pushnumber(L, position.z);
        lua_pushnumber(L, way.x);
        lua_pushnumber(L, way.y);
        lua_pushnumber(L, way.z);
        return 6;
    }

    int Lua_PlaceUnit(lua_State* L)
    {
        for (int i = 1; i <= 6; ++i)
            if (lua_type(L, i) != LUA_TNUMBER)
                return 0;
        double high = lua_tonumber(L, 1);
        double low = lua_tonumber(L, 2);
        if (!(high >= 0.0 && high <= 4294967295.0 && low >= 0.0 && low <= 4294967295.0))
            return 0;
        float x = float(lua_tonumber(L, 3));
        float y = float(lua_tonumber(L, 4));
        float z = float(lua_tonumber(L, 5));
        float facing = float(lua_tonumber(L, 6));
        // Somewhere on a map (they're 34133 yards across, centred on 0), and a real angle.
        if (!(std::fabs(x) < 20000.0f && std::fabs(y) < 20000.0f && std::fabs(z) < 20000.0f && std::isfinite(facing)))
            return 0;
        if (!WorldReady())
            return 0;
        uint64_t guid = (uint64_t(uint32_t(high)) << 32) | uint32_t(low);
        uint8_t* unit = guid ? ObjectPtr(guid, TYPEMASK_UNIT) : nullptr;
        if (!unit)
            return 0;
        facing = std::fmod(facing, 6.28318530717958647692f);
        if (facing < 0.0f)
            facing += 6.28318530717958647692f;
        Vec3 position = { x, y, z };
        std::memcpy(unit + UNIT_POSITION, &position, sizeof(position));
        std::memcpy(unit + UNIT_FACING, &facing, sizeof(facing));
        lua_pushnumber(L, 1.0);
        return 1;
    }

    int Lua_DLLInfo(lua_State* L)
    {
        char line[400];
        int length = std::snprintf(line, sizeof(line), "PlayerHousing.dll %d: %s (added to Lua %u times; screen %s, %s)", VERSION, g_status,
            g_registrations, g_axes.xRight ? "x right" : "x left", g_axes.yUp ? "y up" : "y down");
        // Where the camera is and which way it looks, for when a point comes out wrong.
        uint8_t const* camera = WorldReady() && *WORLD_FRAME ? GetActiveCamera() : nullptr;
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
        lua_pushcclosure(L, Lua_CursorRay, 0);
        lua_setfield(L, LUA_GLOBALSINDEX, "PlayerHousing_CursorRay");
        lua_pushcclosure(L, Lua_PlaceUnit, 0);
        lua_setfield(L, LUA_GLOBALSINDEX, "PlayerHousing_PlaceUnit");
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
