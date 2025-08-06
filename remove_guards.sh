#\!/bin/bash

# Remove platform guards from Metal backend files
for file in /Users/admin/Documents/xenia_arm64_mac/src/xenia/gpu/metal/*.cc /Users/admin/Documents/xenia_arm64_mac/src/xenia/gpu/metal/*.h; do
  echo "Processing $file"
  
  # Remove #if XE_PLATFORM_MAC lines
  sed -i '' '/#if XE_PLATFORM_MAC$/d' "$file"
  sed -i '' '/#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)$/d' "$file"
  sed -i '' '/#ifdef METAL_CPP_AVAILABLE$/d' "$file"
  
  # Remove #endif comments for these guards
  sed -i '' '/#endif.*XE_PLATFORM_MAC/d' "$file"
  sed -i '' '/#endif.*METAL_CPP_AVAILABLE/d' "$file"
  
  # Remove #else lines that might be related
  # This is trickier - we'll be conservative and only remove obvious ones
done

echo "Done removing platform guards"
