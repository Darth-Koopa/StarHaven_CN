#ifndef _PM3DS_AUDIO_H_
#define _PM3DS_AUDIO_H_

#include <stdbool.h>

/* M5.0: 3DS NDSP audio service lifecycle. See
 * port/3ds/source/n64_shim/audio_dsp.c. */

/* Returns 0 on success, -1 if NDSP failed to start (e.g. dspfirm.cdc
 * missing). Audio is non-essential; the port continues either way. */
int pm3ds_audio_init(void);

void pm3ds_audio_close(void);

/* Per-frame tick. No-op until M5.1 wires MSEQ → NDSP voice fills. */
void pm3ds_audio_step(void);

bool pm3ds_audio_is_up(void);

/* M5.1a: one-shot 880 Hz 1-second test tone on NDSP channel 0.
 * Queues a mono PCM16 sine wave and returns immediately; playback
 * is asynchronous. No-op if NDSP isn't up. Hidden behind the
 * PM3DS_AUDIO_TEST_TONE preprocessor flag in the boot path. */
void pm3ds_audio_test_tone(void);

#endif
