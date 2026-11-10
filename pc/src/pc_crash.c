#include "pc_crash.h"

#include <stdio.h>
#include <string.h>

#ifdef __ANDROID__
#define _GNU_SOURCE
#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <unwind.h>

#include <SDL.h>

#define PC_DIAG_MAX 48
#define PC_DIAG_RING 50
#define PC_DIAG_LINE 180

static char s_ring[PC_DIAG_RING][PC_DIAG_LINE];
static int s_ring_pos = 0;
static int s_ring_count = 0;

static void pc_diag_remember(const char* msg) {
    char* dst;
    size_t n;
    if (msg == NULL) return;
    dst = s_ring[s_ring_pos];
    n = 0;
    while (msg[n] != '\0' && n + 1 < PC_DIAG_LINE) {
        dst[n] = msg[n];
        n++;
    }
    dst[n] = '\0';
    s_ring_pos = (s_ring_pos + 1) % PC_DIAG_RING;
    if (s_ring_count < PC_DIAG_RING) s_ring_count++;
}

static int s_session_fd = -1;
static char s_crash_path[768];
static char s_session_path[768];
static int s_signals_ready = 0;

static void wr_fd(int fd, const char* s) {
    size_t n = 0;
    if (fd < 0 || s == NULL) return;
    while (s[n] != '\0') n++;
    if (n > 0) {
        ssize_t w = write(fd, s, n);
        (void)w;
    }
}

static void wr_hex(int fd, uintptr_t v) {
    static const char hex[] = "0123456789abcdef";
    char buf[18];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 16; i++) {
        buf[2 + i] = hex[(v >> ((15 - i) * 4)) & 0xF];
    }
    if (write(fd, buf, 18) < 0) {
        /* the crash file is best-effort */
    }
}

struct pc_diag_bt {
    uintptr_t pc[PC_DIAG_MAX];
    int n;
};

static _Unwind_Reason_Code pc_diag_unwind(struct _Unwind_Context* ctx, void* arg) {
    struct pc_diag_bt* bt = (struct pc_diag_bt*)arg;
    if (bt->n >= PC_DIAG_MAX) return _URC_END_OF_STACK;
    bt->pc[bt->n++] = (uintptr_t)_Unwind_GetIP(ctx);
    return _URC_NO_REASON;
}

static void pc_diag_write_crash(int fd, int sig, siginfo_t* info) {
    struct pc_diag_bt bt;
    const char* name = "SIG?";
    int i;
    if (sig == SIGSEGV) name = "SIGSEGV";
    else if (sig == SIGBUS) name = "SIGBUS";
    else if (sig == SIGABRT) name = "SIGABRT";
    else if (sig == SIGILL) name = "SIGILL";
    wr_fd(fd, "\nCRASH signal ");
    wr_fd(fd, name);
    wr_fd(fd, "\nfault_addr ");
    wr_hex(fd, info != NULL ? (uintptr_t)info->si_addr : 0);
    wr_fd(fd, "\nbacktrace (library+offset from its load base):\n");
    bt.n = 0;
    _Unwind_Backtrace(pc_diag_unwind, &bt);
    for (i = 0; i < bt.n; i++) {
        Dl_info inf;
        memset(&inf, 0, sizeof(inf));
        wr_fd(fd, "  ");
        if (dladdr((void*)bt.pc[i], &inf) && inf.dli_fbase != NULL) {
            const char* base = inf.dli_fname != NULL ? inf.dli_fname : "?";
            const char* slash = base;
            const char* p;
            for (p = base; *p; p++) if (*p == '/') slash = p + 1;
            wr_fd(fd, slash);
            wr_fd(fd, "+");
            wr_hex(fd, bt.pc[i] - (uintptr_t)inf.dli_fbase);
            if (inf.dli_sname != NULL) {
                wr_fd(fd, " ");
                wr_fd(fd, inf.dli_sname);
            }
        } else {
            wr_hex(fd, bt.pc[i]);
        }
        wr_fd(fd, "\n");
    }
    if (s_ring_count > 0) {
        int start = (s_ring_pos - s_ring_count + PC_DIAG_RING) % PC_DIAG_RING;
        int k;
        wr_fd(fd, "recent ACGC lines:\n");
        for (k = 0; k < s_ring_count; k++) {
            wr_fd(fd, "  ");
            wr_fd(fd, s_ring[(start + k) % PC_DIAG_RING]);
            wr_fd(fd, "\n");
        }
    }
}

static int read_file_line(const char* path, char* dst, size_t dst_n) {
    int fd;
    int n;
    int i;
    if (dst_n < 2) return 0;
    fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    n = (int)read(fd, dst, dst_n - 1);
    close(fd);
    if (n <= 0) return 0;
    dst[n] = '\0';
    for (i = 0; i < n; i++) {
        if (dst[i] == '\n' || dst[i] == '\r') {
            dst[i] = '\0';
            break;
        }
    }
    return dst[0] != '\0';
}

/* Java writes files/acgc-logdir.txt before System.loadLibrary, so a constructor crash still has a path. */
static void pc_diag_paths_from_logdir(void) {
    char cmd[256];
    char file[512];
    char dir[512];
    const char* roots[2];
    int fd;
    int n;
    int r;
    if (s_crash_path[0] != '\0') return;
    fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd < 0) return;
    n = (int)read(fd, cmd, sizeof(cmd) - 1);
    close(fd);
    if (n <= 0) return;
    cmd[n] = '\0';
    roots[0] = "/data/user/0/";
    roots[1] = "/data/data/";
    for (r = 0; r < 2; r++) {
        snprintf(file, sizeof(file), "%s%s/files/acgc-logdir.txt", roots[r], cmd);
        if (!read_file_line(file, dir, sizeof(dir))) continue;
        snprintf(s_crash_path, sizeof(s_crash_path), "%s/acgc-crash.log", dir);
        snprintf(s_session_path, sizeof(s_session_path), "%s/acgc-session.log", dir);
        return;
    }
}

