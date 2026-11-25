#!/bin/sh
# Host tests for q2-vortex (x86-64 Linux, clang, python3 with Pillow), with the player's own .ipg
# in $VX_IPG: the real game and platform layer on fbshim.so's fake /dev/fb0 and alsa_fake.c's PCM,
# driven through Q2 Pod's input socket (test/drive.py). Each scenario must end on TERM with status
# 0, the panel back on Q2 Pod's page and the socket gone. Frames land in test/out/*.raw and .png.
#   test/run.sh          the quick set
#   test/run.sh clear    also plays level 1 through on the autopilot (about 2 minutes)
set -e
cd "$(dirname "$0")/.."
[ -f "$VX_IPG" ] || { echo "set VX_IPG to your .ipg"; exit 1; }
mkdir -p test/out
./test/hostbuild.sh
scenarios="menus help play pause lose finger shots"
[ "$1" = clear ] && scenarios="$scenarios clear"
for sc in $scenarios; do
    python3 -I test/drive.py build/host test/out $sc > test/out/$sc.log 2>&1 || { cat test/out/$sc.log; exit 1; }
    grep -q "^exit status: 0" test/out/$sc.log || { echo "FAIL: $sc: exit status"; cat test/out/$sc.log; exit 1; }
    grep -q "^panel: yoffset 0 " test/out/$sc.log || { echo "FAIL: $sc: panel not handed back"; exit 1; }
    grep -q "^socket left: False" test/out/$sc.log || { echo "FAIL: $sc: socket left behind"; exit 1; }
    echo "$sc: ok"
done
res() { grep "^result: $2:" test/out/$1.log | sed 's/.*: //'; }
fail() { echo "FAIL: $*"; exit 1; }
# menus: the splash to the main menu, the wheel moves, personal info, options toggles, sfx steps,
# new game -> difficulty -> level 1's intro -> play
[ "$(res menus main)" = 1 ] && [ "$(res menus sel_after_wheel)" = 1 ] && [ "$(res menus stats)" = 3 ] || fail "menus: main menu"
[ "$(res menus options_menu)" = 2 ] && [ "$(res menus control_toggled)" = 1 ] && [ "$(res menus sfx)" = 1 ] || fail "menus: options"
[ "$(res menus diff_menu)" = 1 ] && [ "$(res menus intro)" = 5 ] && [ "$(res menus intro_level)" = 1 ] && [ "$(res menus playing)" = 6 ] ||
    fail "menus: new game"
[ "$(res help help)" = 2 ] && [ "$(res help wrapped)" = 0 ] || fail "help: six pages round"
[ "$(res play broken)" -gt 2 ] && [ "$(res play state)" = 6 ] || fail "play: the autopilot broke no bricks"
[ "$(res pause paused)" = 1 ] && [ "$(res pause resumed)" = 6 ] && [ "$(res pause confirm)" = 4 ] &&
    [ "$(res pause after)" = 1 ] && [ "$(res pause after_menu)" = 0 ] || fail "pause: a held Return paused once, resume, abandon"
[ "$(res lose lives_after_one)" = 1 ] && [ "$(res lose over)" = 10 ] && [ "$(res lose menu_after)" = 1 ] || fail "lose: lives, game over"
[ "$(res shots broken)" -gt 0 ] || fail "shots: guns broke nothing"
b=$(res finger bat); [ "$b" -gt 3000 ] && [ "$b" -lt 3280 ] || fail "finger: the bat went to $b mrad, not the touch (3142)"
if [ "$1" = clear ]; then
    [ "$(res clear cleared)" = 1 ] && [ "$(res clear next_level)" = 2 ] || fail "clear: level 1 to level 2"
fi
for f in test/out/*.raw; do python3 -I test/view.py "$f" "${f%.raw}.png"; done
echo "all ok"
