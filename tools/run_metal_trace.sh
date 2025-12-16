#!/usr/bin/env bash
set -euo pipefail

# Quick helper for building and running the Metal trace-dump target
# for A-Train HX with a short timeout and filtered logs.
#
# Usage:
#   tools/run_metal_trace.sh [trace_file] [grep_pattern] [timeout_seconds]
#
# Defaults:
#   trace_file     = reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace
#   grep_pattern   = "MetalRenderTargetCache|BG_DEST_DEBUG|BG_OVERLAP_DEBUG"
#   timeout        = 8 seconds
#
# The script:
#   1. Creates a timestamped output folder in scratch/metal_trace_runs/
#   2. Builds xenia-gpu-metal-trace-dump, suppressing verbose output.
#   3. Runs it with configurable timeout.
#   4. Writes full log, build log, and output PNG to the timestamped folder.
#   5. Prints only lines matching the grep pattern (with small context).
#   6. Enables programmatic GPU capture (saved to output folder).
#
# Output folder structure:
#   scratch/metal_trace_runs/YYYYMMDD_HHMMSS/
#     ├── build.log
#     ├── trace.log  
#     ├── output.png (copied from generated PNG)
#     ├── gpu_capture_0000.gputrace (if capture succeeded)
#     ├── shaders/
#     │   ├── shader_*_vs.dxbc
#     │   ├── shader_*_vs.dxil
#     │   ├── shader_*_vs.metallib
#     │   ├── shader_*_ps.dxbc
#     │   ├── shader_*_ps.dxil
#     │   └── shader_*_ps.metallib
#     ├── textures/
#     │   └── texture_*.png
#     └── summary.txt

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

TRACE_FILE="${1:-reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace}"
GREP_PATTERN="${2:-MetalRenderTargetCache|BG_DEST_DEBUG|BG_OVERLAP_DEBUG}"
TIMEOUT_SECS="${3:-8}"

# Create timestamped output folder with subdirectories
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
OUTPUT_DIR="scratch/metal_trace_runs/${TIMESTAMP}"
SHADERS_DIR="${OUTPUT_DIR}/shaders"
TEXTURES_DIR="${OUTPUT_DIR}/textures"
mkdir -p "${OUTPUT_DIR}" "${SHADERS_DIR}" "${TEXTURES_DIR}"

# Set up file paths
BUILD_LOG="${OUTPUT_DIR}/build.log"
LOG_FILE="${OUTPUT_DIR}/trace.log"
SUMMARY_FILE="${OUTPUT_DIR}/summary.txt"

# Also keep a symlink to latest run for convenience
LATEST_LINK="scratch/metal_trace_runs/latest"
rm -f "${LATEST_LINK}" 2>/dev/null || true
ln -sf "${TIMESTAMP}" "${LATEST_LINK}"

echo "[run_metal_trace] Output folder: ${OUTPUT_DIR}"
echo "Run started at: $(date)" > "${SUMMARY_FILE}"
echo "Trace file: ${TRACE_FILE}" >> "${SUMMARY_FILE}"
echo "Grep pattern: ${GREP_PATTERN}" >> "${SUMMARY_FILE}"
echo "Timeout: ${TIMEOUT_SECS}s" >> "${SUMMARY_FILE}"
echo "" >> "${SUMMARY_FILE}"

echo "[run_metal_trace] Building xenia-gpu-metal-trace-dump (this may take a while)..."
# Suppress detailed build output; only report success/failure.
if ./xb build --target=xenia-gpu-metal-trace-dump >"${BUILD_LOG}" 2>&1; then
  echo "[run_metal_trace] Build succeeded. Build log: ${BUILD_LOG}"
  echo "Build: SUCCESS" >> "${SUMMARY_FILE}"
else
  echo "[run_metal_trace] Build FAILED. Showing relevant errors from ${BUILD_LOG}:" >&2
  echo "Build: FAILED" >> "${SUMMARY_FILE}"
  if command -v rg >/dev/null 2>&1; then
    rg -n -C5 "error:|fatal error|Undefined symbols for architecture|ld: error" "${BUILD_LOG}" || \
      echo "[run_metal_trace] No obvious error lines found in build log." >&2
  else
    grep -nE -C5 "error:|fatal error|Undefined symbols for architecture|ld: error" "${BUILD_LOG}" || \
      echo "[run_metal_trace] No obvious error lines found in build log." >&2
  fi
  exit 1
