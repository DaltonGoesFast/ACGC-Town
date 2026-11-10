/* Compiled only for the Android ABI this project ships. A mismatch fails the NDK build. */
#include <jni.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

_Static_assert(sizeof(void*) == 4, "armeabi-v7a: pointers must be 4 bytes to match the dedicated server");
_Static_assert(sizeof(long) == 4, "armeabi-v7a: long must be 4 bytes to match the dedicated server");
_Static_assert(_Alignof(double) == 8, "double must be 8-byte aligned, as on the server's -malign-double build");

JNIEXPORT jstring JNICALL Java_com_acgc_town_Abi_report(JNIEnv* env, jclass cls) {
    char buf[160];
    (void)cls;
    snprintf(buf, sizeof(buf), "armeabi-v7a pointer=%u long=%u double_align=%u",
             (unsigned)sizeof(void*), (unsigned)sizeof(long), (unsigned)_Alignof(double));
    return (*env)->NewStringUTF(env, buf);
}
