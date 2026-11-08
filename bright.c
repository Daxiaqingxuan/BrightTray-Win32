/* ============================================================================
 *  BrightTray - 极轻量屏幕亮度托盘工具 (Twinkle Tray 替代品)
 *
 *  - 纯 Win32 / C，无 .NET、无 Electron、无第三方控件库
 *  - 通过 DDC/CI (dxva2 Monitor Configuration API) 直接调节显示器硬件亮度
 *  - 自绘滑条，常驻内存约 1MB
 *
 *  构建 (TinyCC):
 *    tcc bright.c -o BrightTray.exe -lshell32 -ladvapi32 -Wl,-subsystem=windows
 *
 *  用法:
 *    左键单击托盘图标  -> 弹出滑条面板（每台显示器一条，可拖动）
 *    面板内滚轮        -> 所有显示器 +/-5%
 *    右键托盘图标      -> 菜单（开机自启动 / 退出）
 *    BrightTray.exe test -> 自检，输出 BrightTray_test.log
 * ==========================================================================*/
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include "icon_data.h"

/* ---------------------------------------------------------------------------
 * 补齐 TCC 精简头文件中缺失的 shell API 定义
 * (结构与 shellapi.h 中的 NOTIFYICONDATAW 二进制兼容)
 * -------------------------------------------------------------------------*/
#define NIM_ADD              0x00000000
#define NIM_DELETE           0x00000002
#define NIM_SETVERSION       0x00000004
#define NIF_MESSAGE          0x00000001
#define NIF_ICON             0x00000002
#define NIF_TIP              0x00000004
#define NOTIFYICON_VERSION_4 4

typedef struct {
    DWORD cbSize;
    HWND  hWnd;
    UINT  uID;
    UINT  uFlags;
    UINT  uCallbackMessage;
    HICON hIcon;
    WCHAR szTip[128];
    DWORD dwState;
    DWORD dwStateMask;
    WCHAR szInfo[256];
    union { UINT uTimeout; UINT uVersion; } u;
    WCHAR szInfoTitle[64];
    DWORD dwInfoFlags;
    struct { DWORD a; WORD b, c; BYTE d[8]; } guidItem;
    HICON hBalloonIcon;
} NOTIFYICONDATAW;

BOOL WINAPI Shell_NotifyIconW(DWORD dwMessage, NOTIFYICONDATAW *lpData);

/* TCC 精简头文件未包含 winnls.h，手动声明编码转换 API */
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
int WINAPI MultiByteToWideChar(UINT CodePage, DWORD dwFlags, const char *lpMultiByteStr,
                               int cbMultiByte, WCHAR *lpWideCharStr, int cchWideChar);
int WINAPI WideCharToMultiByte(UINT CodePage, DWORD dwFlags, const WCHAR *lpWideCharStr,
                               int cchWideChar, char *lpMultiByteStr, int cbMultiByte,
                               const char *lpDefaultChar, BOOL *lpUsedDefaultChar);
BOOL WINAPI SetLayeredWindowAttributes(HWND hwnd, COLORREF key, BYTE alpha, DWORD flags);
HICON WINAPI CreateIconFromResourceEx(unsigned char *presbits, DWORD dwResSize, BOOL fIcon,
                                      DWORD dwVer, int cxDesired, int cyDesired, UINT flags);

/* 求托盘图标在屏幕上的矩形 —— 用来判断鼠标是不是滚在图标上 */
typedef struct {
    DWORD cbSize;
    HWND  hWnd;
    UINT  uID;
    GUID  guidItem;
} NOTIFYICONIDENTIFIER;
HRESULT WINAPI Shell_NotifyIconGetRect(const NOTIFYICONIDENTIFIER *id, RECT *rc);

/* 可能缺失的常量兜底 */
#ifndef CS_DROPSHADOW
#define CS_DROPSHADOW 0x00020000
#endif
#ifndef WS_EX_LAYERED
#define WS_EX_LAYERED 0x00080000
#endif
#ifndef WS_EX_NOACTIVATE
#define WS_EX_NOACTIVATE 0x08000000
#endif
#ifndef LWA_ALPHA
#define LWA_ALPHA 0x00000002
#endif
#ifndef TPM_NONOTIFY
#define TPM_NONOTIFY 0x0080
#endif

/* ---------------------------------------------------------------------------
 * 常量 / 布局
 * -------------------------------------------------------------------------*/
#define APPNAME          L"BrightTray"
#define WM_TRAY          (WM_APP + 1)
#define WM_SHOWME        (WM_APP + 2)   /* 二次启动时通知已运行实例弹出面板 */
#define WM_SETPCT        (WM_APP + 3)   /* 命令行 set：交给已运行实例去写硬件 */
#define NIN_SELECT       (WM_USER + 0)
#define NIN_KEYSELECT    (WM_USER + 1)
#define TIMER_OSD        1
#define ID_AUTOSTART     2001
#define ID_EXIT          2002
#define WHEEL_STEP       5
#define DDC_GAP          50   /* 两次硬件写入的最小间隔(ms)，太快显示器会罢工 */
#define DDC_GAP_MAX      500  /* 被拒后退避的上限 */
#define MAXMON           8

#define COLW        60
#define COLW1       76    /* 单显示器列宽：够放滑块+百分比，不铺满整条 */
#define MARGIN      12
#define NAMEH       17
#define PCTH        22
/* g_nameh 在开面板时决定：单显示器不显示顶部标签，这一行整体省掉；
 * 多显示器仍保留显示器名用于区分各列。 */
#define TRACK_TOP   (MARGIN + g_nameh + 4 + PCTH + 8)
#define TRACK_H     140
#define TRACK_BOT   (TRACK_TOP + TRACK_H)
#define POPUP_H     (TRACK_BOT + 12)

#define CLR_BG       RGB(250, 250, 250)
#define CLR_BORDER   RGB(224, 224, 224)
#define CLR_DIVIDER  RGB(238, 238, 238)
#define CLR_TRACK    RGB(226, 226, 226)
#define CLR_TRACK_D  RGB(238, 238, 238)
#define CLR_FILL     RGB(0, 103, 192)
#define CLR_NAME     RGB(96, 96, 96)
#define CLR_PCT      RGB(28, 28, 28)
#define CLR_PCT_D    RGB(165, 165, 165)

static const WCHAR *CLS_MAIN  = L"BrightTrayMain";
static const WCHAR *CLS_POPUP = L"BrightTrayPopup";
static const WCHAR *CLS_OSD   = L"BrightTrayOsd";
static const WCHAR *RUN_KEY   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

/* ---------------------------------------------------------------------------
 * dxva2 显示器亮度 API —— 运行时动态加载，避免任何链接期依赖
 * -------------------------------------------------------------------------*/
typedef struct {
    HANDLE hPhysicalMonitor;
    WCHAR  szPhysicalMonitorDescription[128];
} PHYSMON;

