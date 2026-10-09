# C standard and warnings follow git v2.53.0 config.mak.dev:25-37 (DEVELOPER_CFLAGS)
CC ?= cc
CFLAGS = -std=gnu99 -O2 -Wall -Werror \
	-Wdeclaration-after-statement -Wformat-security -Wold-style-definition \
	-Woverflow -Wpointer-arith -Wstrict-prototypes -Wunused -Wvla -Wwrite-strings
SRC = $(wildcard *.c)

ait: $(SRC) $(wildcard *.h)
	$(CC) $(CFLAGS) -o $@ $(SRC)

test: ait
	@sh t/run.sh

clean:
	rm -f ait

.PHONY: test clean
