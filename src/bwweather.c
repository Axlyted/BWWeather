/* BWWeather: brings back Black & White's live weather feature.

   Black & White (2001) could show the real weather of your home town in the
   game. It asked Lionhead's server weather.bwgame.com for it, and that server
   has been gone for 20 years. This DLL stands in for the game's
   LHMultiplayerR.dll (the original is renamed to LHMultiplayerR_orig.dll and
   every export is forwarded to it, see LHMultiplayerR.def). It replaces only:

   - the HTTP calls of the game's LHHttp2 class, but only for the weather server:
     instead of connecting, it answers the game's two requests itself
       action=queryuserdata  ->  "1\n<country code>\n<area>\n<city>\n"
       action=queryweather   ->  "<d>:<m>:<yyyy>:0:0:<temp C>:<visibility km>:<type>:0\n"
     using the current weather from Open-Meteo (https://open-meteo.com) for the
     town in bwweather.ini. All other HTTP traffic goes to the original code.
   - LHNetGetCurrentProfileWString("login name"), only when called from the
     game's weather code: the game skips the weather feature unless it has a
     bwgame.com login name, so BWWeather supplies a placeholder. The game's
     login data is never sent anywhere.

   Everything is logged to bwweather.log next to the game. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#pragma intrinsic(_ReturnAddress, _AddressOfReturnAddress)

/* runblack.exe v1.42: the weather code (LHInetWeather) lives here */
#define WEATHER_CODE_START 0x005E7F00
#define WEATHER_CODE_END   0x005EA4A0

static char g_dir[MAX_PATH];         /* game folder, with trailing backslash */
static char g_log[MAX_PATH];
static HMODULE g_orig;
static CRITICAL_SECTION g_cs;

static void logf(const char *fmt, ...) {
    FILE *f = NULL; va_list a; SYSTEMTIME t;
    if (!g_log[0] || fopen_s(&f, g_log, "a") || !f) return;
    GetLocalTime(&t);
    fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
    fputc('\n', f); fclose(f);
}

static FARPROC orig(const char *name) {
    FARPROC p = g_orig ? GetProcAddress(g_orig, name) : NULL;
    if (!p) logf("ERROR: %s not found in LHMultiplayerR_orig.dll", name);
    return p;
}

int BWWeatherVersion(void) { return 1; }

/* Exports with the original C++ names (and ordinals). __fastcall functions are
   decorated @name@<argument bytes>, __cdecl ones _name. */
#pragma comment(linker, "/EXPORT:??1LHHttp2@@QAE@XZ=@bww_Dtor@8,@78")
#pragma comment(linker, "/EXPORT:?Close@LHHttp2@@QAE?AW4LH_RETURN@@XZ=@bww_Close@8,@243")
#pragma comment(linker, "/EXPORT:?GetDocument@LHHttp2@@QAE?AW4LH_HTTP_STATUS@@PAD@Z=@bww_GetDocument@12,@359")
#pragma comment(linker, "/EXPORT:?GetDocumentSize@LHHttp2@@QAEKXZ=@bww_GetDocumentSize@8,@362")
#pragma comment(linker, "/EXPORT:?HelperGetDocument@LHHttp2@@QAE?AW4LH_HTTP_STATUS@@_N@Z=@bww_HelperGetDocument@12,@502")
#pragma comment(linker, "/EXPORT:?LHNetGetCurrentProfileWString@@YA?AW4LH_RETURN@@PADPAGPAK@Z=@bww_GetProfileWString@8,@572")
#pragma comment(linker, "/EXPORT:?Open@LHHttp2@@QAE?AW4LH_RETURN@@PADG@Z=@bww_Open@16,@634")
#pragma comment(linker, "/EXPORT:?PrepareRequest@LHHttp2@@QAE?AW4LH_HTTP_STATUS@@W4HTTP_REQUEST_TYPE@@PADPAVLHHttpHeaders@@1K@Z=@bww_PrepareRequest@28,@668")
#pragma comment(linker, "/EXPORT:?Reset@LHHttp2@@QAEXXZ=@bww_Reset@8,@746")

/* ---------------------------------------------------------------- settings */
static struct {
    char city[128], country[8], area[128];
    double lat, lon; int haveCoords;
    int forceType, haveForceTemp, forceTemp;
    char server[128];
} cfg;

