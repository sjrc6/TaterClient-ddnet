#include "crash_handler.h"

#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#include <dbghelp.h>
#include <tchar.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>

#pragma comment(lib, "dbghelp.lib")
#endif

namespace
{
bool g_useFullMemoryDump = false;
}

#if defined(CONF_FAMILY_WINDOWS)
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevExceptionFilter = nullptr;

std::wstring getExecutableName()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring fullPath(path);
    size_t pos = fullPath.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? fullPath.substr(pos + 1) : fullPath;
}

std::wstring GenerateDumpFilename()
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    std::wostringstream oss;
    oss << getExecutableName()
        << L"_"
        << st.wYear << L"-"
        << std::setw(2) << std::setfill(L'0') << st.wMonth << L"-"
        << std::setw(2) << std::setfill(L'0') << st.wDay << L"_"
        << std::setw(2) << std::setfill(L'0') << st.wHour << L"-"
        << std::setw(2) << std::setfill(L'0') << st.wMinute << L"-"
        << std::setw(2) << std::setfill(L'0') << st.wSecond;

    if (g_useFullMemoryDump)
        oss << L"_full";

    oss << L".dmp";

    return oss.str();
}

LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* pExceptionPointers)
{
    std::wstring dumpFilename = GenerateDumpFilename();

    HANDLE hFile = CreateFileW(dumpFilename.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = pExceptionPointers;
        mdei.ClientPointers = FALSE;

        MINIDUMP_TYPE dumpType = g_useFullMemoryDump
            ? MiniDumpWithFullMemory
            : MiniDumpNormal;

        BOOL result = MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            hFile,
            dumpType,
            &mdei,
            nullptr,
            nullptr
        );

        CloseHandle(hFile);
    }

    if (g_prevExceptionFilter)
        return g_prevExceptionFilter(pExceptionPointers);

    return EXCEPTION_EXECUTE_HANDLER;
}

void InitCrashHandler(bool full)
{
    g_useFullMemoryDump = full;
    g_prevExceptionFilter = SetUnhandledExceptionFilter(UnhandledExceptionHandler);
}
#else
void InitCrashHandler(bool full) {}
#endif