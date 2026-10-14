#ifndef PM3DS_TEST_RUNNER_H
#define PM3DS_TEST_RUNNER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int  pm3ds_test_failures = 0;
static int  pm3ds_test_total    = 0;
static const char *pm3ds_test_name = "";

#define PM3DS_TEST(name) \
    static void name(void); \
    static void name##_register(void) __attribute__((constructor)); \
    static void name##_register(void) { \
        pm3ds_test_name = #name; \
        pm3ds_test_total++; \
        printf("[ RUN  ] %s\n", #name); \
        name(); \
        printf("[  OK  ] %s\n", #name); \
    } \
    static void name(void)

#define PM3DS_EXPECT(cond) do { \
    if (!(cond)) { \
        pm3ds_test_failures++; \
        fprintf(stderr, "[ FAIL ] %s: %s:%d: %s\n", \
                pm3ds_test_name, __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while (0)

#define PM3DS_EXPECT_EQ_U32(a, b) do { \
    uint32_t _a = (uint32_t)(a), _b = (uint32_t)(b); \
    if (_a != _b) { \
        pm3ds_test_failures++; \
        fprintf(stderr, "[ FAIL ] %s: %s:%d: %s=0x%08x != %s=0x%08x\n", \
                pm3ds_test_name, __FILE__, __LINE__, #a, _a, #b, _b); \
        exit(1); \
    } \
} while (0)

int main(void) {
    /* All tests registered via constructor; this is just a summary print. */
    printf("\n[ DONE ] total=%d failures=%d\n", pm3ds_test_total, pm3ds_test_failures);
    return pm3ds_test_failures ? 1 : 0;
}

#endif /* PM3DS_TEST_RUNNER_H */
