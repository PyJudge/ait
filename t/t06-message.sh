#!/bin/sh
# T06 메시지 (5.3, 5.4)
. "$(dirname "$0")/lib.sh"
setup

A=$(ait session open --agent agent-a)
prompt_defs
CUT='# ------------------------ >8 ------------------------'
echo f > "$T/f"

# CL is `future offer src/stock.c p`
tick
expect_unchanged expect_err "no message" E_USAGE ait future offer src/stock.c specs/p.yaml
expect_unchanged expect_err "-m and -F together" E_USAGE ait future offer src/stock.c specs/p.yaml -m x -F "$T/f"
expect_unchanged expect_err "blank message" E_USAGE ait future offer src/stock.c specs/p.yaml -m "   "
expect_unchanged expect_err "option inside a clause" E_USAGE ait future offer src/stock.c -m m specs/p.yaml

expect_unchanged expect_err "scissors line in -m" E_USAGE ait future offer src/stock.c specs/p.yaml -m a -m "$CUT" -m b
check "scissors: exact first line" [ "$(head -1 "$T/err")" = "error: the message hides the trailers: '$CUT'" ]
check "scissors: usage line" grep -q '^usage: ait ' "$T/err"
check "scissors: hint" grep -qxF 'hint: remove that line from the message' "$T/err"

printf 'a\n\n%s\n\nb\n' "$CUT" > "$T/cut.txt"
expect_unchanged expect_err "scissors line in -F" E_USAGE ait future offer src/stock.c specs/p.yaml -F "$T/cut.txt"
check "scissors -F: exact first line" [ "$(head -1 "$T/err")" = "error: the message hides the trailers: '$CUT'" ]
check "scissors -F: usage line" grep -q '^usage: ait ' "$T/err"
check "scissors -F: hint" grep -qxF 'hint: remove that line from the message' "$T/err"

subject() { git log -1 --format=%s; }
trailers() { printf 'Ait-Request: %s\nAit-Agent: agent-a\nAit-Session: %s' "$(out_value request)" "$A"; }

tick; expect_ok "-m '[CONTRACT] 이미'" ait future offer src/stock.c specs/p.yaml -m "[CONTRACT] 이미"
check "subject keeps the prefix" [ "$(subject)" = "[CONTRACT] 이미" ]

tick; expect_ok "-m '[CONTRACT]x'" ait future offer src/stock.c specs/p.yaml -m "[CONTRACT]x"
check "subject gets the prefix" [ "$(subject)" = "[CONTRACT] [CONTRACT]x" ]

tick; expect_ok "-m a -m b" ait future offer src/stock.c specs/p.yaml -m a -m b
check "whole message: subject, b, trailers" [ "$(git log -1 --format=%B)" = "$(printf '[CONTRACT] a\n\nb\n\n%s' "$(trailers)")" ]

tick
run sh -c "printf 'from stdin\n' | ait future offer src/stock.c specs/p.yaml -F -"
check "-F - reads stdin" [ "$rc" = 0 ]
check "subject from stdin" [ "$(subject)" = "[CONTRACT] from stdin" ]

printf '[CONTRACT] from file\n\nfile body\n' > "$T/msg.txt"
tick; expect_ok "-F ../msg.txt from \$T/r" ait future offer src/stock.c specs/p.yaml -F ../msg.txt
check "message is msg.txt's content" [ "$(git log -1 --format=%B)" = "$(printf '[CONTRACT] from file\n\nfile body\n\n%s' "$(trailers)")" ]

tick; expect_ok "-m s -m 'Ait-Agent: evil'" ait future offer src/stock.c specs/p.yaml -m s -m "Ait-Agent: evil"
check "body paragraph is not a trailer" [ "$(git log -1 --format='%(trailers:key=Ait-Agent,valueonly)')" = "agent-a" ]

tick; expect_ok "-m a -m --- -m b" ait future offer src/stock.c specs/p.yaml -m a -m --- -m b
RID=$(out_value request)
check "--- does not cut the trailers" [ "$(git log -1 --format='%(trailers:key=Ait-Request,valueonly)')" = "$RID" ]

done_testing
