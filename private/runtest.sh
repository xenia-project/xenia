python xenia-build.py build

if [ "$?" -ne 0 ]; then
  echo "Build failed!"
  exit $?
fi

./build/xenia/release/xenia-run \
    private/$1 \
    --dump_path=build/ \
    --dump_module_map=true \
    --dump_module_bitcode=true \
    --optimize_ir_modules=true \
    --optimize_ir_functions=true \
    --memory_address_verification=true \
    --trace_kernel_calls=true \
    --trace_user_calls=true \
    --trace_instructions=false \
    --abort_before_entry=true \
    1>build/run.txt
