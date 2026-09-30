// A stand-in for Wow.exe 3.3.5a (12340) to test PlayerHousing.dll without the game: it puts
// small fakes of the functions the DLL uses at the same addresses (the Lua calls, the camera,
// world to screen, TraceLine, the frame update) and the same globals, loads the DLL, and
// checks what it does: the hook passes each frame on, the functions reach Lua (again after a
// /reload), and the cursor's ray lands where it should on a flat ground.
//
//     fake_game.exe              the test; prints PASS or what failed, exit code 0 when passed
//     fake_game.exe --launched   started by PlayerHousingLauncher.exe: says whether the DLL is
//                                loaded and what it was given (written to fake_game.result)
//
// Built by test/run_wine_test.sh; runs under Wine or Windows.

#include "CursorRay.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace PlayerHousingDll;

namespace
{
    // The game's memory from its functions to its globals: part of this program, so nothing
    // else can be there. (0x00495000 .. 0x00D42000 must fall inside.)
    alignas(4096) unsigned char g_reserve[0x980000];

    struct lua_State;
    using lua_CFunction = int (*)(lua_State*);
    constexpr int LUA_GLOBALSINDEX = -10002;
    constexpr int LUA_TNONE = -1;
    constexpr int LUA_TNIL = 0;
    constexpr int LUA_TNUMBER = 3;
    constexpr int LUA_TSTRING = 4;
    constexpr int LUA_TFUNCTION = 6;

    struct Value
    {
        int type = LUA_TNIL;
        double number = 0.0;
        std::string text;
        lua_CFunction function = nullptr;
    };

    std::vector<Value> g_stack;
    std::map<std::string, Value> g_globals;
    std::vector<std::string> g_problems;
    lua_State* const FAKE_STATE = reinterpret_cast<lua_State*>(0x5EED);
    int g_frames = 0;
    uint32_t g_lastFlags = 0;
    int g_traces = 0;
    int g_worldToScreenCalls = 0;

    void Problem(std::string const& text)
    {
        g_problems.push_back(text);
    }

    void CheckState(lua_State* L, char const* where)
    {
        if (L != FAKE_STATE)
            Problem(std::string(where) + ": wrong lua_State");
    }

    Value* At(int index)
    {
        int size = int(g_stack.size());
        if (index > 0)
            return index <= size ? &g_stack[index - 1] : nullptr;
        if (index < 0 && -index <= size)
            return &g_stack[size + index];
        return nullptr;
    }

    Value Pop()
    {
        Value v = g_stack.empty() ? Value() : g_stack.back();
        if (!g_stack.empty())
            g_stack.pop_back();
        return v;
    }

