#!/bin/sh
# libyaml owns scalar syntax; ait owns its schema and LF-only subset.
. "$(dirname "$0")/lib.sh"
setup
set -- -std=gnu99 -O1 -Wall -Werror -Wdeclaration-after-statement -I"$AIT_DIR" -I"$AIT_DIR/libyaml"
case $(uname -s) in Linux) set -- "$@" -DHAVE_GETRANDOM ;; Darwin) set -- "$@" -DHAVE_ARC4RANDOM -DPRECOMPOSE_UNICODE ;; esac
compiler=${CC:-cc}
"$compiler" "$@" -Dmain=ait_command_main -c "$AIT_DIR/ait.c" -o "$T/ait.o" || exit 1
for source in "$AIT_DIR"/*.c; do
	case $source in */ait.c) continue ;; esac
	set -- "$@" "$source"
done
set -- "$@" "$AIT_DIR/t/helper-yaml-boundary.c" "$T/ait.o" "$AIT_DIR"/libyaml/*.o
case $(uname -s) in Darwin) set -- "$@" -liconv ;; esac
"$compiler" "$@" -o "$T/yaml-boundary" || exit 1
for value in space leading-lf lf-only plain lf two-lf trailing-space tab doc-end doc-end-lf empty cr nel ls ps control nonchar; do
	style=''
	case $value in
	space) bytes=' leading\nnext'; style='|2-' ;;
	leading-lf) bytes='\nnext'; style='|2-' ;;
	lf-only) bytes='\n\n'; style='|2+' ;;
	plain) bytes='body'; style='|-' ;;
	lf) bytes='body\n'; style='|' ;;
	two-lf) bytes='body\n\n'; style='|+' ;;
	trailing-space) bytes='body \n'; style='"' ;;
	tab) bytes='\tindent\n' ;;
	doc-end) bytes='body\n...\n\n'; style='|+' ;;
	doc-end-lf) bytes='...\n'; style='|' ;;
	empty) bytes='' ;;
	cr) bytes='a\rb'; style='"' ;;
	nel) bytes='a\302\205b'; style='"' ;;
	ls) bytes='a\342\200\250b'; style='"' ;;
	ps) bytes='a\342\200\251b'; style='"' ;;
	control) bytes='\001\177'; style='"' ;;
	nonchar) bytes='\357\277\276'; style='"' ;;
	esac
	printf '%b' "$bytes" >"$T/value"
	run "$T/yaml-boundary" write "$T/value"
	check_rc "$value body roundtrips through an independent reader" 0
	if [ -n "$style" ]; then
		if [ "$style" = '"' ]; then
			check "$value uses the required scalar style" "has out '^- body: \"'"
		else
			check "$value uses the required scalar style" "grep -Fqx -- '- body: $style' '$T/out'"
		fi
	fi
done
# A byte offset remains a file line even after preceding multibyte UTF-8.
for value in invalid overlong cr nel ls ps decoded-nul; do
	case $value in
	invalid) bytes='\377' ;;
	overlong) bytes='\300\257' ;;
	cr) bytes='\r' ;;
	nel) bytes='\302\205' ;;
	ls) bytes='\342\200\250' ;;
	ps) bytes='\342\200\251' ;;
	decoded-nul) bytes='"\\0"' ;;
	esac
	{ printf '# \355\225\234\n# prefix\nbody: '; printf '%b' "$bytes"; printf '\n'; } >"$T/input"
	run "$T/yaml-boundary" parse "$T/input"
	check "$value is rejected at its original third line" "test $rc = 1 && has out 'in line 3$'"
done
for feature in anchor alias tag directive documents duplicate folded composite-key; do
	case $feature in
	anchor) text='body: &a value' ;;
	alias) text='body: *a' ;;
	tag) text='body: !!str value' ;;
	directive) text='%YAML 1.1\n---\nbody: value' ;;
	documents) text='body: one\n---\nbody: two' ;;
	duplicate) text='body: one\nbody: two' ;;
	folded) text='body: >\n  value' ;;
	composite-key) text='? [one, two]\n: value' ;;
	esac
	printf '%b\n' "$text" >"$T/input"
	run "$T/yaml-boundary" parse "$T/input"
	check_rc "the subset still rejects $feature" 1
done
printf 'body: " leading\\nline\\n"\nengine: exec\ncommand:\n- sh\n- -c\n- exit 0\n' >cond.yaml
run ait future offer src/stock.c cond.yaml -m 'library scalar'
check_rc 'indentless command and leading body are accepted together' 0
O=$(newest O)
yaml "$O" >"$T/stored"
check 'omitted success still defaults and fulfilled keeps an empty key' "grep -Fxq '  success: [0]' '$T/stored' && grep -qx 'fulfilled:' '$T/stored'"
run ait fulfill "$O" --force -m first
check_rc 'the first fulfilment attaches to the empty key' 0
yaml "$O" >"$T/stored"
check 'the first attachment uses an indentless row' "grep -q '^- \"fulfill/' '$T/stored'"
printf 'success: []\n' >>cond.yaml
run ait future offer src/order.c cond.yaml -m 'explicit empty success'
check_rc 'explicit empty success remains a distinct valid value' 0
EMPTY=$(newest O)
check 'explicit success stays empty after serialization' "yaml $EMPTY | grep -Fxq '  success: []'"
printf '\357\273\277body: "valid"\n' >"$T/input"
run "$T/yaml-boundary" parse "$T/input"
check_rc 'a UTF-8 BOM is accepted without changing encoding' 0
printf '\377\376b\000o\000d\000y\000' >"$T/input"
run "$T/yaml-boundary" parse "$T/input"
check_rc 'UTF-16 input is rejected rather than autodetected' 1
done_testing
