#!/bin/bash

# Create an expect script to automate lldb
cat > /tmp/debug_lldb.exp << 'EOF'
#!/usr/bin/expect -f

set timeout 30

spawn lldb ./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump -- testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace

expect "(lldb)"
send "run\r"

# Wait for the program to start and potentially hang
sleep 5

# Send interrupt
send "\003"

expect "(lldb)"
send "thread backtrace all\r"

expect "(lldb)"
send "thread list\r"

expect "(lldb)"
send "quit\r"

expect eof
EOF

chmod +x /tmp/debug_lldb.exp

# Run the expect script
expect /tmp/debug_lldb.exp