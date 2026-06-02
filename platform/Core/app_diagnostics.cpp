#include "Core/app_diagnostics.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <dbghelp.h>
#  ifdef _MSC_VER
#    include <crtdbg.h>
#  endif
#  pragma comment(lib, "dbghelp.lib")
#else
#  include <unistd.h>
#endif

static int  g_crashing = 0;
static char g_last_save_error[1024] = "";

static void log_timestamp(char *out, size_t outsz)
{
    time_t now;
    struct tm tmv;
    if (!out || outsz == 0) return;
    snprintf(out, outsz, "unknown-time");
    now = time(NULL);
#ifdef _WIN32
    if (localtime_s(&tmv, &now) == 0)
#else
    if (localtime_r(&now, &tmv) != NULL)
#endif
        strftime(out, outsz, "%Y-%m-%d %H:%M:%S", &tmv);
}

void bdd_diag_write(const char *msg)
{
    FILE *f = fopen("crash.log", "a");
    if (f) { fprintf(f, "%s", msg); fclose(f); }
    fprintf(stderr, "%s", msg);
}

static void crash_printf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    bdd_diag_write(buf);
}

static void crash_event(const char *msg)
{
    char ts[32];
    log_timestamp(ts, sizeof ts);
    crash_printf("[%s] %s\n", ts, msg ? msg : "");
}

static void crash_log_argv(int argc, char **argv)
{
    bdd_diag_write("argv:");
    for (int i = 0; i < argc; i++)
        crash_printf(" \"%s\"", argv[i] ? argv[i] : "");
    bdd_diag_write("\n");
}

static void crash_log_session_context(int argc, char **argv)
{
    char buf[1024];
    crash_event("=== bddview started ===");
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)sizeof buf);
    if (n > 0 && n < sizeof buf) crash_printf("exe: %s\n", buf);
    n = GetCurrentDirectoryA((DWORD)sizeof buf, buf);
    if (n > 0 && n < sizeof buf) crash_printf("cwd: %s\n", buf);
    crash_printf("command_line: %s\n", GetCommandLineA());
#else
    if (getcwd(buf, sizeof buf)) crash_printf("cwd: %s\n", buf);
#endif
    crash_log_argv(argc, argv);
#ifdef NDEBUG
    bdd_diag_write("build: Release\n");
#else
    bdd_diag_write("build: Debug\n");
#endif
}

static void crash_log_normal_exit(void)
{
    if (!g_crashing)
        crash_event("=== bddview exited: normal process exit ===");
}

const char *bdd_last_save_error(void)
{
    return g_last_save_error;
}

void bdd_clear_last_save_error(void)
{
    g_last_save_error[0] = '\0';
}

void bdd_save_logf(const char *fmt, ...)
{
    FILE *f;
    char ts[32] = "unknown-time";
    char body[900];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    snprintf(g_last_save_error, sizeof g_last_save_error, "%s", body);

    f = fopen("save_errors.log", "a");
    if (!f) return;

    log_timestamp(ts, sizeof ts);

    fprintf(f, "[%s] ", ts);
    fprintf(f, "%s", body);
    fprintf(f, "\n");
    fclose(f);
}

static void sig_handler(int sig)
{
    g_crashing = 1;
    crash_event("=== bddview CRASH ===");
    if (sig == SIGSEGV) bdd_diag_write("Signal: SIGSEGV (segmentation fault)\n");
    else if (sig == SIGABRT) bdd_diag_write("Signal: SIGABRT (abort/assert)\n");
    else crash_printf("Signal: %d\n", sig);
    bdd_diag_write("Please report this crash with crash.log");
#ifdef _WIN32
    bdd_diag_write(" and crashdump.dmp if one was written");
#endif
    bdd_diag_write("\n");
    fflush(stderr);
#ifdef _WIN32
    ExitProcess(3);
#else
    _exit(1);
#endif
}

#ifdef _WIN32
static LONG WINAPI crash_handler(EXCEPTION_POINTERS *ep)
{
    g_crashing = 1;
    crash_event("=== bddview CRASH ===");
    crash_printf("Exception code: 0x%08lX\n", ep->ExceptionRecord->ExceptionCode);
    crash_printf("Exception addr: 0x%p\n", ep->ExceptionRecord->ExceptionAddress);
    HANDLE h = CreateFileA("crashdump.dmp", GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION ei;
        ei.ThreadId = GetCurrentThreadId();
        ei.ExceptionPointers = ep;
        ei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                          h, MiniDumpNormal, &ei, NULL, NULL);
        CloseHandle(h);
        bdd_diag_write("Minidump written to crashdump.dmp\n");
    }
    bdd_diag_write("Please report this crash with crashdump.dmp and crash.log\n");
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void invalid_parameter_handler(const wchar_t *expr, const wchar_t *func,
                                      const wchar_t *file, unsigned int line,
                                      uintptr_t reserved)
{
    (void)expr; (void)func; (void)file; (void)line; (void)reserved;
    g_crashing = 1;
    crash_event("CRT invalid parameter handler fired");
}

#ifdef _MSC_VER
static int __cdecl crt_report_hook(int report_type, char *message, int *return_value)
{
    (void)return_value;
    if (report_type == _CRT_ASSERT || report_type == _CRT_ERROR) {
        g_crashing = 1;
        crash_event(report_type == _CRT_ASSERT ? "CRT assert" : "CRT runtime error");
        if (message && message[0]) bdd_diag_write(message);
    }
    return FALSE;
}
#endif
#endif

void bdd_diag_init(int argc, char **argv)
{
    crash_log_session_context(argc, argv);
    atexit(crash_log_normal_exit);
#ifdef _WIN32
    SetUnhandledExceptionFilter(crash_handler);
    signal(SIGABRT, sig_handler);
    signal(SIGSEGV, sig_handler);
    _set_invalid_parameter_handler(invalid_parameter_handler);
#ifdef _MSC_VER
    _CrtSetReportHook(crt_report_hook);
#endif
#else
    signal(SIGSEGV, sig_handler);
    signal(SIGABRT, sig_handler);
#endif
}