static void load_settings(void) {
    char ini[MAX_PATH], b[64];
    HKEY k; DWORD sz;
    sprintf_s(ini, MAX_PATH, "%sbwweather.ini", g_dir);
    GetPrivateProfileStringA("BWWeather", "City", "", cfg.city, sizeof cfg.city, ini);
    GetPrivateProfileStringA("BWWeather", "Country", "", cfg.country, sizeof cfg.country, ini);
    GetPrivateProfileStringA("BWWeather", "Area", "", cfg.area, sizeof cfg.area, ini);
    GetPrivateProfileStringA("BWWeather", "Latitude", "", b, sizeof b, ini);
    if (b[0]) { cfg.lat = atof(b); GetPrivateProfileStringA("BWWeather", "Longitude", "", b, sizeof b, ini); if (b[0]) { cfg.lon = atof(b); cfg.haveCoords = 1; } }
    cfg.forceType = GetPrivateProfileIntA("BWWeather", "ForceType", 0, ini);
    GetPrivateProfileStringA("BWWeather", "ForceTemperature", "", b, sizeof b, ini);
    if (b[0]) { cfg.forceTemp = atoi(b); cfg.haveForceTemp = 1; }

    strcpy_s(cfg.server, sizeof cfg.server, "weather.bwgame.com");
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Lionhead Studios Ltd\\Black & White\\BWSetup", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        sz = sizeof cfg.server - 1;
        if (RegQueryValueExA(k, "WeatherSystemServerName", NULL, NULL, (BYTE *)cfg.server, &sz) != ERROR_SUCCESS || !cfg.server[0])
            strcpy_s(cfg.server, sizeof cfg.server, "weather.bwgame.com");
        RegCloseKey(k);
    }
}

/* ------------------------------------------------------- Open-Meteo fetch */
static char *https_get(const wchar_t *host, const wchar_t *path) {
    HINTERNET s = NULL, c = NULL, r = NULL;
    char *buf = NULL; DWORD len = 0, cap = 0, got, status = 0, ssz = sizeof status;
    s = WinHttpOpen(L"BWWeather/1.0 (Black & White live weather)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    if (!s) s = WinHttpOpen(L"BWWeather/1.0 (Black & White live weather)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!s) goto done;
    WinHttpSetTimeouts(s, 5000, 5000, 8000, 8000);
    if (!(c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0))) goto done;
    if (!(r = WinHttpOpenRequest(c, L"GET", path, NULL, NULL, NULL, WINHTTP_FLAG_SECURE))) goto done;
    if (!WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0) || !WinHttpReceiveResponse(r, NULL)) goto done;
    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &ssz, NULL);
    for (;;) {
        if (len + 4097 > cap) { char *n = realloc(buf, cap = cap * 2 + 8192); if (!n) { free(buf); buf = NULL; goto done; } buf = n; }
        if (!WinHttpReadData(r, buf + len, 4096, &got) || !got) break;
        len += got;
    }
    if (buf) buf[len] = 0;
    if (status != 200) { logf("HTTP %lu from %S", status, host); free(buf); buf = NULL; }
done:
    if (!buf) logf("could not reach %S (error %lu)", host, GetLastError());
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return buf;
}

/* tiny JSON helpers: find "key": after 'from' and read a number or string */
static const char *json_key(const char *from, const char *key) {
    char pat[64]; const char *p;
    sprintf_s(pat, sizeof pat, "\"%s\":", key);
    p = from ? strstr(from, pat) : NULL;
    return p ? p + strlen(pat) : NULL;
}
static int json_num(const char *from, const char *key, double *out) {
    const char *p = json_key(from, key);
    if (!p || *p == 'n') return 0;      /* missing or null */
    *out = atof(p); return 1;
}
/* JSON string (UTF-8) -> plain text in the ANSI code page the game uses */
static int json_str(const char *from, const char *key, char *out, int outsz) {
    const char *p = json_key(from, key); char utf8[256]; wchar_t w[256]; int n = 0;
    out[0] = 0;
    if (!p || *p != '"') return 0;
    for (p++; *p && *p != '"' && n < (int)sizeof utf8 - 1; p++) {
        if (*p == '\\' && p[1]) p++;
        utf8[n++] = *p;
    }
    utf8[n] = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, 256)) return 0;
    WideCharToMultiByte(CP_ACP, 0, w, -1, out, outsz, "?", NULL);
    for (n = 0; out[n]; n++) if (out[n] == ':' || out[n] == '\n' || out[n] == '\r') out[n] = ' ';
    return 1;
}