static HMODULE hDxva2;
static BOOL (WINAPI *pGetNumPhysMon)(HMONITOR, LPDWORD);
static BOOL (WINAPI *pGetPhysMons)(HMONITOR, DWORD, PHYSMON *);
static BOOL (WINAPI *pDestroyPhysMon)(HANDLE);
static BOOL (WINAPI *pGetBrightness)(HANDLE, LPDWORD, LPDWORD, LPDWORD);
static BOOL (WINAPI *pSetBrightness)(HANDLE, DWORD);

static void LoadDxva2(void)
{
    hDxva2 = LoadLibraryW(L"dxva2.dll");
    if (!hDxva2) return;
    pGetNumPhysMon  = (void *)GetProcAddress(hDxva2, "GetNumberOfPhysicalMonitorsFromHMONITOR");
    pGetPhysMons    = (void *)GetProcAddress(hDxva2, "GetPhysicalMonitorsFromHMONITOR");
    pDestroyPhysMon = (void *)GetProcAddress(hDxva2, "DestroyPhysicalMonitor");
    pGetBrightness  = (void *)GetProcAddress(hDxva2, "GetMonitorBrightness");
    pSetBrightness  = (void *)GetProcAddress(hDxva2, "SetMonitorBrightness");
}

/* ---------------------------------------------------------------------------
 * 显示器表
 * -------------------------------------------------------------------------*/
typedef struct {
    HANDLE hpm;
    WCHAR  name[48];
    DWORD  bmin, bmax;
    int    pct;
    BOOL   ok;
    BOOL   stalled;      /* 写入被硬件忽略（回读值与目标值长期不符） */
} MONINFO;

static MONINFO g_mon[MAXMON];
static int     g_nmon;

/* ---------------------------------------------------------------------------
 * 全局状态
 * -------------------------------------------------------------------------*/
static HINSTANCE g_hInst;
static HWND      g_main, g_popup, g_osd;
static HFONT     g_fSmall, g_fPct, g_fOsd;
static HICON     g_iconBig, g_iconSmall;
static HBRUSH    g_brBg, g_brBorder, g_brDivider, g_brTrack, g_brTrackD, g_brFill, g_brKnob, g_brOsd;
static HPEN      g_penBorder, g_penKnob, g_penKnobD, g_penNull;
static HBITMAP   g_bufBmp;
static HDC       g_bufDc;
static WCHAR     g_osdText[64];
static UINT      g_msgTaskbarCreated;
static int       g_dragIdx = -1;
static int       g_colw = COLW;
static int       g_nameh = NAMEH;   /* 顶部标签行高度；单显示器时为 0 */
static BOOL      g_shotMode;
static BOOL      g_osdWarn;
static int       g_wheelAcc;
static DWORD     g_lastClose;
static DWORD     g_lastTrayClick;
static BOOL      g_trayV4;
static HHOOK     g_mouseHook;
static HHOOK     g_kbHook;
static DWORD     g_keyTick;
static CRITICAL_SECTION g_cs;              /* 保护 g_mon[].hpm 与硬件写入 */
static HANDLE    g_hEvt;                   /* 唤醒后台写亮度线程 */
static volatile LONG g_target[MAXMON];     /* pct+1，0 表示无待写 */
static LONG      g_written[MAXMON];        /* 最近一次写入成功的 pct+1，用于回读校验 */
static DWORD     g_wrTick[MAXMON];         /* 最近一次写入时刻 */
static DWORD     g_vfTick[MAXMON];         /* 最近一次回读校验时刻 */
static int       g_retry[MAXMON];          /* 连续校验失败次数 */
static DWORD     g_monTick;                /* 显示器表上次枚举时刻 */
static BOOL      g_trayRectOk;
static RECT      g_trayRect;
static DWORD     g_trayRectTick;
static int       g_trayWheelAcc;
static HANDLE    g_log;


/* UI strings are UTF-8 in the source and converted to UTF-16 at runtime (TCC
 * does no narrow->wide transcoding). English is the default; Simplified
 * Chinese is chosen automatically on zh-CN systems. BRIGHTTRAY_LANG=en|zh
 * overrides the detection, e.g. to capture English screenshots on a Chinese
 * Windows. Keep each English string short: the popup column is 76 px wide and
 * the OSD window is 168 px wide. */
static WCHAR w_exit[16], w_autostart[32], w_osdfmt[32], w_monfmt[32],
             w_unsupported[16], w_tip[64], w_running[64], w_noresp[16];

enum { LANG_EN = 0, LANG_ZH = 1 };
enum { S_EXIT, S_AUTOSTART, S_OSDFMT, S_MONFMT, S_UNSUPPORTED,
       S_TIP, S_RUNNING, S_NORESP, S_COUNT };

static const char *const UI_TEXT[S_COUNT][2] = {
    { "Exit",                           "退出" },
    { "Start with Windows",             "开机自启动" },
    { "%d%%",                           "亮度 %d%%" },
    { "Display %d",                     "显示器 %d" },
    { "No DDC",                         "不支持" },
    { "BrightTray - scroll on the tray icon to adjust brightness",
                                        "BrightTray 屏幕亮度（图标上滚动可调）" },
    { "BrightTray is already running", "BrightTray 已在运行" },
    { "No response",                    "无响应" },
};
static int g_lang = LANG_EN;

static void ToW(const char *u8, WCHAR *out, int cch)
{
    MultiByteToWideChar(CP_UTF8, 0, u8, -1, out, cch);
}

/* TCC's minimal winnls.h omits this prototype; kernel32 exports it. */
extern WORD WINAPI GetUserDefaultUILanguage(void);

static void ChooseLang(void)
{
    const char *env;
    WORD id = GetUserDefaultUILanguage();
    /* zh-CN only; zh-TW / zh-HK fall back to English rather than showing
     * Simplified glyphs to a Traditional Chinese user. */
    g_lang = (PRIMARYLANGID(id) == LANG_CHINESE &&
              SUBLANGID(id) == SUBLANG_CHINESE_SIMPLIFIED) ? LANG_ZH : LANG_EN;
    env = getenv("BRIGHTTRAY_LANG");
    if (env) {
        if (env[0] == 'z' || env[0] == 'Z') g_lang = LANG_ZH;
        else if (env[0] == 'e' || env[0] == 'E') g_lang = LANG_EN;
    }
}

static void InitStrings(void)
{
    ToW(UI_TEXT[S_EXIT][g_lang],        w_exit, 16);
    ToW(UI_TEXT[S_AUTOSTART][g_lang],   w_autostart, 32);
    ToW(UI_TEXT[S_OSDFMT][g_lang],      w_osdfmt, 32);
    ToW(UI_TEXT[S_MONFMT][g_lang],      w_monfmt, 32);
    ToW(UI_TEXT[S_UNSUPPORTED][g_lang], w_unsupported, 16);
    ToW(UI_TEXT[S_TIP][g_lang],         w_tip, 64);
    ToW(UI_TEXT[S_RUNNING][g_lang],     w_running, 64);
    ToW(UI_TEXT[S_NORESP][g_lang],      w_noresp, 16);
}

