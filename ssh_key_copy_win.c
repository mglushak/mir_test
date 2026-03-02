/*
 * ssh_key_copy_win.c  —  Windows GUI (Win32 API)
 *
 * КОМПІЛЯЦІЯ (cross-compile з Linux, MinGW-w64):
 *   convert ssh-copy-id.png -define icon:auto-resize="256,64,48,32,16" ssh-copy-id.ico
 *   x86_64-w64-mingw32-windres resource.rc -O coff -o resource.res
 *   x86_64-w64-mingw32-gcc ssh_key_copy_win.c resource.res \
 *       -o ssh_key_copy.exe -lws2_32 -lcomctl32 -lshell32 -mwindows -municode \
 *       -DUNICODE -D_UNICODE -O2
 */

#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <commdlg.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/*  ID елементів вікна                                                 */
/* ------------------------------------------------------------------ */
#define ID_LISTVIEW       101
#define ID_KEYFILE        102
#define ID_BTN_BROWSE     103
#define ID_BTN_SEND       104
#define ID_BTN_KEYGEN     105
#define ID_BTN_LOAD       106
#define ID_BTN_SEL_ALL    107
#define ID_BTN_SEL_NONE   108
#define ID_LOG            109
#define ID_PROGRESS       110
#define TIMER_PROG_RESET  201
#define ID_BTN_SAVE       113   /* Зберегти конфіг */
#define ID_BTN_ADD        114   /* Додати хост     */
#define ID_BTN_DEL        115   /* Видалити хост   */
/* ID елементів діалогу редагування */
#define IDE_NAME     2001
#define IDE_HOST     2002
#define IDE_USER     2003
#define IDE_PORT     2004
#define IDE_KEYFILE  2005
#define IDE_BROWSE   2006

/* Колонки ListView */
/* Чекбокс LVS_EX_CHECKBOXES вбудований у колонку 0 — окремий COL_CHECK не потрібен */
#define COL_NAME    0
#define COL_HOST    1
#define COL_USER    2
#define COL_PORT    3

/* ------------------------------------------------------------------ */
/*  Структура одного хоста                                            */
/* ------------------------------------------------------------------ */
#define MAX_HOSTS 256

typedef struct {
    wchar_t name[128];    /* Ім'я блоку (Host alias)  */
    wchar_t host[256];    /* HostName / IP             */
    wchar_t user[128];    /* User                      */
    wchar_t keyfile[MAX_PATH]; /* IdentityFile (.pub)  */
    int     port;         /* Port (default 22)         */
    BOOL    selected;     /* Чи вибраний для передачі  */
} HostEntry;

/* ------------------------------------------------------------------ */
/*  Глобальний стан                                                    */
/* ------------------------------------------------------------------ */
static HWND      g_wnd;
static HWND      g_listview;
static HWND      g_keyfile_global;   /* глобальний .pub якщо не заданий у хості */
static HWND      g_btnBrowse, g_btnSend, g_btnKeygen, g_btnLoad;
static HWND      g_btnSelAll, g_btnSelNone, g_btnSave, g_btnAdd, g_btnDel;
static HWND      g_log, g_progress;
static HFONT     g_font;
static HBRUSH    g_logBrush;
static HICON     g_icon;

static HostEntry g_hosts[MAX_HOSTS];
static wchar_t   g_loaded_path[MAX_PATH] = {0}; /* шлях до поточного файлу */
static int       g_host_count = 0;

/* ------------------------------------------------------------------ */
/*  Логування                                                          */
/* ------------------------------------------------------------------ */
static void log_w(const wchar_t *msg)
{
    int len = GetWindowTextLengthW(g_log);
    SendMessageW(g_log, EM_SETSEL, len, len);
    SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)msg);
}

static void log_f(const char *fmt, ...)
{
    char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    int n = MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc(n * sizeof(wchar_t));
    if (w) { MultiByteToWideChar(CP_UTF8, 0, buf, -1, w, n); log_w(w); free(w); }
}

/* ------------------------------------------------------------------ */
/*  Іконка з ресурсу                                                  */
/* ------------------------------------------------------------------ */
static HICON load_icon(HINSTANCE hi)
{
    /* resource.rc: 1 ICON "ssh-copy-id.ico" — числовий ID = 1 */
    HICON ic = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1),
                                 IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
    if (!ic) ic = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1),
                                 IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    if (!ic) ic = LoadIconW(NULL, IDI_APPLICATION);
    return ic;
}

/* ------------------------------------------------------------------ */
/*  Зчитати публічний ключ                                            */
/* ------------------------------------------------------------------ */
static int read_pubkey(const wchar_t *path, char *out, int outsz)
{
    HANDLE hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) return -1;
    DWORD rb = 0;
    BOOL ok = ReadFile(hf, out, outsz - 1, &rb, NULL);
    CloseHandle(hf);
    if (!ok || rb == 0) return -1;
    out[rb] = '\0';
    /* Обрізаємо переноси рядка */
    for (int i = rb - 1; i >= 0 && (out[i] == '\r' || out[i] == '\n'); i--)
        out[i] = '\0';
    return 0;
}

/* ================================================================== */
/*  ListView — оновлення                                               */
/* ================================================================== */
static void listview_populate(void)
{
    ListView_DeleteAllItems(g_listview);

    for (int i = 0; i < g_host_count; i++) {
        LVITEMW lvi = {0};
        lvi.mask    = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem   = i;
        lvi.lParam  = i;
        lvi.pszText = g_hosts[i].name[0] ? g_hosts[i].name : g_hosts[i].host;
        ListView_InsertItem(g_listview, &lvi);

        ListView_SetItemText(g_listview, i, COL_HOST, g_hosts[i].host);
        ListView_SetItemText(g_listview, i, COL_USER, g_hosts[i].user);

        wchar_t portbuf[8];
        _snwprintf_s(portbuf, 8, _TRUNCATE, L"%d", g_hosts[i].port);
        ListView_SetItemText(g_listview, i, COL_PORT, portbuf);

        /* Чекбокс */
        ListView_SetCheckState(g_listview, i, g_hosts[i].selected);
    }
}

static void listview_sync_checks(void)
{
    for (int i = 0; i < g_host_count; i++)
        g_hosts[i].selected = ListView_GetCheckState(g_listview, i);
}

