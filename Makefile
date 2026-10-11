# C standard and warnings follow git v2.53.0 config.mak.dev:25-37 (DEVELOPER_CFLAGS)
CC ?= cc
CFLAGS = -std=gnu99 -O2 -Wall -Werror \
	-Wdeclaration-after-statement -Wformat-security -Wold-style-definition \
	-Woverflow -Wpointer-arith -Wstrict-prototypes -Wunused -Wvla -Wwrite-strings
SRC = $(wildcard *.c)
# libyaml 0.2.5 (libyaml/, as released; yaml-0.2.5.tar.gz sha256 c642ae9b...678ef4), built as its own code
YAML = $(patsubst %.c,%.o,$(wildcard libyaml/*.c))
YAML_CFLAGS = -std=gnu99 -O2 -Ilibyaml -DYAML_VERSION_MAJOR=0 -DYAML_VERSION_MINOR=2 \
	-DYAML_VERSION_PATCH=5 -DYAML_VERSION_STRING='"0.2.5"'
# the CSPRNG of csprng_bytes() as git picks it (git v2.53.0 config.mak.uname, Makefile:2299-2310)
uname_S := $(shell uname -s)
ifeq ($(uname_S),Linux)
	CFLAGS += -DHAVE_GETRANDOM
endif
ifneq ($(filter Darwin SunOS FreeBSD OpenBSD MirBSD NetBSD,$(uname_S)),)
	CFLAGS += -DHAVE_ARC4RANDOM
endif
# argv precomposed as git does on macOS (git v2.53.0 config.mak.uname:144-145), with iconv
ifeq ($(uname_S),Darwin)
	CFLAGS += -DPRECOMPOSE_UNICODE
	LDLIBS += -liconv
endif

ait: $(SRC) $(wildcard *.h) $(YAML)
	$(CC) $(CFLAGS) -Ilibyaml -o $@ $(SRC) $(YAML) $(LDLIBS)

libyaml/%.o: libyaml/%.c libyaml/yaml.h libyaml/yaml_private.h
	$(CC) $(YAML_CFLAGS) -c -o $@ $<

test: ait
	@sh t/run.sh
	@sh t/pull/run.sh

test-pull: ait
	@sh t/pull/run.sh

# a copy, not a link: the hooks of this repository run the installed ait, not the one being built
prefix ?= $(HOME)/.local
install: ait
	install -d $(prefix)/bin
	install ait $(prefix)/bin/ait

# ait-mini (records of version 1) stays as its last commit was; its experiment bundle is built from there
MINI = 190f2d9
bundle:
	rm -rf dist/mini && mkdir -p dist/mini
	git archive $(MINI) | tar -x -C dist/mini
	$(MAKE) -C dist/mini bundle
	mv dist/mini/dist/ait-mini.tar.gz dist/
	rm -rf dist/mini

clean:
	rm -f ait $(YAML)

.PHONY: test test-pull install bundle clean
