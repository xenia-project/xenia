/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_MALLOC_H_
#define XENIA_MALLOC_H_

#include <xenia/types.h>


#define xe_alloca(size) alloca(size)

void *xe_malloc(const size_t size);
void *xe_calloc(const size_t size);
void *xe_realloc(void *ptr, const size_t old_size, const size_t new_size);
void *xe_recalloc(void *ptr, const size_t old_size, const size_t new_size);
void xe_free(void *ptr);

xe_aligned_void_t *xe_malloc_aligned(const size_t size);
void xe_free_aligned(xe_aligned_void_t *ptr);

int xe_zero_struct(void *ptr, const size_t size);
int xe_zero_memory(void *ptr, const size_t size, const size_t offset,
                   const size_t length);
int xe_copy_struct(void *dest, const void *source, const size_t size);
int xe_copy_memory(void *dest, const size_t dest_size, const void *source,
                   const size_t source_size);


#endif  // XENIA_MALLOC_H_