static int Clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ---------------------------------------------------------------------------
 * 亮度读写
 * -------------------------------------------------------------------------*/
/* 真正写硬件（阻塞几十毫秒），只允许后台线程或单次命令行模式调用。
 * 返回是否写入成功 —— 显示器忙时会拒绝，调用方据此拉长间隔。 */
static BOOL WriteHw(int i, int pct)
{
    MONINFO *m = &g_mon[i];
    DWORD v;

    /* 注意：不能用 hpm != 0 判断有效性！dxva2 在某些显卡/驱动组合下返回的
     * 物理显示器句柄本身就是 0（相当于索引 0），用它读写完全正常。
     * 早先这里写成 "!m->hpm 就返回"，结果每一次写硬件都被静默跳过，
     * 表现为拖动滑块没反应、亮度永远不动。有效与否只看 ok 标志。 */
    if (!m->ok || !pSetBrightness || m->bmax <= m->bmin) return FALSE;
    pct = Clamp(pct, 0, 100);
    v = m->bmin + (DWORD)((DWORDLONG)(m->bmax - m->bmin) * (DWORD)pct / 100);
    if (v > m->bmax) v = m->bmax;
    if (!pSetBrightness(m->hpm, v)) return FALSE;
    return TRUE;   /* 不改 m->pct：界面上的值以用户最新操作为准，避免写回旧值 */
}

/* 后台写亮度线程：每次只取最新目标值，中间态直接丢弃，
 * 所以拖得再快也不会积压 DDC 命令，界面始终跟手。
 * 写入之间留最小间隔 —— DDC/CI 不是随便什么时候都能写，
 * 命令密到一定程度显示器会干脆忽略后续命令（亮度卡住不动）。
 * 一旦某次写入被拒，间隔翻倍退避，给显示器喘口气。 */
static DWORD WINAPI BrightWorker(void *arg)
{
    int i;
    DWORD gap = DDC_GAP;
    (void)arg;

    for (;;) {
        /* 有活时等事件立刻干；没活时也定时醒一次，做回读校验 */
        WaitForSingleObject(g_hEvt, 300);

        for (i = 0; i < MAXMON; i++) {
            LONG t = InterlockedExchange(&g_target[i], 0);
            LONG t2;

            if (t <= 0) continue;
            t2 = InterlockedExchange(&g_target[i], 0);
            if (t2 > 0) t = t2;        /* 处理期间又变了，只写最新的那个 */

            EnterCriticalSection(&g_cs);
            if (WriteHw(i, (int)t - 1)) {
                g_written[i] = t;
                g_wrTick[i] = GetTickCount();
                g_retry[i] = 0;
                g_mon[i].stalled = FALSE;
                gap = DDC_GAP;
            } else if (gap < DDC_GAP_MAX) {
                gap *= 2;               /* 被拒就退避，别把显示器逼死 */
            }
            LeaveCriticalSection(&g_cs);

            Sleep(gap);                 /* 两次 DDC 命令之间必须留间隔 */
        }

        /* 空闲回读校验：确认硬件真的接受了最后一次值。
         * 有些显示器/显卡会"返回成功但不执行"，只靠返回值判断不出来，
         * 所以每隔一会儿读一次实物，不一致就补写，补两次还不行就标红提醒。 */
        for (i = 0; i < MAXMON; i++) {
            DWORD mn = 0, cu = 0, mx = 0, now = GetTickCount();
            LONG w = g_written[i];
            int back;

            if (w <= 0) continue;
            if (InterlockedCompareExchange(&g_target[i], 0, 0) != 0) continue;
            if (g_dragIdx >= 0) continue;                 /* 拖动中不打扰 */
            if (now - g_wrTick[i] < 500) continue;
            /* 只在自己刚写完的一小段时间内校验。时间窗一过就认账，
             * 否则用户用显示器按键或别的程序改了亮度，会被我们当成
             * "写入失败"反复改回去（两个进程抢着写就是这么来的）。 */
            if (now - g_wrTick[i] > 2500) continue;
            if (g_retry[i] >= 2) continue;
            if (now - g_vfTick[i] < 800) continue;

            EnterCriticalSection(&g_cs);
            g_vfTick[i] = now;
            if (g_mon[i].ok && pGetBrightness &&
                pGetBrightness(g_mon[i].hpm, &mn, &cu, &mx) && mx > mn) {
                back = (int)((cu - mn) * 100 / (mx - mn));
                if (back > (int)w - 1 + 2 || back < (int)w - 1 - 2) {
                    if (g_retry[i] < 2) {
                        /* 硬件没接受：原值重写一遍 */
                        g_retry[i]++;
                        InterlockedExchange(&g_target[i], w);
                        SetEvent(g_hEvt);
                    } else {
                        g_mon[i].stalled = TRUE;
                        if (g_popup) InvalidateRect(g_popup, NULL, FALSE);
                    }
                } else {
                    g_retry[i] = 0;
                    if (g_mon[i].stalled) {
                        g_mon[i].stalled = FALSE;
                        if (g_popup) InvalidateRect(g_popup, NULL, FALSE);
                    }
                }
            }
            LeaveCriticalSection(&g_cs);
        }
    }
    return 0;
}

static void ApplyPct(int i, int pct)
{
    pct = Clamp(pct, 0, 100);
    g_mon[i].pct = pct;
    if (!g_mon[i].ok) return;

    /* 关键：不在这里直接写硬件。DDC/CI 写一次要几十毫秒，放在 UI 线程里
     * 会让拖动卡成 PPT，系统还会给窗口挂上"忙碌"光标（那个转圈）。
     * 这里只登记"最新目标值"并唤醒后台线程，界面立刻重绘，硬件异步追。 */
    if (!g_hEvt) { WriteHw(i, pct); return; }
    InterlockedExchange(&g_target[i], (LONG)pct + 1);   /* 0 表示无待写 */
    SetEvent(g_hEvt);
}

