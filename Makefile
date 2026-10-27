# Warnings as git v2.53.0 builds: -Wall always (Makefile:966); the developer
# warnings, errors among them, only with DEVELOPER (Makefile:1045, config.mak.dev:5-40),
# which "make test" sets, so a new compiler's warning cannot stop an install
CC ?= cc
CFLAGS = -std=gnu99 -O2 -Wall
DEVELOPER_CFLAGS = -Werror \
	-Wdeclaration-after-statement -Wformat-security -Wold-style-definition \
	-Woverflow -Wpointer-arith -Wstrict-prototypes -Wunused -Wvla -Wwrite-strings
# exported, so the make a check runs (t50) does not rebuild ait under the others
ifneq ($(filter test,$(MAKECMDGOALS)),)
DEVELOPER = 1
export DEVELOPER
endif
ALL_CFLAGS = $(strip $(if $(DEVELOPER),$(DEVELOPER_CFLAGS)) $(CFLAGS))
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
# Windows as Git for Windows builds git (git v2.53.0 config.mak.uname:697-760, MINGW):
# git's compat layer (git/, as v2.53.0 has it) built as its own code, beside windows.c;
# ait's sources see its headers as git's do (-Icompat/win32: dirent.h matches its readdir)
ifneq ($(findstring MINGW,$(uname_S)),)
	CFLAGS += -DWIN32 -DHAVE_RTLGENRANDOM -DNO_POLL -DNO_SETENV -DNO_OPENSSL -DNOGDI \
		-D__USE_MINGW_ANSI_STDIO=0 -DDETECT_MSYS_TTY -Igit -Igit/compat/win32 -Igit/compat/poll
	LDLIBS += -municode -lws2_32 -lntdll
	GIT_OBJS = $(patsubst %.c,%.o,git/compat/mingw.c git/compat/winansi.c git/compat/poll/poll.c \
		git/compat/win32/path-utils.c git/compat/win32/dirent.c git/compat/win32/pthread.c \
		git/compat/setenv.c git/ctype.c git/stable-qsort.c)
endif
# git v2.53.0 Makefile's flags for its MINGW64 objects (make -n compat/mingw.o)
GIT_CFLAGS = -std=gnu99 -O2 -Igit -Igit/compat -Igit/compat/win32 -Igit/compat/poll -Igit/compat/regex \
	-DWIN32 -DNO_ICONV -DNO_GETTEXT -DNO_OPENSSL -DHAVE_ALLOCA_H -DNO_ST_BLOCKS_IN_STRUCT_STAT \
	-DNO_SYMLINK_HEAD -DNO_POLL_H -DNO_SYS_POLL_H -DMMAP_PREVENTS_DELETE -DNO_POSIX_GOODIES \
	-DUNRELIABLE_FSTAT -DNATIVE_CRLF -DHAVE_RTLGENRANDOM -DHAVE_WPGMPTR -DNOGDI \
	-DSTRIP_EXTENSION='".exe"' -D__USE_MINGW_ANSI_STDIO=0 -DDETECT_MSYS_TTY -DNO_LIBGEN_H -DNO_POLL \
	-DNO_STRCASESTR -DNO_STRLCPY -DNO_STRTOUMAX -DNO_SETENV -DNO_MKDTEMP -DUSE_WIN32_MMAP -DNO_PREAD \
	-DNO_MEMMEM -DRUNTIME_PREFIX -DSHELL_PATH='"/bin/sh"'

# flags other than the last build's rebuild, as git's GIT-CFLAGS (Makefile:3255-3260);
# compared by content, since a file time can miss a change within its second
ifneq ($(ALL_CFLAGS),$(shell cat AIT-CFLAGS 2>/dev/null))
ait: FORCE
endif
ait: $(SRC) $(wildcard *.h) $(YAML) $(GIT_OBJS)
	$(CC) $(ALL_CFLAGS) -Ilibyaml -o $@ $(SRC) $(YAML) $(GIT_OBJS) $(LDLIBS)
	@echo '$(ALL_CFLAGS)' >AIT-CFLAGS

libyaml/%.o: libyaml/%.c libyaml/yaml.h libyaml/yaml_private.h
	$(CC) $(YAML_CFLAGS) -c -o $@ $<

git/%.o: git/%.c
	$(CC) $(GIT_CFLAGS) -c -o $@ $<

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

# The Claude cloud kit: the committed source, built by the session's own compiler
# (spec/drafts/v1.1-cloud-plan.md); a Linux binary is not built here.
cloud:
	rm -rf dist/ait-cloud
	mkdir -p dist/ait-cloud
	git archive --prefix=src/ HEAD -- Makefile '*.c' '*.h' libyaml bundle spec/skills/ait t | tar -x -C dist/ait-cloud
	cp bundle/cloud/README.md bundle/cloud/session-start.sh bundle/cloud/settings.json dist/ait-cloud/
	git rev-parse HEAD > dist/ait-cloud/COMMIT
	tar -czf dist/ait-v1.1-cloud.tar.gz -C dist ait-cloud

clean:
	rm -f ait ait.exe AIT-CFLAGS $(YAML) $(GIT_OBJS)

FORCE:

.PHONY: test install bundle cloud clean FORCE
