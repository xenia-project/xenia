/*
 * Copyright (c) 2017 The Khronos Group Inc.
 * Copyright (c) 2017 Valve Corporation
 * Copyright (c) 2017 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Lenny Komow <lenny@lunarg.com>
 */

// This code generates an assembly file which provides offsets to get struct members from assembly code.

#include <stdio.h>
#include "loader.h"

#if !defined(_MSC_VER) || (_MSC_VER >= 1900)
#define SIZE_T_FMT "%-8zu"
#else
#define SIZE_T_FMT "%-8lu"
#endif

struct ValueInfo
{
    const char *name;
    size_t value;
    const char *comment;
};

int main(int argc, char **argv) {
    const char *assembler = NULL;
    for (int i = 0; i < argc; ++i) {
        if (!strcmp(argv[i], "MASM")) {
            assembler = "MASM";
        } else if (!strcmp(argv[i], "GAS")) {
            assembler = "GAS";
        }
    }
    if (assembler == NULL) {
        return 1;
    }

    struct loader_instance instance;
    size_t hash_offset = ((size_t) &instance.phys_dev_ext_disp_hash) - ((size_t) &instance);

    struct loader_instance_dispatch_table disp;
    size_t phys_dev_offset_dispatch = ((size_t) &disp.phys_dev_ext) - ((size_t) &disp);

    struct loader_physical_device_tramp phys_dev_tramp;
    size_t phys_dev_offset_tramp = ((size_t) &phys_dev_tramp.phys_dev) - ((size_t) &phys_dev_tramp);

    struct loader_physical_device_term phys_dev_term;
    size_t icd_term_offset = ((size_t) &phys_dev_term.this_icd_term) - ((size_t) &phys_dev_term);
    size_t phys_dev_offset_term = ((size_t) &phys_dev_term.phys_dev) - ((size_t) &phys_dev_term);

    struct loader_icd_term icd_term;
    size_t instance_offset = ((size_t) &icd_term.this_instance) - ((size_t) &icd_term);
    size_t dispatch_offset = ((size_t) &icd_term.phys_dev_ext) - ((size_t) &icd_term);

    struct loader_dispatch_hash_entry hash;
    size_t func_name_offset = ((size_t) &hash.func_name) - ((size_t) &hash);

    struct loader_dev_dispatch_table dev_disp;
    size_t dev_disp_offset = ((size_t) &dev_disp.ext_dispatch) - ((size_t) &dev_disp);

    struct ValueInfo values[] = {
        { .name = "VK_DEBUG_REPORT_ERROR_BIT_EXT", .value = (size_t) VK_DEBUG_REPORT_ERROR_BIT_EXT,
            .comment = "The numerical value of the enum value 'VK_DEBUG_REPORT_ERROR_BIT_EXT'" },
        { .name = "PTR_SIZE", .value = sizeof(void*),
            .comment = "The size of a pointer" },
        { .name = "HASH_SIZE", .value = sizeof(struct loader_dispatch_hash_entry),
            .comment = "The size of a 'loader_dispatch_hash_entry' struct" },
        { .name = "HASH_OFFSET_INSTANCE", .value = hash_offset,
            .comment = "The offset of 'phys_dev_ext_disp_hash' within a 'loader_instance' struct" },
        { .name = "PHYS_DEV_OFFSET_INST_DISPATCH", .value = phys_dev_offset_dispatch,
            .comment = "The offset of 'phys_dev_ext' within in 'loader_instance_dispatch_table' struct" },
        { .name = "PHYS_DEV_OFFSET_PHYS_DEV_TRAMP", .value = phys_dev_offset_tramp,
            .comment = "The offset of 'phys_dev' within a 'loader_physical_device_tramp' struct" },
        { .name = "ICD_TERM_OFFSET_PHYS_DEV_TERM", .value = icd_term_offset,
            .comment = "The offset of 'this_icd_term' within a 'loader_physical_device_term' struct" },
        { .name = "PHYS_DEV_OFFSET_PHYS_DEV_TERM", .value = phys_dev_offset_term,
            .comment = "The offset of 'phys_dev' within a 'loader_physical_device_term' struct" },
        { .name = "INSTANCE_OFFSET_ICD_TERM", .value = instance_offset,
            .comment = "The offset of 'this_instance' within a 'loader_icd_term' struct" },
        { .name = "DISPATCH_OFFSET_ICD_TERM", .value = dispatch_offset,
            .comment = "The offset of 'phys_dev_ext' within a 'loader_icd_term' struct" },
        { .name = "FUNC_NAME_OFFSET_HASH", .value = func_name_offset,
            .comment = "The offset of 'func_name' within a 'loader_dispatch_hash_entry' struct" },
        { .name = "EXT_OFFSET_DEVICE_DISPATCH", .value = dev_disp_offset,
            .comment = "The offset of 'ext_dispatch' within a 'loader_dev_dispatch_table' struct" },
    };

    FILE *file = fopen("gen_defines.asm", "w");
    fprintf(file, "\n");
    if (!strcmp(assembler, "MASM")) {
        for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
            fprintf(file, "%-32s equ " SIZE_T_FMT "; %s\n", values[i].name, values[i].value, values[i].comment);
        }
    } else if (!strcmp(assembler, "GAS")) {
#ifdef __x86_64__
        fprintf(file, ".set X86_64, 1\n");
#endif // __x86_64__
        for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
            fprintf(file, ".set %-32s, " SIZE_T_FMT "# %s\n", values[i].name, values[i].value, values[i].comment);
        }
    }
    return fclose(file);
}