static BOOL CALLBACK MonEnumProc(HMONITOR hm, HDC dc, LPRECT rc, LPARAM lp)
{
    MONINFO *m;
    (void)dc; (void)rc; (void)lp;
    if (g_nmon >= MAXMON) return FALSE;

    m = &g_mon[g_nmon];
    ZeroMemory(m, sizeof(*m));

    {
        /* 自行声明，避免 TCC 头文件中 MONITORINFOEXW 的嵌套布局差异 */
        typedef struct {
            DWORD cbSize;
            RECT  rcMonitor;
            RECT  rcWork;
            DWORD dwFlags;
            WCHAR szDevice[32];
        } MIX;
        MIX mi;
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hm, (MONITORINFO *)&mi)) {
            WCHAR *s = mi.szDevice;
            while (*s == L'\\' || *s == L'.') s++;
            lstrcpynW(m->name, s, 48);
        }
    }

    if (pGetNumPhysMon && pGetPhysMons) {
        DWORD cnt = 0;
        if (pGetNumPhysMon(hm, &cnt) && cnt >= 1 && cnt <= 4) {
            PHYSMON pm[4];
            DWORD k;
            ZeroMemory(pm, sizeof(pm));
            if (pGetPhysMons(hm, cnt, pm)) {
                DWORD mn = 0, cu = 0, mx = 0;
                if (pGetBrightness && pGetBrightness(pm[0].hPhysicalMonitor, &mn, &cu, &mx) && mx > mn) {
                    if (pm[0].szPhysicalMonitorDescription[0])
                        lstrcpynW(m->name, pm[0].szPhysicalMonitorDescription, 48);
                    m->hpm  = pm[0].hPhysicalMonitor;
                    m->bmin = mn;
                    m->bmax = mx;
                    m->pct  = (int)((cu - mn) * 100 / (mx - mn));
                    m->ok   = TRUE;
                } else if (pDestroyPhysMon) {
                    pDestroyPhysMon(pm[0].hPhysicalMonitor);
                }
                for (k = 1; k < cnt; k++)
                    if (pDestroyPhysMon) pDestroyPhysMon(pm[k].hPhysicalMonitor);
            }
        }
    }
    if (!m->name[0]) wsprintfW(m->name, w_monfmt, g_nmon + 1);
    g_nmon++;
    return TRUE;
}

static void RefreshMonitors(void)
{
    int i;
    if (!pGetNumPhysMon) return;
    EnterCriticalSection(&g_cs);
    for (i = 0; i < MAXMON; i++) {
        InterlockedExchange(&g_target[i], 0);
        g_written[i] = 0;          /* 句柄换了，旧的校验基准作废 */
        g_retry[i] = 0;
    }
    for (i = 0; i < g_nmon; i++)
        /* 同样不能以 hpm != 0 判断：句柄可能是 0。用 ok 标志 */
        if (g_mon[i].ok && pDestroyPhysMon) pDestroyPhysMon(g_mon[i].hpm);
    g_nmon = 0;
    EnumDisplayMonitors(NULL, NULL, MonEnumProc, 0);
    g_monTick = GetTickCount();
    LeaveCriticalSection(&g_cs);
}

/* 打开面板时不必每次都重新枚举：枚举会销毁并重建全部 DDC 句柄，
 * 代价高，还会和后台正在进行的写入抢同一块硬件状态。
 * 短时间内直接用缓存，静置超过 2 秒才重新枚举。 */
static void RefreshMonitorsIfStale(void)
{
    if (!g_monTick || GetTickCount() - g_monTick > 2000) RefreshMonitors();
}

/* ---------------------------------------------------------------------------
 * 托盘图标：程序化绘制一个太阳
 * -------------------------------------------------------------------------*/
static HICON MakeSunIcon(int sz)
{
    if (sz <= 16)
        return CreateIconFromResourceEx((unsigned char *)k_icon16, sizeof(k_icon16),
                                        TRUE, 0x00030000, 16, 16, 0);
    return CreateIconFromResourceEx((unsigned char *)k_icon32, sizeof(k_icon32),
                                    TRUE, 0x00030000, 32, 32, 0);
}

/* ---------------------------------------------------------------------------
 * OSD：滚轮调节时闪现的百分比提示
 * -------------------------------------------------------------------------*/
