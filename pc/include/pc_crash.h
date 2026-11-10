#ifndef PC_CRASH_H
#define PC_CRASH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Startup and crash notes. On Android these go to acgc-session.log and, after a
 * fatal signal, acgc-crash.log in the app's external files directory. */
void pc_diag_init(void);
void pc_diag_milestone(const char* msg);
void pc_diag_fail_box(const char* title, const char* msg);

#ifdef __cplusplus
}
#endif

#endif