/* ================================================================== */
/*  Парсер SSH config (~/.ssh/config стиль)                            */
/* ================================================================== */
static void parse_ssh_config(const wchar_t *wbuf, int append)
{
    if (!append) {
        g_host_count = 0;
        ListView_DeleteAllItems(g_listview);
    }

    wchar_t home[MAX_PATH] = {0};
    GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);

    /* Глобальний ключ з GUI */
    wchar_t global_key[MAX_PATH] = {0};
    GetWindowTextW(g_keyfile_global, global_key, MAX_PATH);

    int cur = -1; /* поточний індекс хоста */

    const wchar_t *line = wbuf;
    while (*line) {
        const wchar_t *eol = line;
        while (*eol && *eol != L'\n' && *eol != L'\r') eol++;

        /* Копіюємо рядок у буфер для модифікації */
        int len = (int)(eol - line);
        if (len >= 512) { line = eol; while (*line==L'\r'||*line==L'\n') line++; continue; }
        wchar_t row[512]; wcsncpy_s(row, 512, line, len); row[len] = L'\0';

        /* Trim left */
        wchar_t *p = row;
        while (*p == L' ' || *p == L'\t') p++;

        if (*p != L'#' && *p != L'\0') {
            /* Розбиваємо ключ / значення */
            wchar_t *sep = p;
            while (*sep && *sep != L' ' && *sep != L'\t' && *sep != L'=') sep++;
            if (*sep) {
                wchar_t key[64] = {0};
                int klen = (int)(sep - p); if (klen >= 64) klen = 63;
                wcsncpy_s(key, 64, p, klen);

                while (*sep == L' ' || *sep == L'\t' || *sep == L'=') sep++;
                wchar_t *val = sep;
                /* Trim right */
                wchar_t *ve = val + wcslen(val) - 1;
                while (ve >= val && (*ve==L' '||*ve==L'\t'||*ve==L'\r'||*ve==L'\n')) *ve--=L'\0';

                if (_wcsicmp(key, L"Host") == 0 && wcscmp(val, L"*") != 0) {
                    if (g_host_count < MAX_HOSTS) {
                        cur = g_host_count++;
                        ZeroMemory(&g_hosts[cur], sizeof(HostEntry));
                        wcscpy_s(g_hosts[cur].name,    128,      val);
                        wcscpy_s(g_hosts[cur].host,    256,      val); /* default = alias */
                        g_hosts[cur].port     = 22;
                        g_hosts[cur].selected = TRUE;
                        /* Успадковуємо глобальний ключ якщо є */
                        if (global_key[0])
                            wcscpy_s(g_hosts[cur].keyfile, MAX_PATH, global_key);
                    }
                } else if (cur >= 0) {
                    if (_wcsicmp(key, L"HostName") == 0) {
                        wcscpy_s(g_hosts[cur].host, 256, val);
                    } else if (_wcsicmp(key, L"User") == 0) {
                        wcscpy_s(g_hosts[cur].user, 128, val);
                    } else if (_wcsicmp(key, L"Port") == 0) {
                        g_hosts[cur].port = _wtoi(val);
                        if (g_hosts[cur].port <= 0 || g_hosts[cur].port > 65535)
                            g_hosts[cur].port = 22;
                    } else if (_wcsicmp(key, L"IdentityFile") == 0) {
                        wchar_t kp[MAX_PATH] = {0};
                        if (val[0] == L'~')
                            _snwprintf_s(kp, MAX_PATH, _TRUNCATE, L"%s%s", home, val+1);
                        else
                            wcscpy_s(kp, MAX_PATH, val);
                        for (wchar_t *c = kp; *c; c++) if (*c == L'/') *c = L'\\';
                        /* Додаємо .pub */
                        size_t kl = wcslen(kp);
                        if (kl < 4 || _wcsicmp(kp + kl - 4, L".pub") != 0)
                            wcscat_s(kp, MAX_PATH, L".pub");
                        wcscpy_s(g_hosts[cur].keyfile, MAX_PATH, kp);
                    }
                }
            }
        }

        line = eol;
        while (*line == L'\r' || *line == L'\n') line++;
    }
}

static void load_params_file(const wchar_t *path)
{
    HANDLE hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_wnd, L"Не вдалося відкрити файл параметрів.",
                    L"Помилка", MB_OK|MB_ICONERROR);
        return;
    }
    char raw[65536] = {0};
    DWORD rb = 0;
    ReadFile(hf, raw, sizeof(raw)-1, &rb, NULL);
    CloseHandle(hf);
    raw[rb] = '\0';

    wchar_t wbuf[65536] = {0};
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw, -1, wbuf, 65536))
        MultiByteToWideChar(CP_ACP, 0, raw, -1, wbuf, 65536);

    parse_ssh_config(wbuf, FALSE);
    listview_populate();

    if (g_host_count > 0) {
        log_f("[INFO]  Завантажено %d хост(ів) з файлу.\r\n", g_host_count);
        wcscpy_s(g_loaded_path, MAX_PATH, path);
        wchar_t title[512];
        _snwprintf_s(title, 512, _TRUNCATE,
            L"SSH Key Copy \u2014 [%s]", path);
        SetWindowTextW(g_wnd, title);
    } else {
        MessageBoxW(g_wnd,
            L"Файл не містить жодного блоку Host.\n\n"
            L"Очікуваний формат (SSH config стиль):\n"
            L"  Host server1\n"
            L"      HostName 192.168.1.10\n"
            L"      User     admin\n"
            L"      Port     22\n"
            L"      IdentityFile ~/.ssh/id_rsa\n\n"
            L"  Host server2\n"
            L"      HostName 10.0.0.5\n"
            L"      ...",
            L"Формат файлу", MB_OK|MB_ICONINFORMATION);
    }
}

/* ================================================================== */
/*  Потік: передача ключа на ОДИН хост                                */
/* ================================================================== */
typedef struct {
    wchar_t host[256];
    wchar_t user[128];
    wchar_t keyfile[MAX_PATH];
    int     port;
    int     index;      /* номер у черзі (для логу) */
    int     total;      /* всього хостів у черзі    */
} SendParams;

/* ================================================================== */
/*  Потік: передача на ВСІ вибрані хости — одне консольне вікно       */
/* ================================================================== */
typedef struct {
    int    count;
    SendParams entries[MAX_HOSTS];
} MultiSendParams;