    lua_State* FakeGetLuaState() { return FAKE_STATE; }
    int FakeGetTop(lua_State* L) { CheckState(L, "lua_gettop"); return int(g_stack.size()); }
    void FakeSetTop(lua_State* L, int index)
    {
        CheckState(L, "lua_settop");
        if (index >= 0)
            g_stack.resize(size_t(index));
        else
            g_stack.resize(g_stack.size() + size_t(index + 1));
    }
    int FakeType(lua_State* L, int index)
    {
        CheckState(L, "lua_type");
        Value* v = At(index);
        return v ? v->type : LUA_TNONE;
    }
    double FakeToNumber(lua_State* L, int index)
    {
        CheckState(L, "lua_tonumber");
        Value* v = At(index);
        return v && v->type == LUA_TNUMBER ? v->number : 0.0;
    }
    void FakePushNumber(lua_State* L, double n)
    {
        CheckState(L, "lua_pushnumber");
        Value v;
        v.type = LUA_TNUMBER;
        v.number = n;
        g_stack.push_back(v);
    }
    void FakePushString(lua_State* L, char const* s)
    {
        CheckState(L, "lua_pushstring");
        Value v;
        v.type = LUA_TSTRING;
        v.text = s ? s : "";
        g_stack.push_back(v);
    }
    void FakePushCClosure(lua_State* L, lua_CFunction f, int upvalues)
    {
        CheckState(L, "lua_pushcclosure");
        // The game's check: C functions must be inside this range.
        uint32_t address = reinterpret_cast<uint32_t>(f);
        if (address < *reinterpret_cast<uint32_t*>(0x00D415B8) || address > *reinterpret_cast<uint32_t*>(0x00D415BC))
            Problem("lua_pushcclosure: invalid function pointer (the range wasn't widened)");
        if (upvalues != 0)
            Problem("lua_pushcclosure: upvalues");
        Value v;
        v.type = LUA_TFUNCTION;
        v.function = f;
        g_stack.push_back(v);
    }
    void FakeRawGet(lua_State* L, int index)
    {
        CheckState(L, "lua_rawget");
        if (index != LUA_GLOBALSINDEX)
            Problem("lua_rawget: not the globals");
        Value key = Pop();
        auto found = g_globals.find(key.text);
        g_stack.push_back(found != g_globals.end() ? found->second : Value());
    }
    void FakeSetField(lua_State* L, int index, char const* name)
    {
        CheckState(L, "lua_setfield");
        if (index != LUA_GLOBALSINDEX)
            Problem("lua_setfield: not the globals");
        g_globals[name] = Pop();
    }

    int FakeFireOnUpdate(int a1, int a2, int a3, int a4)
    {
        ++g_frames;
        return a1 + a2 * 10 + a3 * 100 + a4 * 1000;
    }

    // The camera: 18 yards up, looking north-north-west and down.
    struct FakeCamera
    {
        uint32_t vtable = 0;
        uint32_t m2scene = 0;
        Vec3 position = { 16222.0f, 16252.0f, 30.0f };
        Vec3 forward;
        Vec3 right;
        Vec3 up;
        float nearClip = 0.2f;
        float farClip = 500.0f;
        float fov = 1.5708f;
        float aspect = 16.0f / 9.0f;
    };
    static_assert(offsetof(FakeCamera, position) == 0x08, "camera position");
    static_assert(offsetof(FakeCamera, forward) == 0x14, "camera forward");
    static_assert(offsetof(FakeCamera, right) == 0x20, "camera right");
    static_assert(offsetof(FakeCamera, up) == 0x2C, "camera up");
    FakeCamera g_camera;
    constexpr float VERTICAL_FOV = 0.77f;
    constexpr float SCREEN_HEIGHT = 0.75f;  // the game's screen units
    constexpr float GROUND_Z = 12.0f;
    uint32_t g_worldFrame[64];

    FakeCamera* FakeGetActiveCamera() { return &g_camera; }

    // CGWorldFrame::GetScreenCoordinates is a thiscall: the world frame in ecx.
    int __attribute__((thiscall)) FakeWorldToScreen(void* self, Vec3* world, Vec3* screen, uint32_t* mask)
    {
        ++g_worldToScreenCalls;
        if (self != g_worldFrame)
            Problem("world to screen: the world frame didn't arrive (calling convention?)");
        Vec3 d = Sub(*world, g_camera.position);
        float depth = Dot(d, g_camera.forward);
        float t = std::tan(VERTICAL_FOV / 2.0f);
        float x = Dot(d, g_camera.right) / depth / (t * g_camera.aspect);
        float y = Dot(d, g_camera.up) / depth / t;
        float width = SCREEN_HEIGHT * g_camera.aspect;
        screen->x = width / 2.0f * (1.0f + x);
        screen->y = SCREEN_HEIGHT / 2.0f * (1.0f + y);
        screen->z = depth;
        if (mask)
            *mask = 0;
        return depth > 0.0f && std::fabs(x) <= 1.0f && std::fabs(y) <= 1.0f;
    }

