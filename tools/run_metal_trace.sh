#!/usr/bin/env bash
set -euo pipefail

# Quick helper for building and running the Metal trace-dump target.
#
# Usage:
#   tools/run_metal_trace.sh [trace_path] [grep_pattern] [timeout_seconds] [capture_enabled] [output_root]
#
# Arguments:
#   trace_path     Path to a single .xenia_gpu_trace file OR a directory containing traces.
#                  If a directory is provided, all .xenia_gpu_trace files in it are processed.
#                  Defaults to: testdata/reference-gpu-traces/traces/
#   grep_pattern   Regex pattern to grep from logs (in addition to XELOG errors/warnings).
#                  Defaults to: "MetalRenderTargetCache|BG_DEST_DEBUG|BG_OVERLAP_DEBUG"
#   timeout        Timeout in seconds per trace. Default: 8.
#   capture        1 to enable programmatic GPU capture, 0 to disable. Default: 0.
#   output_root    Base output directory for trace dumps. Default: scratch/metal_trace_runs/
#
# The script:
#   1. Builds xenia-gpu-metal-trace-dump (once).
#   2. Runs the dump for the specified trace(s).
#   3. Generates a timestamped output directory in scratch/metal_trace_runs/.
#   4. Captures logs, highlighting XELOG[E|W|I] and custom patterns.
#   5. Summarizes results.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

# --- Configuration ---
INPUT_PATH="${1:-testdata/reference-gpu-traces/traces/}"
CUSTOM_GREP_PATTERN="${2:-MetalRenderTargetCache|BG_DEST_DEBUG|BG_OVERLAP_DEBUG|unsupported|not implemented|missing|failed}"
TIMEOUT_SECS="${3:-8}"
CAPTURE_ENABLED="${4:-0}"
OUTPUT_ROOT="${5:-scratch/metal_trace_runs}"

# Always grep for these important log levels
BASE_GREP_PATTERN="XELOG[EWI]"
# Combine with user pattern
FULL_GREP_PATTERN="${BASE_GREP_PATTERN}|${CUSTOM_GREP_PATTERN}"