static DWORD WINAPI thread_send_multi(LPVOID lp)
{
    MultiSendParams *mp = (MultiSendParams *)lp;
    int total = mp->count;

    log_f("[INFO]  Генеруємо скрипт для %d хост(ів)...\r\n", total);
    SendMessageW(g_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessageW(g_progress, PBM_SETPOS, 10, 0);

    /* ── Тимчасовий .bat ── */
    wchar_t tmpdir[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, tmpdir);
    wchar_t bat_path[MAX_PATH];
    _snwprintf_s(bat_path, MAX_PATH, _TRUNCATE,
        L"%sssh_copy_%lu.bat", tmpdir, GetCurrentProcessId());

    /* ── Будуємо єдиний bat для всіх хостів ── */
    HANDLE hf = CreateFileW(bat_path, GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        log_f("[ERROR] Не вдалося створити .bat скрипт.\r\n");
        goto done;
    }

    /* Макрос для запису рядка у файл */
    #define BAT_WRITE(s) WriteFile(hf,(s),(DWORD)strlen(s),&wr_,NULL)
    DWORD wr_ = 0;

    BAT_WRITE("@echo off\r\n");
    BAT_WRITE("chcp 65001 >nul\r\n");
    BAT_WRITE("echo.\r\n");
    BAT_WRITE("echo ================================================\r\n");
    BAT_WRITE("echo   SSH Key Copy\r\n");
    BAT_WRITE("echo ================================================\r\n");
    {
        char hdr[128];
        _snprintf_s(hdr, sizeof(hdr), _TRUNCATE,
            "echo   Передача ключа на %d хост(ів)\r\n", total);
        BAT_WRITE(hdr);
    }
    BAT_WRITE("echo ================================================\r\n");
    BAT_WRITE("echo.\r\n");
    BAT_WRITE("set TOTAL_OK=0\r\n");
    BAT_WRITE("set TOTAL_ERR=0\r\n\r\n");

    for (int i = 0; i < total; i++) {
        SendParams *sp = &mp->entries[i];

        /* Читаємо ключ */
        char pubkey[4096] = {0};
        char host_u8[256] = {0}, user_u8[128] = {0}, kf_u8[MAX_PATH] = {0};
        WideCharToMultiByte(CP_UTF8, 0, sp->host,    -1, host_u8, 255,        NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, sp->user,    -1, user_u8, 127,        NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, sp->keyfile, -1, kf_u8,  MAX_PATH-1,  NULL, NULL);

        if (read_pubkey(sp->keyfile, pubkey, sizeof(pubkey)) != 0) {
            log_f("[WARN]  Пропускаємо %s — не вдалося прочитати ключ\r\n", host_u8);
            continue;
        }

        /* Remote команда */
        char remote[8192] = {0};
        _snprintf_s(remote, sizeof(remote), _TRUNCATE,
            "mkdir -p ~/.ssh && chmod 700 ~/.ssh"
            " && grep -qxF '%s' ~/.ssh/authorized_keys 2>/dev/null"
            " || echo '%s' >> ~/.ssh/authorized_keys"
            " && chmod 600 ~/.ssh/authorized_keys"
            " && echo KEY_OK",
            pubkey, pubkey);

        /* Секція для одного хоста */
        char sec[16384] = {0};
        _snprintf_s(sec, sizeof(sec), _TRUNCATE,
            "echo ------------------------------------------\r\n"
            "echo  [%d/%d] %s@%s:%d\r\n"
            "echo  Ключ: %s\r\n"
            "echo  Введiть пароль SSH якщо з'явиться запит.\r\n"
            "echo.\r\n"
            "ssh.exe -o StrictHostKeyChecking=accept-new"
            " -o PasswordAuthentication=yes"
            " -o PubkeyAuthentication=no"
            " -p %d %s@%s \"%s\"\r\n"
            "if %%ERRORLEVEL%%==0 (\r\n"
            "    echo  [OK] Успiшно!\r\n"
            "    set /a TOTAL_OK+=1\r\n"
            ") else (\r\n"
            "    echo  [ПОМИЛКА] Код: %%ERRORLEVEL%%\r\n"
            "    set /a TOTAL_ERR+=1\r\n"
            ")\r\n"
            "echo.\r\n",
            i+1, total, user_u8, host_u8, sp->port, kf_u8,
            sp->port, user_u8, host_u8, remote);
        BAT_WRITE(sec);
    }

    /* Підсумок */
    BAT_WRITE("echo ================================================\r\n");
    BAT_WRITE("echo   Результат:\r\n");
    BAT_WRITE("echo   Успiшно : %TOTAL_OK%\r\n");
    BAT_WRITE("echo   Помилок : %TOTAL_ERR%\r\n");
    BAT_WRITE("echo ================================================\r\n");
    BAT_WRITE("echo.\r\n");
    BAT_WRITE("pause\r\n");
    #undef BAT_WRITE

    CloseHandle(hf);

    SendMessageW(g_progress, PBM_SETPOS, 50, 0);

    /* ── Запускаємо ОДИН bat у ОДНОМУ консольному вікні ── */
    {
        wchar_t cmd[MAX_PATH + 32];
        _snwprintf_s(cmd, sizeof(cmd)/sizeof(wchar_t), _TRUNCATE,
            L"cmd.exe /c \"%s\"", bat_path);

        STARTUPINFOW si = {0}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {0};
        if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE,
                CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
            log_f("[ERROR] Не вдалося запустити консоль: %lu\r\n", GetLastError());
            goto cleanup;
        }

        /* Чекаємо поки користувач закриє вікно (натисне pause) */
        WaitForSingleObject(pi.hProcess, 600000); /* 10 хвилин */
        DWORD ec = 0;
        GetExitCodeProcess(pi.hProcess, &ec);
        if (ec == (DWORD)STILL_ACTIVE) TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    log_f("[INFO]  Передачу завершено.\r\n");

cleanup:
    DeleteFileW(bat_path);
done:
    SendMessageW(g_progress, PBM_SETPOS, 100, 0);
    SetTimer(g_wnd, TIMER_PROG_RESET, 3000, NULL);
    EnableWindow(g_btnSend,    TRUE);
    EnableWindow(g_btnKeygen,  TRUE);
    EnableWindow(g_btnLoad,    TRUE);
    EnableWindow(g_btnSelAll,  TRUE);
    EnableWindow(g_btnSelNone, TRUE);
    free(mp);
    return 0;
}

/* ================================================================== */
/*  Потік: генерація ключа                                            */
/* ================================================================== */
static DWORD WINAPI thread_keygen(LPVOID lp)
{
    (void)lp;
    wchar_t home[MAX_PATH] = {0};
    GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
    wchar_t defkey[MAX_PATH];
    _snwprintf_s(defkey, MAX_PATH, _TRUNCATE, L"%s\\.ssh\\id_rsa", home);
    wchar_t cmd[1024];
    _snwprintf_s(cmd, 1024, _TRUNCATE, L"ssh-keygen.exe -t rsa -b 4096 -f \"%s\"", defkey);

    log_w(L"\r\n--- Генерація SSH-ключа ---\r\n");
    STARTUPINFOW si = {0}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE,
                        CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        log_f("[ERROR] Не вдалося запустити ssh-keygen (%lu)\r\n", GetLastError());
        goto done;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 1; GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    if (ec == 0) {
        wchar_t pub[MAX_PATH];
        _snwprintf_s(pub, MAX_PATH, _TRUNCATE, L"%s\\.ssh\\id_rsa.pub", home);
        SetWindowTextW(g_keyfile_global, pub);
        log_w(L"[OK] Ключ згенеровано. Шлях оновлено.\r\n");
    } else {
        log_f("[ERROR] ssh-keygen завершився з кодом %lu\r\n", ec);
    }
done:
    EnableWindow(g_btnSend,   TRUE);
    EnableWindow(g_btnKeygen, TRUE);
    return 0;
}

/* ================================================================== */
/*  Дії кнопок                                                         */
/* ================================================================== */
static void action_browse(void)
{
    OPENFILENAMEW ofn = {0};
    wchar_t path[MAX_PATH] = {0};
    GetWindowTextW(g_keyfile_global, path, MAX_PATH);
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_wnd;
    ofn.lpstrFilter = L"Публічний ключ (*.pub)\0*.pub\0Всі файли (*.*)\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"Виберіть публічний SSH-ключ";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn))
        SetWindowTextW(g_keyfile_global, path);
}

/* ================================================================== */
/*  Діалог редагування / додавання хоста                              */
/* ================================================================== */
/* Дані що передаються у діалог */
typedef struct {
    HostEntry *entry;  /* NULL = новий хост */
    BOOL       ok;     /* TRUE = підтвердили */
} EditDlgData;

static HFONT g_dlgFont = NULL;

static HWND make_label(HWND p, const wchar_t *t, int x, int y, int w)
{
    HWND h = CreateWindowExW(0, L"STATIC", t, WS_CHILD|WS_VISIBLE|SS_RIGHT,
        x, y+3, w, 20, p, NULL, NULL, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_dlgFont, TRUE);
    return h;
}
static HWND make_edit(HWND p, UINT id, int x, int y, int w, BOOL pwd)
{
    DWORD st = WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL|(pwd?ES_NUMBER:0);
    HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL, st,
        x, y, w, 24, p, (HMENU)(UINT_PTR)id, NULL, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_dlgFont, TRUE);
    return h;
}

