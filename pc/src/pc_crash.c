#include "pc_crash.h"

#include <stdio.h>
#include <string.h>

#ifdef __ANDROID__
#define _GNU_SOURCE
#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
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

static pthread_t s_game_thread;
static int s_game_thread_set = 0;
static pthread_mutex_t s_hb_mu = PTHREAD_MUTEX_INITIALIZER;
static char s_step[160] = "startup";
static char s_dvd[64] = "-";
static char s_gx[48] = "-";
static char s_view[48] = "-";
static char s_pad[96] = "-";
static void* s_scene = NULL;
static int s_doing = -1;
static unsigned s_frames = 0;
static unsigned s_retrace = 0;
static uint64_t s_last_frame_ms = 0;
static int s_boot_test = 0;
static uint64_t s_boot_test_t0 = 0;
static int s_boot_stop = 0;

static uint64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static void pc_diag_on_stall(int sig, siginfo_t* info, void* uctx) {
    int fd;
    (void)sig;
    (void)info;
    (void)uctx;
    __android_log_print(ANDROID_LOG_ERROR, "ACGC", "watchdog: game thread stalled, unwinding");
    if (s_crash_path[0] == '\0') pc_diag_paths_from_logdir();
    fd = open(s_crash_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        wr_fd(fd, "\nSTALL game thread did not finish a frame for 5s\n");
    }
    pc_diag_write_crash(fd, 0, NULL);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
    if (s_session_fd >= 0) {
        wr_fd(s_session_fd, "\nSTALL game thread did not finish a frame for 5s\n");
        fsync(s_session_fd);
    }
}

static void* pc_hb_thread(void* arg) {
    unsigned seen = 0;
    int dumped = 0;
    (void)arg;
    for (;;) {
        char step[160], dvd[64], gx[48], view[48], pad[96], line[560], sym[80];
        void* scene;
        int doing;
        unsigned frames, retrace;
        uint64_t last;
        Dl_info inf;
        sleep(1);
        pthread_mutex_lock(&s_hb_mu);
        memcpy(step, s_step, sizeof(step));
        memcpy(dvd, s_dvd, sizeof(dvd));
        memcpy(gx, s_gx, sizeof(gx));
        memcpy(view, s_view, sizeof(view));
        memcpy(pad, s_pad, sizeof(pad));
        scene = s_scene;
        doing = s_doing;
        frames = s_frames;
        retrace = s_retrace;
        last = s_last_frame_ms;
        pthread_mutex_unlock(&s_hb_mu);
        sym[0] = '?';
        sym[1] = '\0';
        memset(&inf, 0, sizeof(inf));
        if (scene != NULL && dladdr(scene, &inf) && inf.dli_sname != NULL) {
            snprintf(sym, sizeof(sym), "%s", inf.dli_sname);
        }
        snprintf(line, sizeof(line),
                 "hb frame=%u scene=%s doing=%d step=%s retrace=%u dvd=%s gx=%s view=%s pad=%s idle=%llums",
                 frames, sym, doing, step, retrace, dvd, gx, view, pad,
                 (unsigned long long)(last == 0 ? 0 : mono_ms() - last));
        pc_diag_milestone(line);
        if (s_game_thread_set && last != 0 && mono_ms() - last >= 5000ull && frames == seen && !dumped) {
            pthread_t target;
            int live;
            pthread_mutex_lock(&s_hb_mu);
            live = s_game_thread_set;
            target = s_game_thread;
            pthread_mutex_unlock(&s_hb_mu);
            if (live) {
                dumped = 1;
                if (pthread_kill(target, SIGUSR1) != 0) dumped = 0;
            }
        }
        if (frames != seen) {
            dumped = 0;
            seen = frames;
        }
    }
    return NULL;
}

void pc_hb_set_step(const char* step) {
    if (step == NULL) return;
    pthread_mutex_lock(&s_hb_mu);
    snprintf(s_step, sizeof(s_step), "%s", step);
    pthread_mutex_unlock(&s_hb_mu);
}

void pc_hb_set_scene(void* exec, int doing) {
    pthread_mutex_lock(&s_hb_mu);
    s_scene = exec;
    s_doing = doing;
    pthread_mutex_unlock(&s_hb_mu);
}

