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
	MACOSX_DEPLOYMENT_TARGET ?= 11.0
	export MACOSX_DEPLOYMENT_TARGET
	CFLAGS += -DPRECOMPOSE_UNICODE
	LDLIBS += -liconv
endif

ait: $(SRC) $(wildcard *.h) $(YAML)
	$(CC) $(CFLAGS) -Ilibyaml -o $@ $(SRC) $(YAML) $(LDLIBS)

libyaml/%.o: libyaml/%.c libyaml/yaml.h libyaml/yaml_private.h
	$(CC) $(YAML_CFLAGS) -c -o $@ $<

test: ait
	@sh t/run.sh

# Source and archive installs use the same installer and resource layout.
prefix ?= $(HOME)/.local
install: bundle
	PREFIX="$(prefix)" sh dist/ait/install.sh

# The v1.1 release: native executable, skill and setup scripts.
bundle: ait
	rm -rf dist/ait
	mkdir -p dist/ait/bin dist/ait/skills
	cp ait dist/ait/bin/
	cp -R spec/skills/ait dist/ait/skills/
	cp bundle/README.md bundle/install.sh bundle/setup.sh bundle/try.sh bundle/ait-graph.sh dist/ait/
	printf '1.1\n' > dist/ait/VERSION
	uname -sm > dist/ait/PLATFORM
	tar -czf dist/ait-v1.1.tar.gz -C dist ait
	cp dist/ait-v1.1.tar.gz dist/ait.tar.gz

clean:
	rm -f ait $(YAML)

.PHONY: test install bundle clean
