#!/bin/sh
# grv6-capture.sh -- guided MIDI capture for the DDJ-GRV6.
# Prompts for one control at a time, records only what that control sends,
# and writes everything into one labelled file for writing map_grv6.c.
#
# Usage on the Pi:   sh grv6-capture.sh            (all sections)
#                    sh grv6-capture.sh mixer       (one section: idle deck1 decks mixer fx pads browse shift)
# Output:            ~/grv6-dump.txt  (appended, one block per control; delete it
#                    first for a fresh run)
#
# Each block is raw aseqdump output under a '=== [section] instruction' header,
# so a block can be fed to tools/aseqdump2dump.py once its header is stripped.

OUT="$HOME/grv6-dump.txt"
PORT=$(aseqdump -l | awk '/DDJ-GRV6 MIDI 1/ {print $1; exit}')
[ -z "$PORT" ] && { echo "DDJ-GRV6 MIDI port not found (aseqdump -l)"; exit 1; }
ONLY="$1"

# aseqdump runs in the background into a file and is then killed: line-buffer it
# so no event of the control just captured is still sitting in a stdio buffer.
SB=""; command -v stdbuf >/dev/null 2>&1 && SB="stdbuf -oL"

cap() {   # cap <section> <instruction>
    sec="$1"; shift
    [ -n "$ONLY" ] && [ "$ONLY" != "$sec" ] && return
    printf '\n>>> %s\n    press ENTER, do it, then press ENTER again (s = skip) ' "$*"
    read ans; [ "$ans" = "s" ] && return
    tmp=$(mktemp)
    $SB aseqdump -p "$PORT" > "$tmp" 2>/dev/null &
    pid=$!
    printf '    recording... ENTER when done '
    read _
    kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
    n=$(grep -vc -e '^Waiting' -e '^Source' "$tmp")
    printf '    %s events\n' "$n"
    { echo "=== [$sec] $*"; grep -v -e '^Waiting' -e '^Source' "$tmp"; } >> "$OUT"
    rm -f "$tmp"
}

echo "Capturing from $PORT into $OUT"
echo "Do ONE thing per prompt and nothing else. Release buttons before pressing ENTER."

# ---- idle: does the GRV6 send anything on its own? (keepalive question, PORTING 6.3 step 5) ----
cap idle "Touch NOTHING for 60 seconds"

# ---- deck 1, every control (the reference deck) ----
cap deck1 "Deck 1: PLAY/PAUSE once"
cap deck1 "Deck 1: CUE once"
cap deck1 "Deck 1: BEAT SYNC once (short press)"
cap deck1 "Deck 1: BEAT SYNC long press (hold ~2 s)"
cap deck1 "Deck 1: SHIFT alone"
cap deck1 "Deck 1: SHIFT + PLAY"
cap deck1 "Deck 1: SHIFT + CUE"
cap deck1 "Deck 1: touch the TOP of the jog, then let go (do not turn)"
cap deck1 "Deck 1: turn the jog by its TOP exactly ONE full turn clockwise"
cap deck1 "Deck 1: turn the jog by its TOP exactly TEN full turns clockwise, steadily (counts per revolution)"
cap deck1 "Deck 1: turn the jog by its SIDE exactly ONE full turn clockwise"
cap deck1 "Deck 1: turn the jog by its SIDE a little counter-clockwise"
cap deck1 "Deck 1: SHIFT held + turn the jog a little"
cap deck1 "Deck 1: TEMPO fader all the way to one end, then the other, then centre"
cap deck1 "Deck 1: loop IN"
cap deck1 "Deck 1: loop OUT"
cap deck1 "Deck 1: 4 BEAT / EXIT (or RELOOP)"
cap deck1 "Deck 1: CUE/LOOP CALL <"
cap deck1 "Deck 1: CUE/LOOP CALL >"
cap deck1 "Deck 1: any remaining deck button (e.g. KEY SYNC, MASTER TEMPO, DECK select) -- press each once, in order, and note the order"

# ---- decks 2-4: only enough to learn the channels ----
cap decks "Deck 2: PLAY once"
cap decks "Deck 2: turn the jog a little"
cap decks "Switch the LEFT side to DECK 3, then press PLAY once"
cap decks "Switch the RIGHT side to DECK 4, then press PLAY once (then switch both back to 1 and 2)"

# ---- mixer ----
cap mixer "CH1: channel fader bottom -> top -> bottom"
cap mixer "CH1: TRIM full sweep"
cap mixer "CH1: EQ HI full sweep"
cap mixer "CH1: EQ MID full sweep"
cap mixer "CH1: EQ LOW full sweep"
cap mixer "CH1: Sound Color FX / filter knob full sweep"
cap mixer "CH1: channel CUE button once"
cap mixer "CH2: channel fader up and down"
cap mixer "CH3: channel fader up and down"
cap mixer "CH4: channel fader up and down"
cap mixer "CH3: channel CUE button once"
cap mixer "Crossfader left -> right -> centre"
cap mixer "MASTER LEVEL full sweep"
cap mixer "BOOTH LEVEL full sweep"
cap mixer "HEADPHONES LEVEL full sweep"
cap mixer "HEADPHONES CUE/MASTER MIX full sweep"
cap mixer "MASTER CUE button once"
cap mixer "Any Sound Color FX type buttons, each once, in order (skip if none)"

# ---- browse / load ----
cap browse "Browse knob: turn 3 clicks clockwise, then 3 counter-clockwise"
cap browse "Browse knob: push once"
cap browse "Browse knob: tilt UP, DOWN, LEFT, RIGHT -- in that order"
cap browse "LOAD deck 1 (left LOAD button)"
cap browse "LOAD deck 2 (right LOAD button)"
cap browse "Any other browse-area button (BACK, VIEW, etc.) -- each once, in order"

# ---- Beat FX ----
cap fx "Beat FX: FX SELECT (turn or press, a few steps)"
cap fx "Beat FX: BEAT < then BEAT >"
cap fx "Beat FX: each CH SELECT position, left to right"
cap fx "Beat FX: LEVEL/DEPTH full sweep"
cap fx "Beat FX: FX ON/OFF once"

# ---- pads (deck 1) ----
cap pads "Deck 1: press each PAD MODE button once, left to right, top row then bottom"
cap pads "Deck 1: select HOT CUE mode, then press pads 1..8 in order"
cap pads "Deck 1: select the 2nd pad mode, then pads 1..8"
cap pads "Deck 1: select the 3rd pad mode, then pads 1..8"
cap pads "Deck 1: select the 4th pad mode, then pads 1..8"
cap pads "Deck 1: back in HOT CUE mode, SHIFT + pad 1"
cap pads "Deck 2: HOT CUE mode, pad 1 once"

# ---- shift combos on the browse/load side ----
cap shift "SHIFT + browse push"
cap shift "SHIFT + LOAD deck 1"
cap shift "SHIFT + LOAD deck 2"
cap shift "Anything not covered yet (Groove Circuit, sampler, etc.) -- each once, say the order in chat"

echo
echo "Done. File: $OUT  ($(grep -c '^===' "$OUT") blocks)"
echo "Audio interface, for rb.conf:"
cat /proc/asound/DDJGRV6/stream0 2>/dev/null | head -30