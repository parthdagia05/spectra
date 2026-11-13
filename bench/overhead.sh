#!/bin/bash
set -euo pipefail

SPECTRA="${1:-./build/spectra}"
RUNS=100

echo "spectra benchmark suite"
echo "====================="
echo ""

# 1. Local scan latency
echo "1. Local scan latency ($RUNS runs)"
times=()
for i in $(seq 1 $RUNS); do
    t=$( { time "$SPECTRA" scan --json > /dev/null 2>&1; } 2>&1 )
    real=$(echo "$t" | grep real | awk '{print $2}')
    # convert to milliseconds
    min=$(echo "$real" | sed 's/m.*//')
    sec=$(echo "$real" | sed 's/.*m//' | sed 's/s//')
    ms=$(echo "$min $sec" | awk '{printf "%.0f", ($1*60 + $2)*1000}')
    times+=("$ms")
    printf "\r  Run %d/%d" "$i" "$RUNS"
done
echo ""

# sort and compute percentiles
sorted=($(printf '%s\n' "${times[@]}" | sort -n))
n=${#sorted[@]}
p50=${sorted[$((n*50/100))]}
p95=${sorted[$((n*95/100))]}
p99=${sorted[$((n*99/100))]}
echo "  p50: ${p50}ms  p95: ${p95}ms  p99: ${p99}ms"
echo ""

# 2. Memory footprint
echo "2. Memory footprint"
if command -v /usr/bin/time &>/dev/null; then
    /usr/bin/time -l "$SPECTRA" scan --json > /dev/null 2>&1 || true
elif command -v time &>/dev/null; then
    # macOS: use /usr/bin/time -l
    /usr/bin/time -l "$SPECTRA" scan --json > /dev/null 2>/tmp/spectra_mem.txt || true
    rss=$(grep "maximum resident" /tmp/spectra_mem.txt | awk '{print $1}')
    echo "  Peak RSS: $((rss / 1024)) KB"
fi
echo ""

# 3. Android scan latency (if device connected)
echo "3. Android scan latency"
if adb get-state >/dev/null 2>&1; then
    ANDROID_RUNS=10
    atimes=()
    for i in $(seq 1 $ANDROID_RUNS); do
        t=$( { time "$SPECTRA" scan --android --json > /dev/null 2>&1; } 2>&1 )
        real=$(echo "$t" | grep real | awk '{print $2}')
        min=$(echo "$real" | sed 's/m.*//')
        sec=$(echo "$real" | sed 's/.*m//' | sed 's/s//')
        ms=$(echo "$min $sec" | awk '{printf "%.0f", ($1*60 + $2)*1000}')
        atimes+=("$ms")
        printf "\r  Run %d/%d" "$i" "$ANDROID_RUNS"
    done
    echo ""
    sorted_a=($(printf '%s\n' "${atimes[@]}" | sort -n))
    na=${#sorted_a[@]}
    echo "  p50: ${sorted_a[$((na*50/100))]}ms  p95: ${sorted_a[$((na*95/100))]}ms"
else
    echo "  No Android device connected, skipping"
fi
echo ""

echo "Done."
