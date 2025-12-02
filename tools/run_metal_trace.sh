#!/usr/bin/env bash
set -euo pipefail

# Quick helper for building and running the Metal trace-dump target
# for A-Train HX with a short timeout and filtered logs.
#
# Usage:
#   tools/run_metal_trace.sh [trace_file] [grep_pattern]
#
# Defaults:
#   trace_file   = reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace
#   grep_pattern = "MetalRenderTargetCache|BG_DEST_DEBUG|BG_OVERLAP_DEBUG"
#
# The script:
#   1. Builds xenia-gpu-metal-trace-dump, suppressing verbose output.
#   2. Runs it with an ~8 second timeout.
#   3. Writes a full log to tools/run_metal_trace.log.
#   4. Prints only lines matching the grep pattern (with small context).

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

TRACE_FILE="${1:-reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace}"
GREP_PATTERN="${2:-MetalRenderTargetCache|BG_DEST_DEBUG|BG_OVERLAP_DEBUG}"

LOG_FILE="tools/run_metal_trace.log"

echo "[run_metal_trace] Building xenia-gpu-metal-trace-dump (this may take a while)..."
# Suppress detailed build output; only report success/failure.
if ./xb build --target=xenia-gpu-metal-trace-dump >"${LOG_FILE}.build" 2>&1; then
  echo "[run_metal_trace] Build succeeded. Build log: ${LOG_FILE}.build"
else
  echo "[run_metal_trace] Build FAILED. Showing relevant errors from ${LOG_FILE}.build:" >&2
  if command -v rg >/dev/null 2>&1; then
    rg -n -C5 "error:|fatal error|Undefined symbols for architecture|ld: error" "${LOG_FILE}.build" || \
      echo "[run_metal_trace] No obvious error lines found in build log." >&2
  else
    grep -nE -C5 "error:|fatal error|Undefined symbols for architecture|ld: error" "${LOG_FILE}.build" || \
      echo "[run_metal_trace] No obvious error lines found in build log." >&2
  fi
  exit 1
fi

# Run the trace-dump with an ~8 second timeout.
# Use Python's subprocess timeout for portability across macOS setups.
BIN="${ROOT_DIR}/build/bin/Mac/Checked/xenia-gpu-metal-trace-dump"
if [[ ! -x "${BIN}" ]]; then
  echo "[run_metal_trace] ERROR: Binary not found at ${BIN}" >&2
  exit 1
fi

echo "[run_metal_trace] Running trace-dump on ${TRACE_FILE} (timeout ~8s)..."

export ROOT_DIR_ENV="${ROOT_DIR}"
export LOG_FILE_ENV="${LOG_FILE}"
export TRACE_FILE_ENV="${TRACE_FILE}"
export BIN_ENV="${BIN}"

python3 - << 'EOF'
import os
import subprocess
import sys

log_file = os.environ.get("LOG_FILE_ENV")
trace_file = os.environ.get("TRACE_FILE_ENV")
bin_path = os.environ.get("BIN_ENV")

if not bin_path:
    print("[run_metal_trace] ERROR: BIN_ENV not set")
    raise SystemExit(1)

cmd = [bin_path, f"--target_trace_file={trace_file}", f"--log_file={log_file}", "--log_level=4"]

# Suppress the binary's stdout/stderr to avoid flooding the console; rely on
# the log file + filtered rg/grep output instead.
with open(os.devnull, 'wb') as devnull:
    try:
        proc = subprocess.run(cmd, check=False, timeout=8,
                              stdout=devnull, stderr=devnull)
        if proc.returncode != 0:
            print(f"[run_metal_trace] Trace-dump exited with code {proc.returncode}")
    except subprocess.TimeoutExpired:
        print("[run_metal_trace] Trace-dump TIMED OUT after 8 seconds; killing process.")
EOF

if [[ ! -f "${LOG_FILE}" ]]; then
  echo "[run_metal_trace] No log file produced at ${LOG_FILE}." >&2
  exit 1
fi

# Filter log output to only the lines we care about, with small context.
echo "[run_metal_trace] Filtered log (pattern: ${GREP_PATTERN}) from ${LOG_FILE}:"
if command -v rg >/dev/null 2>&1; then
  rg -n -C5 "${GREP_PATTERN}" "${LOG_FILE}" || echo "[run_metal_trace] No matches found."
else
  grep -nE -C5 "${GREP_PATTERN}" "${LOG_FILE}" || echo "[run_metal_trace] No matches found."
fi