static LRESULT CALLBACK EditDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
    static EditDlgData *dd = NULL;
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        dd = (EditDlgData *)cs->lpCreateParams;
        g_dlgFont = CreateFontW(16,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_SWISS, L"Segoe UI");

        const int LX=10, LW=120, EX=135, EW=280;
        int y=14, step=34;

        make_label(hDlg, L"Назва (Host):", LX, y, LW);
        HWND hN = make_edit(hDlg, IDE_NAME, EX, y, EW, FALSE); y+=step;
        make_label(hDlg, L"HostName / IP:", LX, y, LW);
        HWND hH = make_edit(hDlg, IDE_HOST, EX, y, EW, FALSE); y+=step;
        make_label(hDlg, L"Користувач:", LX, y, LW);
        HWND hU = make_edit(hDlg, IDE_USER, EX, y, EW, FALSE); y+=step;
        make_label(hDlg, L"Порт:", LX, y, LW);
        HWND hP = make_edit(hDlg, IDE_PORT, EX, y, 60, FALSE); y+=step;
        make_label(hDlg, L"IdentityFile:", LX, y, LW);
        HWND hK = make_edit(hDlg, IDE_KEYFILE, EX, y, EW-36, FALSE);
        HWND hBr = CreateWindowExW(0, L"BUTTON", L"...",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
            EX+EW-34, y, 34, 24, hDlg, (HMENU)IDE_BROWSE, NULL, NULL);
        SendMessageW(hBr, WM_SETFONT, (WPARAM)g_dlgFont, TRUE);
        y += step+4;

        /* Кнопки */
        HWND hOK = CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
            EX, y, 80, 28, hDlg, (HMENU)IDOK, NULL, NULL);
        SendMessageW(hOK, WM_SETFONT, (WPARAM)g_dlgFont, TRUE);
        HWND hCan = CreateWindowExW(0, L"BUTTON", L"Скасувати",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
            EX+90, y, 100, 28, hDlg, (HMENU)IDCANCEL, NULL, NULL);
        SendMessageW(hCan, WM_SETFONT, (WPARAM)g_dlgFont, TRUE);

        /* Заповнюємо поля якщо редагування */
        if (dd && dd->entry) {
            SetWindowTextW(hN, dd->entry->name);
            SetWindowTextW(hH, dd->entry->host);
            SetWindowTextW(hU, dd->entry->user);
            wchar_t pbuf[8]; _snwprintf_s(pbuf,8,_TRUNCATE,L"%d",dd->entry->port);
            SetWindowTextW(hP, pbuf);
            SetWindowTextW(hK, dd->entry->keyfile);
        } else {
            SetWindowTextW(hP, L"22");
        }
        (void)hN; (void)hH; (void)hU; (void)hK;
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == IDE_BROWSE) {
            OPENFILENAMEW ofn={0}; wchar_t path[MAX_PATH]={0};
            HWND hK = GetDlgItem(hDlg, IDE_KEYFILE);
            GetWindowTextW(hK, path, MAX_PATH);
            ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=hDlg;
            ofn.lpstrFilter=L"Публічний ключ (*.pub)\0*.pub\0Всі файли (*.*)\0*.*\0";
            ofn.lpstrFile=path; ofn.nMaxFile=MAX_PATH;
            ofn.lpstrTitle=L"Виберіть .pub ключ";
            ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) SetWindowTextW(hK, path);
        }
        if (id == IDOK) {
            if (!dd) { DestroyWindow(hDlg); return 0; }
            /* Зчитуємо значення */
            HostEntry tmp = {0};
            GetWindowTextW(GetDlgItem(hDlg,IDE_NAME),    tmp.name,    128);
            GetWindowTextW(GetDlgItem(hDlg,IDE_HOST),    tmp.host,    256);
            GetWindowTextW(GetDlgItem(hDlg,IDE_USER),    tmp.user,    128);
            GetWindowTextW(GetDlgItem(hDlg,IDE_KEYFILE), tmp.keyfile, MAX_PATH);
            wchar_t pb[8]={0};
            GetWindowTextW(GetDlgItem(hDlg,IDE_PORT), pb, 8);
            tmp.port = pb[0] ? _wtoi(pb) : 22;
            if (tmp.port<=0||tmp.port>65535) tmp.port=22;
            tmp.selected = TRUE;
            if (!tmp.host[0]) {
                MessageBoxW(hDlg, L"Вкажіть HostName / IP.", L"Увага",
                            MB_OK|MB_ICONWARNING);
                return 0;
            }
            /* Якщо назва порожня — використовуємо хост */
            if (!tmp.name[0]) wcscpy_s(tmp.name,128,tmp.host);
            *dd->entry = tmp;
            dd->ok = TRUE;
            DestroyWindow(hDlg);
        }
        if (id == IDCANCEL) DestroyWindow(hDlg);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(hDlg); return 0; }
        break;
    case WM_DESTROY:
        if (g_dlgFont) { DeleteObject(g_dlgFont); g_dlgFont=NULL; }
        /* НЕ викликаємо PostQuitMessage — це знищить головний цикл! */
        return 0;
    }
    return DefWindowProcW(hDlg, msg, wp, lp);
}