static void OsdPaint(HWND hw, HDC dc)
{
    RECT r, tr;
    GetClientRect(hw, &r);
    FillRect(dc, &r, g_brOsd);
    SetBkMode(dc, TRANSPARENT);
    /* 有显示器不认写入时，数字改成警示色，不用打开面板也能看出来 */
    SetTextColor(dc, g_osdWarn ? RGB(255, 120, 110) : RGB(245, 245, 245));
    tr = r;
    SelectObject(dc, g_fOsd);
    DrawTextW(dc, g_osdText, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static LRESULT CALLBACK OsdProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hw, &ps);
        OsdPaint(hw, dc);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static void ShowOsd(int pct)
{
    RECT wa;
    int i;
    if (!g_osd) return;
    g_osdWarn = FALSE;
    for (i = 0; i < g_nmon; i++)
        if (g_mon[i].ok && g_mon[i].stalled) g_osdWarn = TRUE;
    wsprintfW(g_osdText, w_osdfmt, pct);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(g_osd, HWND_TOPMOST,
                 (wa.left + wa.right) / 2 - 84, wa.top + 48, 168, 64,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_osd, NULL, FALSE);
    SetTimer(g_main, TIMER_OSD, g_shotMode ? 8000 : 900, NULL);
}

/* ---------------------------------------------------------------------------
 * 弹窗绘制
 * -------------------------------------------------------------------------*/
static void DrawSlider(HDC dc, int cx, BOOL ok, int pct)
{
    int top = TRACK_TOP + 8, bot = TRACK_BOT - 8, ky;
    HGDIOBJ ob, op;

    ky = top + (100 - pct) * (bot - top) / 100;

    ob = SelectObject(dc, ok ? g_brTrack : g_brTrackD);
    op = SelectObject(dc, g_penNull);
    RoundRect(dc, cx - 3, top, cx + 4, bot + 1, 7, 7);

    if (ok) {
        SelectObject(dc, g_brFill);
        RoundRect(dc, cx - 3, ky, cx + 4, bot + 1, 7, 7);
    }

    SelectObject(dc, g_brKnob);
    SelectObject(dc, ok ? g_penKnob : g_penKnobD);
    Ellipse(dc, cx - 8, ky - 8, cx + 9, ky + 9);

    SelectObject(dc, ob);
    SelectObject(dc, op);
}

static void PopupPaint(HWND hw, HDC dc)
{
    RECT rc;
    int i;
    WCHAR buf[32];

    GetClientRect(hw, &rc);
    FillRect(dc, &rc, g_brBg);
    SetBkMode(dc, TRANSPARENT);

    for (i = 0; i < g_nmon; i++) {
        int left = MARGIN + i * g_colw;
        int cx = left + g_colw / 2;
        RECT tr;

        if (i > 0) {
            RECT dv;
            dv.left = left - 1; dv.right = left;
            dv.top = 8; dv.bottom = rc.bottom - 8;
            FillRect(dc, &dv, g_brDivider);
        }

        tr.left = left + 6; tr.right = left + g_colw - 6;

        /* 顶部标签：单显示器不显示"亮度"字样，整行省掉，面板更紧凑；
         * 多显示器时才需要显示器名区分各列。 */
        if (g_nameh > 0) {
            tr.top = MARGIN; tr.bottom = MARGIN + g_nameh;
            SelectObject(dc, g_fSmall);
            /* 写入被硬件忽略时改显红字"无响应"，免得以为程序没反应。 */
            if (g_mon[i].stalled) {
                SetTextColor(dc, RGB(190, 50, 50));
                DrawTextW(dc, w_noresp, -1, &tr,
                          DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            } else {
                SetTextColor(dc, CLR_NAME);
                DrawTextW(dc, g_mon[i].name, -1, &tr,
                          DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            }
        }

        tr.top = MARGIN + g_nameh + 4;
        tr.bottom = tr.top + PCTH;
        /* 单显示器已无标签行，把"无响应"警示落到百分比这一行；
         * 用小字号，大号百分比字体放不下中文。 */
        if (g_nameh == 0 && g_mon[i].stalled) {
            SelectObject(dc, g_fSmall);
            SetTextColor(dc, RGB(190, 50, 50));
            lstrcpynW(buf, w_noresp, 32);
        } else {
            SelectObject(dc, g_fPct);
            SetTextColor(dc, g_mon[i].ok ? CLR_PCT : CLR_PCT_D);
            if (g_mon[i].ok) wsprintfW(buf, L"%d%%", g_mon[i].pct);
            else lstrcpynW(buf, w_unsupported, 32);
        }
        DrawTextW(dc, buf, -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        DrawSlider(dc, cx, g_mon[i].ok, g_mon[i].pct);
    }

    SelectObject(dc, g_penBorder);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, 0, 0, rc.right, rc.bottom, 14, 14);
}

static void SetPctFromY(int idx, int y)
{
    int top = TRACK_TOP + 8, bot = TRACK_BOT - 8;
    int p = (bot - y) * 100 / (bot - top);
    p = Clamp(p, 0, 100);
    if (p == g_mon[idx].pct) return;    /* 百分比没变就不重绘，省掉大量无谓刷新 */
    ApplyPct(idx, p);
    InvalidateRect(g_popup, NULL, FALSE);
}

/* ---------------------------------------------------------------------------
 * 弹窗消息
 * -------------------------------------------------------------------------*/
static void WheelAll(int step)
{
    int i, sum = 0, cnt = 0;
    for (i = 0; i < g_nmon; i++) {
        if (!g_mon[i].ok) continue;
        ApplyPct(i, g_mon[i].pct + step);
        sum += g_mon[i].pct;
        cnt++;
    }
    if (cnt) ShowOsd(sum / cnt);
    if (g_popup) InvalidateRect(g_popup, NULL, FALSE);
}

/* 取托盘图标的屏幕矩形。图标被 Win11 收进隐藏区且浮层没打开时拿不到，
 * 这种情况直接当作"没命中"，绝不会误调亮度。 */
static BOOL TrayRect(RECT *out)
{
    NOTIFYICONIDENTIFIER id;
    RECT r;
    DWORD now = GetTickCount();

    if (g_trayRectOk && now - g_trayRectTick < 1200) { *out = g_trayRect; return TRUE; }

    ZeroMemory(&id, sizeof(id));
    id.cbSize = sizeof(id);
    id.hWnd = g_main;
    id.uID = 1;
    if (Shell_NotifyIconGetRect(&id, &r) == 0 && r.right > r.left && r.bottom > r.top) {
        g_trayRect = r;
        g_trayRectOk = TRUE;
        g_trayRectTick = now;
        *out = r;
        return TRUE;
    }
    g_trayRectOk = FALSE;
    g_trayRectTick = now;
    return FALSE;
}

/* 常驻低级鼠标钩子，管两件事：
 *   1) 光标停在托盘图标上滚滚轮 → 直接调亮度，不用点开面板
 *   2) 面板显示时点到面板外任何位置 → 收起面板
 *      （比 WM_ACTIVATE 可靠：托盘弹出窗口不一定拿得到前台激活权，
 *       那种情况下永远收不到 WA_INACTIVE）
 * 其余情况一律旁观放行。 */
static LRESULT CALLBACK MouseHook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION) {
        MSLLHOOKSTRUCT *ms = (MSLLHOOKSTRUCT *)lp;

        if (wp == WM_MOUSEWHEEL && !g_popup) {
            RECT tr;
            if (TrayRect(&tr) &&
                ms->pt.x >= tr.left - 4 && ms->pt.x <= tr.right + 4 &&
                ms->pt.y >= tr.top - 4  && ms->pt.y <= tr.bottom + 4) {
                int d = (int)(short)HIWORD(ms->mouseData);
                int steps;
                g_trayWheelAcc += d;
                steps = g_trayWheelAcc / WHEEL_DELTA;
                g_trayWheelAcc -= steps * WHEEL_DELTA;
                if (steps) WheelAll(steps * WHEEL_STEP);
                return 1;              /* 吞掉，别让任务栏跟着滚 */
            }
        }

        if (wp == WM_MOUSEWHEEL && g_popup) {
            RECT pr;
            if (GetWindowRect(g_popup, &pr) &&
                ms->pt.x >= pr.left && ms->pt.x < pr.right &&
                ms->pt.y >= pr.top  && ms->pt.y < pr.bottom) {
                int d = (int)(short)HIWORD(ms->mouseData);
                int steps;
                g_wheelAcc += d;
                steps = g_wheelAcc / WHEEL_DELTA;
                g_wheelAcc -= steps * WHEEL_DELTA;
                if (steps) WheelAll(steps * WHEEL_STEP);
                return 1;
            }
        }

        if (g_popup &&
            (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN)) {
            RECT r;
            if (GetWindowRect(g_popup, &r) &&
                (ms->pt.x < r.left || ms->pt.x >= r.right ||
                 ms->pt.y < r.top  || ms->pt.y >= r.bottom))
                PostMessageW(g_popup, WM_CLOSE, 0, 0);
        }
    }
    return CallNextHookEx(g_mouseHook, code, wp, lp);
}

static BOOL EnsureHook(void)
{
    if (!g_mouseHook)
        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, g_hInst, 0);
    return g_mouseHook != NULL;
}

static void LogW(const WCHAR *s);   /* 定义在文件下方，此处先声明 */

/* ---------------------------------------------------------------------------
 * 面板不抢焦点后的键盘处理
 * 面板带 WS_EX_NOACTIVATE，永远拿不到键盘焦点，收不到 WM_KEYDOWN。
 * 用两条独立通道，互为备份：
 *   1) 面板显示期间挂低级键盘钩子（正常情况走这条）
 *   2) 同时把 Esc / Up / Down 注册成全局热键（前台是提权窗口时，
 *      钩子可能收不到按键，但热键仍能送达）
 * 不会重复执行：钩子把按键吞掉后热键就不会触发；再加 100ms 时间去重兜底。
 * ------------------------------------------------------------------------- */
static void KeyAction(int vk)
{
    DWORD now;
    if (!g_popup) return;
    now = GetTickCount();
    if (now - g_keyTick < 100) return;
    g_keyTick = now;
    if (vk == VK_ESCAPE)      PostMessageW(g_popup, WM_CLOSE, 0, 0);
    else if (vk == VK_UP)     WheelAll(WHEEL_STEP);
    else if (vk == VK_DOWN)   WheelAll(-WHEEL_STEP);
}