static void url_encode(const char *in, char *out, int outsz) {      /* UTF-8 percent-encoding */
    wchar_t w[256]; char u[512]; int i, o = 0;
    MultiByteToWideChar(CP_ACP, 0, in, -1, w, 256);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, NULL, NULL);
    for (i = 0; u[i] && o < outsz - 4; i++) {
        unsigned char ch = (unsigned char)u[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_') out[o++] = ch;
        else o += sprintf_s(out + o, outsz - o, "%%%02X", ch);
    }
    out[o] = 0;
}

/* The game's weather types are the rows of Scripts\weatherinfo.lhw (2..12).
   Nothing in the game names them; this mapping from WMO weather codes is
   read from what each row sets (clouds, rain, lightning, storms, wind).
   The game decides between rain and snow from the temperature. */
static int game_type(int wmo, double windKmh) {
    int t;
    switch (wmo) {
    case 0: case 1:                     t = 4;  break;  /* clear */
    case 2:                             t = 7;  break;  /* partly cloudy */
    case 3: case 45: case 48:           t = 11; break;  /* overcast, fog */
    case 51: case 53: case 55:
    case 56: case 57:                   t = 3;  break;  /* drizzle */
    case 61: case 80: case 71: case 85: t = 2;  break;  /* light rain / snow / showers */
    case 63: case 81: case 73: case 77: t = 9;  break;  /* moderate */
    case 65: case 82: case 66: case 67:
    case 75: case 86:                   t = 6;  break;  /* heavy */
    case 95: case 96: case 99:          t = 10; break;  /* thunderstorm */
    default:                            t = 11; break;
    }
    if (windKmh >= 60.0 && t != 10) t = 5;               /* gale: storm */
    return t;
}

static struct {
    volatile LONG done;
    int ok;
    char country[3], area[150], city[150];
    int temp, vis, type, wmo;
} wx;
static HANDLE g_fetchDone;

static DWORD WINAPI fetch_thread(LPVOID unused) {
    char q[512], enc[256], *j = NULL; wchar_t wq[512];
    double lat = cfg.lat, lon = cfg.lon, t, w, v, code;
    const char *cur;
    (void)unused;

    if (!cfg.haveCoords) {
        url_encode(cfg.city, enc, sizeof enc);
        sprintf_s(q, sizeof q, "/v1/search?name=%s&count=1&language=en&format=json%s%s", enc,
                  cfg.country[0] ? "&countryCode=" : "", cfg.country);
        MultiByteToWideChar(CP_ACP, 0, q, -1, wq, 512);
        if (!(j = https_get(L"geocoding-api.open-meteo.com", wq))) goto out;
        if (!json_num(j, "latitude", &lat) || !json_num(j, "longitude", &lon)) { logf("Open-Meteo does not know the town \"%s\" - check City/Country in bwweather.ini", cfg.city); goto out; }
        json_str(j, "name", wx.city, sizeof wx.city);
        json_str(j, "admin1", wx.area, sizeof wx.area);
        json_str(j, "country_code", wx.country, sizeof wx.country);
        free(j); j = NULL;
    } else {
        strcpy_s(wx.city, sizeof wx.city, cfg.city[0] ? cfg.city : "Home");
        strcpy_s(wx.country, sizeof wx.country, cfg.country[0] ? cfg.country : "XX");
        strcpy_s(wx.area, sizeof wx.area, cfg.area);
    }
    if (!wx.area[0]) strcpy_s(wx.area, sizeof wx.area, wx.city);
    if (!wx.country[0]) strcpy_s(wx.country, sizeof wx.country, "XX");

    sprintf_s(q, sizeof q, "/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code,wind_speed_10m,visibility", lat, lon);
    MultiByteToWideChar(CP_ACP, 0, q, -1, wq, 512);
    if (!(j = https_get(L"api.open-meteo.com", wq))) goto out;
    cur = strstr(j, "\"current\":{");
    if (!cur || !json_num(cur, "temperature_2m", &t) || !json_num(cur, "weather_code", &code)) { logf("unexpected weather reply: %.200s", j); goto out; }
    if (!json_num(cur, "wind_speed_10m", &w)) w = 0;
    if (!json_num(cur, "visibility", &v)) v = 20000;
    wx.wmo = (int)code;
    wx.temp = (int)(t < 0 ? t - 0.5 : t + 0.5);
    wx.vis = (int)(v / 1000.0 + 0.5); if (wx.vis > 99) wx.vis = 99;
    wx.type = game_type(wx.wmo, w);
    wx.ok = 1;
    logf("weather for %s, %s, %s (%.3f, %.3f): %d C, WMO code %d, wind %.0f km/h, visibility %d km -> game type %d",
         wx.city, wx.area, wx.country, lat, lon, wx.temp, wx.wmo, w, wx.vis, wx.type);
out:
    free(j);
    if (wx.ok) {
        if (cfg.forceType >= 2 && cfg.forceType <= 12) { wx.type = cfg.forceType; logf("ForceType: using game type %d", wx.type); }
        if (cfg.haveForceTemp) { wx.temp = cfg.forceTemp; logf("ForceTemperature: using %d C", wx.temp); }
    }
    InterlockedExchange(&wx.done, 1);
    SetEvent(g_fetchDone);
    return 0;
}

