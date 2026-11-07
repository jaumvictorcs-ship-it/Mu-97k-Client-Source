// Auto-update do Mu CLT. Fluxo, formatos e regras de seguranca: docs/launcher/SPEC.md (repo do servidor).
#include "Updater.h"
#include "PublicKey.h"

#include <bcrypt.h>
#include <winhttp.h>

#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <vector>

namespace
{
const wchar_t* const kHost = L"muclt.online";
const wchar_t* const kBase = L"/updates/";
const size_t kMaxManifest = 8u * 1024u * 1024u;
const int kDownloadAttempts = 3;

std::wstring g_root;

// ---------------------------------------------------------------- texto
std::wstring Widen(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string Narrow(const std::wstring& w)
{
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring Lower(std::wstring s)
{
    if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
    return s;
}

void Log(const wchar_t* format, ...)
{
    wchar_t line[1024];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(line, _countof(line), _TRUNCATE, format, args);
    va_end(args);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stamp[32];
    swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::string text = Narrow(std::wstring(stamp) + line + L"\r\n");
    HANDLE f = CreateFileW((g_root + L"launcher.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, text.data(), (DWORD)text.size(), &written, nullptr);
    CloseHandle(f);
}

std::string ToHex(const BYTE* data, size_t size)
{
    static const char digits[] = "0123456789abcdef";
    std::string out(size * 2, '0');
    for (size_t i = 0; i < size; i++)
    {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 15];
    }
    return out;
}

bool FromHex(const std::string& hex, BYTE* out, size_t size)
{
    if (hex.size() != size * 2) return false;
    for (size_t i = 0; i < size * 2; i++)
    {
        char c = hex[i];
        int v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (v < 0) return false;
        if (i % 2 == 0) out[i / 2] = (BYTE)(v << 4); else out[i / 2] |= (BYTE)v;
    }
    return true;
}

bool IsSha256Hex(const std::string& s)
{
    BYTE tmp[32];
    return FromHex(s, tmp, 32);
}

// ---------------------------------------------------------------- SHA-256 (BCrypt)
class Sha256
{
public:
    Sha256()
    {
        if (BCryptOpenAlgorithmProvider(&m_alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) { m_alg = nullptr; return; }
        DWORD length = 0, got = 0;
        BCryptGetProperty(m_alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&length, sizeof(length), &got, 0);
        m_object.resize(length ? length : 1);   // Windows 7 exige o buffer do objeto
        if (BCryptCreateHash(m_alg, &m_hash, m_object.data(), (ULONG)m_object.size(), nullptr, 0, 0) < 0) m_hash = nullptr;
    }
    ~Sha256()
    {
        if (m_hash) BCryptDestroyHash(m_hash);
        if (m_alg) BCryptCloseAlgorithmProvider(m_alg, 0);
    }
    bool Ok() const { return m_hash != nullptr; }
    void Update(const void* data, size_t size) { if (m_hash && size) BCryptHashData(m_hash, (PUCHAR)data, (ULONG)size, 0); }
    bool Final(BYTE out[32]) { return m_hash && BCryptFinishHash(m_hash, out, 32, 0) >= 0; }
    std::string FinalHex()
    {
        BYTE out[32];
        return Final(out) ? ToHex(out, 32) : std::string();
    }
private:
    BCRYPT_ALG_HANDLE m_alg = nullptr;
    BCRYPT_HASH_HANDLE m_hash = nullptr;
    std::vector<BYTE> m_object;
};

bool HashFile(const std::wstring& path, std::string* hex)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    Sha256 sha;
    std::vector<BYTE> buffer(1 << 20);
    DWORD read = 0;
    bool ok = sha.Ok();
    while (ok && ReadFile(f, buffer.data(), (DWORD)buffer.size(), &read, nullptr) && read > 0) sha.Update(buffer.data(), read);
    CloseHandle(f);
    if (!ok) return false;
    *hex = sha.FinalHex();
    return !hex->empty();
}

// Assinatura ECDSA P-256 (r||s, 64 bytes) sobre o SHA-256 dos bytes exatos do manifest.
bool VerifyManifestSignature(const std::string& manifest, const std::string& signatureHex)
{
    BYTE signature[64];
    if (!FromHex(signatureHex, signature, sizeof(signature))) return false;
    Sha256 sha;
    sha.Update(manifest.data(), manifest.size());
    BYTE digest[32];
    if (!sha.Final(digest)) return false;

    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0) return false;
    std::vector<BYTE> blob(sizeof(BCRYPT_ECCKEY_BLOB) + sizeof(kUpdatePublicKey));
    BCRYPT_ECCKEY_BLOB* header = (BCRYPT_ECCKEY_BLOB*)blob.data();
    header->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    header->cbKey = 32;
    memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), kUpdatePublicKey, sizeof(kUpdatePublicKey));
    BCRYPT_KEY_HANDLE key = nullptr;
    bool ok = false;
    if (BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, blob.data(), (ULONG)blob.size(), 0) >= 0)
    {
        ok = BCryptVerifySignature(key, nullptr, digest, sizeof(digest), signature, sizeof(signature), 0) >= 0;
        BCryptDestroyKey(key);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

// ---------------------------------------------------------------- HTTPS (WinHTTP)
class Http
{
public:
    ~Http()
    {
        if (m_connect) WinHttpCloseHandle(m_connect);
        if (m_session) WinHttpCloseHandle(m_session);
    }

    bool Open()
    {
        if (m_connect) return true;
        m_session = WinHttpOpen(L"MuCLT-Launcher/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!m_session) return false;
        WinHttpSetTimeouts(m_session, 10000, 10000, 30000, 30000);
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        DWORD modern = protocols | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        if (!WinHttpSetOption(m_session, WINHTTP_OPTION_SECURE_PROTOCOLS, &modern, sizeof(modern)))
#endif
            WinHttpSetOption(m_session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
        m_connect = WinHttpConnect(m_session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
        return m_connect != nullptr;
    }

    // sink devolve false para abortar. Retorna o status HTTP (0 = falha de rede).
    DWORD Get(const std::wstring& path, const std::function<bool(const BYTE*, DWORD)>& sink)
    {
        HINTERNET request = WinHttpOpenRequest(m_connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!request) return 0;
        DWORD status = 0;
        if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr))
        {
            DWORD size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            if (status == 200)
            {
                std::vector<BYTE> buffer(64 * 1024);
                for (;;)
                {
                    DWORD available = 0, read = 0;
                    if (!WinHttpQueryDataAvailable(request, &available)) { status = 0; break; }
                    if (available == 0) break;
                    if (!WinHttpReadData(request, buffer.data(), (available < (DWORD)buffer.size() ? available : (DWORD)buffer.size()), &read) || read == 0) { status = 0; break; }
                    if (!sink(buffer.data(), read)) { status = 0; break; }
                }
            }
        }
        WinHttpCloseHandle(request);
        return status;
    }

    bool GetText(const std::wstring& path, std::string* out, size_t limit)
    {
        out->clear();
        DWORD status = Get(path, [&](const BYTE* data, DWORD size) {
            if (out->size() + size > limit) return false;
            out->append((const char*)data, size);
            return true;
        });
        return status == 200;
    }

private:
    HINTERNET m_session = nullptr;
    HINTERNET m_connect = nullptr;
};

std::wstring UrlFor(const std::wstring& path, const std::string& sha)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string utf8 = Narrow(path), url;
    for (unsigned char c : utf8)
    {
        if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/') url += (char)c;
        else { url += '%'; url += hex[c >> 4]; url += hex[c & 15]; }
    }
    return kBase + Widen(url) + L"?v=" + Widen(sha);
}

// ---------------------------------------------------------------- manifest (JSON minimo)
struct ManifestFile
{
    std::wstring path;
    uint64_t size = 0;
    std::string sha256;
};

struct Manifest
{
    std::string version;
    bool hasLauncher = false;
    ManifestFile launcher;
    std::vector<ManifestFile> files;
};

class Json
{
public:
    explicit Json(const std::string& text) : s(text) {}

    void Ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++; }
    bool Lit(char c) { Ws(); if (i < s.size() && s[i] == c) { i++; return true; } return false; }
    bool AtEnd() { Ws(); return i == s.size(); }

    bool Str(std::string& out)
    {
        out.clear();
        if (!Lit('"')) return false;
        while (i < s.size())
        {
            char c = s[i++];
            if (c == '"') return true;
            if ((unsigned char)c < 0x20) return false;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e)
            {
            case '"': case '\\': case '/': out += e; break;
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u':
            {
                if (i + 4 > s.size()) return false;
                unsigned v = 0;
                for (int k = 0; k < 4; k++)
                {
                    char h = s[i++];
                    v <<= 4;
                    if (h >= '0' && h <= '9') v |= h - '0';
                    else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10;
                    else return false;
                }
                if (v >= 0xD800 && v <= 0xDFFF) return false;   // nomes de arquivo do jogo nao usam surrogates
                if (v < 0x80) out += (char)v;
                else if (v < 0x800) { out += (char)(0xC0 | (v >> 6)); out += (char)(0x80 | (v & 0x3F)); }
                else { out += (char)(0xE0 | (v >> 12)); out += (char)(0x80 | ((v >> 6) & 0x3F)); out += (char)(0x80 | (v & 0x3F)); }
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool Num(uint64_t& v)
    {
        Ws();
        size_t start = i;
        v = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9')
        {
            v = v * 10 + (uint64_t)(s[i++] - '0');
            if (v > (1ull << 40)) return false;   // 1 TB: tamanho absurdo
        }
        return i > start;
    }

    bool Skip(int depth = 0)
    {
        if (depth > 16) return false;
        Ws();
        if (i >= s.size()) return false;
        std::string ignored;
        if (s[i] == '"') return Str(ignored);
        if (s[i] == '{' || s[i] == '[')
        {
            char close = s[i] == '{' ? '}' : ']';
            bool object = s[i] == '{';
            i++;
            if (Lit(close)) return true;
            do
            {
                if (object && (!Str(ignored) || !Lit(':'))) return false;
                if (!Skip(depth + 1)) return false;
            } while (Lit(','));
            return Lit(close);
        }
        size_t start = i;
        while (i < s.size() && strchr("-+.eE0123456789truefalsn", s[i])) i++;
        return i > start;
    }

    template <typename F> bool Object(F onKey)
    {
        if (!Lit('{')) return false;
        if (Lit('}')) return true;
        do
        {
            std::string key;
            if (!Str(key) || !Lit(':') || !onKey(key)) return false;
        } while (Lit(','));
        return Lit('}');
    }

private:
    const std::string& s;
    size_t i = 0;
};

bool ParseFile(Json& json, ManifestFile& file)
{
    bool hasPath = false, hasSize = false, hasSha = false;
    bool ok = json.Object([&](const std::string& key) {
        std::string text;
        if (key == "path") { hasPath = json.Str(text); file.path = Widen(text); return hasPath; }
        if (key == "size") { hasSize = json.Num(file.size); return hasSize; }
        if (key == "sha256") { hasSha = json.Str(file.sha256) && IsSha256Hex(file.sha256); return hasSha; }
        return json.Skip();
    });
    for (char& c : file.sha256) if (c >= 'A' && c <= 'F') c = (char)(c - 'A' + 'a');
    return ok && hasPath && hasSize && hasSha && !file.path.empty();
}

bool ParseManifest(const std::string& text, Manifest& manifest)
{
    Json json(text);
    bool ok = json.Object([&](const std::string& key) {
        if (key == "version") return json.Str(manifest.version);
        if (key == "launcher") { manifest.hasLauncher = ParseFile(json, manifest.launcher); return manifest.hasLauncher; }
        if (key == "files")
        {
            if (!json.Lit('[')) return false;
            if (json.Lit(']')) return true;
            do
            {
                ManifestFile file;
                if (!ParseFile(json, file)) return false;
                manifest.files.push_back(file);
            } while (json.Lit(','));
            return json.Lit(']');
        }
        return json.Skip();
    });
    return ok && json.AtEnd();
}

// ---------------------------------------------------------------- arquivos locais
std::wstring LocalPath(const std::wstring& path)
{
    std::wstring local = g_root + path;
    for (wchar_t& c : local) if (c == L'/') c = L'\\';
    return local;
}

bool FileInfo(const std::wstring& path, uint64_t* size, uint64_t* mtime)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    *size = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
    *mtime = ((uint64_t)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
    return true;
}

void CreateParentDirectories(const std::wstring& file)
{
    for (size_t pos = g_root.size(); (pos = file.find(L'\\', pos)) != std::wstring::npos; pos++)
        CreateDirectoryW(file.substr(0, pos).c_str(), nullptr);
}

// Cache de hash: "sha256\tsize\tmtime\tpath" (UTF-8). Evita recalcular os ~3.600 arquivos a cada abertura.
struct CacheEntry { uint64_t size; uint64_t mtime; std::string sha; };
typedef std::map<std::wstring, CacheEntry> HashCache;

void LoadCache(HashCache& cache)
{
    HANDLE f = CreateFileW((g_root + L"launcher.cache").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size;
    std::string text;
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < 16 * 1024 * 1024)
    {
        text.resize((size_t)size.QuadPart);
        DWORD read = 0;
        if (!ReadFile(f, &text[0], (DWORD)text.size(), &read, nullptr)) read = 0;
        text.resize(read);
    }
    CloseHandle(f);
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        char sha[65] = { 0 };
        unsigned long long fileSize = 0, mtime = 0;
        int consumed = 0;
        if (sscanf_s(line.c_str(), "%64[0-9a-f]\t%llu\t%llu\t%n", sha, (unsigned)sizeof(sha), &fileSize, &mtime, &consumed) == 3 && consumed > 0)
            cache[Lower(Widen(line.substr(consumed)))] = CacheEntry{ fileSize, mtime, sha };
    }
}

void SaveCache(const HashCache& cache)
{
    std::string text;
    char head[128];
    for (const auto& item : cache)
    {
        sprintf_s(head, "%s\t%llu\t%llu\t", item.second.sha.c_str(), (unsigned long long)item.second.size, (unsigned long long)item.second.mtime);
        text += head + Narrow(item.first) + "\n";
    }
    std::wstring tmp = g_root + L"launcher.cache.tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    bool ok = WriteFile(f, text.data(), (DWORD)text.size(), &written, nullptr) && written == text.size();
    CloseHandle(f);
    if (ok) MoveFileExW(tmp.c_str(), (g_root + L"launcher.cache").c_str(), MOVEFILE_REPLACE_EXISTING);
    else DeleteFileW(tmp.c_str());
}

// ---------------------------------------------------------------- mensagens para a janela
HWND g_window = nullptr;

void Status(const std::wstring& text)
{
    std::wstring* copy = new std::wstring(text);
    if (!PostMessageW(g_window, WM_UPDATE_STATUS, 0, (LPARAM)copy)) delete copy;
}

void Progress(int file, int total)
{
    static int lastFile = -1, lastTotal = -1;
    if (file == lastFile && total == lastTotal) return;
    lastFile = file;
    lastTotal = total;
    PostMessageW(g_window, WM_UPDATE_PROGRESS, (WPARAM)file, (LPARAM)total);
}

std::wstring FileName(const std::wstring& path)
{
    size_t slash = path.find_last_of(L'/');
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// ---------------------------------------------------------------- download com validacao
enum DownloadResult { DOWNLOAD_OK, DOWNLOAD_FAILED, DOWNLOAD_IN_USE };

// Baixa para <destino>.tmp, confere tamanho e SHA-256 e so entao substitui o destino.
DownloadResult Download(Http& http, const ManifestFile& file, const std::wstring& destination, uint64_t* doneBytes, uint64_t totalBytes)
{
    std::wstring tmp = destination + L".tmp";
    CreateParentDirectories(destination);
    for (int attempt = 1; attempt <= kDownloadAttempts; attempt++)
    {
        HANDLE out = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE)
        {
            Log(L"nao foi possivel criar %s (erro %lu)", tmp.c_str(), GetLastError());
            return DOWNLOAD_FAILED;
        }
        Sha256 sha;
        uint64_t received = 0;
        bool writeOk = true;
        int lastPercent = -1;
        DWORD status = http.Get(UrlFor(file.path, file.sha256), [&](const BYTE* data, DWORD size) {
            if (received + size > file.size) return false;   // maior que o manifest: aborta
            DWORD written = 0;
            if (!WriteFile(out, data, size, &written, nullptr) || written != size) { writeOk = false; return false; }
            sha.Update(data, size);
            received += size;
            int filePermille = file.size ? (int)(received * 1000 / file.size) : 1000;
            int totalPermille = totalBytes ? (int)((*doneBytes + received) * 1000 / totalBytes) : 1000;
            Progress(filePermille, totalPermille);
            int percent = filePermille / 10;
            if (percent != lastPercent)
            {
                lastPercent = percent;
                wchar_t text[300];
                swprintf_s(text, L"Baixando %s (%d%%)...", FileName(file.path).c_str(), percent);
                Status(text);
            }
            return true;
        });
        FlushFileBuffers(out);
        CloseHandle(out);
        std::string hash = sha.FinalHex();
        if (status == 200 && writeOk && received == file.size && hash == file.sha256)
        {
            if (MoveFileExW(tmp.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                *doneBytes += received;
                return DOWNLOAD_OK;
            }
            DWORD error = GetLastError();
            DeleteFileW(tmp.c_str());
            Log(L"nao foi possivel substituir %s (erro %lu)", destination.c_str(), error);
            return (error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) ? DOWNLOAD_IN_USE : DOWNLOAD_FAILED;
        }
        DeleteFileW(tmp.c_str());
        Log(L"download invalido de %s (tentativa %d, http %lu, %llu/%llu bytes)", file.path.c_str(), attempt, status, (unsigned long long)received, (unsigned long long)file.size);
        if (!writeOk) return DOWNLOAD_FAILED;
        Sleep(500 * attempt);
    }
    return DOWNLOAD_FAILED;
}

void Finish(UpdateResult result, const std::wstring& text)
{
    Status(text);
    PostMessageW(g_window, WM_UPDATE_DONE, (WPARAM)result, 0);
}

bool SelfUpdate(Http& http, const ManifestFile& launcher)
{
    wchar_t selfBuffer[MAX_PATH * 2];
    DWORD length = GetModuleFileNameW(nullptr, selfBuffer, _countof(selfBuffer));
    if (length == 0 || length >= _countof(selfBuffer)) return false;
    std::wstring self(selfBuffer, length), current;
    if (!HashFile(self, &current) || current == launcher.sha256) return false;

    Status(L"Atualizando o Launcher...");
    std::wstring fresh = g_root + L"Launcher.new", old = g_root + L"Launcher.old";
    uint64_t done = 0;
    if (Download(http, launcher, fresh, &done, launcher.size) != DOWNLOAD_OK) { Log(L"falha ao baixar o Launcher novo"); return false; }
    DeleteFileW(old.c_str());
    if (!MoveFileExW(self.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) { Log(L"falha ao renomear o Launcher (erro %lu)", GetLastError()); DeleteFileW(fresh.c_str()); return false; }
    if (!MoveFileExW(fresh.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        Log(L"falha ao instalar o Launcher novo (erro %lu)", GetLastError());
        MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }
    std::wstring command = L"\"" + self + L"\" --updated";
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(self.c_str(), &command[0], nullptr, nullptr, FALSE, 0, nullptr, g_root.c_str(), &startup, &process))
    {
        Log(L"falha ao abrir o Launcher novo (erro %lu)", GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    Log(L"Launcher atualizado para %S", launcher.sha256.c_str());
    PostMessageW(g_window, WM_UPDATE_RESTART, 0, 0);
    return true;
}
}  // namespace

bool IsAllowedPath(const std::wstring& path)
{
    if (path.empty() || path.size() > 200) return false;
    if (path[0] == L'/' || path.back() == L'/') return false;
    for (wchar_t c : path)
        if (c < 0x20 || c == L'\\' || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|') return false;
    size_t start = 0;
    for (;;)
    {
        size_t end = path.find(L'/', start);
        std::wstring part = path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (part.empty() || part == L"." || part == L".." || part[0] == L'.' || part.back() == L'.' || part.back() == L' ') return false;
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    std::wstring lower = Lower(path);
    if (lower == L"main.exe") return true;
    if (lower.compare(0, 5, L"data/") == 0) return lower.size() > 5;
    return false;
}

bool ParseManifestForTest(const std::string& json, size_t* fileCount, std::string* version)
{
    Manifest manifest;
    if (!ParseManifest(json, manifest)) return false;
    *fileCount = manifest.files.size();
    *version = manifest.version;
    return true;
}

DWORD WINAPI UpdateThread(LPVOID parameter)
{
    UpdateContext* context = (UpdateContext*)parameter;
    g_window = context->window;
    g_root = context->root;
    for (int i = 0; i < 10 && GetFileAttributesW((g_root + L"Launcher.old").c_str()) != INVALID_FILE_ATTRIBUTES; i++)
    {
        if (DeleteFileW((g_root + L"Launcher.old").c_str())) break;
        Sleep(200);
    }

    Status(L"Verificando atualiza\u00e7\u00f5es...");
    Http http;
    std::string manifestText, signature;
    bool signatureOk = false;
    // 2 tentativas: uma publicacao trocando manifest.json e manifest.sig no mesmo instante nao vira falso alarme.
    for (int attempt = 0; attempt < 2 && !signatureOk; attempt++)
    {
        if (attempt > 0) Sleep(2000);
        if (!http.Open() || !http.GetText(std::wstring(kBase) + L"manifest.json", &manifestText, kMaxManifest) ||
            !http.GetText(std::wstring(kBase) + L"manifest.sig", &signature, 1024))
        {
            Log(L"sem conexao com https://%s%smanifest.json (erro %lu)", kHost, kBase, GetLastError());
            Finish(UPDATE_OFFLINE, L"Sem conex\u00e3o com o servidor de atualiza\u00e7\u00f5es. O cliente pode estar desatualizado.");
            return 0;
        }
        while (!signature.empty() && (signature.back() == '\n' || signature.back() == '\r' || signature.back() == ' ')) signature.pop_back();
        signatureOk = VerifyManifestSignature(manifestText, signature);
    }
    if (!signatureOk)
    {
        Log(L"assinatura do manifest invalida: atualizacao recusada");
        Finish(UPDATE_OFFLINE, L"Atualiza\u00e7\u00e3o recusada: assinatura inv\u00e1lida. Avise a equipe Mu CLT.");
        return 0;
    }
    Manifest manifest;
    if (!ParseManifest(manifestText, manifest))
    {
        Log(L"manifest com formato invalido");
        Finish(UPDATE_OFFLINE, L"Manifest de atualiza\u00e7\u00e3o inv\u00e1lido. Avise a equipe Mu CLT.");
        return 0;
    }
    Log(L"manifest %S com %u arquivos", manifest.version.c_str(), (unsigned)manifest.files.size());

    if (manifest.hasLauncher && !context->justUpdated && SelfUpdate(http, manifest.launcher)) return 0;

    // 1) O que precisa baixar.
    HashCache cache;
    LoadCache(cache);
    std::vector<const ManifestFile*> pending;
    uint64_t pendingBytes = 0;
    size_t checked = 0;
    for (const ManifestFile& file : manifest.files)
    {
        checked++;
        Progress(0, (int)(checked * 1000 / (manifest.files.size() ? manifest.files.size() : 1)));
        if (!IsAllowedPath(file.path)) { Log(L"caminho recusado no manifest: %s", file.path.c_str()); continue; }
        std::wstring local = LocalPath(file.path), key = Lower(file.path);
        uint64_t size = 0, mtime = 0;
        bool needed = true;
        if (FileInfo(local, &size, &mtime) && size == file.size)
        {
            auto hit = cache.find(key);
            if (hit != cache.end() && hit->second.size == size && hit->second.mtime == mtime) needed = hit->second.sha != file.sha256;
            else
            {
                if (checked % 50 == 1) Status(L"Verificando arquivos...");
                std::string hash;
                if (HashFile(local, &hash))
                {
                    cache[key] = CacheEntry{ size, mtime, hash };
                    needed = hash != file.sha256;
                }
            }
        }
        if (needed) { pending.push_back(&file); pendingBytes += file.size; }
    }

    // 2) Downloads.
    uint64_t done = 0;
    for (const ManifestFile* file : pending)
    {
        std::wstring local = LocalPath(file->path);
        DownloadResult result = Download(http, *file, local, &done, pendingBytes);
        if (result == DOWNLOAD_IN_USE)
        {
            SaveCache(cache);
            Finish(UPDATE_BLOCKED, L"Feche o jogo (" + FileName(file->path) + L" em uso) e abra o Launcher novamente.");
            return 0;
        }
        if (result != DOWNLOAD_OK)
        {
            SaveCache(cache);
            Finish(UPDATE_BLOCKED, L"Falha ao baixar " + FileName(file->path) + L". Verifique a conex\u00e3o e abra o Launcher novamente.");
            return 0;
        }
        uint64_t size = 0, mtime = 0;
        if (FileInfo(local, &size, &mtime)) cache[Lower(file->path)] = CacheEntry{ size, mtime, file->sha256 };
    }
    SaveCache(cache);
    Progress(1000, 1000);
    Log(L"cliente atualizado: %u arquivo(s) baixado(s)", (unsigned)pending.size());
    Finish(UPDATE_OK, L"Cliente atualizado! Pronto para jogar.");
    return 0;
}