static LRESULT CALLBACK KbHookProc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && g_popup) {
        KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)lp;
        if (k->vkCode == VK_ESCAPE || k->vkCode == VK_UP || k->vkCode == VK_DOWN) {
            if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)
                KeyAction((int)k->vkCode);
            return 1;   /* 按下和抬起都吞掉，别让焦点窗口收到半截按键 */
        }
    }
    return CallNextHookEx(g_kbHook, code, wp, lp);
}

static void EnsureKbHook(void)
{
    if (!g_kbHook)
        g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, KbHookProc, g_hInst, 0);
    if (!g_kbHook) LogW(L"kbhook: install FAILED");
    /* 热键 ID 固定为 1/2/3，对应 Esc / Up / Down */
    if (g_main) {
        RegisterHotKey(g_main, 1, 0, VK_ESCAPE);
        RegisterHotKey(g_main, 2, 0, VK_UP);
        RegisterHotKey(g_main, 3, 0, VK_DOWN);
    }
}

static void DropKbHook(void)
{
    if (g_main) {
        UnregisterHotKey(g_main, 1);
        UnregisterHotKey(g_main, 2);
        UnregisterHotKey(g_main, 3);
    }
    if (g_kbHook) {
        UnhookWindowsHookEx(g_kbHook);
        g_kbHook = NULL;
    }
}

static LRESULT CALLBACK PopupProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT rc;
        HDC dc = BeginPaint(hw, &ps);
        GetClientRect(hw, &rc);
        if (!g_bufDc) {
            g_bufDc = CreateCompatibleDC(dc);
            g_bufBmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            SelectObject(g_bufDc, g_bufBmp);
        }
        PopupPaint(hw, g_bufDc);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, g_bufDc, 0, 0, SRCCOPY);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN: {
        int x = (int)(short)LOWORD(lp), y = (int)(short)HIWORD(lp);
        int idx = (x - MARGIN) / g_colw;
        if (x >= MARGIN && idx >= 0 && idx < g_nmon && y >= TRACK_TOP && y <= TRACK_BOT) {
            if (g_mon[idx].ok) {
                g_dragIdx = idx;
                SetCapture(hw);
                SetPctFromY(idx, y);
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_dragIdx >= 0) {
            SetPctFromY(g_dragIdx, (int)(short)HIWORD(lp));
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (g_dragIdx >= 0) {
            g_dragIdx = -1;
            ReleaseCapture();
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if ((HWND)lp != hw) g_dragIdx = -1;
        break;
    case WM_MOUSEWHEEL: {
        int d = (int)(short)HIWORD(wp);
        int steps;
        g_wheelAcc += d;
        steps = g_wheelAcc / WHEEL_DELTA;
        g_wheelAcc -= steps * WHEEL_DELTA;
        if (steps) WheelAll(steps * WHEEL_STEP);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { PostMessageW(hw, WM_CLOSE, 0, 0); return 0; }
        if (wp == VK_UP)   { WheelAll(WHEEL_STEP); return 0; }
        if (wp == VK_DOWN) { WheelAll(-WHEEL_STEP); return 0; }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) DestroyWindow(hw);
        return 0;
    case WM_DESTROY:
        /* 注意：不要在这里卸钩子。钩子是常驻的（托盘滚轮要用），
         * 面板关闭时卸掉会让滚轮调节失效 */
        DropKbHook();
        g_popup = NULL;
        g_dragIdx = -1;
        g_lastClose = GetTickCount();
        if (g_bufDc) {
            if (g_bufBmp) DeleteObject(g_bufBmp);
            DeleteDC(g_bufDc);
            g_bufDc = NULL;
            g_bufBmp = NULL;
        }
        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static void OpenPopup(POINT pt)
{
    int n, w, h, i;
    HMONITOR hm;
    MONITORINFO mi;

    if (g_popup) { DestroyWindow(g_popup); return; }
    if (GetTickCount() - g_lastClose < 220) return;

    RefreshMonitorsIfStale();
    if (g_nmon <= 0) return;

    n = g_nmon;
    g_colw = (n == 1) ? COLW1 : COLW;
    g_nameh = (n == 1) ? 0 : NAMEH;  /* 单显示器省掉顶部标签行 */
    w = MARGIN * 2 + g_colw * n;
    h = POPUP_H;

    g_popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, CLS_POPUP, L"",
                              WS_POPUP, 0, 0, w, h, NULL, NULL, g_hInst, NULL);
    if (!g_popup) return;

    SetWindowRgn(g_popup, CreateRoundRectRgn(0, 0, w + 1, h + 1, 14, 14), FALSE);

    hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hm, &mi)) {
        RECT wa = mi.rcWork;
        int x = pt.x - w / 2;
        int y = pt.y - h - 12;
        x = Clamp(x, wa.left + 4, wa.right - w - 4);
        if (y < wa.top + 4) y = pt.y + 12;
        if (y + h > wa.bottom - 4) y = wa.bottom - h - 4;
        SetWindowPos(g_popup, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    }
    ShowWindow(g_popup, SW_SHOWNOACTIVATE);
    EnsureKbHook();
    EnsureHook();
    (void)i;
}

/* ---------------------------------------------------------------------------
 * 开机自启动 (HKCU\...\Run)
 * -------------------------------------------------------------------------*/
static BOOL AutostartQuery(void)
{
    HKEY k;
    LONG r;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return FALSE;
    r = RegQueryValueExW(k, APPNAME, NULL, NULL, NULL, NULL);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

static void AutostartSet(BOOL on)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    if (on) {
        WCHAR path[MAX_PATH], q[MAX_PATH + 4];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        wsprintfW(q, L"\"%s\"", path);
        RegSetValueExW(k, APPNAME, 0, REG_SZ, (const BYTE *)q,
                       (DWORD)((lstrlenW(q) + 1) * sizeof(WCHAR)));
    } else {
        RegDeleteValueW(k, APPNAME);
    }
    RegCloseKey(k);
}

/* ---------------------------------------------------------------------------
 * 托盘 / 主窗口
 * -------------------------------------------------------------------------*/
static void AddTrayIcon(HWND hw)
{
    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hw;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = g_iconBig ? g_iconBig : g_iconSmall;
    lstrcpynW(nid.szTip, w_tip, 128);
    nid.u.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_ADD, &nid);
    /* 记下版本协商结果：v4 与旧协议的报文布局完全相反，解析时必须区分 */
    g_trayV4 = Shell_NotifyIconW(NIM_SETVERSION, &nid) ? TRUE : FALSE;
}

static void RemoveTrayIcon(HWND hw)
{
    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hw;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

static void ShowTrayMenu(HWND hw, POINT pt)
{
    HMENU menu = CreatePopupMenu();
    int cmd;
    if (!menu) return;
    SetForegroundWindow(hw);
    AppendMenuW(menu, MF_STRING | (AutostartQuery() ? MF_CHECKED : 0), ID_AUTOSTART, w_autostart);
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_EXIT, w_exit);
    cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                           pt.x, pt.y, hw, NULL);
    PostMessageW(hw, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (cmd == ID_EXIT) {
        DestroyWindow(hw);
    } else if (cmd == ID_AUTOSTART) {
        AutostartSet(!AutostartQuery());
    }
}

static LRESULT CALLBACK MainProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_msgTaskbarCreated && msg == g_msgTaskbarCreated) {
        AddTrayIcon(hw);
        return 0;
    }
    switch (msg) {
    case WM_HOTKEY:
        if (wp == 1)      KeyAction(VK_ESCAPE);
        else if (wp == 2) KeyAction(VK_UP);
        else if (wp == 3) KeyAction(VK_DOWN);
        return 0;
    case WM_TRAY: {
        UINT ev;
        POINT pt;
        if (g_trayV4) {
            /* NOTIFYICON_VERSION_4 布局：
             *   wp = MAKELONG(x, y)
             *   lp = MAKELONG(鼠标/通知消息, 图标ID)      */
            ev = LOWORD(lp);
            pt.x = (int)(short)LOWORD(wp);
            pt.y = (int)(short)HIWORD(wp);
        } else {
            /* 旧协议布局：wp = 图标ID, lp = MAKELONG(x, y) */
            ev = LOWORD(lp);
            pt.x = (int)(short)LOWORD(lp);
            pt.y = (int)(short)HIWORD(lp);
        }
        if (ev == WM_LBUTTONUP || ev == NIN_SELECT || ev == NIN_KEYSELECT) {
            /* v4 下同一次单击可能先送 WM_LBUTTONUP 再送 NIN_SELECT，
             * 不去重会导致"刚打开又被自己关掉"，表现为点了没反应 */
            DWORD now = GetTickCount();
            if (now - g_lastTrayClick > 260) {
                g_lastTrayClick = now;
                OpenPopup(pt);
            }
        } else if (ev == WM_CONTEXTMENU || ev == WM_RBUTTONUP) {
            ShowTrayMenu(hw, pt);
        }
        return 0;
    }
    case WM_SETPCT: {
        int i;
        for (i = 0; i < g_nmon; i++)
            if (g_mon[i].ok) ApplyPct(i, (int)wp);
        if (g_popup) InvalidateRect(g_popup, NULL, FALSE);
        return 0;
    }
    case WM_SHOWME: {
        /* 用户又双击了一次 exe：把面板亮出来。已经开着就不要来回开关 */
        POINT c;
        GetCursorPos(&c);
        if (g_popup) {
            SetWindowPos(g_popup, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            return 0;
        }
        g_lastClose = 0;
        g_lastTrayClick = GetTickCount();
        OpenPopup(c);
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_OSD) {
            KillTimer(hw, TIMER_OSD);
            if (g_osd) ShowWindow(g_osd, SW_HIDE);
            SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
        }
        return 0;
    case WM_DESTROY:
        if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = NULL; }
        RemoveTrayIcon(hw);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

/* ---------------------------------------------------------------------------
 * 自检模式：BrightTray.exe test
 * -------------------------------------------------------------------------*/
static void LogW(const WCHAR *s)
{
    DWORD n;
    char u[1100];
    int k;
    if (!g_log) return;
    k = WideCharToMultiByte(CP_UTF8, 0, s, -1, u, 1000, NULL, NULL);
    if (k > 1 && k < 1000) {
        u[k - 1] = '\r';
        u[k] = '\n';
        WriteFile(g_log, u, k + 1, &n, NULL);
    }
}

static void TestMode(void)
{
    WCHAR buf[600];
    int i;

    g_log = CreateFileW(L"BrightTray_test.log", GENERIC_WRITE, 0, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    LoadDxva2();
    LogW(hDxva2 ? L"dxva2.dll: loaded" : L"dxva2.dll: NOT FOUND");
    if (!hDxva2) goto done;
    wsprintfW(buf, L"exports: GetNumPhys=%d GetPhys=%d Destroy=%d GetBright=%d SetBright=%d",
              pGetNumPhysMon != 0, pGetPhysMons != 0, pDestroyPhysMon != 0,
              pGetBrightness != 0, pSetBrightness != 0);
    LogW(buf);

    RefreshMonitors();
    wsprintfW(buf, L"monitors: %d", g_nmon);
    LogW(buf);
    for (i = 0; i < g_nmon; i++) {
        wsprintfW(buf, L"  [%d] %s | %s | pct=%d%% min=%u max=%u",
                  i + 1, g_mon[i].name, g_mon[i].ok ? L"OK" : L"unsupported",
                  g_mon[i].pct, g_mon[i].bmin, g_mon[i].bmax);
        LogW(buf);
    }

    for (i = 0; i < g_nmon; i++) {
        if (g_mon[i].ok) {
            int p0 = g_mon[i].pct, p1 = Clamp(p0 + 10, 0, 100);
            DWORD mn = 0, cu = 0, mx = 0;
            BOOL wr;
            /* test 模式没有后台线程，WriteHw 就是同步写，返回值能直接反映
             * 驱动是否接受了这条 DDC 命令 */
            wr = WriteHw(i, p1);
            wsprintfW(buf, L"write test [%d]: set %d%% ret=%d", i + 1, p1, wr);
            LogW(buf);
            Sleep(400);
            if (pGetBrightness(g_mon[i].hpm, &mn, &cu, &mx) && mx > mn) {
                int back = (int)((cu - mn) * 100 / (mx - mn));
                wsprintfW(buf, L"write test [%d]: set %d%% -> readback %d%%", i + 1, p1, back);
                LogW(buf);
            } else {
                LogW(L"write test: SetMonitorBrightness called but readback failed");
            }
            WriteHw(i, p0);
            wsprintfW(buf, L"restored [%d] to %d%%", i + 1, p0);
            LogW(buf);
            /* 多屏时逐台测，台与台之间留点间隔，别把 DDC 命令挤在一起 */
            if (i + 1 < g_nmon) Sleep(300);
        }
    }
done:
    RefreshMonitors();
    if (g_log) { CloseHandle(g_log); g_log = NULL; }
}

/* ---------------------------------------------------------------------------
 * 崩溃日志（便于排查异常）
 * -------------------------------------------------------------------------*/
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS *ep)
{
    HANDLE f = CreateFileW(L"BrightTray_crash.txt", GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        char b[256];
        DWORD n;
        wsprintfA(b, "code=0x%08X addr=%p flags=0x%08X\r\n",
                  ep->ExceptionRecord->ExceptionCode,
                  ep->ExceptionRecord->ExceptionAddress,
                  ep->ExceptionRecord->ExceptionFlags);
        WriteFile(f, b, lstrlenA(b), &n, NULL);
        CloseHandle(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ---------------------------------------------------------------------------
 * 启动
 * -------------------------------------------------------------------------*/
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    WNDCLASSW wc;
    MSG msg;
    HANDLE mutex;
    BOOL wantTest = FALSE, wantShot = FALSE;

    (void)hPrev; (void)show;
    g_hInst = hInst;
    ChooseLang();
    InitStrings();
    InitializeCriticalSection(&g_cs);

    if (cmd) {
        const char *p = cmd;
        while (*p) {
            if ((p[0] == 't' || p[0] == 'T') && lstrcmpiA(p, "test") == 0) { wantTest = TRUE; break; }
            if ((p[0] == 's' || p[0] == 'S') && lstrcmpiA(p, "shot") == 0) { wantShot = g_shotMode = TRUE; break; }
            p++;
        }
    }

    {
        typedef BOOL (WINAPI *PFN)(void);
        PFN p = (PFN)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDPIAware");
        if (p) p();
    }

    if (wantTest) { TestMode(); return 0; }

    /* 命令行: BrightTray.exe set <0-100>  直接设置所有显示器亮度 */
    if (cmd && (cmd[0] == 's' || cmd[0] == 'S') && (cmd[1] == 'e' || cmd[1] == 'E')
             && (cmd[2] == 't' || cmd[2] == 'T') && (cmd[3] == ' ' || cmd[3] == 0)) {
        const char *c = cmd + 3;
        int pct = 0, i;
        HWND hRun;
        while (*c == ' ') c++;
        while (*c >= '0' && *c <= '9') pct = pct * 10 + (*c++ - '0');
        pct = Clamp(pct, 0, 100);

        /* 已经有常驻实例时，把命令交给它执行。
         * 两个进程同时持有 DDC 句柄互相写，谁都可能被对方覆盖，
         * 最终亮度取决于谁后写 —— 让唯一的那个实例写，结果才是确定的。 */
        hRun = FindWindowW(CLS_MAIN, NULL);
        if (hRun) {
            PostMessageW(hRun, WM_SETPCT, (WPARAM)pct, 0);
            Sleep(600);
            return 0;
        }

        LoadDxva2();
        RefreshMonitors();
        for (i = 0; i < g_nmon; i++) {
            int attempt;
            if (!g_mon[i].ok) continue;
            /* 单次命令可以慢一点：写完回读确认，没生效就重试，
             * 这样脚本/快捷方式调用不会出现"执行了但屏幕没变" */
            for (attempt = 0; attempt < 3; attempt++) {
                DWORD mn = 0, cu = 0, mx = 0;
                int back;
                WriteHw(i, pct);
                Sleep(180);
                if (!pGetBrightness || !pGetBrightness(g_mon[i].hpm, &mn, &cu, &mx) || mx <= mn)
                    break;
                back = (int)((cu - mn) * 100 / (mx - mn));
                if (back >= pct - 2 && back <= pct + 2) break;
            }
        }
        RefreshMonitors();
        return 0;
    }

    mutex = CreateMutexW(NULL, TRUE, L"Local\\BrightTray.Lock");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        /* 已有实例在跑：不弹说明框，直接把它的面板调出来（也是图标被系统
         * 收进隐藏区时最可靠的入口） */
        HWND old = FindWindowW(CLS_MAIN, NULL);
        if (old) PostMessageW(old, WM_SHOWME, 0, 0);
        return 0;
    }

    SetUnhandledExceptionFilter(CrashFilter);
    /* 64KB 栈足够，省内存；建线程失败也无所谓，ApplyPct 会退回同步写 */
    g_hEvt = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (g_hEvt) CreateThread(NULL, 64 * 1024, BrightWorker, NULL, 0, NULL);
    LoadDxva2();
    /* 不加载 comctl32：本程序没有任何公共控件（全部 GDI 自绘），也没有
     * InitCommonControlsEx。实测省下 1 个模块 / 16KB 工作集 / 112KB 私有提交 */

    g_brBg      = CreateSolidBrush(CLR_BG);
    g_brBorder  = CreateSolidBrush(CLR_BORDER);
    g_brTrack   = CreateSolidBrush(CLR_TRACK);
    g_brTrackD  = CreateSolidBrush(CLR_TRACK_D);
    g_brFill    = CreateSolidBrush(CLR_FILL);
    g_brKnob    = CreateSolidBrush(RGB(255, 255, 255));
    g_brOsd     = CreateSolidBrush(RGB(32, 32, 36));
    g_brDivider = CreateSolidBrush(CLR_DIVIDER);
    g_penBorder = CreatePen(PS_SOLID, 1, CLR_BORDER);
    g_penKnob   = CreatePen(PS_SOLID, 2, CLR_FILL);
    g_penKnobD  = CreatePen(PS_SOLID, 2, RGB(200, 200, 200));
    g_penNull   = CreatePen(PS_NULL, 0, RGB(0, 0, 0));

    g_fSmall = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fPct = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fOsd = CreateFontW(-26, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    /* 图标必须在注册窗口类之前创建：否则 wc.hIcon 取到的是 NULL，
     * 窗口类注册成了无图标（主窗口隐藏，平时看不出来，但确实是错的） */
    g_iconBig = MakeSunIcon(32);
    g_iconSmall = MakeSunIcon(16);

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = MainProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = CLS_MAIN;
    wc.hIcon = g_iconBig;
    /* 注：WNDCLASSW 没有 hIconSm 字段（那是 WNDCLASSEXW 的），
     * 而主窗口是隐藏的、面板无标题栏，系统本就不会去取小图标，
     * 所以 16px 那枚只作为托盘图标的回退，见 AddTrayIcon */
    RegisterClassW(&wc);

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = PopupProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = CLS_POPUP;
    wc.style = CS_DROPSHADOW;
    wc.hbrBackground = NULL;
    RegisterClassW(&wc);

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = OsdProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = CLS_OSD;
    wc.hbrBackground = NULL;
    RegisterClassW(&wc);

    g_main = CreateWindowExW(0, CLS_MAIN, APPNAME, WS_OVERLAPPED,
                             0, 0, 0, 0, NULL, NULL, g_hInst, NULL);
    if (!g_main) return 1;

    g_osd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
                            CLS_OSD, L"", WS_POPUP,
                            0, 0, 168, 64, NULL, NULL, g_hInst, NULL);
    if (g_osd) {
        SetLayeredWindowAttributes(g_osd, 0, 225, LWA_ALPHA);
        SetWindowRgn(g_osd, CreateRoundRectRgn(0, 0, 169, 65, 16, 16), FALSE);
    }

    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon(g_main);
    RefreshMonitors();
    EnsureHook();               /* 常驻钩子：托盘滚轮调亮度 + 点外面关面板 */
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);

    if (wantShot) {
        RECT wa;
        POINT pt;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        pt.x = (wa.left + wa.right) / 2;
        pt.y = wa.bottom - 30;
        OpenPopup(pt);
        ShowOsd(72);
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
