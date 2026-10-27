#!/bin/sh
# tools/netlab/crowd.sh PLAYERS SECONDS: a hidden host (a) and PLAYERS-1 hidden joiners (b, b2, b3...),
# each with a console of its own, in one network game against the local server (see pair.sh for it).
# the joiners start GAP s apart (default 12) from 45 s on, each firing, turning and walking at random
# from when it's in the level, so players die, drop guns and pick them up. everything in build/netlab
# extra environment for all of them in build/netlab/crowd.env (GE_PATCH_FILE, GE_TRACEOWNED...)
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE/../.." || exit 1
mkdir -p build/netlab && cd build/netlab || exit 1
N=${1:-8}; SECS=${2:-300}; GAP=${GAP:-12}
S=${S:-"8:A 11:D 12:D 13:D 15:A 18:D 19:D 21:A 52:A 58:A"}
#(name, from) -> GE_STICK, GE_MOUSEMOVE and GE_BUTTONS lines, seeded by the name
inputs()
{
    python3 -I - "$1" "$2" "$3" <<'EOF'
import random, sys
name, start, end = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
r = random.Random(name)
stick, mouse, fire = [], [], []
t = start
while t < end:
    d = r.uniform(1.5, 4)
    stick.append(f"{t:.1f}-{t + d:.1f}:l{r.uniform(-1, 1):.2f},{r.uniform(-0.3, 1):.2f}")
    mouse.append(f"{t:.1f}-{t + d:.1f}:{r.choice((-1, 1)) * r.randint(60, 400)},{r.randint(-30, 30)}")
    if r.random() < 0.7: fire.append(f"{t:.1f}-{t + d * r.uniform(0.3, 1):.1f}:0004")
    elif r.random() < 0.5: fire.append(f"{t:.1f}-{t + 0.2:.1f}:0008") #x: the next weapon
    t += d
print(f'GE_STICK="{" ".join(stick)}"\nGE_MOUSEMOVE="{" ".join(mouse)}"\nGE_BUTTONS="{" ".join(fire)}"')
EOF
}
one()
{
    name=$1; from=$2; secs=$3
    inputs "$name" "$from" "$secs" > "$name.inputs"
    ( set -a; [ -f crowd.env ] && . ./crowd.env; . "./$name.inputs"; set +a; "$HERE/run.sh" "$name" "$S" "$secs" 100000 ) &
}
one a 90 "$SECS"
i=1
while [ $i -lt "$N" ]; do
    sleep $((i == 1 ? 45 : GAP))
    name=b; [ $i -gt 1 ] && name=b$i
    one $name 65 $((SECS - 45 - (i - 1) * GAP))
    i=$((i + 1))
done
wait
