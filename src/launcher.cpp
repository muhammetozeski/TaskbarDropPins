#include <windows.h>
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args || count != 2) {
        if (args) LocalFree(args);
        return ERROR_INVALID_PARAMETER;
    }
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_NOASYNC;
    execute.lpFile = args[1];
    execute.nShow = SW_SHOWNORMAL;
    const BOOL ok = ShellExecuteExW(&execute);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (!ok) {
        wchar_t message[512]{};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, error, 0, message, ARRAYSIZE(message), nullptr);
        MessageBoxW(nullptr, message, L"Taskbar Drop Pins", MB_OK | MB_ICONERROR);
    }
    if (SUCCEEDED(init)) CoUninitialize();
    LocalFree(args);
    return static_cast<int>(error);
}
