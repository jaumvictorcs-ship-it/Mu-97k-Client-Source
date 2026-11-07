// Auto-update do Mu CLT: manifest assinado (ECDSA P-256), SHA-256 por arquivo e troca atomica.
// Spec: docs/launcher/SPEC.md no repositorio do servidor.
#pragma once
#include <windows.h>
#include <string>

// Mensagens da thread de update para a janela.
enum
{
    WM_UPDATE_STATUS = WM_APP + 1,   // lParam = std::wstring* (a janela libera)
    WM_UPDATE_PROGRESS,              // wParam = arquivo atual 0..1000, lParam = total 0..1000
    WM_UPDATE_DONE,                  // wParam = UpdateResult
    WM_UPDATE_RESTART,               // o Launcher novo ja foi iniciado: fechar
};

enum UpdateResult
{
    UPDATE_OK = 0,        // cliente igual ao manifest
    UPDATE_OFFLINE = 1,   // nao deu para verificar: deixa jogar com aviso
    UPDATE_BLOCKED = 2,   // arquivo em uso ou falha ao gravar: nao deixa jogar
};

struct UpdateContext
{
    HWND window;
    std::wstring root;   // pasta do jogo, terminada em '\'
    bool justUpdated;    // aberto pelo proprio auto-update: nao tenta de novo (evita laco)
};

DWORD WINAPI UpdateThread(LPVOID context);

// Exposto para o self-check (launcher_selftest).
bool IsAllowedPath(const std::wstring& path);
bool ParseManifestForTest(const std::string& json, size_t* fileCount, std::string* version);
