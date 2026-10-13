#!/bin/sh
# grv6-ledprobe.sh -- guided LED probe for the DDJ-GRV6 (PORTING 6.3 step 4, G8).
# Lights one candidate LED at a time from the host, asks whether the button you
# were told to watch lit, and writes the answers into one file for writing the
# GRV6 map's LED table.
#
# Usage on the Pi (seqinject2 must sit beside this script, or set SEQINJECT=):
#   sh grv6-ledprobe.sh                  all sections, in order
#   sh grv6-ledprobe.sh deck1            one section: deck1 deck2 modes pads padcolour
#                                        mixer fx browse meter
#   sh grv6-ledprobe.sh pair <ch> <n> [vel]   light one note 3 s, then off (no question)
#   sh grv6-ledprobe.sh sweep <ch> [from] [to] light every note in turn, 1.5 s each,
#                                        printing its number (watch the panel)
# Output: ~/grv6-leds.txt (appended; delete it first for a fresh run). One line
# per answer:  section|label|ch|note|vel|answer|what you saw
#
# Why it works this way (measured on the FLX4, docs/15 "Measuring the notes"):
#  - the route is the sequencer (amidi is EBUSY while the kernel holds the port);
#  - a control's LED was its OWN input note on its OWN input channel, so every
#    candidate below is a measured GRV6 input from docs/19 -- a hypothesis until
#    you see it light;
#  - the panel wanted the message REPEATED: one message lit 1 LED in 4, the same
#    held 3 s and re-sent every 100 ms lit all 4. So each candidate is streamed;
#  - "off" is a Note On with velocity 0 (the FLX4 ignored a real Note Off).
# A write to the device port does not come back as a button press (also measured
# there), so this is safe with the player running.

OUT="$HOME/grv6-leds.txt"
HERE=$(cd "$(dirname "$0")" && pwd)
SEQ="${SEQINJECT:-$HERE/seqinject2}"
[ -x "$SEQ" ] || { echo "seqinject2 not found at $SEQ (copy scripts/shims/seqinject2 beside this script, chmod +x)"; exit 1; }
PORT=$(aseqdump -l 2>/dev/null | awk '/DDJ-GRV6 MIDI 1/ {print $1; exit}')
[ -n "$PORT" ] || { echo "DDJ-GRV6 MIDI port not found (aseqdump -l)"; exit 1; }
HOLD="${HOLD:-3}"    # seconds each candidate is held lit

send()  { "$SEQ" --dest "$PORT" note "$1" "$2" on "$3" >/dev/null 2>&1; }
# light <ch> <note> <vel> <secs>: stream the Note On every 100 ms, then send off
light() {
    i=0; n=$(( $4 * 10 ))
    while [ "$i" -lt "$n" ]; do send "$1" "$2" "$3"; sleep 0.1; i=$((i + 1)); done
    send "$1" "$2" 0; send "$1" "$2" 0
}

ONLY="$1"
case "$1" in
  pair)  [ -n "$3" ] || { echo "usage: $0 pair <ch> <note> [vel]"; exit 1; }
         echo "lighting ch $2 note $3 vel ${4:-127} for $HOLD s on $PORT"
         light "$2" "$3" "${4:-127}" "$HOLD"; exit 0 ;;
  sweep) [ -n "$2" ] || { echo "usage: $0 sweep <ch> [from] [to]"; exit 1; }
         n=${3:-0}; last=${4:-127}
         echo "sweeping ch $2 notes $n..$last on $PORT, 1.5 s each: note the numbers that light something"
         while [ "$n" -le "$last" ]; do
             printf '  ch %s note %s\n' "$2" "$n"
             i=0; while [ "$i" -lt 15 ]; do send "$2" "$n" 127; sleep 0.1; i=$((i + 1)); done
             send "$2" "$n" 0; n=$((n + 1))
         done; exit 0 ;;
esac

# ask <section> <label> <ch> <note> [vel] [watch-instruction]
ask() {
    sec="$1"; label="$2"; ch="$3"; note="$4"; vel="${5:-127}"; watch="${6:-$2}"
    [ -n "$ONLY" ] && [ "$ONLY" != "$sec" ] && return
    while :; do
        printf '\n>>> [%s] watch: %s   (ch %s note %s vel %s)\n    ENTER to light it (s = skip) ' "$sec" "$watch" "$ch" "$note" "$vel"
        read go; [ "$go" = "s" ] && { echo "$sec|$label|$ch|$note|$vel|skip|" >> "$OUT"; return; }
        light "$ch" "$note" "$vel" "$HOLD"
        printf '    did it light?  y = yes   n = nothing   o = something ELSE lit   r = again : '
        read a
        case "$a" in
          r) continue ;;
          y) printf '    anything to add (colour, blinking)? ENTER for nothing: '; read t
             echo "$sec|$label|$ch|$note|$vel|yes|$t" >> "$OUT"; return ;;
          o) printf '    what lit instead? '; read t
             echo "$sec|$label|$ch|$note|$vel|other|$t" >> "$OUT"; return ;;
          *) echo "$sec|$label|$ch|$note|$vel|no|" >> "$OUT"; return ;;
        esac
    done
}

