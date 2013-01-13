/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/malloc.h>

#include <xenia/common.h>


void *xe_malloc(const size_t size) {
  // Some platforms return NULL from malloc with size zero.
  if (!size) {
    return malloc(1);
  }
  return malloc(size);
}

void *xe_calloc(const size_t size) {
  // Some platforms return NULL from malloc with size zero.
  if (!size) {
    return calloc(1, 1);
  }
  return calloc(1, size);
}

void* xe_realloc(void *ptr, const size_t old_size, const size_t new_size) {
  if (!ptr) {
    // Support realloc as malloc.
    return malloc(new_size);
  }
  if (old_size == new_size) {
    // No-op.
    return ptr;
  }
  if (!new_size) {
    // Zero-size realloc, return a dummy buffer for platforms that don't support
    // zero-size allocs.
    void *dummy = malloc(1);
    if (!dummy) {
      return NULL;
    }
    xe_free(ptr);
    return dummy;
  }

  return realloc(ptr, new_size);
}

void* xe_recalloc(void *ptr, const size_t old_size, const size_t new_size) {
  if (!ptr) {
    // Support realloc as malloc.
    return calloc(1, new_size);
  }
  if (old_size == new_size) {
    // No-op.
    return ptr;
  }
  if (!new_size) {
    // Zero-size realloc, return a dummy buffer for platforms that don't support
    // zero-size allocs.
    void *dummy = calloc(1, 1);
    if (!dummy) {
      return NULL;
    }
    xe_free(ptr);
    return dummy;
  }

  void *result = realloc(ptr, new_size);
  if (!result) {
    return NULL;
  }

  // Zero out new memory.
  if (new_size > old_size) {
    xe_zero_memory(result, new_size, old_size, new_size - old_size);
  }

  return result;
}

void xe_free(void *ptr) {
  if (ptr) {
    free(ptr);
  }
}

xe_aligned_void_t *xe_malloc_aligned(const size_t size) {
  // TODO(benvanik): validate every platform is aligned to XE_ALIGNMENT.
  return xe_malloc(size);
}

void xe_free_aligned(xe_aligned_void_t *ptr) {
  xe_free((void*)ptr);
}

int xe_zero_struct(void *ptr, const size_t size) {
  return xe_zero_memory(ptr, size, 0, size);
}

int xe_zero_memory(void *ptr, const size_t size, const size_t offset,
                   const size_t length) {
  // TODO(benvanik): validate sizing/clamp.
  if (!ptr || !length) {
    return 0;
  }
  if (offset + length > size) {
    return 1;
  }
  memset((uint8_t*)ptr + offset, 0, length);
  return 0;
}

int xe_copy_struct(void *dest, const void *source, const size_t size) {
  return xe_copy_memory(dest, size, source, size);
}

int xe_copy_memory(void *dest, const size_t dest_size, const void *source,
                   const size_t source_size) {
  // TODO(benvanik): validate sizing.
  if (!source_size) {
    return 0;
  }
  if (!dest || !source) {
    return 1;
  }
  if (dest_size < source_size) {
    return 1;
  }
  memcpy(dest, source, source_size);
  return 0;
}