/* Показати модальний діалог редагування.
   entry != NULL — редагування, entry == NULL — новий хост (entry надається ззовні) */
static BOOL show_edit_dialog(HWND parent, HostEntry *entry)
{
    /* Реєструємо клас одноразово */
    static BOOL reg = FALSE;
    if (!reg) {
        WNDCLASSEXW wc={0};
        wc.cbSize=sizeof(wc); wc.lpfnWndProc=EditDlgProc;
        wc.hInstance=(HINSTANCE)GetWindowLongPtrW(parent,GWLP_HINSTANCE);
        wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
        wc.hCursor=LoadCursorW(NULL,IDC_ARROW);
        wc.lpszClassName=L"SSHEditDlg";
        RegisterClassExW(&wc);
        reg=TRUE;
    }

    EditDlgData dd = { entry, FALSE };

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,
        L"SSHEditDlg",
        entry ? L"Редагувати хост" : L"Додати хост",
        WS_POPUP|WS_CAPTION|WS_SYSMENU,
        0,0,440,240, parent, NULL,
        (HINSTANCE)GetWindowLongPtrW(parent,GWLP_HINSTANCE), &dd);

    /* По центру батьківського вікна */
    RECT pr, dr;
    GetWindowRect(parent, &pr);
    GetWindowRect(hDlg,   &dr);
    int dw = dr.right-dr.left, dh = dr.bottom-dr.top;
    SetWindowPos(hDlg, HWND_TOPMOST,
        pr.left+(pr.right-pr.left-dw)/2,
        pr.top +(pr.bottom-pr.top-dh)/2,
        0,0, SWP_NOSIZE);

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    /* Модальний цикл — не використовуємо GetMessageW бо PostQuitMessage
       його зламає. Крутимо вручну поки вікно існує. */
    MSG msg;
    while (IsWindow(hDlg)) {
        BOOL got = GetMessageW(&msg, NULL, 0, 0);
        if (got == 0 || got == -1) break;   /* WM_QUIT або помилка */
        /* Якщо прийшов WM_QUIT — повертаємо його назад у чергу
           щоб головний цикл теж його отримав */
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return dd.ok;
}

/* ================================================================== */
/*  Збереження конфігурації у SSH config файл                          */
/* ================================================================== */
static void action_save(void)
{
    /* Якщо файл вже був завантажений — пропонуємо той самий шлях */
    wchar_t path[MAX_PATH] = {0};
    wcscpy_s(path, MAX_PATH, g_loaded_path);

    OPENFILENAMEW ofn = {0};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = g_wnd;
    ofn.lpstrFilter  =
        L"SSH config файли (*.conf;*.cfg)\0*.conf;*.cfg\0"
        L"Всі файли (*.*)\0*.*\0";
    ofn.lpstrFile    = path;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrTitle   = L"Зберегти SSH config файл";
    ofn.lpstrDefExt  = L"conf";
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    /* Формуємо вміст файлу */
    char buf[65536] = {0};
    int  pos = 0;
    pos += _snprintf_s(buf+pos, sizeof(buf)-pos, _TRUNCATE,
        "# SSH Key Copy — конфігурація\r\n"
        "# Формат: SSH config (~/.ssh/config)\r\n\r\n");

    for (int i = 0; i < g_host_count; i++) {
        char name_u8[128]={0}, host_u8[256]={0}, user_u8[128]={0}, key_u8[MAX_PATH]={0};
        WideCharToMultiByte(CP_UTF8,0,g_hosts[i].name,   -1,name_u8,127,NULL,NULL);
        WideCharToMultiByte(CP_UTF8,0,g_hosts[i].host,   -1,host_u8,255,NULL,NULL);
        WideCharToMultiByte(CP_UTF8,0,g_hosts[i].user,   -1,user_u8,127,NULL,NULL);
        WideCharToMultiByte(CP_UTF8,0,g_hosts[i].keyfile,-1,key_u8, MAX_PATH-1,NULL,NULL);

        /* Замінюємо \ на / у шляху до ключа */
        for (char *c=key_u8; *c; c++) if (*c=='\\') *c='/';

        pos += _snprintf_s(buf+pos, sizeof(buf)-pos, _TRUNCATE,
            "Host %s\r\n"
            "    HostName     %s\r\n"
            "    User         %s\r\n"
            "    Port         %d\r\n",
            name_u8, host_u8, user_u8, g_hosts[i].port);
        if (key_u8[0])
            pos += _snprintf_s(buf+pos, sizeof(buf)-pos, _TRUNCATE,
                "    IdentityFile %s\r\n", key_u8);
        pos += _snprintf_s(buf+pos, sizeof(buf)-pos, _TRUNCATE, "\r\n");
    }

    HANDLE hf = CreateFileW(path, GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_wnd, L"Не вдалося зберегти файл.", L"Помилка", MB_OK|MB_ICONERROR);
        return;
    }
    DWORD wr=0;
    WriteFile(hf, buf, (DWORD)strlen(buf), &wr, NULL);
    CloseHandle(hf);

    wcscpy_s(g_loaded_path, MAX_PATH, path);
    log_f("[INFO]  Конфігурацію збережено: %ls\r\n", path);
    wchar_t title[512];
    _snwprintf_s(title,512,_TRUNCATE,
        L"SSH Key Copy \u2014 [%s]", path);
    SetWindowTextW(g_wnd, title);
}