fi

# Run the trace-dump with configurable timeout.
# Use Python's subprocess timeout for portability across macOS setups.
BIN="${ROOT_DIR}/build/bin/Mac/Checked/xenia-gpu-metal-trace-dump"
if [[ ! -x "${BIN}" ]]; then
  echo "[run_metal_trace] ERROR: Binary not found at ${BIN}" >&2
  exit 1
fi

echo "[run_metal_trace] Running trace-dump on ${TRACE_FILE} (timeout ~${TIMEOUT_SECS}s)..."

# Export environment variables for the trace dump
# These enable programmatic GPU capture and set output directories
export XENIA_GPU_CAPTURE_DIR="${ROOT_DIR}/${OUTPUT_DIR}"
export XENIA_TEXTURE_DUMP_DIR="${ROOT_DIR}/${TEXTURES_DIR}"
export XENIA_SHADER_DUMP_DIR="${ROOT_DIR}/${SHADERS_DIR}"
export XENIA_GPU_CAPTURE_ENABLED="1"
export XENIA_DUMP_SHADERS="1"
export XENIA_DUMP_TEXTURES="1"

# Enable Metal capture layer for programmatic GPU capture
# This is required for MTLCaptureManager to work outside of Xcode
export MTL_CAPTURE_ENABLED="1"

export ROOT_DIR_ENV="${ROOT_DIR}"
export LOG_FILE_ENV="${LOG_FILE}"
export TRACE_FILE_ENV="${TRACE_FILE}"
export BIN_ENV="${BIN}"
export OUTPUT_DIR_ENV="${OUTPUT_DIR}"
export TIMEOUT_SECS_ENV="${TIMEOUT_SECS}"

python3 - << 'EOF'
import os
import subprocess
import sys

log_file = os.environ.get("LOG_FILE_ENV")
trace_file = os.environ.get("TRACE_FILE_ENV")
bin_path = os.environ.get("BIN_ENV")
output_dir = os.environ.get("OUTPUT_DIR_ENV")
timeout_secs = int(os.environ.get("TIMEOUT_SECS_ENV", "8"))

if not bin_path:
    print("[run_metal_trace] ERROR: BIN_ENV not set")
    raise SystemExit(1)

cmd = [bin_path, f"--target_trace_file={trace_file}", f"--log_file={log_file}", "--log_level=4"]

# Suppress the binary's stdout/stderr to avoid flooding the console; rely on
# the log file + filtered rg/grep output instead.
run_status = "UNKNOWN"
with open(os.devnull, 'wb') as devnull:
    try:
        proc = subprocess.run(cmd, check=False, timeout=timeout_secs,
                              stdout=devnull, stderr=devnull)
        if proc.returncode == 0:
            print(f"[run_metal_trace] Trace-dump completed successfully")
            run_status = "SUCCESS"
        else:
            print(f"[run_metal_trace] Trace-dump exited with code {proc.returncode}")
            run_status = f"EXITED_CODE_{proc.returncode}"
    except subprocess.TimeoutExpired:
        print(f"[run_metal_trace] Trace-dump TIMED OUT after {timeout_secs} seconds; killing process.")
        run_status = "TIMEOUT"

# Write status to summary
summary_path = os.path.join(os.environ.get("ROOT_DIR_ENV", "."), output_dir, "summary.txt")
with open(summary_path, "a") as f:
    f.write(f"Run status: {run_status}\n")
EOF

if [[ ! -f "${LOG_FILE}" ]]; then
  echo "[run_metal_trace] No log file produced at ${LOG_FILE}." >&2
  exit 1
fi