    // Flat ground at GROUND_Z.
    uint8_t FakeTraceLine(Vec3* start, Vec3* end, Vec3* hit, float* fraction, uint32_t flags, uint32_t optional)
    {
        ++g_traces;
        g_lastFlags = flags;
        if (optional != 0)
            Problem("TraceLine: the last argument isn't 0");
        if (*fraction != 1.0f)
            Problem("TraceLine: the fraction didn't start at 1");
        float a = start->z - GROUND_Z;
        float b = end->z - GROUND_Z;
        if (a <= 0.0f || b > 0.0f)
            return 0;
        float f = a / (a - b);
        *fraction = f;
        *hit = Add(*start, Scale(Sub(*end, *start), f));
        return 1;
    }

    // Where the cursor's ray meets the ground, worked out plainly from the same camera.
    Vec3 Expected(float fx, float fy)
    {
        float t = std::tan(VERTICAL_FOV / 2.0f);
        Vec3 way = Add(g_camera.forward, Add(Scale(g_camera.right, (2.0f * fx - 1.0f) * t * g_camera.aspect), Scale(g_camera.up, (2.0f * fy - 1.0f) * t)));
        float s = (GROUND_Z - g_camera.position.z) / way.z;
        return Add(g_camera.position, Scale(way, s));
    }

    void Jump(uint32_t from, void const* to)
    {
        unsigned char* at = reinterpret_cast<unsigned char*>(from);
        at[0] = 0xE9;
        int32_t relative = int32_t(reinterpret_cast<uint32_t>(to) - (from + 5));
        std::memcpy(at + 1, &relative, 4);
    }

    bool SetUpGame(std::string& error)
    {
        uint32_t low = reinterpret_cast<uint32_t>(g_reserve);
        uint32_t high = low + sizeof(g_reserve);
        if (low > 0x00495000 || high < 0x00D42000)
        {
            char text[128];
            std::snprintf(text, sizeof(text), "the reserve is at %08X..%08X, not over 00495000..00D42000", unsigned(low), unsigned(high));
            error = text;
            return false;
        }
        DWORD old;
        if (!VirtualProtect(reinterpret_cast<void*>(0x00495000), 0x00D42000 - 0x00495000, PAGE_EXECUTE_READWRITE, &old))
        {
            error = "VirtualProtect failed";
            return false;
        }
        Jump(0x00495810, reinterpret_cast<void const*>(&FakeFireOnUpdate));
        Jump(0x004F5960, reinterpret_cast<void const*>(&FakeGetActiveCamera));
        Jump(0x004F6D20, reinterpret_cast<void const*>(&FakeWorldToScreen));
        Jump(0x007A3B70, reinterpret_cast<void const*>(&FakeTraceLine));
        Jump(0x00817DB0, reinterpret_cast<void const*>(&FakeGetLuaState));
        Jump(0x0084DBD0, reinterpret_cast<void const*>(&FakeGetTop));
        Jump(0x0084DBF0, reinterpret_cast<void const*>(&FakeSetTop));
        Jump(0x0084DEB0, reinterpret_cast<void const*>(&FakeType));
        Jump(0x0084E030, reinterpret_cast<void const*>(&FakeToNumber));
        Jump(0x0084E2A0, reinterpret_cast<void const*>(&FakePushNumber));
        Jump(0x0084E350, reinterpret_cast<void const*>(&FakePushString));
        Jump(0x0084E400, reinterpret_cast<void const*>(&FakePushCClosure));
        Jump(0x0084E600, reinterpret_cast<void const*>(&FakeRawGet));
        Jump(0x0084E900, reinterpret_cast<void const*>(&FakeSetField));
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(0x00495000), 0x00D42000 - 0x00495000);

        // Lua's C function range: just this program's code, as in the game.
        *reinterpret_cast<uint32_t*>(0x00D415B8) = 0x00401000;
        *reinterpret_cast<uint32_t*>(0x00D415BC) = 0x00480000;
        *reinterpret_cast<char*>(0x00BD0792) = 1;  // in the world
        *reinterpret_cast<void**>(0x00B7436C) = g_worldFrame;
        *reinterpret_cast<HWND*>(0x00D41620) = nullptr;