/* Додати новий хост */
static void action_add_host(void)
{
    if (g_host_count >= MAX_HOSTS) {
        MessageBoxW(g_wnd, L"Досягнуто максимальну кількість хостів.", L"Увага",
                    MB_OK|MB_ICONWARNING);
        return;
    }
    HostEntry tmp = {0};
    tmp.port = 22; tmp.selected = TRUE;
    if (show_edit_dialog(g_wnd, &tmp)) {
        g_hosts[g_host_count++] = tmp;
        listview_populate();
        log_f("[INFO]  Додано хост: %ls\r\n", tmp.host);
    }
}

/* Редагувати вибраний хост */
static void action_edit_host(int idx)
{
    if (idx < 0 || idx >= g_host_count) return;
    HostEntry tmp = g_hosts[idx];
    if (show_edit_dialog(g_wnd, &tmp)) {
        g_hosts[idx] = tmp;
        listview_populate();
        log_f("[INFO]  Оновлено хост: %ls\r\n", tmp.host);
    }
}

/* Видалити вибраний хост */
static void action_del_host(void)
{
    int idx = ListView_GetNextItem(g_listview, -1, LVNI_SELECTED);
    if (idx < 0) {
        MessageBoxW(g_wnd, L"Виберіть хост для видалення.", L"Увага",
                    MB_OK|MB_ICONWARNING);
        return;
    }
    wchar_t q[256];
    _snwprintf_s(q,256,_TRUNCATE, L"Видалити хост «%s»?", g_hosts[idx].host);
    if (MessageBoxW(g_wnd,q,L"Підтвердження",MB_YESNO|MB_ICONQUESTION)!=IDYES) return;

    /* Зсуваємо масив */
    for (int i=idx; i<g_host_count-1; i++) g_hosts[i]=g_hosts[i+1];
    g_host_count--;
    listview_populate();
    log_f("[INFO]  Хост видалено.\r\n");
}


static void action_load_file(void)
{
    OPENFILENAMEW ofn = {0};
    wchar_t path[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_wnd;
    ofn.lpstrFilter =
        L"SSH config файли (*.conf;*.cfg;*)\0*.conf;*.cfg;*\0"
        L"Всі файли (*.*)\0*.*\0";
    ofn.lpstrFile    = path;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrTitle   = L"Відкрити SSH config файл";
    ofn.lpstrDefExt  = L"conf";
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn))
        load_params_file(path);
}

static void action_send(void)
{
    listview_sync_checks();

    /* Глобальний ключ */
    wchar_t global_key[MAX_PATH] = {0};
    GetWindowTextW(g_keyfile_global, global_key, MAX_PATH);

    /* Збираємо вибрані хости */
    MultiSendParams *mp = (MultiSendParams *)calloc(1, sizeof(MultiSendParams));
    for (int i = 0; i < g_host_count; i++) {
        if (!g_hosts[i].selected) continue;

        /* Визначаємо ключ: спочатку власний хоста, потім глобальний */
        wchar_t *key = g_hosts[i].keyfile[0] ? g_hosts[i].keyfile : global_key;
        if (!key[0]) {
            log_f("[WARN]  Хост %ls: не вказано шлях до ключа — пропускаємо.\r\n",
                  g_hosts[i].host);
            continue;
        }
        if (!g_hosts[i].user[0]) {
            log_f("[WARN]  Хост %ls: не вказано користувача — пропускаємо.\r\n",
                  g_hosts[i].host);
            continue;
        }

        SendParams *sp = &mp->entries[mp->count++];
        wcscpy_s(sp->host,    256,      g_hosts[i].host);
        wcscpy_s(sp->user,    128,      g_hosts[i].user);
        wcscpy_s(sp->keyfile, MAX_PATH, key);
        sp->port = g_hosts[i].port;
    }

    if (mp->count == 0) {
        free(mp);
        MessageBoxW(g_wnd,
            L"Не вибрано жодного хоста або не вказано необхідні параметри.",
            L"Увага", MB_OK|MB_ICONWARNING);
        return;
    }

    EnableWindow(g_btnSend,    FALSE);
    EnableWindow(g_btnKeygen,  FALSE);
    EnableWindow(g_btnLoad,    FALSE);
    EnableWindow(g_btnSelAll,  FALSE);
    EnableWindow(g_btnSelNone, FALSE);

    log_w(L"\r\n--- Розпочинаємо передачу ключів ---\r\n");

    HANDLE ht = CreateThread(NULL, 0, thread_send_multi, mp, 0, NULL);
    if (ht) {
        CloseHandle(ht);
    } else {
        free(mp);
        EnableWindow(g_btnSend,    TRUE);
        EnableWindow(g_btnKeygen,  TRUE);
        EnableWindow(g_btnLoad,    TRUE);
        EnableWindow(g_btnSelAll,  TRUE);
        EnableWindow(g_btnSelNone, TRUE);
    }
}

static void action_keygen(void)
{
    EnableWindow(g_btnSend,   FALSE);
    EnableWindow(g_btnKeygen, FALSE);
    HANDLE ht = CreateThread(NULL, 0, thread_keygen, NULL, 0, NULL);
    if (ht) CloseHandle(ht);
    else { EnableWindow(g_btnSend, TRUE); EnableWindow(g_btnKeygen, TRUE); }
}