static void pc_diag_on_signal(int sig, siginfo_t* info, void* uctx) {
    int crash_fd;
    (void)uctx;
    __android_log_print(ANDROID_LOG_FATAL, "ACGC", "fatal signal %d fault %p", sig,
                        info != NULL ? info->si_addr : NULL);
    if (s_crash_path[0] == '\0') pc_diag_paths_from_logdir();
    pc_diag_write_crash(s_session_fd, sig, info);
    crash_fd = open(s_crash_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (crash_fd >= 0) {
        int copy_fd = s_session_fd >= 0 ? dup(s_session_fd) : -1;
        wr_fd(crash_fd, "ACGC Town crash log\n");
        wr_fd(crash_fd, "Share this file from the next launch (Share log).\npath ");
        wr_fd(crash_fd, s_crash_path);
        wr_fd(crash_fd, "\n");
        pc_diag_write_crash(crash_fd, sig, info);
        if (copy_fd >= 0) {
            char buf[1024];
            off_t end = lseek(copy_fd, 0, SEEK_END);
            off_t start = end > 65536 ? end - 65536 : 0;
            wr_fd(crash_fd, "\nSession log:\npath ");
            wr_fd(crash_fd, s_session_path);
            wr_fd(crash_fd, "\n");
            lseek(copy_fd, start, SEEK_SET);
            for (;;) {
                ssize_t n = read(copy_fd, buf, sizeof(buf));
                if (n <= 0) break;
                if (write(crash_fd, buf, (size_t)n) < 0) break;
            }
            close(copy_fd);
        }
        fsync(crash_fd);
        close(crash_fd);
    }
    if (s_session_fd >= 0) fsync(s_session_fd);
    _exit(128 + sig);
}

static void pc_diag_open_logs(void) {
    const char* base;
    if (s_session_fd >= 0) return;
    pc_diag_paths_from_logdir();
    if (s_crash_path[0] == '\0') {
        base = SDL_AndroidGetExternalStoragePath();
        if (base == NULL || base[0] == '\0') base = SDL_AndroidGetInternalStoragePath();
        if (base == NULL || base[0] == '\0') base = ".";
        snprintf(s_crash_path, sizeof(s_crash_path), "%s/acgc-crash.log", base);
        snprintf(s_session_path, sizeof(s_session_path), "%s/acgc-session.log", base);
    }
    s_session_fd = open(s_session_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (s_session_fd >= 0) {
        dup2(s_session_fd, STDOUT_FILENO);
        dup2(s_session_fd, STDERR_FILENO);
    }
}

static void pc_diag_sdl_log(void* userdata, int category, SDL_LogPriority pri, const char* message) {
    (void)userdata;
    (void)category;
    (void)pri;
    if (message == NULL) return;
    __android_log_print(ANDROID_LOG_INFO, "ACGC", "%s", message);
    if (s_session_fd >= 0) {
        wr_fd(s_session_fd, message);
        wr_fd(s_session_fd, "\n");
    }
}

static void pc_diag_install_signals(void) {
    struct sigaction sa;
    if (s_signals_ready) return;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = pc_diag_on_signal;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    s_signals_ready = 1;
}

__attribute__((constructor(50))) static void pc_diag_ctor(void) {
    Dl_info inf;
    pc_diag_paths_from_logdir();
    pc_diag_install_signals();
    memset(&inf, 0, sizeof(inf));
    if (dladdr((void*)pc_diag_ctor, &inf) && inf.dli_fbase != NULL &&
        (uintptr_t)inf.dli_fbase >= 0x100000000ull) {
        __android_log_print(ANDROID_LOG_ERROR, "ACGC", "libmain loaded above 4GB at %p", inf.dli_fbase);
        abort();
    }
    __android_log_print(ANDROID_LOG_INFO, "ACGC", "diag ctor base=%p crash=%s",
                        inf.dli_fbase, s_crash_path[0] ? s_crash_path : "(no path yet)");
}

void pc_diag_init(void) {
    pc_diag_install_signals();
    pc_diag_open_logs();
    SDL_LogSetOutputFunction(pc_diag_sdl_log, NULL);
    pc_diag_milestone("diag init");
}

void pc_diag_milestone(const char* msg) {
    if (msg == NULL) return;
    pc_diag_remember(msg);
    __android_log_print(ANDROID_LOG_INFO, "ACGC", "%s", msg);
    fprintf(stderr, "[PC] %s\n", msg);
    if (s_session_fd >= 0) {
        wr_fd(s_session_fd, "[PC] ");
        wr_fd(s_session_fd, msg);
        wr_fd(s_session_fd, "\n");
        fsync(s_session_fd);
    }
}

void pc_diag_fail_box(const char* title, const char* msg) {
    pc_diag_milestone(msg != NULL ? msg : "startup failed");
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0 && SDL_Init(SDL_INIT_VIDEO) != 0) {
        return;
    }
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title != NULL ? title : "ACGC Town",
                             msg != NULL ? msg : "startup failed", NULL);
}

#else

void pc_diag_init(void) {}
void pc_diag_milestone(const char* msg) { (void)msg; }
void pc_diag_fail_box(const char* title, const char* msg) {
    (void)title;
    if (msg != NULL) fprintf(stderr, "[PC] %s\n", msg);
}

#endif
