set -u
export GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@t GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@t GIT_CONFIG_NOSYSTEM=1 HOME=$PWD
git init -q -b main r && cd r
echo c0 > f; git add f; git commit -qm init
for x in "a P" "b Q" "c R"; do set -- $x; git checkout -q -b $1 main; echo $2 > f; git commit -qam "$1 1: f=$2"; done
# each keeps its own side after seeing one other (like D=HEAD)
git checkout -q a; git merge -q c~0 -m x >/dev/null 2>&1; echo P > f; git add f; git commit -qm "a2: saw c1, kept P"
git checkout -q b; git merge -q a~1 -m x >/dev/null 2>&1; echo Q > f; git add f; git commit -qm "b2: saw a1, kept Q"
git checkout -q c; git merge -q b~1 -m x >/dev/null 2>&1; echo R > f; git add f; git commit -qm "c2: saw b1, kept R"
git log --graph --all --format='%h %s' | sed 's/^/   /'
echo "-- merge bases"
for p in "a b" "b c" "c a"; do set -- $p; echo "   $1/$2: $(git merge-base --all $1 $2 | xargs -n1 git log -1 --format=%s | paste -sd'|' -)"; done
echo "-- pairwise: one side merges another"
for p in "a b" "b c" "c a"; do set -- $p; git checkout -q -B t $1; r=$(git merge -q --no-edit $2 >/dev/null 2>&1; echo $?); echo "   on $1, git merge $2: exit $r, f=$(cat f | tr '\n' ' ')"; git merge --abort 2>/dev/null; git reset -q --hard; done
echo "-- main takes all three one by one, in each order"
for o in "a b c" "b c a" "c a b" "a c b"; do git checkout -q -B t main; out=""; for x in $o; do r=$(git merge -q --no-edit $x >/dev/null 2>&1; echo $?); out="$out $x:exit$r(f=$(head -1 f))"; if [ $r != 0 ]; then git merge --abort; break; fi; done; echo "   order $o ->$out"; done
echo "-- octopus from main"
git checkout -q -B t main; git merge --no-edit a b c 2>&1 | tail -3 | sed 's/^/   /'; echo "   f=$(cat f | tr '\n' ' ')"