void pc_hb_note_dvd(unsigned off, unsigned len) {
    pthread_mutex_lock(&s_hb_mu);
    snprintf(s_dvd, sizeof(s_dvd), "off=%u len=%u", off, len);
    pthread_mutex_unlock(&s_hb_mu);
}

void pc_hb_note_gx(int w, int h) {
    pthread_mutex_lock(&s_hb_mu);
    snprintf(s_gx, sizeof(s_gx), "%dx%d", w, h);
    pthread_mutex_unlock(&s_hb_mu);
}

void pc_hb_note_view(int x, int y, int w, int h) {
    pthread_mutex_lock(&s_hb_mu);
    snprintf(s_view, sizeof(s_view), "%d,%d %dx%d", x, y, w, h);
    pthread_mutex_unlock(&s_hb_mu);
}

void pc_hb_note_input(unsigned buttons, int stick_x, int stick_y, int fingers, int hud) {
    pthread_mutex_lock(&s_hb_mu);
    snprintf(s_pad, sizeof(s_pad), "btn=%04x stick=%d,%d touch=%d hud=%d",
             buttons & 0xffffu, stick_x, stick_y, fingers, hud);
    pthread_mutex_unlock(&s_hb_mu);
}

void pc_hb_frame_done(unsigned frame, unsigned retrace) {
    uint64_t now = mono_ms();
    pthread_mutex_lock(&s_hb_mu);
    s_frames = frame;
    s_retrace = retrace;
    s_last_frame_ms = now;
    if (s_boot_test && s_boot_test_t0 == 0) s_boot_test_t0 = now;
    if (s_boot_test && s_boot_test_t0 != 0 && now - s_boot_test_t0 >= 30000ull) s_boot_stop = 1;
    pthread_mutex_unlock(&s_hb_mu);
}

void pc_hb_boot_test_arm(void) {
    pthread_mutex_lock(&s_hb_mu);
    s_boot_test = 1;
    s_boot_test_t0 = 0;
    s_boot_stop = 0;
    pthread_mutex_unlock(&s_hb_mu);
}

int pc_hb_boot_should_stop(void) {
    int stop;
    pthread_mutex_lock(&s_hb_mu);
    stop = s_boot_stop;
    pthread_mutex_unlock(&s_hb_mu);
    return stop;
}

void pc_hb_shutdown(void) {
    pthread_mutex_lock(&s_hb_mu);
    s_game_thread_set = 0;
    pthread_mutex_unlock(&s_hb_mu);
}

static void pc_hb_start(void) {
    pthread_t thr;
    s_game_thread = pthread_self();
    s_game_thread_set = 1;
    s_last_frame_ms = mono_ms();
    if (pthread_create(&thr, NULL, pc_hb_thread, NULL) == 0) pthread_detach(thr);
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
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = pc_diag_on_stall;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGUSR1, &sa, NULL);
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
    pc_hb_start();
    pc_diag_milestone("diag init");
}

void pc_diag_milestone(const char* msg) {
    if (msg == NULL) return;
    if (strncmp(msg, "hb ", 3) != 0) pc_hb_set_step(msg);
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
void pc_hb_set_step(const char* step) { (void)step; }
void pc_hb_set_scene(void* exec, int doing) { (void)exec; (void)doing; }
void pc_hb_note_dvd(unsigned off, unsigned len) { (void)off; (void)len; }
void pc_hb_note_gx(int w, int h) { (void)w; (void)h; }
void pc_hb_note_view(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void pc_hb_note_input(unsigned buttons, int stick_x, int stick_y, int fingers, int hud) {
    (void)buttons; (void)stick_x; (void)stick_y; (void)fingers; (void)hud;
}
void pc_hb_frame_done(unsigned frame, unsigned retrace) { (void)frame; (void)retrace; }
void pc_hb_boot_test_arm(void) {}
int pc_hb_boot_should_stop(void) { return 0; }
void pc_hb_shutdown(void) {}
void pc_diag_fail_box(const char* title, const char* msg) {
    (void)title;
    if (msg != NULL) fprintf(stderr, "[PC] %s\n", msg);
}

#endif
