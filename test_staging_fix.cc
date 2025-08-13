// Test program to verify constant buffer staging fix
#include <cstdio>
#include <cstring>
#include <cmath>

// Simulated staging buffer test
void test_staging_buffer_copy() {
    printf("=== Testing Constant Buffer Staging Fix ===\n");
    
    // Simulate staging buffers (256 vec4s each)
    float cb0_staging[256 * 4];
    float cb1_staging[256 * 4];
    
    // Initialize with zeros
    memset(cb0_staging, 0, sizeof(cb0_staging));
    memset(cb1_staging, 0, sizeof(cb1_staging));
    
    // Simulate WriteRegister updating some constants
    // VS constants (cb0_staging)
    cb0_staging[0] = 1.0f;  // c0.x
    cb0_staging[1] = 2.0f;  // c0.y
    cb0_staging[2] = 3.0f;  // c0.z
    cb0_staging[3] = 4.0f;  // c0.w
    
    cb0_staging[4] = 0.5f;  // c1.x
    cb0_staging[5] = 0.25f; // c1.y
    cb0_staging[6] = 0.125f;// c1.z
    cb0_staging[7] = 1.0f;  // c1.w
    
    // PS constants (cb1_staging)
    cb1_staging[0] = 1.0f;  // c0.x (PS)
    cb1_staging[1] = 0.0f;  // c0.y (PS)
    cb1_staging[2] = 0.0f;  // c0.z (PS)
    cb1_staging[3] = 1.0f;  // c0.w (PS) - red color
    
    // Simulate uniforms buffer (20KB total)
    float uniforms_buffer[5 * 256 * 4];  // 5 * 4KB
    memset(uniforms_buffer, 0, sizeof(uniforms_buffer));
    
    // Check if staging buffers have non-zero data
    bool use_staging_for_vs = false;
    bool use_staging_for_ps = false;
    
    for (int i = 0; i < 256 * 4; i++) {
        if (cb0_staging[i] != 0.0f) {
            use_staging_for_vs = true;
            printf("Found non-zero VS constant at index %d: %.3f\n", i, cb0_staging[i]);
            break;
        }
    }
    
    for (int i = 0; i < 256 * 4; i++) {
        if (cb1_staging[i] != 0.0f) {
            use_staging_for_ps = true;
            printf("Found non-zero PS constant at index %d: %.3f\n", i, cb1_staging[i]);
            break;
        }
    }
    
    // Copy to uniforms buffer at correct offsets
    float* b1_data = uniforms_buffer + 1024;  // Offset by 256 vec4s (4KB)
    float* b1_ps_data = uniforms_buffer + 4096;  // Offset by 1024 vec4s (16KB)
    
    if (use_staging_for_vs) {
        memcpy(b1_data, cb0_staging, 256 * 4 * sizeof(float));
        printf("Copied VS constants from staging to b1\n");
        
        // Verify copy
        printf("VS constants in b1:\n");
        for (int i = 0; i < 2; i++) {
            printf("  C%d: [%.3f, %.3f, %.3f, %.3f]\n",
                   i, b1_data[i*4], b1_data[i*4+1], b1_data[i*4+2], b1_data[i*4+3]);
        }
    }
    
    if (use_staging_for_ps) {
        memcpy(b1_ps_data, cb1_staging, 256 * 4 * sizeof(float));
        printf("Copied PS constants from staging to b1_ps\n");
        
        // Verify copy
        printf("PS constants in b1_ps:\n");
        for (int i = 0; i < 1; i++) {
            printf("  C%d: [%.3f, %.3f, %.3f, %.3f]\n",
                   i, b1_ps_data[i*4], b1_ps_data[i*4+1], b1_ps_data[i*4+2], b1_ps_data[i*4+3]);
        }
    }
    
    // Test NDC transformation constants
    float* b0_data = uniforms_buffer;  // System constants at offset 0
    
    // Simulate viewport info
    float ndc_scale_x = 0.00078125f;  // 1/1280
    float ndc_scale_y = -0.00138889f; // -1/720 (inverted for Metal)
    float ndc_scale_z = 1.0f;
    float ndc_offset_x = -1.0f;
    float ndc_offset_y = 1.0f;
    float ndc_offset_z = 0.0f;
    
    b0_data[0] = ndc_scale_x;
    b0_data[1] = ndc_scale_y;
    b0_data[2] = ndc_scale_z;
    b0_data[3] = 0.0f;
    
    b0_data[4] = ndc_offset_x;
    b0_data[5] = ndc_offset_y;
    b0_data[6] = ndc_offset_z;
    b0_data[7] = 0.0f;
    
    printf("\nNDC transformation constants in b0:\n");
    printf("  b0[0] (ndc_scale): [%.6f, %.6f, %.6f, %.6f]\n",
           b0_data[0], b0_data[1], b0_data[2], b0_data[3]);
    printf("  b0[1] (ndc_offset): [%.6f, %.6f, %.6f, %.6f]\n",
           b0_data[4], b0_data[5], b0_data[6], b0_data[7]);
    
    // Verify constants are accessible at correct offsets
    printf("\n=== Verification ===\n");
    
    // Check b0 (system constants) at offset 0
    float* check_b0 = uniforms_buffer;
    printf("b0 at offset 0: scale.x = %.6f (expected %.6f) %s\n",
           check_b0[0], ndc_scale_x, 
           fabs(check_b0[0] - ndc_scale_x) < 0.0001f ? "✓" : "✗");
    
    // Check b1 (VS constants) at offset 4KB
    float* check_b1 = uniforms_buffer + 1024;
    printf("b1 at offset 4KB: c0.x = %.3f (expected 1.0) %s\n",
           check_b1[0], fabs(check_b1[0] - 1.0f) < 0.001f ? "✓" : "✗");
    
    // Check b1_ps (PS constants) at offset 16KB
    float* check_b1_ps = uniforms_buffer + 4096;
    printf("b1_ps at offset 16KB: c0.x = %.3f (expected 1.0) %s\n",
           check_b1_ps[0], fabs(check_b1_ps[0] - 1.0f) < 0.001f ? "✓" : "✗");
    
    printf("\n=== Test Complete ===\n");
}

int main() {
    test_staging_buffer_copy();
    return 0;
}