#!/usr/bin/env bash
# End-to-end check: the reviewed tree's tools/serial_config.py against the
# reviewed tree's firmware, through the PTY bridge. Host build, not hardware.
#   TREE=/path/to/reviewed/tree BRIDGE=/path/to/serial_pty_bridge ./serial_e2e.sh
set -u
TREE=${TREE:?set TREE to the reviewed tree}
BRIDGE=${BRIDGE:?set BRIDGE to the built serial_pty_bridge}
WORK=$(mktemp -d)
cd "$TREE" || exit 2
"$BRIDGE" > "$WORK/pts" 2>"$WORK/bridge.err" &
BRIDGE_PID=$!
trap 'kill $BRIDGE_PID 2>/dev/null; rm -rf "$WORK"' EXIT
for _ in $(seq 50); do [ -s "$WORK/pts" ] && break; sleep 0.1; done
DEV=$(head -1 "$WORK/pts")
echo "device: $DEV"
sleep 0.3
fails=0
run() { python3 tools/serial_config.py "$@"; }
expect_state() {  # $1 = json file, $2 = python expression over d
  python3 -c "import json,sys; d=json.load(open('$1')); sys.exit(0 if ($2) else 1)"
}

echo "--- 1. get ---"
run "$DEV" get > "$WORK/a.json" 2>"$WORK/a.err"; rc=$?
echo "exit=$rc err=$(cat "$WORK/a.err")"
if [ $rc -eq 0 ] && expect_state "$WORK/a.json" "d['count']==60 and d['timezone']=='UTC0'"; then
  echo "PASS get returns firmware defaults"
else echo "FAIL get"; fails=$((fails+1)); fi

echo "--- 2. set (chunked) ---"
python3 -c "import json; d=json.load(open('$WORK/a.json')); d['count']=72; json.dump(d,open('$WORK/b.json','w'))"
run "$DEV" set "$WORK/b.json" > "$WORK/b.out" 2>"$WORK/b.err"; rc=$?
echo "exit=$rc out=$(cat "$WORK/b.out") err=$(cat "$WORK/b.err")"
if [ $rc -eq 0 ]; then echo "PASS set succeeded"; else echo "FAIL set"; fails=$((fails+1)); fi
run "$DEV" get > "$WORK/c.json" 2>/dev/null
if expect_state "$WORK/c.json" "d['count']==72"; then echo "PASS read-back shows 72"; else echo "FAIL read-back"; fails=$((fails+1)); fi

echo "--- 3. invalid document ---"
python3 -c "import json; d=json.load(open('$WORK/c.json')); d['count']=999; json.dump(d,open('$WORK/d.json','w'))"
run "$DEV" set "$WORK/d.json" > /dev/null 2>"$WORK/d.err"; rc=$?
echo "exit=$rc err=$(cat "$WORK/d.err")"
if [ $rc -ne 0 ]; then echo "PASS invalid document rejected"; else echo "FAIL invalid accepted"; fails=$((fails+1)); fi
run "$DEV" get > "$WORK/e.json" 2>/dev/null
if expect_state "$WORK/e.json" "d['count']==72"; then echo "PASS rejected document changed nothing"; else echo "FAIL state changed"; fails=$((fails+1)); fi

echo "--- 4. time zone change ---"
python3 -c "import json; d=json.load(open('$WORK/e.json')); d['timezone']='EST5EDT,M3.2.0/2,M11.1.0/2'; json.dump(d,open('$WORK/f.json','w'))"
run "$DEV" set "$WORK/f.json" > /dev/null 2>"$WORK/f.err"
run "$DEV" get > "$WORK/g.json" 2>/dev/null
if expect_state "$WORK/g.json" "d['timezone'].startswith('EST5EDT')"; then
  echo "PASS time zone applied end to end"
else echo "FAIL time zone"; fails=$((fails+1)); fi

echo "--- 5. missing device ---"
python3 tools/serial_config.py /dev/does-not-exist get > /dev/null 2>"$WORK/h.err"; rc=$?
echo "exit=$rc err=$(cat "$WORK/h.err")"
if [ $rc -ne 0 ]; then echo "PASS missing device reported"; else echo "FAIL missing device"; fails=$((fails+1)); fi

echo
echo "e2e failures: $fails"
exit $((fails > 0))
