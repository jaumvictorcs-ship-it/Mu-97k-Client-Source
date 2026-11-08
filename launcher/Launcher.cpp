// Mu CLT Launcher: verifica/baixa atualizacoes (Updater.cpp), opcoes no Config.ini e abre o jogo.
// Visual: janela sem borda com arte em launcher/art (gerada por art/make_art.py), botoes e barras desenhados.
#include "Updater.h"

#include <commctrl.h>
#include <wincodec.h>
#include <windowsx.h>

#include <string>

namespace
{
// ---------------------------------------------------------------- layout (pixels da arte 820x420)
const int kWidth = 820, kHeight = 420;
const RECT kPlay = { 586, 304, 790, 374 };
const RECT kConfig = { 586, 262, 790, 294 };
const RECT kMinimize = { 754, 16, 778, 38 };
const RECT kClose = { 782, 16, 806, 38 };
const RECT kStatus = { 50, 300, 534, 322 };
const RECT kFileBar = { 50, 330, 534, 342 };
const RECT kTotalBar = { 50, 354, 534, 366 };
const RECT kPanel = { 34, 292, 548, 380 };
const int kPlayW = 204, kPlayH = 70, kConfigW = 204, kConfigH = 32;   // sprite: 3 estados lado a lado

enum Hot { HOT_NONE, HOT_PLAY, HOT_CONFIG, HOT_MIN, HOT_CLOSE };
enum { ID_RESOLUTION = 200, ID_WINDOWED, ID_SOUND, ID_OK, ID_CANCEL };

// Indices da tabela de resolucoes do cliente (src/Config/UserSettings.cpp).
struct Resolution { const wchar_t* label; int index; };
const Resolution kResolutions[] = {
    { L"800 x 600", 1 }, { L"1024 x 768", 2 }, { L"1280 x 720", 4 }, { L"1920 x 1080", 7 },
};

HWND g_window, g_settings;
HBITMAP g_background, g_buttons;
HFONT g_statusFont, g_smallFont, g_uiFont;
UpdateContext g_context;
std::wstring g_ini, g_status = L"Iniciando...";
int g_filePermille = 0, g_totalPermille = 0;
bool g_updating = true, g_playEnabled = false;
Hot g_hot = HOT_NONE, g_pressed = HOT_NONE;
bool g_tracking = false;

// ---------------------------------------------------------------- Config.ini
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

int CurrentResolutionChoice()
{
    int index = ReadIni(L"Window", L"Resolution", -1);
    for (int i = 0; i < (int)_countof(kResolutions); i++)
        if (kResolutions[i].index == index) return i;
    return -1;   // fora da lista (ex.: 1366x768): so grava se o jogador escolher outra
}

bool CurrentWindowed() { return ReadIni(L"Window", L"WindowMode", 0) == 1; }
bool CurrentSound() { return ReadIni(L"Sound", L"EnableSound", 1) == 1 || ReadIni(L"Sound", L"EnableMusic", 1) == 1; }

// ---------------------------------------------------------------- imagens (PNG embutido, WIC do sistema)
HBITMAP LoadPng(int id)
{
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource) return nullptr;
    HGLOBAL global = LoadResource(nullptr, resource);
    BYTE* data = global ? (BYTE*)LockResource(global) : nullptr;
    DWORD size = SizeofResource(nullptr, resource);
    if (!data || !size) return nullptr;

    HBITMAP bitmap = nullptr;
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromMemory(data, size)) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
    {
        UINT width = 0, height = 0;
        converter->GetSize(&width, &height);
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = (LONG)width;
        info.bmiHeader.biHeight = -(LONG)height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bitmap && FAILED(converter->CopyPixels(nullptr, width * 4, width * height * 4, (BYTE*)bits)))
        {
            DeleteObject(bitmap);
            bitmap = nullptr;
        }
    }
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    return bitmap;
}

// ---------------------------------------------------------------- desenho
void Blit(HDC target, HDC source, HBITMAP bitmap, int x, int y, int w, int h, int sx, int sy)
{
    HGDIOBJ old = SelectObject(source, bitmap);
    BitBlt(target, x, y, w, h, source, sx, sy, SRCCOPY);
    SelectObject(source, old);
}

void ShadowText(HDC dc, const std::wstring& text, RECT rect, UINT format, COLORREF color)
{
    RECT shadow = rect;
    OffsetRect(&shadow, 1, 1);
    SetTextColor(dc, RGB(0, 0, 0));
    DrawTextW(dc, text.c_str(), -1, &shadow, format);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rect, format);
}