echo "Probing LEDs on $PORT; answers go to $OUT"
echo "Each candidate is held lit for $HOLD s. Look at the button named after 'watch:'."
echo "Leave the GRV6 on decks 1 and 2 (not 3/4) for the whole run."

# ---- deck 1 (ch 0): the buttons rbp has a light for, then the rest ----
ask deck1 "PLAY"          0 11
ask deck1 "CUE"           0 12
ask deck1 "BEAT SYNC"     0 88
ask deck1 "LOOP IN"       0 16
ask deck1 "LOOP OUT"      0 17
ask deck1 "4 BEAT/EXIT"   0 77
ask deck1 "MASTER TEMPO"  0 26
ask deck1 "KEY SYNC"      0 101
ask deck1 "MEMORY"        0 61
ask deck1 "CALL <"        0 81 127 "CUE/LOOP CALL <"
ask deck1 "CALL >"        0 83 127 "CUE/LOOP CALL >"
ask deck1 "SHIFT"         0 63
ask deck1 "DECK select"   0 114 127 "the left side's DECK 1/3 button"
ask deck1 "jog"           0 54 127 "the deck 1 jog (any light on or around it)"

# ---- deck 2 (ch 1): only the rule check, the rest follows from deck 1 ----
ask deck2 "PLAY"          1 11 127 "deck 2 PLAY"
ask deck2 "BEAT SYNC"     1 88 127 "deck 2 BEAT SYNC"

# ---- pad mode buttons (deck 1, ch 0) ----
ask modes "HOT CUE"       0 27 127 "deck 1 pad mode button HOT CUE"
ask modes "STEMS"         0 30 127 "deck 1 pad mode button STEMS"
ask modes "BEAT JUMP"     0 32 127 "deck 1 pad mode button BEAT JUMP"
ask modes "SAMPLER"       0 34 127 "deck 1 pad mode button SAMPLER"

# ---- pads: deck 1 must be in HOT CUE mode on the controller for these ----
ask pads "pad 1 (HOT CUE)" 7 0 127 "deck 1 PAD 1 (press HOT CUE on the GRV6 first)"
ask pads "pad 8 (HOT CUE)" 7 7 127 "deck 1 PAD 8"
ask pads "pad 1 deck 2"    9 0 127 "deck 2 PAD 1 (deck 2 in HOT CUE mode)"
ask pads "pad 1 SHIFT ch"  8 0 127 "deck 1 PAD 1 (the +SHIFT channel)"
ask pads "pad 1 BEAT JUMP" 7 32 127 "deck 1 PAD 1 (press BEAT JUMP on the GRV6 first)"

# ---- pad colour: velocity is the colour on many panels. Display only. ----
if [ -z "$ONLY" ] || [ "$ONLY" = padcolour ]; then
    printf '\n>>> [padcolour] deck 1 in HOT CUE mode. PAD 1 is lit at each velocity for 2 s.\n    Write down the colour for each number. ENTER to start (s = skip) '
    read go
    if [ "$go" != "s" ]; then
        for v in 1 8 16 24 32 40 48 56 64 72 80 88 96 104 112 120 127; do
            printf '  vel %s\n' "$v"; light 7 0 "$v" 2
        done
        printf '    colours, as "vel=colour, ..." (or a short description): '; read t
        echo "padcolour|pad 1 velocity sweep|7|0|1..127|seen|$t" >> "$OUT"
    fi
fi

# ---- mixer (CH CUE on the deck channel, MASTER CUE on ch 6) ----
ask mixer "CH1 CUE"       0 84 127 "the channel CUE (headphone) button of CH1"
ask mixer "CH2 CUE"       1 84 127 "the channel CUE button of CH2"
ask mixer "MASTER CUE"    6 99

# ---- Beat FX (ch 4) ----
ask fx "FX ON/OFF"        4 71 127 "Beat FX ON/OFF"
ask fx "FX CH 1"          4 16 127 "FX channel select button 1"
ask fx "FX CH 2"          4 17 127 "FX channel select button 2"
ask fx "FX CH MST"        4 20 127 "FX channel select button MST"
ask fx "FX knob ECHO"     4 33 127 "the Beat FX knob (any position light)"

# ---- browse area (ch 6) ----
ask browse "LOAD 1"       6 70 127 "LOAD deck 1"
ask browse "BACK"         6 101
ask browse "VIEW"         6 122
ask browse "DISCOVER"     6 53

# ---- meter: the FLX4's channel meter was CC 2 on the deck channel ----
if [ -z "$ONLY" ] || [ "$ONLY" = meter ]; then
    printf '\n>>> [meter] watch the CH1 level meter: CC 2 on ch 0 ramps 0 -> 127 -> 0. ENTER (s = skip) '
    read go
    if [ "$go" != "s" ]; then
        for v in 0 16 32 48 64 80 96 112 127 112 96 80 64 48 32 16 0; do
            "$SEQ" --dest "$PORT" cc 0 2 "$v" >/dev/null 2>&1; sleep 0.3
        done
        printf '    did the CH1 meter move?  y / n / o (something else) : '; read a
        printf '    details: '; read t
        echo "meter|CH1 meter CC 2|0|cc2|0..127|$a|$t" >> "$OUT"
    fi
fi

echo
echo "done: $(wc -l < "$OUT") answers in $OUT"
