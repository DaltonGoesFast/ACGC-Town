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

/* Android: the game thread records where it is. A side thread logs that once a
 * second and, if no frame finishes for 5s, asks the game thread to unwind. */
void pc_hb_set_step(const char* step);
void pc_hb_set_scene(void* exec, int doing);
void pc_hb_note_dvd(unsigned off, unsigned len);
void pc_hb_note_gx(int w, int h);
void pc_hb_frame_done(unsigned frame, unsigned retrace);
void pc_hb_boot_test_arm(void);
int pc_hb_boot_should_stop(void);
void pc_hb_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