# Collect output PNG - look for generated PNG in root directory
TRACE_BASENAME="$(basename "${TRACE_FILE}" .xenia_gpu_trace)"
if [[ -f "${ROOT_DIR}/${TRACE_BASENAME}.png" ]]; then
  cp "${ROOT_DIR}/${TRACE_BASENAME}.png" "${OUTPUT_DIR}/output.png"
  echo "[run_metal_trace] Copied output PNG to ${OUTPUT_DIR}/output.png"
  echo "Output PNG: collected" >> "${SUMMARY_FILE}"
else
  echo "[run_metal_trace] No output PNG found at ${ROOT_DIR}/${TRACE_BASENAME}.png"
  echo "Output PNG: not found" >> "${SUMMARY_FILE}"
fi

# Count shaders dumped directly to SHADERS_DIR via XENIA_SHADER_DUMP_DIR env var
SHADER_COUNT=$(find "${SHADERS_DIR}" -name "shader_*" -type f 2>/dev/null | wc -l | tr -d ' ')
# Also collect any from /tmp (legacy location) if they exist
shopt -s nullglob
for f in /tmp/xenia_shader_*.metallib /tmp/xenia_shader_*.dxbc /tmp/xenia_shader_*.dxil; do
  if [[ -f "$f" ]]; then
    mv "$f" "${SHADERS_DIR}/"
    ((SHADER_COUNT++)) || true
  fi
done
shopt -u nullglob
echo "[run_metal_trace] Found ${SHADER_COUNT} shader files in ${SHADERS_DIR}/"
echo "Shaders collected: ${SHADER_COUNT}" >> "${SUMMARY_FILE}"

# Collect any textures dumped
TEXTURE_COUNT=0
shopt -s nullglob
for f in /tmp/xenia_tex_*.raw /tmp/xenia_tex_*.png; do
  if [[ -f "$f" ]]; then
    cp "$f" "${TEXTURES_DIR}/"
    ((TEXTURE_COUNT++)) || true
  fi
done
# Also collect GPU texture dumps from root
for f in "${ROOT_DIR}"/gpu_texture_*.png; do
  if [[ -f "$f" ]]; then
    cp "$f" "${TEXTURES_DIR}/"
    ((TEXTURE_COUNT++)) || true
  fi
done
shopt -u nullglob
echo "[run_metal_trace] Collected ${TEXTURE_COUNT} texture files to ${TEXTURES_DIR}/"
echo "Textures collected: ${TEXTURE_COUNT}" >> "${SUMMARY_FILE}"

# Collect GPU trace if present (note: .gputrace is a directory bundle)
GPUTRACE_COUNT=0
shopt -s nullglob
for f in "${OUTPUT_DIR}"/*.gputrace "${ROOT_DIR}"/*.gputrace; do
  if [[ -d "$f" ]] && [[ "$f" != "${OUTPUT_DIR}"/* ]]; then
    mv "$f" "${OUTPUT_DIR}/"
    ((GPUTRACE_COUNT++)) || true
  elif [[ -d "$f" ]]; then
    ((GPUTRACE_COUNT++)) || true
  fi
done
shopt -u nullglob
echo "GPU traces: ${GPUTRACE_COUNT}" >> "${SUMMARY_FILE}"
if [[ ${GPUTRACE_COUNT} -gt 0 ]]; then
  echo "[run_metal_trace] Found ${GPUTRACE_COUNT} GPU trace(s) - open with: open ${OUTPUT_DIR}/*.gputrace"
fi

# Finalize summary
echo "" >> "${SUMMARY_FILE}"
echo "Run completed at: $(date)" >> "${SUMMARY_FILE}"

# Filter log output to only the lines we care about, with small context.
echo ""
echo "[run_metal_trace] Filtered log (pattern: ${GREP_PATTERN}) from ${LOG_FILE}:"
if command -v rg >/dev/null 2>&1; then
  rg -n -C5 "${GREP_PATTERN}" "${LOG_FILE}" || echo "[run_metal_trace] No matches found."
else
  grep -nE -C5 "${GREP_PATTERN}" "${LOG_FILE}" || echo "[run_metal_trace] No matches found."
fi

echo ""
echo "[run_metal_trace] Summary:"
cat "${SUMMARY_FILE}"
echo ""
echo "[run_metal_trace] All artifacts saved to: ${OUTPUT_DIR}"
