// Autoteste do Launcher (roda no CI): lista de caminhos permitidos e parser do manifest.
#include "Updater.h"

#include <cstdio>

static int g_failures = 0;

static void Check(bool condition, const char* what)
{
    if (!condition) { std::printf("FALHOU: %s\n", what); g_failures++; }
}

int main()
{
    Check(IsAllowedPath(L"main.exe"), "main.exe permitido");
    Check(IsAllowedPath(L"Data/Player/Player.bmd"), "Data/ permitido");
    Check(IsAllowedPath(L"data/Interface/x.ozj"), "data/ sem diferenciar maiusculas");
    Check(!IsAllowedPath(L"Launcher.exe"), "Launcher.exe so pelo campo launcher");
    Check(!IsAllowedPath(L"../evil.exe"), "..");
    Check(!IsAllowedPath(L"Data/../evil.exe"), "Data/..");
    Check(!IsAllowedPath(L"Data/./x"), "Data/.");
    Check(!IsAllowedPath(L"C:/Windows/x.dll"), "absoluto com letra");
    Check(!IsAllowedPath(L"/etc/x"), "absoluto com barra");
    Check(!IsAllowedPath(L"Data\\x.bmd"), "barra invertida");
    Check(!IsAllowedPath(L"Data//x.bmd"), "componente vazio");
    Check(!IsAllowedPath(L"Data/x.bmd:stream"), "fluxo NTFS");
    Check(!IsAllowedPath(L"Config.ini"), "Config.ini fora");
    Check(!IsAllowedPath(L"Data/"), "so a pasta");
    Check(!IsAllowedPath(L"Data/x. "), "ponto/espaco no fim");

    const char* good =
        "{\"version\":\"2026.11.07-1\",\"generated\":\"x\",\"launcher\":{\"path\":\"Launcher.exe\",\"size\":10,"
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"},\"files\":["
        "{\"path\":\"main.exe\",\"size\":1,\"sha256\":\"1111111111111111111111111111111111111111111111111111111111111111\"},"
        "{\"path\":\"Data/\\u00e1.bmd\",\"size\":2,\"sha256\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\",\"extra\":[1,{\"a\":true}]}]}";
    size_t count = 0;
    std::string version;
    Check(ParseManifestForTest(good, &count, &version) && count == 2 && version == "2026.11.07-1", "manifest valido");
    Check(!ParseManifestForTest("{\"files\":[{\"path\":\"main.exe\",\"size\":1,\"sha256\":\"12\"}]}", &count, &version), "sha curto");
    Check(!ParseManifestForTest("{\"files\":[{\"path\":\"main.exe\",\"sha256\":\"1111111111111111111111111111111111111111111111111111111111111111\"}]}", &count, &version), "sem size");
    Check(!ParseManifestForTest("{\"files\":[]} lixo", &count, &version), "lixo no fim");
    Check(!ParseManifestForTest("{\"files\":[", &count, &version), "truncado");

    std::printf(g_failures ? "%d falha(s)\n" : "launcher selftest OK\n", g_failures);
    return g_failures ? 1 : 0;
}
