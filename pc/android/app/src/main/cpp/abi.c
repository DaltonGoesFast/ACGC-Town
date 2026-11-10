/* Reports the ABI this .so was built for. arm64 is allowed: a 64-bit-only phone cannot load armeabi-v7a.
 * The dedicated server's Save_t match is an ILP32 fact. This file does not pretend an arm64 Save_t matches it. */
#include <jni.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

_Static_assert(_Alignof(double) == 8, "double must stay 8-byte aligned");

JNIEXPORT jstring JNICALL Java_com_acgc_town_Abi_report(JNIEnv* env, jclass cls) {
    char buf[192];
    const char* abi = sizeof(void*) == 8 ? "arm64-v8a" : "armeabi-v7a";
    (void)cls;
    snprintf(buf, sizeof(buf), "%s pointer=%u long=%u double_align=%u",
             abi, (unsigned)sizeof(void*), (unsigned)sizeof(long), (unsigned)_Alignof(double));
    return (*env)->NewStringUTF(env, buf);
}
