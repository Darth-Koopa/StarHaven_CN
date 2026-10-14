#include <stdint.h>
#include "../include/pm_endian.h"
#include "runner.h"

PM3DS_TEST(be32_basic) {
    PM3DS_EXPECT_EQ_U32(BE32(0x01020304u), 0x04030201u);
    PM3DS_EXPECT_EQ_U32(BE32(0x00000000u), 0x00000000u);
    PM3DS_EXPECT_EQ_U32(BE32(0xFFFFFFFFu), 0xFFFFFFFFu);
}

PM3DS_TEST(be16_basic) {
    PM3DS_EXPECT_EQ_U32(BE16(0x1234u), 0x3412u);
    PM3DS_EXPECT_EQ_U32(BE16(0x0000u), 0x0000u);
}

PM3DS_TEST(be_read_unaligned) {
    /* A big-endian u32 0xDEADBEEF stored at unaligned offset 1. */
    static const uint8_t buf[8] = { 0x00, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00 };
    PM3DS_EXPECT_EQ_U32(LE_READ_BE_U32(&buf[1]), 0xDEADBEEFu);
}
