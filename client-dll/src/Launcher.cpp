// PlayerHousingLauncher.exe: starts Wow.exe with PlayerHousing.dll loaded, without changing
// Wow.exe. Put both next to Wow.exe and start the game with this instead (under Wine too:
// wine PlayerHousingLauncher.exe). Anything after the launcher's name goes to Wow.exe; a first
// argument ending in .exe names another game program to start.
//
// The game starts paused, the DLL is loaded into it, then it carries on. If the DLL can't be
// loaded the game still starts, and says so: only placing with the mouse needs it.

#include <windows.h>

#include <string>

namespace
{
    std::wstring Folder(std::wstring const& path)
    {
        size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? L"." : path.substr(0, slash);
    }

    bool Exists(std::wstring const& path)
    {
        DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    std::wstring ErrorText(DWORD code)
    {
        wchar_t* buffer = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
            reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
        std::wstring text = buffer ? buffer : L"";
        if (buffer)
            LocalFree(buffer);
        while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' '))
            text.pop_back();
        return text + L" (" + std::to_wstring(code) + L")";
    }

    void Say(std::wstring const& text, UINT icon)
    {
        MessageBoxW(nullptr, text.c_str(), L"Player Housing launcher", MB_OK | icon);
    }

    // The rest of the command line after the program's own name, as typed.
    std::wstring ArgumentsAfterProgram(wchar_t const* line)
    {
        bool quoted = false;
        while (*line && (quoted || (*line != L' ' && *line != L'\t')))
        {
            if (*line == L'"')
                quoted = !quoted;
            ++line;
        }
        while (*line == L' ' || *line == L'\t')
            ++line;
        return line;
    }

    // Load the DLL in the game: a thread there runs LoadLibraryW on its path.
    bool Inject(HANDLE process, HANDLE mainThread, bool& resumed, std::wstring const& dll, std::wstring& error)
    {
        size_t bytes = (dll.size() + 1) * sizeof(wchar_t);
        void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remote)
        {
            error = L"no memory in the game for the DLL's path: " + ErrorText(GetLastError());
            return false;
        }
        if (!WriteProcessMemory(process, remote, dll.c_str(), bytes, nullptr))
        {
            error = L"couldn't write the DLL's path into the game: " + ErrorText(GetLastError());
            return false;
        }
        // kernel32 sits at the same address in every program.
        auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
        if (!thread)
        {
            error = L"couldn't start the DLL in the game: " + ErrorText(GetLastError());
            return false;
        }
        // Paused games can't always start a thread (some Wine versions): let it go on and wait more.
        DWORD waited = WaitForSingleObject(thread, 10000);
        if (waited == WAIT_TIMEOUT)
        {
            ResumeThread(mainThread);
            resumed = true;
            waited = WaitForSingleObject(thread, 30000);
        }
        DWORD module = 0;
        bool loaded = waited == WAIT_OBJECT_0 && GetExitCodeThread(thread, &module) && module != 0;
        CloseHandle(thread);
        if (!loaded)
            error = waited == WAIT_OBJECT_0 ? L"Windows couldn't load the DLL (a missing file, or a 64-bit copy?)" : L"the game didn't load the DLL in time";
        return loaded;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring here = Folder(self);
    std::wstring dll = here + L"\\PlayerHousing.dll";

    std::wstring arguments = ArgumentsAfterProgram(GetCommandLineW());
    std::wstring game = here + L"\\Wow.exe";
    // A first argument ending in .exe: that's the game program.
    if (!arguments.empty())
    {
        std::wstring first;
        std::wstring rest;
        if (arguments[0] == L'"')
        {
            size_t close = arguments.find(L'"', 1);
            first = arguments.substr(1, close == std::wstring::npos ? std::wstring::npos : close - 1);
            rest = close == std::wstring::npos ? L"" : arguments.substr(close + 1);
        }
        else
        {
            size_t space = arguments.find_first_of(L" \t");
            first = arguments.substr(0, space);
            rest = space == std::wstring::npos ? L"" : arguments.substr(space + 1);
        }
        if (first.size() > 4 && _wcsicmp(first.c_str() + first.size() - 4, L".exe") == 0)
        {
            game = first;
            arguments = rest;
            while (!arguments.empty() && (arguments[0] == L' ' || arguments[0] == L'\t'))
                arguments.erase(0, 1);
        }
    }

    if (!Exists(game))
    {
        Say(L"Wow.exe isn't here:\n" + game + L"\n\nPut PlayerHousingLauncher.exe and PlayerHousing.dll in the game's folder, next to Wow.exe.",
            MB_ICONERROR);
        return 1;
    }

    std::wstring commandLine = L"\"" + game + L"\"" + (arguments.empty() ? L"" : L" " + arguments);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION started = {};
    if (!CreateProcessW(game.c_str(), &commandLine[0], nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, Folder(game).c_str(), &startup, &started))
    {
        Say(L"Couldn't start " + game + L":\n" + ErrorText(GetLastError()), MB_ICONERROR);
        return 1;
    }

    bool resumed = false;
    std::wstring error;
    bool loaded = false;
    if (!Exists(dll))
        error = L"PlayerHousing.dll isn't next to the launcher";
    else
        loaded = Inject(started.hProcess, started.hThread, resumed, dll, error);
    if (!resumed)
        ResumeThread(started.hThread);
    CloseHandle(started.hThread);
    CloseHandle(started.hProcess);

    if (!loaded)
        Say(L"The game started without PlayerHousing.dll: " + error + L".\n\nEverything works without it except placing pieces with the mouse "
            L"(they follow you instead).", MB_ICONWARNING);
    return 0;
}