void DrawBar(HDC dc, const RECT& rect, int permille)
{
    HBRUSH track = CreateSolidBrush(RGB(10, 22, 45));
    FillRect(dc, &rect, track);
    DeleteObject(track);
    int fill = (int)((rect.right - rect.left - 2) * (long long)permille / 1000);
    if (fill > 0)
    {
        TRIVERTEX vertex[2] = {
            { rect.left + 1, rect.top + 1, 0x1E00, 0x5A00, 0xB400, 0 },
            { rect.left + 1 + fill, rect.bottom - 1, 0x4600, 0x9600, 0xF000, 0 },
        };
        GRADIENT_RECT gradient = { 0, 1 };
        GradientFill(dc, vertex, 2, &gradient, 1, GRADIENT_FILL_RECT_H);
    }
    HBRUSH border = CreateSolidBrush(RGB(40, 90, 160));
    FrameRect(dc, &rect, border);
    DeleteObject(border);
}

void DrawGlyph(HDC dc, const RECT& rect, bool close, bool hot)
{
    if (hot)
    {
        HBRUSH back = CreateSolidBrush(close ? RGB(150, 30, 20) : RGB(28, 54, 90));
        FillRect(dc, &rect, back);
        DeleteObject(back);
    }
    HPEN pen = CreatePen(PS_SOLID, 2, hot ? RGB(255, 240, 210) : RGB(214, 170, 90));
    HGDIOBJ old = SelectObject(dc, pen);
    int cx = (rect.left + rect.right) / 2, cy = (rect.top + rect.bottom) / 2;
    if (close)
    {
        MoveToEx(dc, cx - 5, cy - 5, nullptr); LineTo(dc, cx + 6, cy + 6);
        MoveToEx(dc, cx + 5, cy - 5, nullptr); LineTo(dc, cx - 6, cy + 6);
    }
    else
    {
        MoveToEx(dc, cx - 6, cy + 4, nullptr); LineTo(dc, cx + 7, cy + 4);
    }
    SelectObject(dc, old);
    DeleteObject(pen);
}

