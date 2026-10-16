#!/bin/sh
# libyaml owns scalar syntax; ait owns its schema and LF-only subset.
. "$(dirname "$0")/lib.sh"
setup
"${CC:-cc}" -I"$AIT_DIR" -o "$T/body-reader" "$AIT_DIR/t/helper-yaml-boundary.c" "$AIT_DIR"/libyaml/*.o || exit 1
# a body given in YAML's escapes is stored by ait and read back by libyaml alone
for value in space leading-lf lf-only plain lf two-lf trailing-space tab doc-end doc-end-lf empty cr nel ls ps control nonchar; do
	style=''
	case $value in
	space) bytes=' leading\nnext'; yaml='" leading\nnext"'; style='|2-' ;;
	leading-lf) bytes='\nnext'; yaml='"\nnext"'; style='|2-' ;;
	lf-only) bytes='\n\n'; yaml='"\n\n"'; style='|2+' ;;
	plain) bytes='body'; yaml='body'; style='|-' ;;
	lf) bytes='body\n'; yaml='"body\n"'; style='|' ;;
	two-lf) bytes='body\n\n'; yaml='"body\n\n"'; style='|+' ;;
	trailing-space) bytes='body \n'; yaml='"body \n"'; style='"' ;;
	tab) bytes='\tindent\n'; yaml='"\tindent\n"' ;;
	doc-end) bytes='body\n...\n\n'; yaml='"body\n...\n\n"'; style='|+' ;;
	doc-end-lf) bytes='...\n'; yaml='"...\n"'; style='|' ;;
	empty) bytes=''; yaml='""' ;;
	cr) bytes='a\rb'; yaml='"a\rb"'; style='"' ;;
	nel) bytes='a\302\205b'; yaml='"a\Nb"'; style='"' ;;
	ls) bytes='a\342\200\250b'; yaml='"a\Lb"'; style='"' ;;
	ps) bytes='a\342\200\251b'; yaml='"a\Pb"'; style='"' ;;
	control) bytes='\001\177'; yaml='"\x01\x7f"'; style='"' ;;
	nonchar) bytes='\357\277\276'; yaml='"\uFFFE"'; style='"' ;;
	esac
	printf '%b' "$bytes" >"$T/value"
	printf 'body: %s\n' "$yaml" >body.yaml
	run ait future offer src/stock.c body.yaml -m "$value body"
	check_rc "$value body is accepted" 0
	yaml "$(newest O)" >"$T/stored"
	check "$value body roundtrips through an independent reader" "'$T/body-reader' '$T/stored' '$T/value'"
	if [ -n "$style" ]; then
		if [ "$style" = '"' ]; then
			check "$value uses the required scalar style" "grep -q '^- body: \"' '$T/stored'"
		else
			check "$value uses the required scalar style" "grep -Fqx -- '- body: $style' '$T/stored'"
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
	{ printf '# \355\225\234\n# prefix\nbody: '; printf '%b' "$bytes"; printf '\n'; } >input.yaml
	run ait future offer src/stock.c input.yaml -m "$value"
	check "$value is rejected at its original third line" "test $rc = 128 && has err \"bad format: 'input.yaml': .*in line 3\$\""
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
	printf '%b\n' "$text" >input.yaml
	run ait future offer src/stock.c input.yaml -m "$feature"
	check "the subset still rejects $feature" "test $rc = 128 && has err \"bad format: 'input.yaml'\""
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
printf '\357\273\277body: "valid"\n' >input.yaml
run ait future offer src/stock.c input.yaml -m bom
check_rc 'a UTF-8 BOM is accepted without changing encoding' 0
printf '\377\376b\000o\000d\000y\000' >input.yaml
run ait future offer src/stock.c input.yaml -m utf16
check "UTF-16 input is rejected rather than autodetected" "test $rc = 128 && has err \"bad format: 'input.yaml'\""
done_testing