/* ------------------------------------------------ fake HTTP connections */
#define NFAKE 8
typedef struct { void *self; char url[512]; char *doc; DWORD len; } Fake;
static Fake g_fake[NFAKE];

static Fake *fake_of(void *self) {
    int i;
    for (i = 0; i < NFAKE; i++) if (g_fake[i].self == self) return &g_fake[i];
    return NULL;
}
static void fake_drop(void *self) {
    Fake *f;
    EnterCriticalSection(&g_cs);
    if ((f = fake_of(self)) != NULL) { free(f->doc); memset(f, 0, sizeof *f); }
    LeaveCriticalSection(&g_cs);
}

/* LHHttp2 methods are __thiscall; __fastcall with an unused edx matches it. */
typedef int  (__fastcall *Open_t)(void *, void *, const char *, unsigned short);
typedef int  (__fastcall *Prep_t)(void *, void *, int, char *, void *, char *, unsigned long);
typedef int  (__fastcall *Help_t)(void *, void *, int);
typedef DWORD(__fastcall *Size_t)(void *, void *);
typedef int  (__fastcall *GetD_t)(void *, void *, char *);
typedef int  (__fastcall *Ret_t)(void *, void *);
typedef void (__fastcall *Void_t)(void *, void *);
typedef int  (__cdecl *Prof_t)(char *, wchar_t *, unsigned long *);

#define HTTP_PREPARED 8   /* what the game expects from PrepareRequest */
#define HTTP_GOT_DOC  7   /* ... and from HelperGetDocument */

int __fastcall bww_Open(void *self, void *edx, const char *host, unsigned short port) {
    static Open_t o; int i;
    if (host && !_stricmp(host, cfg.server)) {
        EnterCriticalSection(&g_cs);
        for (i = 0; i < NFAKE && g_fake[i].self; i++) ;
        if (i < NFAKE) { memset(&g_fake[i], 0, sizeof g_fake[i]); g_fake[i].self = self; }
        LeaveCriticalSection(&g_cs);
        if (i < NFAKE) { logf("game connects to %s:%u - answered by BWWeather", host, port); return 0; }
    }
    if (!o) o = (Open_t)orig("?Open@LHHttp2@@QAE?AW4LH_RETURN@@PADG@Z");
    return o ? o(self, edx, host, port) : 1;
}

int __fastcall bww_PrepareRequest(void *self, void *edx, int type, char *url, void *hdr, char *body, unsigned long len) {
    static Prep_t o; Fake *f;
    EnterCriticalSection(&g_cs);
    if ((f = fake_of(self)) != NULL) strcpy_s(f->url, sizeof f->url, url ? url : "");
    LeaveCriticalSection(&g_cs);
    if (f) return HTTP_PREPARED;
    if (!o) o = (Prep_t)orig("?PrepareRequest@LHHttp2@@QAE?AW4LH_HTTP_STATUS@@W4HTTP_REQUEST_TYPE@@PADPAVLHHttpHeaders@@1K@Z");
    return o ? o(self, edx, type, url, hdr, body, len) : 0;
}