        // Looking north-north-west, 35 degrees down.
        float yaw = 0.35f;
        float pitch = -0.61f;
        g_camera.forward = { std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch) };
        g_camera.right = { std::sin(yaw), -std::cos(yaw), 0.0f };
        g_camera.up = Cross(g_camera.right, g_camera.forward);
        return true;
    }

    std::string Folder()
    {
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string text = path;
        size_t slash = text.find_last_of("\\/");
        return slash == std::string::npos ? "." : text.substr(0, slash);
    }

    std::string ReadFile(std::string const& path)
    {
        std::string text;
        if (FILE* file = std::fopen(path.c_str(), "rb"))
        {
            char buffer[4096];
            size_t n;
            while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
                text.append(buffer, n);
            std::fclose(file);
        }
        return text;
    }

    // Calls a Lua function the DLL added, as Lua would: the arguments on an empty stack.
    std::vector<Value> Call(char const* name, std::vector<double> const& arguments)
    {
        std::vector<Value> results;
        auto found = g_globals.find(name);
        if (found == g_globals.end() || found->second.type != LUA_TFUNCTION)
        {
            Problem(std::string(name) + " isn't in Lua");
            return results;
        }
        g_stack.clear();
        for (double a : arguments)
            FakePushNumber(FAKE_STATE, a);
        int count = found->second.function(FAKE_STATE);
        if (count < 0 || size_t(count) > g_stack.size())
        {
            Problem(std::string(name) + " returned a bad count");
            return results;
        }
        results.assign(g_stack.end() - count, g_stack.end());
        g_stack.clear();
        return results;
    }

    void CheckHit(float fx, float fy)
    {
        std::vector<Value> r = Call("PlayerHousing_CursorWorld", { fx, fy });
        Vec3 want = Expected(fx, fy);
        char text[256];
        if (r.size() != 6)
        {
            std::snprintf(text, sizeof(text), "cursor %.2f %.2f: %u results, want 6 (x y z and the ground's facing)", fx, fy, unsigned(r.size()));
            Problem(text);
            return;
        }
        Vec3 got = { float(r[0].number), float(r[1].number), float(r[2].number) };
        if (Length(Sub(got, want)) > 0.02f)
        {
            std::snprintf(text, sizeof(text), "cursor %.2f %.2f: hit %.3f %.3f %.3f, want %.3f %.3f %.3f", fx, fy, got.x, got.y, got.z, want.x, want.y, want.z);
            Problem(text);
        }
        if (std::fabs(r[3].number) > 0.01 || std::fabs(r[4].number) > 0.01 || std::fabs(r[5].number - 1.0) > 0.01)
        {
            std::snprintf(text, sizeof(text), "cursor %.2f %.2f: facing %.3f %.3f %.3f, want 0 0 1", fx, fy, r[3].number, r[4].number, r[5].number);
            Problem(text);
        }
    }

    int RunTest()
    {
        std::string error;
        if (!SetUpGame(error))
        {
            std::printf("FAIL: %s\n", error.c_str());
            return 2;
        }
        std::string dllPath = Folder() + "\\PlayerHousing.dll";
        if (!LoadLibraryA(dllPath.c_str()))
        {
            std::printf("FAIL: couldn't load %s (%lu)\n", dllPath.c_str(), GetLastError());
            return 2;
        }
        std::string log = ReadFile(Folder() + "\\PlayerHousing.log");
        if (log.find("ready") == std::string::npos)
            Problem("the log doesn't say ready: " + log);

        auto frame = reinterpret_cast<int (*)(int, int, int, int)>(0x00495810);
        if (frame(1, 2, 3, 4) != 4321)
            Problem("the frame update's result didn't come back through the hook");
        if (g_frames != 1)
            Problem("the frame update didn't run once");
        if (g_globals.count("PlayerHousingDLL") != 1 || g_globals["PlayerHousingDLL"].number != 1.0)
            Problem("PlayerHousingDLL isn't 1 after a frame");
        if (!g_stack.empty())
            Problem("the frame left things on the Lua stack");
        frame(0, 0, 0, 0);
        frame(0, 0, 0, 0);

        std::vector<Value> info = Call("PlayerHousing_DLLInfo", {});
        if (info.size() != 1 || info[0].text.find("ready") == std::string::npos || info[0].text.find("1 times") == std::string::npos)
            Problem("DLLInfo after three frames: " + (info.empty() ? std::string("nothing") : info[0].text));

        // The cursor over the view, and one corner.
        CheckHit(0.5f, 0.5f);
        CheckHit(0.2f, 0.3f);
        CheckHit(0.85f, 0.1f);
        CheckHit(0.02f, 0.6f);
        if (g_lastFlags != 0x100111)
            Problem("TraceLine didn't get the line of sight flags");

        // Flags and reach from Lua.
        Call("PlayerHousing_CursorWorld", { 0.5, 0.5, double(0x100000), 150.0 });
        if (g_lastFlags != 0x100000)
            Problem("TraceLine didn't get the flags given");

        // The top of the view sees ground about 79 yards off: beyond a 50 yard reach, nothing.
        if (Call("PlayerHousing_CursorWorld", { 0.5, 0.99 }).size() != 6)
            Problem("the far ground gave no hit");
        if (!Call("PlayerHousing_CursorWorld", { 0.5, 0.99, double(0x100111), 50.0 }).empty())
            Problem("ground beyond the reach gave a hit");
        // Off the view: nothing.
        if (!Call("PlayerHousing_CursorWorld", { 1.5, 0.5 }).empty())
            Problem("a cursor off the view gave a hit");
        // Not in the world: nothing, and no tracing.
        *reinterpret_cast<char*>(0x00BD0792) = 0;
        int before = g_traces;
        if (!Call("PlayerHousing_CursorWorld", { 0.5, 0.5 }).empty() || g_traces != before)
            Problem("out of the world, it still traced");
        *reinterpret_cast<char*>(0x00BD0792) = 1;

        // A /reload: a new Lua without the functions; the next frame adds them again.
        g_globals.clear();
        frame(0, 0, 0, 0);
        info = Call("PlayerHousing_DLLInfo", {});
        if (info.size() != 1 || info[0].text.find("2 times") == std::string::npos)
            Problem("after a reload: " + (info.empty() ? std::string("nothing") : info[0].text));
        if (*reinterpret_cast<uint32_t*>(0x00D415B8) != 1 || *reinterpret_cast<uint32_t*>(0x00D415BC) != 0x7FFFFFFF)
            Problem("the C function range wasn't widened");

        if (g_problems.empty())
        {
            std::printf("PASS: PlayerHousing.dll in the fake game (%d frames, %d traces, %d projections)\n", g_frames, g_traces, g_worldToScreenCalls);
            return 0;
        }
        for (std::string const& p : g_problems)
            std::printf("FAIL: %s\n", p.c_str());
        return 1;
    }

    // Started by the launcher: is the DLL here, and what was this given?
    int Launched(int argc, char** argv)
    {
        std::string result = GetModuleHandleA("PlayerHousing.dll") ? "dll loaded" : "dll missing";
        result += "; args:";
        for (int i = 1; i < argc; ++i)
            result += std::string(" [") + argv[i] + "]";
        std::string path = Folder() + "\\fake_game.result";
        if (FILE* file = std::fopen(path.c_str(), "w"))
        {
            std::fprintf(file, "%s\n", result.c_str());
            std::fclose(file);
        }
        return 0;
    }
}

int main(int argc, char** argv)
{
    // Keep the reserve: nothing else may land there.
    g_reserve[0] = 1;
    if (argc > 1 && std::strcmp(argv[1], "--launched") == 0)
        return Launched(argc, argv);
    return RunTest();
}
