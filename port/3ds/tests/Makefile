# Host-side unit tests for port/3ds shim primitives.
# Runs on the developer's Linux/macOS box, not on 3DS.
#
# Each line in TESTS is a host-compilable C source file under this directory
# (without the .c suffix) that #includes ../include/pm_endian.h, runner.h,
# and whichever shim it exercises. Tests are appended task-by-task as the
# corresponding shims are implemented; see
# docs/superpowers/plans/2026-05-13-papermario-3ds-boot-hal.md.

CC      := cc
CFLAGS  := -std=c11 -Wall -Wextra -O0 -g -DPM3DS_HOST_TEST=1 -D_GNU_SOURCE \
           -I../include -I../source -fno-pie
# -no-pie: keeps static globals in the lower 32 bits of the host address
# space so tests can put data behind Gfx.w1 (which is uint32_t to match
# the N64 ABI) without truncation. Required by test_gfx_state's
# movemem viewport test and later by test_gfx_geom's SP_MATRIX test.
LDFLAGS := -pthread -no-pie

TESTS := \
    test_pm_endian \
    test_pm3ds_heap \
    test_os_mesgqueue \
    test_os_timer \
    test_gfx_state \
    test_gfx_dl \
    test_gfx_geom \
    test_gfx_texture \
    test_gfx_combiner

.PHONY: all clean run

all: $(TESTS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# pm3ds_heap test pulls in the platform/ implementation alongside the test
# source so the linker can resolve pm3ds_heap_* symbols at host-test time.
test_pm3ds_heap: test_pm3ds_heap.c ../source/platform/pm3ds_heap.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# os_mesgqueue test #includes the shim .c directly (so ctru_stub.h types
# are visible inside it); only the test .c is on the cc command line.
test_os_mesgqueue: test_os_mesgqueue.c ../source/n64_shim/os_mesgqueue.c
	$(CC) $(CFLAGS) -pthread -o $@ $< $(LDFLAGS)

# os_timer test also #includes both shim .c files directly so it sees the
# host-only typedefs (OSMesg / OSMesgQueue / OSTimer / LightLock / ...).
test_os_timer: test_os_timer.c ../source/n64_shim/os_timer.c ../source/n64_shim/os_mesgqueue.c
	$(CC) $(CFLAGS) -pthread -o $@ $< $(LDFLAGS)

# gfx_state test #includes gfx_translate.c directly; the shim source is
# referenced as a prerequisite so changes there trigger a rebuild.
# gfx_geom.c is on the link line because the dispatch table now references
# gfx_geom_* from the SP_MATRIX / POPMTX handlers.
test_gfx_state: test_gfx_state.c ../source/n64_shim/gfx_translate.c ../source/n64_shim/gfx_geom.c
	$(CC) $(CFLAGS) -o $@ $< ../source/n64_shim/gfx_geom.c $(LDFLAGS)

# gfx_dl test pulls in the queue implementation directly so it sees the
# host pthread-backed LightLock typedefs from ctru_stub.h.
test_gfx_dl: test_gfx_dl.c ../source/n64_shim/gfx_dl.c
	$(CC) $(CFLAGS) -pthread -o $@ $< $(LDFLAGS)

# gfx_geom test #includes gfx_translate.c (for state init) and
# gfx_geom.c (the matrix stack). Two TUs in one binary.
test_gfx_geom: test_gfx_geom.c ../source/n64_shim/gfx_geom.c ../source/n64_shim/gfx_translate.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# gfx_texture test #includes gfx_texture.c directly.
test_gfx_texture: test_gfx_texture.c ../source/n64_shim/gfx_texture.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_gfx_combiner: test_gfx_combiner.c ../source/n64_shim/gfx_combiner.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

run: all
	@set -e; \
	for t in $(TESTS); do \
	    echo "=== $$t ==="; \
	    ./$$t; \
	done

clean:
	rm -f $(TESTS)