int __fastcall bww_HelperGetDocument(void *self, void *edx, int flag) {
    static Help_t o; Fake *f; char doc[512]; SYSTEMTIME t; char url[512];
    EnterCriticalSection(&g_cs);
    f = fake_of(self);
    if (f) strcpy_s(url, sizeof url, f->url);
    LeaveCriticalSection(&g_cs);
    if (!f) {
        if (!o) o = (Help_t)orig("?HelperGetDocument@LHHttp2@@QAE?AW4LH_HTTP_STATUS@@_N@Z");
        return o ? o(self, edx, flag) : 0;
    }
    if (strstr(url, "queryuserdata")) logf("game asks for the user's town (queryuserdata; login details stay on this PC)");
    else logf("game asks for the weather: %s", url);
    if (!wx.done && WaitForSingleObject(g_fetchDone, 10000) != WAIT_OBJECT_0) { logf("weather not ready after 10 s - game gets no weather this time"); return 0; }
    if (!wx.ok) { logf("no weather available - game gets no weather this time"); return 0; }

    if (strstr(url, "queryuserdata"))
        sprintf_s(doc, sizeof doc, "1\n%s\n%s\n%s\n", wx.country, wx.area, wx.city);
    else if (strstr(url, "queryweather")) {
        GetLocalTime(&t);
        sprintf_s(doc, sizeof doc, "%d:%d:%d:0:0:%d:%d:%d:0\n", t.wDay, t.wMonth, t.wYear, wx.temp, wx.vis, wx.type);
    } else { logf("unknown request - ignored"); return 0; }

    EnterCriticalSection(&g_cs);
    if ((f = fake_of(self)) != NULL) {
        free(f->doc);
        f->len = (DWORD)strlen(doc);
        f->doc = _strdup(doc);
    }
    LeaveCriticalSection(&g_cs);
    { char *nl; for (nl = doc; (nl = strchr(nl, '\n')) != NULL; ) *nl = '|'; }
    logf("answered: %s", doc);
    return HTTP_GOT_DOC;
}

DWORD __fastcall bww_GetDocumentSize(void *self, void *edx) {
    static Size_t o; Fake *f; DWORD n = 0;
    EnterCriticalSection(&g_cs);
    if ((f = fake_of(self)) != NULL) n = f->doc ? f->len : 0;
    LeaveCriticalSection(&g_cs);
    if (f) return n;
    if (!o) o = (Size_t)orig("?GetDocumentSize@LHHttp2@@QAEKXZ");
    return o ? o(self, edx) : 0;
}

int __fastcall bww_GetDocument(void *self, void *edx, char *buf) {
    static GetD_t o; Fake *f;
    EnterCriticalSection(&g_cs);
    if ((f = fake_of(self)) != NULL && f->doc && buf) memcpy(buf, f->doc, f->len);
    LeaveCriticalSection(&g_cs);
    if (f) return HTTP_GOT_DOC;
    if (!o) o = (GetD_t)orig("?GetDocument@LHHttp2@@QAE?AW4LH_HTTP_STATUS@@PAD@Z");
    return o ? o(self, edx, buf) : 0;
}

int __fastcall bww_Close(void *self, void *edx) {
    static Ret_t o;
    if (fake_of(self)) return 0;                 /* never really opened */
    if (!o) o = (Ret_t)orig("?Close@LHHttp2@@QAE?AW4LH_RETURN@@XZ");
    return o ? o(self, edx) : 0;
}

void __fastcall bww_Reset(void *self, void *edx) {
    static Void_t o;
    if (fake_of(self)) { fake_drop(self); return; }
    if (!o) o = (Void_t)orig("?Reset@LHHttp2@@QAEXXZ");
    if (o) o(self, edx);
}

void __fastcall bww_Dtor(void *self, void *edx) {
    static Void_t o;
    fake_drop(self);
    if (!o) o = (Void_t)orig("??1LHHttp2@@QAE@XZ");
    if (o) o(self, edx);
}

/* The weather feature needs a bwgame.com login name. Only for the game's
   weather code, hand it a placeholder when there is none.
   The original is __cdecl (key, buf, size). The linker cuts a /EXPORT name at
   the first @ when the internal symbol is a plain C name, so this is declared
   __fastcall with only the two register arguments: that gives it a decorated
   name, and like __cdecl it leaves the stack arguments for the caller to pop.
   The three real arguments are read from the stack above the return address. */
