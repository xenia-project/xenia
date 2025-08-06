#!/bin/bash

# Start lldb with the trace dump tool
lldb ./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump <<EOF &
run testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace
EOF

# Get the lldb process ID
LLDB_PID=$!

# Wait for the program to hang (5 seconds should be enough)
sleep 5

# Send interrupt signal to lldb to break into debugger
kill -INT $LLDB_PID 2>/dev/null

# Send commands to lldb to get thread info
cat <<EOF
process interrupt
thread backtrace all
thread list
quit
EOF

wait $LLDB_PID