# Create master timestamped output folder
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
if [[ "${OUTPUT_ROOT}" != /* ]]; then
  OUTPUT_ROOT="${ROOT_DIR}/${OUTPUT_ROOT}"
fi
MASTER_OUTPUT_DIR="${OUTPUT_ROOT}/${TIMESTAMP}"
mkdir -p "${MASTER_OUTPUT_DIR}"

# Symlink 'latest'
LATEST_LINK="${OUTPUT_ROOT}/latest"
rm -f "${LATEST_LINK}" 2>/dev/null || true
ln -sf "${TIMESTAMP}" "${LATEST_LINK}"

BUILD_LOG="${MASTER_OUTPUT_DIR}/build.log"
MASTER_SUMMARY="${MASTER_OUTPUT_DIR}/master_summary.txt"

echo "[run_metal_trace] Output folder: ${MASTER_OUTPUT_DIR}"
echo "Run started at: $(date)" > "${MASTER_SUMMARY}"
echo "Input path: ${INPUT_PATH}" >> "${MASTER_SUMMARY}"
echo "Output root: ${OUTPUT_ROOT}" >> "${MASTER_SUMMARY}"
echo "Grep pattern: ${FULL_GREP_PATTERN}" >> "${MASTER_SUMMARY}"
echo "Timeout: ${TIMEOUT_SECS}s" >> "${MASTER_SUMMARY}"
echo "GPU capture: ${CAPTURE_ENABLED}" >> "${MASTER_SUMMARY}"
echo "" >> "${MASTER_SUMMARY}"

# --- Build Step ---
echo "[run_metal_trace] Building xenia-gpu-metal-trace-dump..."
if ./xb build --target=xenia-gpu-metal-trace-dump --config=Checked >"${BUILD_LOG}" 2>&1; then
  echo "[run_metal_trace] Build succeeded."
  echo "Build: SUCCESS" >> "${MASTER_SUMMARY}"
else
  echo "[run_metal_trace] Build FAILED. See ${BUILD_LOG}" >&2
  echo "Build: FAILED" >> "${MASTER_SUMMARY}"
  if command -v rg >/dev/null 2>&1; then
    rg -n -C5 "error:|fatal error|ld: error" "${BUILD_LOG}" || true
  else
    grep -nE -C5 "error:|fatal error|ld: error" "${BUILD_LOG}" || true
  fi
  exit 1
fi

BIN="${ROOT_DIR}/build/bin/Mac/Checked/xenia-gpu-metal-trace-dump"
if [[ ! -x "${BIN}" ]]; then
  echo "[run_metal_trace] ERROR: Binary not found at ${BIN}" >&2
  exit 1
fi

# --- Helper Function to Run Single Trace ---
run_single_trace() {
  local t_file="$1"
  local t_name
  t_name="$(basename "${t_file}" .xenia_gpu_trace)"
  
  local run_dir="${MASTER_OUTPUT_DIR}/${t_name}"
  local shaders_dir="${run_dir}/shaders"
  local textures_dir="${run_dir}/textures"
  local log_file="${run_dir}/trace.log"
  local summary_file="${run_dir}/summary.txt"
  
  mkdir -p "${run_dir}" "${shaders_dir}" "${textures_dir}"

  echo "----------------------------------------------------------------"
  echo "Processing: ${t_name}"
  echo "Trace: ${t_file}"
  
  # Environment setup
  export XENIA_TEXTURE_DUMP_DIR="${textures_dir}"
  export XENIA_SHADER_DUMP_DIR="${shaders_dir}"
  export XENIA_DUMP_SHADERS="1"
  export XENIA_DUMP_TEXTURES="1"
  
  if [[ "${CAPTURE_ENABLED}" == "1" ]]; then
    export XENIA_GPU_CAPTURE_DIR="${run_dir}"
    export XENIA_GPU_CAPTURE_ENABLED="1"
    export MTL_CAPTURE_ENABLED="1"
  else
    export XENIA_GPU_CAPTURE_ENABLED="0"
    export MTL_CAPTURE_ENABLED="0"
  fi

  # Pass variables to Python script via env
  export LOG_FILE_ENV="${log_file}"
  export TRACE_FILE_ENV="${t_file}"
  export BIN_ENV="${BIN}"
  export OUTPUT_DIR_ENV="${run_dir}" # Relative to CWD for Python script logic usually, but here absolute is safer or handle carefully
  export TIMEOUT_SECS_ENV="${TIMEOUT_SECS}"
  export ROOT_DIR_ENV="${ROOT_DIR}"

  # Execute trace dump using Python for reliable timeout
  python3 - << 'EOF'
import os
import subprocess
import sys

log_file = os.environ.get("LOG_FILE_ENV")
trace_file = os.environ.get("TRACE_FILE_ENV")
bin_path = os.environ.get("BIN_ENV")
output_dir = os.environ.get("OUTPUT_DIR_ENV")
timeout_secs = int(os.environ.get("TIMEOUT_SECS_ENV", "8"))

cmd = [bin_path, f"--target_trace_file={trace_file}", f"--log_file={log_file}", "--log_level=4", "--metal_log_memexport=true", "--metal_readback_memexport=true", "--metal_presenter_log_gamma_ramp=true", "--metal_presenter_log_mailbox=true", "--metal_presenter_probe_copy=true", "--metal_texture_load_probe=true", "--metal_force_bc_decompress=true", "--metal_swap_probe_decode_by_format=true"]
# trace_dump_path determines where the output PNG goes
cmd.append(f"--trace_dump_path={output_dir}")

run_status = "UNKNOWN"
with open(os.devnull, 'wb') as devnull:
    try:
        proc = subprocess.run(cmd, check=False, timeout=timeout_secs,
                              stdout=devnull, stderr=devnull)
        if proc.returncode == 0:
            run_status = "SUCCESS"
        else:
            run_status = f"EXITED_CODE_{proc.returncode}"
    except subprocess.TimeoutExpired:
        run_status = "TIMEOUT"

with open(os.path.join(output_dir, "status.txt"), "w") as f:
    f.write(run_status)
EOF

  local status
  if [[ -f "${run_dir}/status.txt" ]]; then
    status=$(cat "${run_dir}/status.txt")
  else
    status="UNKNOWN_ERROR"
  fi
  
  echo "Status: ${status}"
  
  # Check for output PNG
  local png_status="MISSING"
  if [[ -f "${run_dir}/${t_name}.png" ]]; then
    png_status="GENERATED"
    
    # Save a copy to scratch/images for tracking
    local git_hash
    git_hash=$(git rev-parse --short HEAD 2>/dev/null || echo "nogit")
    local img_timestamp
    img_timestamp=$(date +%Y%m%d_%H%M%S)
    local images_dir="${ROOT_DIR}/scratch/images"
    mkdir -p "${images_dir}"
    cp "${run_dir}/${t_name}.png" "${images_dir}/${t_name}_${git_hash}_${img_timestamp}.png"
  fi

  # Post-process artifacts (move from /tmp/ or root if leaked, though env vars should prevent most)
  # (Simpler cleanup here than original script for brevity, relying on env vars mostly working)
  # Just double check /tmp/ for shaders if env var didn't catch them all (sometimes happens)
  shopt -s nullglob
  for f in /tmp/xenia_shader_*.metallib /tmp/xenia_shader_*.dxbc /tmp/xenia_shader_*.dxil; do
    if [[ -f "$f" ]]; then mv "$f" "${shaders_dir}/"; fi
  done
  shopt -u nullglob

  # Log Analysis
  local log_matches=""
  if [[ -f "${log_file}" ]]; then
    if command -v rg >/dev/null 2>&1; then
      log_matches=$(rg -n "${FULL_GREP_PATTERN}" "${log_file}" | head -n 20) || true
    else
      log_matches=$(grep -nE "${FULL_GREP_PATTERN}" "${log_file}" | head -n 20) || true
    fi
  fi

  # Append to summary
  {
    echo "Trace: ${t_name}"
    echo "  Status: ${status}"
    echo "  PNG: ${png_status}"
    if [[ -n "${log_matches}" ]]; then
      echo "  Important Logs:"
      echo "${log_matches}" | sed 's/^/    /'
    fi
    echo ""
  } >> "${MASTER_SUMMARY}"
  
  echo "  -> See ${run_dir} for details."
}

# --- Main Logic ---

if [[ -d "${INPUT_PATH}" ]]; then
  echo "[run_metal_trace] Processing all traces in directory: ${INPUT_PATH}"
  # Find all .xenia_gpu_trace and .xtr files
  # Use while loop to handle filenames with spaces correctly
  find "${INPUT_PATH}" \( -name "*.xenia_gpu_trace" -o -name "*.xtr" \) -print0 | sort -z | while IFS= read -r -d '' trace; do
    run_single_trace "${trace}"
  done
elif [[ -f "${INPUT_PATH}" ]]; then
  echo "[run_metal_trace] Processing single trace: ${INPUT_PATH}"
  run_single_trace "${INPUT_PATH}"
else
  echo "[run_metal_trace] Error: Input path '${INPUT_PATH}' is not a file or directory." >&2
  exit 1
fi

echo "----------------------------------------------------------------"
echo "[run_metal_trace] All Done."
echo "Master Summary available at: ${MASTER_SUMMARY}"
cat "${MASTER_SUMMARY}"
