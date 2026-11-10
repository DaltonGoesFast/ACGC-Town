/* Place libmain.so below 4 GB before the dynamic linker applies ASLR.
 * The GameCube decomp stores host pointers in u32. Static buffers (.bss/.data)
 * only stay valid if the whole library is mapped in the low 4 GB. */
#include <android/dlext.h>
#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ACGC", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ACGC", __VA_ARGS__)

static char s_err[512];

static int elf_span(const char* path, uint64_t* span_out) {
    int fd;
    unsigned char hdr[64];
    uint64_t phoff;
    uint16_t phentsize, phnum, i;
    uint64_t min_v = ~0ull;
    uint64_t max_v = 0;
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        snprintf(s_err, sizeof(s_err), "open %s: %s", path, strerror(errno));
        return -1;
    }
    if (read(fd, hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr) || memcmp(hdr, "\x7f" "ELF", 4) != 0 || hdr[4] != 2) {
        snprintf(s_err, sizeof(s_err), "%s is not an ELF64", path);
        close(fd);
        return -1;
    }
    phoff = (uint64_t)hdr[32] | ((uint64_t)hdr[33] << 8) | ((uint64_t)hdr[34] << 16) | ((uint64_t)hdr[35] << 24) |
            ((uint64_t)hdr[36] << 32) | ((uint64_t)hdr[37] << 40) | ((uint64_t)hdr[38] << 48) | ((uint64_t)hdr[39] << 56);
    phentsize = (uint16_t)(hdr[54] | (hdr[55] << 8));
    phnum = (uint16_t)(hdr[56] | (hdr[57] << 8));
    if (phentsize < 56 || phnum == 0 || phnum > 64) {
        snprintf(s_err, sizeof(s_err), "bad program headers in %s", path);
        close(fd);
        return -1;
    }
    for (i = 0; i < phnum; i++) {
        unsigned char ph[64];
        uint32_t type;
        uint64_t vaddr, memsz;
        if (lseek(fd, (off_t)(phoff + (uint64_t)i * phentsize), SEEK_SET) < 0 ||
            read(fd, ph, 56) != 56) {
            snprintf(s_err, sizeof(s_err), "short program header in %s", path);
            close(fd);
            return -1;
        }
        type = (uint32_t)ph[0] | ((uint32_t)ph[1] << 8) | ((uint32_t)ph[2] << 16) | ((uint32_t)ph[3] << 24);
        if (type != 1) continue; /* PT_LOAD */
        vaddr = (uint64_t)ph[16] | ((uint64_t)ph[17] << 8) | ((uint64_t)ph[18] << 16) | ((uint64_t)ph[19] << 24) |
                ((uint64_t)ph[20] << 32) | ((uint64_t)ph[21] << 40) | ((uint64_t)ph[22] << 48) | ((uint64_t)ph[23] << 56);
        memsz = (uint64_t)ph[40] | ((uint64_t)ph[41] << 8) | ((uint64_t)ph[42] << 16) | ((uint64_t)ph[43] << 24) |
                ((uint64_t)ph[44] << 32) | ((uint64_t)ph[45] << 40) | ((uint64_t)ph[46] << 48) | ((uint64_t)ph[47] << 56);
        if (vaddr < min_v) min_v = vaddr;
        if (vaddr + memsz > max_v) max_v = vaddr + memsz;
    }
    close(fd);
    if (min_v == ~0ull || max_v <= min_v) {
        snprintf(s_err, sizeof(s_err), "no PT_LOAD in %s", path);
        return -1;
    }
    *span_out = max_v - min_v;
    return 0;
}

JNIEXPORT jstring JNICALL Java_com_acgc_town_LowLoad_place(JNIEnv* env, jclass cls, jstring jpath) {
    static const uintptr_t bases[] = {
        0x10000000u, 0x20000000u, 0x40000000u, 0x50000000u,
        0x60000000u, 0x70000000u, 0x80000000u, 0x90000000u, 0xA0000000u
    };
    const char* path;
    uint64_t span;
    long page;
    size_t bytes;
    unsigned i;
    void* reserved = MAP_FAILED;
    android_dlextinfo info;
    void* handle;
    void* mem;
    (void)cls;
    path = (*env)->GetStringUTFChars(env, jpath, NULL);
    if (path == NULL) return (*env)->NewStringUTF(env, "null libmain path");
    if (elf_span(path, &span) != 0) {
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return (*env)->NewStringUTF(env, s_err);
    }
    page = sysconf(_SC_PAGESIZE);
    if (page < 4096) page = 4096;
    bytes = (size_t)((span + (uint64_t)page - 1ull) & ~((uint64_t)page - 1ull));
    bytes += (size_t)page;
    for (i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
        if ((uint64_t)bases[i] + bytes >= 0x100000000ull) continue;
        reserved = mmap((void*)bases[i], bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (reserved == (void*)bases[i]) break;
        if (reserved != MAP_FAILED) munmap(reserved, bytes);
        reserved = MAP_FAILED;
    }
    if (reserved == MAP_FAILED) {
        snprintf(s_err, sizeof(s_err), "no free %zu-byte hole below 4GB for libmain (%s)", bytes, strerror(errno));
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return (*env)->NewStringUTF(env, s_err);
    }
    memset(&info, 0, sizeof(info));
    info.flags = ANDROID_DLEXT_RESERVED_ADDRESS;
    info.reserved_addr = reserved;
    info.reserved_size = bytes;
    handle = android_dlopen_ext(path, RTLD_NOW | RTLD_GLOBAL, &info);
    if (handle == NULL) {
        snprintf(s_err, sizeof(s_err), "android_dlopen_ext: %s", dlerror());
        munmap(reserved, bytes);
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return (*env)->NewStringUTF(env, s_err);
    }
    mem = dlsym(handle, "audiomemory");
    LOGI("libmain reserved %p span %zu audiomemory %p", reserved, bytes, mem);
    (*env)->ReleaseStringUTFChars(env, jpath, path);
    if (mem == NULL || (uintptr_t)mem >= 0x100000000ull) {
        snprintf(s_err, sizeof(s_err), "audiomemory landed at %p (need it below 4GB)", mem);
        return (*env)->NewStringUTF(env, s_err);
    }
    return NULL;
}