void Paint(HWND window)
{
    PAINTSTRUCT ps;
    HDC screen = BeginPaint(window, &ps);
    HDC canvas = CreateCompatibleDC(screen);
    HBITMAP frame = CreateCompatibleBitmap(screen, kWidth, kHeight);
    HGDIOBJ oldFrame = SelectObject(canvas, frame);
    HDC source = CreateCompatibleDC(screen);

    if (g_background) Blit(canvas, source, g_background, 0, 0, kWidth, kHeight, 0, 0);
    else { HBRUSH b = CreateSolidBrush(RGB(10, 20, 40)); RECT all = { 0, 0, kWidth, kHeight }; FillRect(canvas, &all, b); DeleteObject(b); }

    if (g_buttons)
    {
        int playState = !g_playEnabled ? 2 : (g_hot == HOT_PLAY ? 1 : 0);
        Blit(canvas, source, g_buttons, kPlay.left, kPlay.top + (g_pressed == HOT_PLAY && g_hot == HOT_PLAY ? 1 : 0), kPlayW, kPlayH, playState * kPlayW, 0);
        int configState = g_hot == HOT_CONFIG ? 1 : 0;
        Blit(canvas, source, g_buttons, kConfig.left, kConfig.top, kConfigW, kConfigH, configState * kConfigW, kPlayH);
    }

    SetBkMode(canvas, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(canvas, g_statusFont);
    ShadowText(canvas, g_status, kStatus, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, RGB(246, 226, 190));
    SelectObject(canvas, g_smallFont);
    wchar_t percent[32];
    swprintf_s(percent, L"Total %d%%", g_totalPermille / 10);
    RECT percentRect = { kStatus.right - 90, kStatus.top, kStatus.right, kStatus.bottom };
    if (g_updating) ShadowText(canvas, percent, percentRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, RGB(214, 180, 120));
    DrawBar(canvas, kFileBar, g_filePermille);
    DrawBar(canvas, kTotalBar, g_totalPermille);
    SelectObject(canvas, oldFont);

    DrawGlyph(canvas, kMinimize, false, g_hot == HOT_MIN);
    DrawGlyph(canvas, kClose, true, g_hot == HOT_CLOSE);

    BitBlt(screen, 0, 0, kWidth, kHeight, canvas, 0, 0, SRCCOPY);
    DeleteDC(source);
    SelectObject(canvas, oldFrame);
    DeleteObject(frame);
    DeleteDC(canvas);
    EndPaint(window, &ps);
}

Hot HitTest(int x, int y)
{
    POINT p = { x, y };
    if (PtInRect(&kPlay, p)) return HOT_PLAY;
    if (PtInRect(&kConfig, p)) return HOT_CONFIG;
    if (PtInRect(&kMinimize, p)) return HOT_MIN;
    if (PtInRect(&kClose, p)) return HOT_CLOSE;
    return HOT_NONE;
}

// ---------------------------------------------------------------- acoes
bool StartGame()
{
    std::wstring exe = g_context.root + L"main.exe";
    std::wstring command = L"\"" + exe + L"\" -launcher";
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(exe.c_str(), &command[0], nullptr, nullptr, FALSE, 0, nullptr, g_context.root.c_str(), &startup, &process))
    {
        MessageBoxW(g_window, L"Não foi possível abrir o main.exe. Verifique se ele está na pasta do Launcher.", L"Mu CLT", MB_ICONERROR);
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

void Play()
{
    if (!g_playEnabled || g_updating) return;
    if (StartGame()) DestroyWindow(g_window);
}

// ---------------------------------------------------------------- janela de configuracoes
HWND g_resolution, g_windowed, g_sound;

HWND Control(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id)
{
    HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    return control;
}

// Grava so o que o jogador mudou: nenhuma outra chave do Config.ini e tocada.
void SaveSettings()
{
    int initial = CurrentResolutionChoice();
    int selected = (int)SendMessageW(g_resolution, CB_GETCURSEL, 0, 0);
    if (selected >= 0 && selected != (initial >= 0 ? initial : 1)) WriteIni(L"Window", L"Resolution", kResolutions[selected].index);
    bool windowed = Button_GetCheck(g_windowed) == BST_CHECKED;
    if (windowed != CurrentWindowed()) WriteIni(L"Window", L"WindowMode", windowed ? 1 : 0);
    bool sound = Button_GetCheck(g_sound) == BST_CHECKED;
    if (sound != CurrentSound())
    {
        WriteIni(L"Sound", L"EnableSound", sound ? 1 : 0);
        WriteIni(L"Sound", L"EnableMusic", sound ? 1 : 0);
    }
}

void CloseSettings()
{
    EnableWindow(g_window, TRUE);
    DestroyWindow(g_settings);
    g_settings = nullptr;
    SetForegroundWindow(g_window);
}

LRESULT CALLBACK SettingsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        Control(window, L"STATIC", L"Resolução", SS_LEFT, 20, 22, 80, 20, 0);
        g_resolution = Control(window, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 110, 18, 150, 200, ID_RESOLUTION);
        for (const Resolution& r : kResolutions) SendMessageW(g_resolution, CB_ADDSTRING, 0, (LPARAM)r.label);
        int choice = CurrentResolutionChoice();
        SendMessageW(g_resolution, CB_SETCURSEL, choice >= 0 ? choice : 1, 0);
        g_windowed = Control(window, L"BUTTON", L"Modo janela", BS_AUTOCHECKBOX | WS_TABSTOP, 20, 58, 240, 20, ID_WINDOWED);
        g_sound = Control(window, L"BUTTON", L"Som / Música", BS_AUTOCHECKBOX | WS_TABSTOP, 20, 86, 240, 20, ID_SOUND);
        Button_SetCheck(g_windowed, CurrentWindowed() ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(g_sound, CurrentSound() ? BST_CHECKED : BST_UNCHECKED);
        Control(window, L"BUTTON", L"Salvar", BS_DEFPUSHBUTTON | WS_TABSTOP, 90, 124, 80, 28, ID_OK);
        Control(window, L"BUTTON", L"Cancelar", BS_PUSHBUTTON | WS_TABSTOP, 180, 124, 80, 28, ID_CANCEL);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_OK) { SaveSettings(); CloseSettings(); }
        else if (LOWORD(wParam) == ID_CANCEL) CloseSettings();
        return 0;
    case WM_CLOSE:
        CloseSettings();
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void OpenSettings()
{
    if (g_settings) { SetForegroundWindow(g_settings); return; }
    RECT area = { 0, 0, 280, 166 };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&area, style, FALSE);
    RECT owner;
    GetWindowRect(g_window, &owner);
    int w = area.right - area.left, h = area.bottom - area.top;
    g_settings = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MuCLTSettings", L"Configurações", style,
                                 owner.left + kConfig.left - w / 2, owner.top + kConfig.top - h - 10, w, h, g_window, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_settings) return;
    EnableWindow(g_window, FALSE);
    ShowWindow(g_settings, SW_SHOW);
}