/* ================================================================== */
/*  Створення елементів вікна                                         */
/* ================================================================== */
static void create_controls(HWND hWnd, HINSTANCE hi)
{
    g_font = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_SWISS, L"Segoe UI");

    HFONT fBold = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_SWISS, L"Segoe UI");
    HFONT fSmall = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_SWISS, L"Segoe UI");

    const int M = 10;   /* margin */
    int W = 900, y = M;

    /* ---- Іконка + заголовок ---- */
    if (g_icon) {
        HWND hIco = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD|WS_VISIBLE|SS_ICON,
            M, y, 48, 48, hWnd, NULL, hi, NULL);
        SendMessageW(hIco, STM_SETICON, (WPARAM)g_icon, 0);
    }
    HWND hT = CreateWindowExW(0, L"STATIC", L"SSH Key Copy  \u2014  Windows \u2192 Unix/Linux",
        WS_CHILD|WS_VISIBLE|SS_LEFT, M+56, y+2, 600, 24, hWnd, NULL, hi, NULL);
    SendMessageW(hT, WM_SETFONT, (WPARAM)fBold, TRUE);
    HWND hS = CreateWindowExW(0, L"STATIC",
        L"Передача публічного SSH-ключа на Unix/Linux сервери",
        WS_CHILD|WS_VISIBLE|SS_LEFT, M+56, y+28, 600, 18, hWnd, NULL, hi, NULL);
    SendMessageW(hS, WM_SETFONT, (WPARAM)g_font, TRUE);
    y += 60;

    /* ---- Рядок: Шлях до ключа ---- */
    HWND hKL = CreateWindowExW(0, L"STATIC", L"Публічний ключ (.pub):",
        WS_CHILD|WS_VISIBLE|SS_RIGHT, M, y+3, 170, 22, hWnd, NULL, hi, NULL);
    SendMessageW(hKL, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_keyfile_global = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
        WS_CHILD|WS_VISIBLE|WS_TABSTOP, 185, y, W-185-M-90, 26, hWnd, NULL, hi, NULL);
    SendMessageW(g_keyfile_global, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_keyfile_global, EM_SETCUEBANNER, 1,
        (LPARAM)L"Шлях до id_rsa.pub (використовується якщо не вказано у хості)");

    wchar_t defkey[MAX_PATH] = {0};
    GetEnvironmentVariableW(L"USERPROFILE", defkey, MAX_PATH-20);
    wcsncat_s(defkey, MAX_PATH, L"\\.ssh\\id_rsa.pub", 20);
    SetWindowTextW(g_keyfile_global, defkey);

    g_btnBrowse = CreateWindowExW(0, L"BUTTON", L"\U0001f4c2 Огляд\u2026",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
        W-M-84, y, 84, 28, hWnd, (HMENU)ID_BTN_BROWSE, hi, NULL);
    SendMessageW(g_btnBrowse, WM_SETFONT, (WPARAM)g_font, TRUE);
    y += 36;

    /* ---- Підказка ---- */
    HWND hNote = CreateWindowExW(0, L"STATIC",
        L"\u26a0  Пароль вводиться у консольному вікні. "
        L"Перетягніть SSH config файл на вікно або натисніть «\U0001f4cb Завантажити файл».",
        WS_CHILD|WS_VISIBLE|SS_LEFT, 185, y+2, W-185-M, 18, hWnd, NULL, hi, NULL);
    SendMessageW(hNote, WM_SETFONT, (WPARAM)fSmall, TRUE);
    y += 26;

    /* ---- Роздільник ---- */
    CreateWindowExW(0, L"STATIC", NULL, WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
        M, y, W-M*2, 2, hWnd, NULL, hi, NULL);
    y += 8;

    /* ---- ListView хостів ---- */
    HWND hLvLabel = CreateWindowExW(0, L"STATIC",
        L"Хости (поставте \u2611 для передачі ключа):",
        WS_CHILD|WS_VISIBLE|SS_LEFT, M, y, 400, 20, hWnd, NULL, hi, NULL);
    SendMessageW(hLvLabel, WM_SETFONT, (WPARAM)g_font, TRUE);

    /* Кнопки вибору */
    g_btnSelAll = CreateWindowExW(0, L"BUTTON", L"\u2611 Всі",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
        W-M-170, y-2, 80, 24, hWnd, (HMENU)ID_BTN_SEL_ALL, hi, NULL);
    SendMessageW(g_btnSelAll, WM_SETFONT, (WPARAM)fSmall, TRUE);

    g_btnSelNone = CreateWindowExW(0, L"BUTTON", L"\u2610 Жодного",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
        W-M-84, y-2, 84, 24, hWnd, (HMENU)ID_BTN_SEL_NONE, hi, NULL);
    SendMessageW(g_btnSelNone, WM_SETFONT, (WPARAM)fSmall, TRUE);
    y += 24;

    g_listview = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL,
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|
        LVS_REPORT|LVS_SHOWSELALWAYS|LVS_SINGLESEL,
        M, y, W-M*2, 220, hWnd, (HMENU)ID_LISTVIEW, hi, NULL);
    SendMessageW(g_listview, WM_SETFONT, (WPARAM)g_font, TRUE);
    ListView_SetExtendedListViewStyle(g_listview,
        LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    /* Колонки ListView */
    LVCOLUMNW col = {0};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt  = LVCFMT_LEFT;

    col.cx = 160; col.pszText = L"Назва (Host)";     ListView_InsertColumn(g_listview, COL_NAME, &col);
    col.cx = 200; col.pszText = L"Хост / IP";         ListView_InsertColumn(g_listview, COL_HOST, &col);
    col.cx = 120; col.pszText = L"Користувач";        ListView_InsertColumn(g_listview, COL_USER, &col);
    col.cx =  60; col.pszText = L"Порт";              ListView_InsertColumn(g_listview, COL_PORT, &col);
    y += 228;

    /* ---- Горизонтальна лінія ---- */
    CreateWindowExW(0, L"STATIC", NULL, WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
        M, y, W-M*2, 2, hWnd, NULL, hi, NULL);
    y += 8;

    /* ---- Кнопки дій ---- */
    /* Рядок 1: маніпуляції зі списком */
    g_btnLoad = CreateWindowExW(0, L"BUTTON", L"\U0001f4c2 Завантажити",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
        M, y, 140, 28, hWnd, (HMENU)ID_BTN_LOAD, hi, NULL);
    SendMessageW(g_btnLoad, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_btnSave = CreateWindowExW(0, L"BUTTON", L"\U0001f4be Зберегти",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
        M+148, y, 130, 28, hWnd, (HMENU)ID_BTN_SAVE, hi, NULL);
    SendMessageW(g_btnSave, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_btnAdd = CreateWindowExW(0, L"BUTTON", L"\u2795 Додати хост",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
        M+286, y, 140, 28, hWnd, (HMENU)ID_BTN_ADD, hi, NULL);
    SendMessageW(g_btnAdd, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_btnDel = CreateWindowExW(0, L"BUTTON", L"\u2716 Видалити",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
        M+434, y, 110, 28, hWnd, (HMENU)ID_BTN_DEL, hi, NULL);
    SendMessageW(g_btnDel, WM_SETFONT, (WPARAM)g_font, TRUE);

    /* Рядок 2: генерація + передача */
    y += 36;
    g_btnKeygen = CreateWindowExW(0, L"BUTTON", L"\U0001f511 Генерувати ключ",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
        M, y, 190, 30, hWnd, (HMENU)ID_BTN_KEYGEN, hi, NULL);
    SendMessageW(g_btnKeygen, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_btnSend = CreateWindowExW(0, L"BUTTON", L"\U0001f680 Передати ключ на вибрані",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
        M+200, y, 250, 30, hWnd, (HMENU)ID_BTN_SEND, hi, NULL);
    SendMessageW(g_btnSend, WM_SETFONT, (WPARAM)g_font, TRUE);
    y += 38;

    /* ---- Прогресбар ---- */
    g_progress = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
        WS_CHILD|WS_VISIBLE|PBS_SMOOTH,
        M, y, W-M*2, 12, hWnd, (HMENU)ID_PROGRESS, hi, NULL);
    SendMessageW(g_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    y += 18;

    /* ---- Лог ---- */
    g_log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,
        M, y, W-M*2, 9999, hWnd, (HMENU)ID_LOG, hi, NULL);
    SendMessageW(g_log, WM_SETFONT, (WPARAM)g_font, TRUE);

    log_w(L"SSH Key Copy готовий.\r\n");
    log_w(L"Завантажте SSH config файл або передайте ключ перетягуванням.\r\n\r\n");
}

/* ================================================================== */
/*  WndProc                                                           */
/* ================================================================== */
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    case WM_CREATE:
        g_logBrush = CreateSolidBrush(RGB(13, 13, 23));
        create_controls(hWnd, ((LPCREATESTRUCT)lp)->hInstance);
        DragAcceptFiles(hWnd, TRUE);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BTN_BROWSE:   action_browse();    break;
        case ID_BTN_SEND:     action_send();      break;
        case ID_BTN_KEYGEN:   action_keygen();    break;
        case ID_BTN_LOAD:     action_load_file(); break;
        case ID_BTN_SAVE:     action_save();       break;
        case ID_BTN_ADD:      action_add_host();   break;
        case ID_BTN_DEL:      action_del_host();   break;
        case ID_BTN_SEL_ALL:
            for (int i = 0; i < g_host_count; i++) {
                ListView_SetCheckState(g_listview, i, TRUE);
                g_hosts[i].selected = TRUE;
            }
            break;
        case ID_BTN_SEL_NONE:
            for (int i = 0; i < g_host_count; i++) {
                ListView_SetCheckState(g_listview, i, FALSE);
                g_hosts[i].selected = FALSE;
            }
            break;
        }
        return 0;

    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->idFrom == ID_LISTVIEW) {
            if (nm->code == LVN_ITEMCHANGED) {
                NMLISTVIEW *nmlv = (NMLISTVIEW *)lp;
                if (nmlv->iItem >= 0 && nmlv->iItem < g_host_count)
                    g_hosts[nmlv->iItem].selected =
                        ListView_GetCheckState(g_listview, nmlv->iItem);
            } else if (nm->code == NM_DBLCLK) {
                /* Подвійний клік — редагувати хост */
                NMITEMACTIVATE *nma = (NMITEMACTIVATE *)lp;
                action_edit_host(nma->iItem);
            }
        }
        return 0;
    }

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wp;
        wchar_t dropped[MAX_PATH] = {0};
        DragQueryFileW(hDrop, 0, dropped, MAX_PATH);
        DragFinish(hDrop);
        load_params_file(dropped);
        return 0;
    }

    case WM_SIZE: {
        RECT rc; GetClientRect(hWnd, &rc);
        int W  = rc.right - rc.left;
        int H  = rc.bottom - rc.top;
        /* Розтягуємо лог по висоті та ширині */
        HDWP hdwp = BeginDeferWindowPos(3);
        /* ListView */
        RECT lvrc; GetWindowRect(g_listview, &lvrc);
        MapWindowPoints(NULL, hWnd, (POINT*)&lvrc, 2);
        DeferWindowPos(hdwp, g_listview, NULL,
            lvrc.left, lvrc.top, W-20, lvrc.bottom-lvrc.top, SWP_NOZORDER|SWP_NOMOVE);
        /* Лог */
        RECT lgrc; GetWindowRect(g_log, &lgrc);
        MapWindowPoints(NULL, hWnd, (POINT*)&lgrc, 2);
        DeferWindowPos(hdwp, g_log, NULL,
            lgrc.left, lgrc.top, W-20, H-lgrc.top-10, SWP_NOZORDER|SWP_NOMOVE);
        /* Прогресбар */
        RECT pgrc; GetWindowRect(g_progress, &pgrc);
        MapWindowPoints(NULL, hWnd, (POINT*)&pgrc, 2);
        DeferWindowPos(hdwp, g_progress, NULL,
            pgrc.left, pgrc.top, W-20, pgrc.bottom-pgrc.top, SWP_NOZORDER|SWP_NOMOVE);
        EndDeferWindowPos(hdwp);
        return 0;
    }

    case WM_TIMER:
        if (wp == TIMER_PROG_RESET) {
            KillTimer(hWnd, TIMER_PROG_RESET);
            SendMessageW(g_progress, PBM_SETPOS, 0, 0);
        }
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = 700;
        mm->ptMinTrackSize.y = 600;
        return 0;
    }

    case WM_CTLCOLOREDIT:
        if ((HWND)lp == g_log) {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, RGB(180, 255, 180));
            SetBkColor(hdc, RGB(13, 13, 23));
            return (LRESULT)g_logBrush;
        }
        break;

    case WM_DESTROY:
        if (g_font)     DeleteObject(g_font);
        if (g_logBrush) DeleteObject(g_logBrush);
        if (g_icon)     DestroyIcon(g_icon);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

