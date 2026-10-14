#ifndef _ABI_H_
#define _ABI_H_

// 3DS stub for N64 abi.h (Audio Binary Interface)
// Provides minimal audio state typedefs referenced by src/audio.h

#define ADPCMFSIZE 16
#define UNITY_PITCH 0x8000

typedef short ADPCM_STATE[ADPCMFSIZE];
typedef short POLEF_STATE[4];
typedef short RESAMPLE_STATE[16];
typedef short ENVMIX_STATE[40];

// Minimal Acmd typedef for shims
typedef struct {
    unsigned int w0;
    unsigned int w1;
} Acmd;

#endif