// ---------------------------------------------------------------- janela principal
void SetHot(Hot hot)
{
    if (hot == g_hot) return;
    g_hot = hot;
    InvalidateRect(g_window, nullptr, FALSE);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint(window);
        return 0;
    case WM_NCHITTEST:
    {
        POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(window, &p);
        return HitTest(p.x, p.y) == HOT_NONE ? HTCAPTION : HTCLIENT;   // fora dos botoes: arrasta a janela
    }
    case WM_MOUSEMOVE:
        if (!g_tracking)
        {
            TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, window, 0 };
            g_tracking = TrackMouseEvent(&track) != FALSE;
        }
        SetHot(HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
        SetCursor(LoadCursorW(nullptr, g_hot == HOT_PLAY && !g_playEnabled ? IDC_NO : (g_hot != HOT_NONE ? IDC_HAND : IDC_ARROW)));
        return 0;
    case WM_MOUSELEAVE:
        g_tracking = false;
        SetHot(HOT_NONE);
        return 0;
    case WM_LBUTTONDOWN:
        g_pressed = HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        SetCapture(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP:
    {
        ReleaseCapture();
        Hot released = HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        Hot pressed = g_pressed;
        g_pressed = HOT_NONE;
        InvalidateRect(window, nullptr, FALSE);
        if (released != pressed) return 0;
        if (released == HOT_PLAY) Play();
        else if (released == HOT_CONFIG) OpenSettings();
        else if (released == HOT_MIN) ShowWindow(window, SW_MINIMIZE);
        else if (released == HOT_CLOSE) DestroyWindow(window);
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) Play();
        else if (wParam == VK_ESCAPE) DestroyWindow(window);
        return 0;
    case WM_UPDATE_STATUS:
    {
        std::wstring* text = (std::wstring*)lParam;
        g_status = *text;
        delete text;
        InvalidateRect(window, &kPanel, FALSE);
        return 0;
    }
    case WM_UPDATE_PROGRESS:
        g_filePermille = (int)wParam;
        g_totalPermille = (int)lParam;
        InvalidateRect(window, &kPanel, FALSE);
        return 0;
    case WM_UPDATE_DONE:
        g_updating = false;
        g_playEnabled = wParam != UPDATE_BLOCKED;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_UPDATE_RESTART:
        DestroyWindow(window);
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

HFONT MakeFont(int height, int weight, const wchar_t* face)
{
    return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int show)
{
    // Uma instancia so: duas atualizando a mesma pasta se atrapalham.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\MuCLT.Launcher");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS && wcsstr(commandLine, L"--updated") == nullptr)
    {
        HWND other = FindWindowW(L"MuCLTLauncher", nullptr);
        if (other) { ShowWindow(other, SW_RESTORE); SetForegroundWindow(other); }
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    wchar_t path[MAX_PATH * 2];
    DWORD length = GetModuleFileNameW(nullptr, path, _countof(path));
    std::wstring root(path, length);
    root.resize(root.find_last_of(L'\\') + 1);
    g_context.root = root;
    g_context.justUpdated = wcsstr(commandLine, L"--updated") != nullptr;
    g_ini = root + L"Config.ini";

    INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&controls);
    g_background = LoadPng(101);
    g_buttons = LoadPng(102);
    g_statusFont = MakeFont(15, FW_SEMIBOLD, L"Segoe UI");
    g_smallFont = MakeFont(13, FW_NORMAL, L"Segoe UI");
    NONCLIENTMETRICSW metrics = { sizeof(metrics) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    g_uiFont = CreateFontIndirectW(&metrics.lfMessageFont);

    WNDCLASSEXW windowClass = { sizeof(windowClass) };
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"MuCLTLauncher";
    RegisterClassExW(&windowClass);

    WNDCLASSEXW settingsClass = { sizeof(settingsClass) };
    settingsClass.lpfnWndProc = SettingsProc;
    settingsClass.hInstance = instance;
    settingsClass.hIcon = windowClass.hIcon;
    settingsClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    settingsClass.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    settingsClass.lpszClassName = L"MuCLTSettings";
    RegisterClassExW(&settingsClass);

    int x = (GetSystemMetrics(SM_CXSCREEN) - kWidth) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - kHeight) / 2;
    g_window = CreateWindowExW(WS_EX_APPWINDOW, L"MuCLTLauncher", L"Mu CLT Launcher", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
                               x, y, kWidth, kHeight, nullptr, nullptr, instance, nullptr);
    if (!g_window) return 1;
    ShowWindow(g_window, show);
    UpdateWindow(g_window);

    g_context.window = g_window;
    HANDLE worker = CreateThread(nullptr, 0, UpdateThread, &g_context, 0, nullptr);
    if (worker) CloseHandle(worker);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (g_settings && IsDialogMessageW(g_settings, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (mutex) CloseHandle(mutex);
    return 0;
}
