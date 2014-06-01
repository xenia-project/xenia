/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/buffer.h>

#include <xenia/gpu/xenos/ucode_disassembler.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


Buffer::Buffer(
    const uint8_t* src_ptr, size_t length) :
    src_(src_ptr), length_(length) {
}

Buffer::~Buffer() {
}

IndexBuffer::IndexBuffer(const IndexBufferInfo& info,
                         const uint8_t* src_ptr, size_t length)
    : Buffer(src_ptr, length),
      info_(info) {
}

IndexBuffer::~IndexBuffer() {}

VertexBuffer::VertexBuffer(const uint8_t* src_ptr, size_t length)
    : Buffer(src_ptr, length) {
}

VertexBuffer::~VertexBuffer() {}
