#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* wsl_cheatload.c'den */
void cheat_init(void);

int wmain(int argc, wchar_t* argv[])
{
    cheat_init();

    /* Gerçek WSL'i çalıştır — argümanları aynen ilet */
    wchar_t realWsl[MAX_PATH] = {};
    GetSystemDirectoryW(realWsl, MAX_PATH);

    /* System32\wsl_real.exe olarak yedeklenmiş orijinal wsl */
    wcscat_s(realWsl, MAX_PATH, L"\\wsl_real.exe");

    /* wsl_real.exe yoksa doğrudan çık — WSL kurulu değilse zaten çalışmaz */
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    /* Komut satırını yeniden oluştur */
    wchar_t cmdLine[4096] = {};
    wcscpy_s(cmdLine, 4096, L"\"");
    wcscat_s(cmdLine, 4096, realWsl);
    wcscat_s(cmdLine, 4096, L"\"");
    for (int i = 1; i < argc; i++) {
        wcscat_s(cmdLine, 4096, L" ");
        wcscat_s(cmdLine, 4096, argv[i]);
    }

    if (CreateProcessW(NULL, cmdLine, NULL, NULL, TRUE,
                       CREATE_UNICODE_ENVIRONMENT, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return (int)exitCode;
    }

    return 0;
}
