# Host tests: plain C checks of the firmware's pure logic, built with the host
# compiler (no Pico SDK) and run in seconds.
#
#   make -C tests/host test         # build and run everything; fails on any failure
#   make -C tests/host known-bugs   # the defects the firmware still has
#   make -C tests/host clean
#
# Every test_*.c here is one program: it includes the firmware units it tests
# from booster/src, with shim/ standing in for the few SDK headers they pull
# in, and exits non-zero when a check fails. Address and undefined-behaviour
# sanitizers make a write past a buffer fail the test instead of passing by
# luck.
#
# A bug_*.c test shows a defect the firmware still has: it fails today, and
# `known-bugs` passes only while every one of them does. When the defect is
# fixed the test starts passing: rename it to test_*.c so it guards the fix.

CC ?= cc
REPO := ../..
BUILD := build

# The firmware's headers are included as system headers: the tests' own code
# is held to -Werror, the firmware's style is clang-tidy's job, and its
# headers do not all build warning-free with the host compiler. gnu11 is the
# dialect the firmware itself is built with (strnlen() is POSIX, not C11).
CFLAGS := -std=gnu11 -g -O1 -Wall -Wextra -Werror -Wno-unknown-pragmas \
          -fsanitize=address,undefined -fno-omit-frame-pointer \
          -fno-sanitize-recover=undefined \
          -I shim -isystem $(REPO)/booster/src/include
LDFLAGS := -fsanitize=address,undefined -pthread

C_TESTS := $(patsubst %.c,$(BUILD)/%,$(wildcard test_*.c))
BUG_TESTS := $(patsubst %.c,$(BUILD)/%,$(wildcard bug_*.c))

.PHONY: test known-bugs clean FORCE

test: $(C_TESTS)
	@set -e; for t in $(C_TESTS); do echo "== $$t"; ./$$t; done

known-bugs: $(BUG_TESTS)
	@set -e; for t in $(BUG_TESTS); do echo "== $$t (must fail)"; \
	  if ./$$t > $$t.log 2>&1; then echo "$$t passes: the defect is fixed, rename it to test_*"; exit 1; \
	  else tail -3 $$t.log; fi; done

# A test's own firmware sources are listed in its first line, relative to the
# repository root (empty for a header-only unit):
#   // units: booster/src/wificonf.c
# Always rebuilt (it takes about a second): a test must never pass on a binary
# older than the firmware source it checks.
$(BUILD)/%: %.c FORCE | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< \
	  $(addprefix $(REPO)/,$(shell sed -n '1s|^// units:||p' $<)) $(LDFLAGS)

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

FORCE:
