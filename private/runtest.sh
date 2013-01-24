python xenia-build.py xethunk
python xenia-build.py build

rm build/$1*

./build/xenia/release/xenia-run \
    private/$1 \
    --optimize_ir_modules=true \
    --optimize_ir_functions=false \
    --trace_kernel_calls=true \
    --trace_user_calls=true \
    --trace_instructions=false \
    2>build/run.llvm.txt 1>build/run.txt

if [ ! -s build/run.llvm.txt ]; then
  rm build/run.llvm.txt
fi

if [ -e build/$1-preopt.bc ]; then
  ./build/llvm/release/bin/llvm-dis build/$1-preopt.bc
fi
if [ -e build/$1.bc ]; then
  ./build/llvm/release/bin/llvm-dis build/$1.bc
fi
