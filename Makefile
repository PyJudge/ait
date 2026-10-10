# C standard and warnings follow git v2.53.0 config.mak.dev:25-37 (DEVELOPER_CFLAGS)
CC ?= cc
CFLAGS = -std=gnu99 -O2 -Wall -Werror \
	-Wdeclaration-after-statement -Wformat-security -Wold-style-definition \
	-Woverflow -Wpointer-arith -Wstrict-prototypes -Wunused -Wvla -Wwrite-strings
SRC = $(wildcard *.c)
# the CSPRNG of csprng_bytes() as git picks it (git v2.53.0 config.mak.uname, Makefile:2299-2310)
uname_S := $(shell uname -s)
ifeq ($(uname_S),Linux)
	CFLAGS += -DHAVE_GETRANDOM
endif
ifneq ($(filter Darwin SunOS FreeBSD OpenBSD MirBSD NetBSD,$(uname_S)),)
	CFLAGS += -DHAVE_ARC4RANDOM
endif

ait: $(SRC) $(wildcard *.h)
	$(CC) $(CFLAGS) -o $@ $(SRC)

test: ait
	@sh t/run.sh

clean:
	rm -f ait

.PHONY: test clean