/* ================================================================== */
/*  wWinMain                                                           */
/* ================================================================== */
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev,
                    LPWSTR lpCmd, int nShow)
{
    (void)hPrev;
    wchar_t cmdline_file[MAX_PATH] = {0};
    if (lpCmd && lpCmd[0]) {
        wchar_t *s = lpCmd;
        if (*s == L'"') { s++; wchar_t *e = wcschr(s, L'"'); if (e) *e = L'\0'; }
        wcscpy_s(cmdline_file, MAX_PATH, s);
    }

    INITCOMMONCONTROLSEX icx = {sizeof(icx),
        ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES|ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icx);

    g_icon = load_icon(hInst);

    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = L"SSHKeyCopyWin";
    wc.hIcon = wc.hIconSm = g_icon;
    if (!RegisterClassExW(&wc)) return 1;

    int W = 920, H = 640;
    g_wnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"SSHKeyCopyWin",
        L"SSH Key Copy \u2014 Windows \u2192 Unix/Linux",
        WS_OVERLAPPEDWINDOW,
        (GetSystemMetrics(SM_CXSCREEN)-W)/2,
        (GetSystemMetrics(SM_CYSCREEN)-H)/2,
        W, H, NULL, NULL, hInst, NULL);
    if (!g_wnd) return 1;

    ShowWindow(g_wnd, nShow);
    UpdateWindow(g_wnd);

    if (cmdline_file[0])
        load_params_file(cmdline_file);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(g_wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}

int wmain(void)
{
    return wWinMain(GetModuleHandleW(NULL), NULL, GetCommandLineW(), SW_SHOW);
}