int __fastcall bww_GetProfileWString(void *ecx_unused, void *edx_unused) {
    static Prof_t o;
    void **ra = (void **)_AddressOfReturnAddress();
    char *key = (char *)ra[1];
    wchar_t *buf = (wchar_t *)ra[2];
    unsigned long *size = (unsigned long *)ra[3];
    DWORD_PTR from = (DWORD_PTR)_ReturnAddress();
    int r;
    (void)ecx_unused; (void)edx_unused;
    if (!o) o = (Prof_t)orig("?LHNetGetCurrentProfileWString@@YA?AW4LH_RETURN@@PADPAGPAK@Z");
    r = o ? o(key, buf, size) : 1;
    if (from >= WEATHER_CODE_START && from < WEATHER_CODE_END && key && !strcmp(key, "login name") &&
        buf && (r != 0 || !buf[0])) {
        wcscpy_s(buf, 32, L"BWWeather");
        logf("no bwgame.com login - gave the weather code the placeholder name \"BWWeather\"");
        r = 0;
    }
    return r;
}

/* The game decides whether it is online with LHCheckForInternetConnection,
   which tries Lionhead's old www.bwgame.com and so always says "offline" now.
   The weather code copies that result into its own "offline" flag and then
   skips both requests:
       005E9B4D  mov al,[00D01974]   ; online?
       005E9B52  test al,al
       005E9B54  sete cl             ; offline = !online
       005E9B57  mov [ebp+0CCh],cl
   Only for the weather code, "sete cl" becomes "xor cl,cl; nop" (never
   offline). The rest of the game keeps its own online check. If the PC
   really is offline, BWWeather's own weather fetch fails and the game gets
   no weather, as before. */
static void patch_online_check(void) {
    static const BYTE expect[] = { 0xA0, 0x74, 0x19, 0xD0, 0x00, 0x84, 0xC0, 0x0F, 0x94, 0xC1, 0x88, 0x8D, 0xCC, 0x00, 0x00, 0x00 };
    BYTE *p = (BYTE *)0x005E9B4D;
    DWORD old;
    HMODULE exe = GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
    if (exe != (HMODULE)0x00400000 || 0x00400000 + nt->OptionalHeader.SizeOfImage < 0x005E9B60 || memcmp(p, expect, sizeof expect)) {
        logf("runblack.exe is not the expected v1.42 - the game's own online check is left alone");
        return;
    }
    if (VirtualProtect(p + 7, 3, PAGE_EXECUTE_READWRITE, &old)) {
        p[7] = 0x32; p[8] = 0xC9; p[9] = 0x90;          /* xor cl,cl ; nop */
        VirtualProtect(p + 7, 3, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p + 7, 3);
        logf("weather code set to always count as online (the game's online check uses the dead www.bwgame.com)");
    }
}

/* ------------------------------------------------------------------ main */
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        char p[MAX_PATH], *s; HANDLE th;
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_cs);
        GetModuleFileNameA(inst, g_dir, MAX_PATH);
        if ((s = strrchr(g_dir, '\\')) != NULL) s[1] = 0;
        sprintf_s(g_log, MAX_PATH, "%sbwweather.log", g_dir);
        DeleteFileA(g_log);
        sprintf_s(p, MAX_PATH, "%sLHMultiplayerR_orig.dll", g_dir);
        g_orig = LoadLibraryA(p);
        logf("BWWeather loaded (original multiplayer DLL: %s)", g_orig ? "ok" : "MISSING");
        load_settings();
        g_fetchDone = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (!cfg.city[0] && !cfg.haveCoords) {
            logf("no town set - put your town in bwweather.ini (City=...). The game gets no weather.");
            InterlockedExchange(&wx.done, 1); SetEvent(g_fetchDone);
        } else {
            logf("town: %s%s%s, weather server name the game uses: %s", cfg.haveCoords ? "coordinates" : cfg.city,
                 cfg.country[0] ? ", " : "", cfg.country, cfg.server);
            th = CreateThread(NULL, 0, fetch_thread, NULL, 0, NULL);
            if (th) CloseHandle(th); else { InterlockedExchange(&wx.done, 1); SetEvent(g_fetchDone); }
            patch_online_check();
        }
    }
    return TRUE;
}
