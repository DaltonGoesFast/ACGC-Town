/* pc_daily_fallback.h - Client-Guaranteed Daily Resource Fallbacks (Phase 4).
 * Ensures every player sees at least 1 private fruit tree and 1 private buried fossil
 * per calendar day, even if active peers picked the shared town clean earlier.
 */
#ifndef PC_DAILY_FALLBACK_H
#define PC_DAILY_FALLBACK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Check current town field buffers on SCENE_FG entry/sync and, if needed for today,
 * materialize up to 1 private fruit-bearing tree and 1 private buried fossil locally. */
void pc_daily_fallback_apply_if_needed(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_DAILY_FALLBACK_H */
