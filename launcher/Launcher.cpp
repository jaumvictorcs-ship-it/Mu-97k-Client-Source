// Mu CLT Launcher: verifica/baixa atualizacoes (Updater.cpp), opcoes rapidas no Config.ini e abre o jogo.
#include "Updater.h"

#include <commctrl.h>
#include <windowsx.h>

#include <string>

namespace
{
enum
{
    ID_STATUS = 100, ID_FILE_BAR, ID_TOTAL_BAR, ID_RESOLUTION, ID_WINDOWED, ID_SOUND, ID_PLAY, ID_EXIT,
};

// Indices da tabela de resolucoes do cliente (src/Config/UserSettings.cpp).
struct Resolution { const wchar_t* label; int index; };
const Resolution kResolutions[] = {
    { L"800 x 600", 1 }, { L"1024 x 768", 2 }, { L"1280 x 720", 4 }, { L"1920 x 1080", 7 },
};

HWND g_status, g_fileBar, g_totalBar, g_resolution, g_windowed, g_sound, g_play;
HFONT g_font, g_titleFont;
UpdateContext g_context;
std::wstring g_ini;
int g_initialResolution = -1;
bool g_initialWindowed = false, g_initialSound = true;
bool g_updating = true;

int ReadIni(const wchar_t* section, const wchar_t* key, int fallback)
{
    return (int)GetPrivateProfileIntW(section, key, fallback, g_ini.c_str());
}

void WriteIni(const wchar_t* section, const wchar_t* key, int value)
{
    wchar_t text[16];
    swprintf_s(text, L"%d", value);
    WritePrivateProfileStringW(section, key, text, g_ini.c_str());
}

void LoadSettings()
{
    int index = ReadIni(L"Window", L"Resolution", -1);
    g_initialResolution = -1;
    for (int i = 0; i < (int)_countof(kResolutions); i++)
        if (kResolutions[i].index == index) g_initialResolution = i;
    // Resolucao fora da lista (ex.: 1366x768 escrita pelo jogo): mostra 1024x768, mas so grava se o jogador mudar.
    SendMessageW(g_resolution, CB_SETCURSEL, g_initialResolution >= 0 ? g_initialResolution : 1, 0);
    g_initialWindowed = ReadIni(L"Window", L"WindowMode", 0) == 1;
    g_initialSound = ReadIni(L"Sound", L"EnableSound", 1) == 1 || ReadIni(L"Sound", L"EnableMusic", 1) == 1;
    Button_SetCheck(g_windowed, g_initialWindowed ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(g_sound, g_initialSound ? BST_CHECKED : BST_UNCHECKED);
}

// Grava so o que o jogador mudou: nenhuma outra chave do Config.ini e tocada.
void SaveSettings()
{
    int selected = (int)SendMessageW(g_resolution, CB_GETCURSEL, 0, 0);
    if (selected >= 0 && selected != (g_initialResolution >= 0 ? g_initialResolution : 1)) WriteIni(L"Window", L"Resolution", kResolutions[selected].index);
    bool windowed = Button_GetCheck(g_windowed) == BST_CHECKED;
    if (windowed != g_initialWindowed) WriteIni(L"Window", L"WindowMode", windowed ? 1 : 0);
    bool sound = Button_GetCheck(g_sound) == BST_CHECKED;
    if (sound != g_initialSound)
    {
        WriteIni(L"Sound", L"EnableSound", sound ? 1 : 0);
        WriteIni(L"Sound", L"EnableMusic", sound ? 1 : 0);
    }
}

bool StartGame(HWND window)
{
    std::wstring exe = g_context.root + L"main.exe";
    std::wstring command = L"\"" + exe + L"\" -launcher";
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(exe.c_str(), &command[0], nullptr, nullptr, FALSE, 0, nullptr, g_context.root.c_str(), &startup, &process))
    {
        MessageBoxW(window, L"Não foi possível abrir o main.exe. Verifique se ele está na pasta do Launcher.", L"Mu CLT", MB_ICONERROR);
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

HWND Control(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT font)
{
    HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, (WPARAM)font, TRUE);
    return control;
}

void CreateControls(HWND window)
{
    NONCLIENTMETRICSW metrics = { sizeof(metrics) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    g_font = CreateFontIndirectW(&metrics.lfMessageFont);
    LOGFONTW title = metrics.lfMessageFont;
    title.lfHeight = -26;
    title.lfWeight = FW_BOLD;
    g_titleFont = CreateFontIndirectW(&title);

    Control(window, L"STATIC", L"MU CLT", SS_CENTER, 20, 14, 460, 36, 0, g_titleFont);
    g_status = Control(window, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 20, 62, 460, 20, ID_STATUS, g_font);
    Control(window, L"STATIC", L"Arquivo", SS_LEFT, 20, 88, 60, 18, 0, g_font);
    g_fileBar = Control(window, PROGRESS_CLASSW, L"", 0, 80, 88, 400, 16, ID_FILE_BAR, g_font);
    Control(window, L"STATIC", L"Total", SS_LEFT, 20, 112, 60, 18, 0, g_font);
    g_totalBar = Control(window, PROGRESS_CLASSW, L"", 0, 80, 112, 400, 16, ID_TOTAL_BAR, g_font);
    SendMessageW(g_fileBar, PBM_SETRANGE32, 0, 1000);
    SendMessageW(g_totalBar, PBM_SETRANGE32, 0, 1000);

    Control(window, L"BUTTON", L"Opções", BS_GROUPBOX, 20, 146, 460, 86, 0, g_font);
    Control(window, L"STATIC", L"Resolução", SS_LEFT, 36, 172, 80, 20, 0, g_font);
    g_resolution = Control(window, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 120, 168, 140, 200, ID_RESOLUTION, g_font);
    for (const Resolution& r : kResolutions) SendMessageW(g_resolution, CB_ADDSTRING, 0, (LPARAM)r.label);
    g_windowed = Control(window, L"BUTTON", L"Modo janela", BS_AUTOCHECKBOX | WS_TABSTOP, 290, 170, 170, 20, ID_WINDOWED, g_font);
    g_sound = Control(window, L"BUTTON", L"Som / Música", BS_AUTOCHECKBOX | WS_TABSTOP, 290, 198, 170, 20, ID_SOUND, g_font);

    g_play = Control(window, L"BUTTON", L"JOGAR", BS_DEFPUSHBUTTON | WS_TABSTOP | WS_DISABLED, 260, 248, 130, 34, ID_PLAY, g_font);
    Control(window, L"BUTTON", L"Sair", BS_PUSHBUTTON | WS_TABSTOP, 400, 248, 80, 34, ID_EXIT, g_font);
    LoadSettings();
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateControls(window);
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_UPDATE_STATUS:
    {
        std::wstring* text = (std::wstring*)lParam;
        SetWindowTextW(g_status, text->c_str());
        delete text;
        return 0;
    }
    case WM_UPDATE_PROGRESS:
        SendMessageW(g_fileBar, PBM_SETPOS, wParam, 0);
        SendMessageW(g_totalBar, PBM_SETPOS, (WPARAM)lParam, 0);
        return 0;
    case WM_UPDATE_DONE:
        g_updating = false;
        EnableWindow(g_play, wParam != UPDATE_BLOCKED);
        if (wParam != UPDATE_BLOCKED) SetFocus(g_play);
        return 0;
    case WM_UPDATE_RESTART:
        DestroyWindow(window);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_PLAY && !g_updating)
        {
            SaveSettings();
            if (StartGame(window)) DestroyWindow(window);
        }
        else if (LOWORD(wParam) == ID_EXIT) DestroyWindow(window);
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int show)
{
    // Uma instancia so: duas atualizando a mesma pasta se atrapalham.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\MuCLT.Launcher");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS && wcsstr(commandLine, L"--updated") == nullptr)
    {
        HWND other = FindWindowW(L"MuCLTLauncher", nullptr);
        if (other) SetForegroundWindow(other);
        return 0;
    }

    wchar_t path[MAX_PATH * 2];
    DWORD length = GetModuleFileNameW(nullptr, path, _countof(path));
    std::wstring root(path, length);
    root.resize(root.find_last_of(L'\\') + 1);
    g_context.root = root;
    g_context.justUpdated = wcsstr(commandLine, L"--updated") != nullptr;
    g_ini = root + L"Config.ini";

    INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&controls);

    WNDCLASSEXW windowClass = { sizeof(windowClass) };
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    windowClass.lpszClassName = L"MuCLTLauncher";
    RegisterClassExW(&windowClass);

    RECT area = { 0, 0, 500, 300 };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&area, style, FALSE);
    HWND window = CreateWindowExW(0, L"MuCLTLauncher", L"Mu CLT Launcher", style, CW_USEDEFAULT, CW_USEDEFAULT,
                                  area.right - area.left, area.bottom - area.top, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show);
    UpdateWindow(window);

    g_context.window = window;
    HANDLE worker = CreateThread(nullptr, 0, UpdateThread, &g_context, 0, nullptr);
    if (worker) CloseHandle(worker);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (IsDialogMessageW(window, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (mutex) CloseHandle(mutex);
    return 0;
}
