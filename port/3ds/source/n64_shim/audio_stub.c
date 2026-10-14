/*
 * audio_stub.c — silent audio shim.
 * Fidelity tier: STUB — game's audio thread keeps ticking, no sound is
 *                produced. MSEQ → DSP work lands in the audio milestone.
 */

#include <stddef.h>
#include <stdint.h>

int n_alInit          (void *p, int max_voices) { (void)p; (void)max_voices; return 0; }
int n_alClose         (void *p)                 { (void)p; return 0; }
int n_alAudioFrame    (int16_t *out, int32_t *cmd_list, int16_t *cmd_len, int outLen) {
    (void)cmd_list; (void)cmd_len;
    if (out) {
        for (int i = 0; i < outLen * 2; i++) out[i] = 0;
    }
    return 0;
}
int n_alSeqpPlay      (void *p, void *seq)  { (void)p; (void)seq; return 0; }
int n_alSeqpStop      (void *p, void *seq)  { (void)p; (void)seq; return 0; }
int n_alSeqpSetVol    (void *p, int16_t vol){ (void)p; (void)vol; return 0; }
int n_alSeqpSetTempo  (void *p, int tempo)  { (void)p; (void)tempo; return 0; }

/* Audio thread will block on its audio-frame mq forever; we post a
 * heartbeat from a low-rate libctru thread so it doesn't deadlock. */
