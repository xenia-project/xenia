/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gl4/texture_cache.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/gpu_flags.h"

namespace xe {
namespace gpu {
namespace gl4 {

struct TextureConfig {
  TextureFormat texture_format;
  GLenum internal_format;
  GLenum format;
  GLenum type;
};

// https://code.google.com/p/glsnewton/source/browse/trunk/Source/uDDSLoader.pas?r=62
// http://dench.flatlib.jp/opengl/textures
// http://fossies.org/linux/WebKit/Source/ThirdParty/ANGLE/src/libGLESv2/formatutils.cpp
static const TextureConfig texture_configs[64] = {
    {TextureFormat::k_1_REVERSE, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::k_1, GL_INVALID_ENUM, GL_INVALID_ENUM, GL_INVALID_ENUM},
    {TextureFormat::k_8, GL_R8, GL_RED, GL_UNSIGNED_BYTE},
    {TextureFormat::k_1_5_5_5, GL_RGB5_A1, GL_RGBA,
     GL_UNSIGNED_SHORT_1_5_5_5_REV},
    {TextureFormat::k_5_6_5, GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5_REV},
    {TextureFormat::k_6_5_5, GL_INVALID_ENUM, GL_INVALID_ENUM, GL_INVALID_ENUM},
    {TextureFormat::k_8_8_8_8, GL_RGBA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV},
    {TextureFormat::k_2_10_10_10, GL_RGB10_A2, GL_RGBA,
     GL_UNSIGNED_INT_2_10_10_10_REV},
    {TextureFormat::k_8_A, GL_INVALID_ENUM, GL_INVALID_ENUM, GL_INVALID_ENUM},
    {TextureFormat::k_8_B, GL_INVALID_ENUM, GL_INVALID_ENUM, GL_INVALID_ENUM},
    {TextureFormat::k_8_8, GL_RG8, GL_RG, GL_UNSIGNED_BYTE},
    {TextureFormat::k_Cr_Y1_Cb_Y0, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::k_Y1_Cr_Y0_Cb, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::kUnknown, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::k_8_8_8_8_A, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::k_4_4_4_4, GL_RGBA4, GL_RGBA,
     GL_UNSIGNED_SHORT_4_4_4_4_REV},
    {TextureFormat::k_10_11_11, GL_R11F_G11F_B10F, GL_RGB,
     GL_UNSIGNED_INT_10F_11F_11F_REV},  // ?
    {TextureFormat::k_11_11_10, GL_R11F_G11F_B10F, GL_RGB,
     GL_UNSIGNED_INT_10F_11F_11F_REV},  // ?
    {TextureFormat::k_DXT1, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
     GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, GL_UNSIGNED_BYTE},
    {TextureFormat::k_DXT2_3, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,
     GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_UNSIGNED_BYTE},
    {TextureFormat::k_DXT4_5, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
     GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_UNSIGNED_BYTE},
    {TextureFormat::kUnknown, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::k_24_8, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL,
     GL_UNSIGNED_INT_24_8},
    {TextureFormat::k_24_8_FLOAT, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL,
     GL_FLOAT_32_UNSIGNED_INT_24_8_REV},
    {TextureFormat::k_16, GL_R16, GL_RED, GL_UNSIGNED_SHORT},
    {TextureFormat::k_16_16, GL_RG16, GL_RG, GL_UNSIGNED_SHORT},
    {TextureFormat::k_16_16_16_16, GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT},
    {TextureFormat::k_16_EXPAND, GL_R16, GL_RED, GL_UNSIGNED_SHORT},
    {TextureFormat::k_16_16_EXPAND, GL_RG16, GL_RG, GL_UNSIGNED_SHORT},
    {TextureFormat::k_16_16_16_16_EXPAND, GL_RGBA16, GL_RGBA,
     GL_UNSIGNED_SHORT},
    {TextureFormat::k_16_FLOAT, GL_R16F, GL_RED, GL_HALF_FLOAT},
    {TextureFormat::k_16_16_FLOAT, GL_RG16F, GL_RG, GL_HALF_FLOAT},
    {TextureFormat::k_16_16_16_16_FLOAT, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT},
    {TextureFormat::k_32, GL_R32I, GL_RED, GL_UNSIGNED_INT},
    {TextureFormat::k_32_32, GL_RG32I, GL_RG, GL_UNSIGNED_INT},
    {TextureFormat::k_32_32_32_32, GL_RGBA32I, GL_RGBA, GL_UNSIGNED_INT},
    {TextureFormat::k_32_FLOAT, GL_R32F, GL_RED, GL_FLOAT},
    {TextureFormat::k_32_32_FLOAT, GL_RG32F, GL_RG, GL_FLOAT},
    {TextureFormat::k_32_32_32_32_FLOAT, GL_RGBA32F, GL_RGBA, GL_FLOAT},
    {TextureFormat::k_32_AS_8, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::k_32_AS_8_8, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::k_16_MPEG, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::k_16_16_MPEG, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::k_8_INTERLACED, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::k_32_AS_8_INTERLACED, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::k_32_AS_8_8_INTERLACED, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::k_16_INTERLACED, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::k_16_MPEG_INTERLACED, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::k_16_16_MPEG_INTERLACED, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::k_DXN, GL_COMPRESSED_RG_RGTC2, GL_COMPRESSED_RG_RGTC2,
     GL_INVALID_ENUM},
    {TextureFormat::k_8_8_8_8_AS_16_16_16_16, GL_RGBA8, GL_RGBA,
     GL_UNSIGNED_BYTE},
    {TextureFormat::k_DXT1_AS_16_16_16_16, GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
     GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_UNSIGNED_BYTE},
    {TextureFormat::k_DXT2_3_AS_16_16_16_16, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,
     GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_UNSIGNED_BYTE},
    {TextureFormat::k_DXT4_5_AS_16_16_16_16, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
     GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_UNSIGNED_BYTE},
    {TextureFormat::k_2_10_10_10_AS_16_16_16_16, GL_RGB10_A2, GL_RGBA,
     GL_UNSIGNED_INT_2_10_10_10_REV},
    {TextureFormat::k_10_11_11_AS_16_16_16_16, GL_R11F_G11F_B10F, GL_RGB,
     GL_UNSIGNED_INT_10F_11F_11F_REV},
    {TextureFormat::k_11_11_10_AS_16_16_16_16, GL_R11F_G11F_B10F,
     GL_INVALID_ENUM, GL_INVALID_ENUM},
    {TextureFormat::k_32_32_32_FLOAT, GL_RGB32F, GL_RGB, GL_FLOAT},
    {TextureFormat::k_DXT3A, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,
     GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_UNSIGNED_BYTE},
    {TextureFormat::k_DXT5A, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
     GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_UNSIGNED_BYTE},
    {TextureFormat::k_CTX1, GL_INVALID_ENUM, GL_INVALID_ENUM, GL_INVALID_ENUM},
    {TextureFormat::k_DXT3A_AS_1_1_1_1, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::kUnknown, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
    {TextureFormat::kUnknown, GL_INVALID_ENUM, GL_INVALID_ENUM,
     GL_INVALID_ENUM},
};

TextureCache::TextureCache() : memory_(nullptr), scratch_buffer_(nullptr) {
  invalidated_textures_sets_[0].reserve(64);
  invalidated_textures_sets_[1].reserve(64);
  invalidated_textures_ = &invalidated_textures_sets_[0];
}

TextureCache::~TextureCache() { Shutdown(); }

bool TextureCache::Initialize(Memory* memory, CircularBuffer* scratch_buffer) {
  memory_ = memory;
  scratch_buffer_ = scratch_buffer;
  return true;
}

void TextureCache::Shutdown() { Clear(); }

void TextureCache::Scavenge() {
  invalidated_textures_mutex_.lock();
  std::vector<TextureEntry*>& invalidated_textures = *invalidated_textures_;
  if (invalidated_textures_ == &invalidated_textures_sets_[0]) {
    invalidated_textures_ = &invalidated_textures_sets_[1];
  } else {
    invalidated_textures_ = &invalidated_textures_sets_[0];
  }
  invalidated_textures_mutex_.unlock();
  if (invalidated_textures.empty()) {
    return;
  }

  for (auto& entry : invalidated_textures) {
    EvictTexture(entry);
  }
  invalidated_textures.clear();
}

void TextureCache::Clear() {
  EvictAllTextures();

  // Samplers must go last, as textures depend on them.
  while (sampler_entries_.size()) {
    auto entry = sampler_entries_.begin()->second;
    EvictSampler(entry);
  }
}

void TextureCache::EvictAllTextures() {
  // Kill all textures - some may be in the eviction list, but that's fine
  // as we will clear that below.
  while (!texture_entries_.empty()) {
    auto entry = texture_entries_.begin()->second;
    EvictTexture(entry);
  }

  {
    std::lock_guard<std::mutex> lock(invalidated_textures_mutex_);
    invalidated_textures_sets_[0].clear();
    invalidated_textures_sets_[1].clear();
  }

  // Kill all readbuffer textures.
  while (!read_buffer_textures_.empty()) {
    auto it = --read_buffer_textures_.end();
    auto entry = *it;
    glDeleteTextures(1, &entry->handle);
    delete entry;
    read_buffer_textures_.erase(it);
  }
}

TextureCache::TextureEntryView* TextureCache::Demand(
    const TextureInfo& texture_info, const SamplerInfo& sampler_info) {
  uint64_t texture_hash = texture_info.hash();
  auto texture_entry = LookupOrInsertTexture(texture_info, texture_hash);
  if (!texture_entry) {
    XELOGE("Failed to setup texture");
    return nullptr;
  }

  // We likely have the sampler in the texture view listing, so scan for it.
  uint64_t sampler_hash = sampler_info.hash();
  for (auto& it : texture_entry->views) {
    if (it->sampler_hash == sampler_hash) {
      // Found.
      return it.get();
    }
  }

  // No existing view found - build it.
  auto sampler_entry = LookupOrInsertSampler(sampler_info, sampler_hash);
  if (!sampler_entry) {
    XELOGE("Failed to setup texture sampler");
    return nullptr;
  }

  auto view = std::make_unique<TextureEntryView>();
  view->texture = texture_entry;
  view->sampler = sampler_entry;
  view->sampler_hash = sampler_hash;
  view->texture_sampler_handle = 0;

  // Get the uvec2 handle to the texture/sampler pair and make it resident.
  // The handle can be passed directly to the shader.
  view->texture_sampler_handle = glGetTextureSamplerHandleARB(
      texture_entry->handle, sampler_entry->handle);
  if (!view->texture_sampler_handle) {
    assert_always("Unable to get texture handle?");
    return nullptr;
  }
  glMakeTextureHandleResidentARB(view->texture_sampler_handle);

  // Entry takes ownership.
  auto view_ptr = view.get();
  texture_entry->views.push_back(std::move(view));
  return view_ptr;
}

TextureCache::SamplerEntry* TextureCache::LookupOrInsertSampler(
    const SamplerInfo& sampler_info, uint64_t opt_hash) {
  const uint64_t hash = opt_hash ? opt_hash : sampler_info.hash();
  for (auto it = sampler_entries_.find(hash); it != sampler_entries_.end();
       ++it) {
    if (it->second->sampler_info == sampler_info) {
      // Found in cache!
      return it->second;
    }
  }

  // Not found, create.
  auto entry = std::make_unique<SamplerEntry>();
  entry->sampler_info = sampler_info;
  glCreateSamplers(1, &entry->handle);

  // TODO(benvanik): border color from texture fetch.
  GLfloat border_color[4] = {0.0f};
  glSamplerParameterfv(entry->handle, GL_TEXTURE_BORDER_COLOR, border_color);

  // TODO(benvanik): setup LODs for mipmapping.
  glSamplerParameterf(entry->handle, GL_TEXTURE_LOD_BIAS, 0.0f);
  glSamplerParameterf(entry->handle, GL_TEXTURE_MIN_LOD, 0.0f);
  glSamplerParameterf(entry->handle, GL_TEXTURE_MAX_LOD, 0.0f);

  // Texture wrapping modes.
  // TODO(benvanik): not sure if the middle ones are correct.
  static const GLenum wrap_map[] = {
      GL_REPEAT,                      //
      GL_MIRRORED_REPEAT,             //
      GL_CLAMP_TO_EDGE,               //
      GL_MIRROR_CLAMP_TO_EDGE,        //
      GL_CLAMP_TO_BORDER,             // ?
      GL_MIRROR_CLAMP_TO_BORDER_EXT,  // ?
      GL_CLAMP_TO_BORDER,             //
      GL_MIRROR_CLAMP_TO_BORDER_EXT,  //
  };
  glSamplerParameteri(entry->handle, GL_TEXTURE_WRAP_S,
                      wrap_map[static_cast<int>(sampler_info.clamp_u)]);
  glSamplerParameteri(entry->handle, GL_TEXTURE_WRAP_T,
                      wrap_map[static_cast<int>(sampler_info.clamp_v)]);
  glSamplerParameteri(entry->handle, GL_TEXTURE_WRAP_R,
                      wrap_map[static_cast<int>(sampler_info.clamp_w)]);

  // Texture level filtering.
  GLenum min_filter;
  switch (sampler_info.min_filter) {
    case TextureFilter::kPoint:
      switch (sampler_info.mip_filter) {
        case TextureFilter::kBaseMap:
          min_filter = GL_NEAREST;
          break;
        case TextureFilter::kPoint:
          // min_filter = GL_NEAREST_MIPMAP_NEAREST;
          min_filter = GL_NEAREST;
          break;
        case TextureFilter::kLinear:
          // min_filter = GL_NEAREST_MIPMAP_LINEAR;
          min_filter = GL_NEAREST;
          break;
        default:
          assert_unhandled_case(sampler_info.mip_filter);
          return nullptr;
      }
      break;
    case TextureFilter::kLinear:
      switch (sampler_info.mip_filter) {
        case TextureFilter::kBaseMap:
          min_filter = GL_LINEAR;
          break;
        case TextureFilter::kPoint:
          // min_filter = GL_LINEAR_MIPMAP_NEAREST;
          min_filter = GL_LINEAR;
          break;
        case TextureFilter::kLinear:
          // min_filter = GL_LINEAR_MIPMAP_LINEAR;
          min_filter = GL_LINEAR;
          break;
        default:
          assert_unhandled_case(sampler_info.mip_filter);
          return nullptr;
      }
      break;
    default:
      assert_unhandled_case(sampler_info.min_filter);
      return nullptr;
  }
  GLenum mag_filter;
  switch (sampler_info.mag_filter) {
    case TextureFilter::kPoint:
      mag_filter = GL_NEAREST;
      break;
    case TextureFilter::kLinear:
      mag_filter = GL_LINEAR;
      break;
    default:
      assert_unhandled_case(mag_filter);
      return nullptr;
  }
  glSamplerParameteri(entry->handle, GL_TEXTURE_MIN_FILTER, min_filter);
  glSamplerParameteri(entry->handle, GL_TEXTURE_MAG_FILTER, mag_filter);

  GLfloat aniso;
  switch (sampler_info.aniso_filter) {
    case AnisoFilter::kDisabled:
      aniso = 0.0f;
      break;
    case AnisoFilter::kMax_1_1:
      aniso = 1.0f;
      break;
    case AnisoFilter::kMax_2_1:
      aniso = 2.0f;
      break;
    case AnisoFilter::kMax_4_1:
      aniso = 4.0f;
      break;
    case AnisoFilter::kMax_8_1:
      aniso = 8.0f;
      break;
    case AnisoFilter::kMax_16_1:
      aniso = 16.0f;
      break;
    default:
      assert_unhandled_case(aniso);
      return nullptr;
  }

  if (aniso) {
    glSamplerParameterf(entry->handle, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
  }

  // Add to map - map takes ownership.
  auto entry_ptr = entry.get();
  sampler_entries_.insert({hash, entry.release()});
  return entry_ptr;
}

void TextureCache::EvictSampler(SamplerEntry* entry) {
  glDeleteSamplers(1, &entry->handle);

  for (auto it = sampler_entries_.find(entry->sampler_info.hash());
       it != sampler_entries_.end(); ++it) {
    if (it->second == entry) {
      sampler_entries_.erase(it);
      break;
    }
  }

  delete entry;
}

TextureCache::TextureEntry* TextureCache::LookupOrInsertTexture(
    const TextureInfo& texture_info, uint64_t opt_hash) {
  const uint64_t hash = opt_hash ? opt_hash : texture_info.hash();
  for (auto it = texture_entries_.find(hash); it != texture_entries_.end();
       ++it) {
    if (it->second->pending_invalidation) {
      // Whoa, we've been invalidated! Let's scavenge to cleanup and try again.
      // TODO(benvanik): reuse existing texture storage.
      Scavenge();
      break;
    }
    if (it->second->texture_info == texture_info) {
      // Found in cache!
      return it->second;
    }
  }

  // Not found, create.
  auto entry = std::make_unique<TextureEntry>();
  entry->texture_info = texture_info;
  entry->access_watch_handle = 0;
  entry->pending_invalidation = false;
  entry->handle = 0;

  // Check read buffer textures - there may be one waiting for us.
  // TODO(benvanik): speed up existence check?
  for (auto it = read_buffer_textures_.begin();
       it != read_buffer_textures_.end(); ++it) {
    auto read_buffer_entry = *it;
    if (read_buffer_entry->guest_address == texture_info.guest_address &&
        read_buffer_entry->block_width == texture_info.size_2d.block_width &&
        read_buffer_entry->block_height == texture_info.size_2d.block_height) {
      // Found! Acquire the handle and remove the readbuffer entry.
      read_buffer_textures_.erase(it);
      entry->handle = read_buffer_entry->handle;
      entry->access_watch_handle = read_buffer_entry->access_watch_handle;
      delete read_buffer_entry;
      // TODO(benvanik): set more texture properties? swizzle/etc?
      auto entry_ptr = entry.get();
      texture_entries_.insert({hash, entry.release()});
      return entry_ptr;
    }
  }

  GLenum target;
  switch (texture_info.dimension) {
    case Dimension::k1D:
      target = GL_TEXTURE_1D;
      break;
    case Dimension::k2D:
      target = GL_TEXTURE_2D;
      break;
    case Dimension::k3D:
      target = GL_TEXTURE_3D;
      break;
    case Dimension::kCube:
      target = GL_TEXTURE_CUBE_MAP;
      break;
  }

  // Setup the base texture.
  glCreateTextures(target, 1, &entry->handle);

  // TODO(benvanik): texture mip levels.
  glTextureParameteri(entry->handle, GL_TEXTURE_BASE_LEVEL, 0);
  glTextureParameteri(entry->handle, GL_TEXTURE_MAX_LEVEL, 1);

  // Upload/convert.
  bool uploaded = false;
  switch (texture_info.dimension) {
    case Dimension::k1D:
      uploaded = UploadTexture1D(entry->handle, texture_info);
      break;
    case Dimension::k2D:
      uploaded = UploadTexture2D(entry->handle, texture_info);
      break;
    case Dimension::kCube:
      uploaded = UploadTextureCube(entry->handle, texture_info);
      break;
    case Dimension::k3D:
      assert_unhandled_case(texture_info.dimension);
      return nullptr;
  }
  if (!uploaded) {
    XELOGE("Failed to convert/upload texture");
    return nullptr;
  }

  // Add a write watch. If any data in the given range is touched we'll get a
  // callback and evict the texture. We could reuse the storage, though the
  // driver is likely in a better position to pool that kind of stuff.
  entry->access_watch_handle = memory_->AddPhysicalAccessWatch(
      texture_info.guest_address, texture_info.input_length,
      cpu::MMIOHandler::kWatchWrite,
      [](void* context_ptr, void* data_ptr, uint32_t address) {
        auto self = reinterpret_cast<TextureCache*>(context_ptr);
        auto touched_entry = reinterpret_cast<TextureEntry*>(data_ptr);
        // Clear watch handle first so we don't redundantly
        // remove.
        touched_entry->access_watch_handle = 0;
        touched_entry->pending_invalidation = true;
        // Add to pending list so Scavenge will clean it up.
        self->invalidated_textures_mutex_.lock();
        self->invalidated_textures_->push_back(touched_entry);
        self->invalidated_textures_mutex_.unlock();
      },
      this, entry.get());

  // Add to map - map takes ownership.
  auto entry_ptr = entry.get();
  texture_entries_.insert({hash, entry.release()});
  return entry_ptr;
}

TextureCache::TextureEntry* TextureCache::LookupAddress(uint32_t guest_address,
                                                        uint32_t width,
                                                        uint32_t height,
                                                        TextureFormat format) {
  // TODO(benvanik): worth speeding up?
  for (auto it = texture_entries_.begin(); it != texture_entries_.end(); ++it) {
    const auto& texture_info = it->second->texture_info;
    if (texture_info.guest_address == guest_address &&
        texture_info.dimension == Dimension::k2D &&
        texture_info.size_2d.input_width == width &&
        texture_info.size_2d.input_height == height) {
      return it->second;
    }
  }
  return nullptr;
}

GLuint TextureCache::CopyTexture(Blitter* blitter, uint32_t guest_address,
                                 uint32_t logical_width,
                                 uint32_t logical_height, uint32_t block_width,
                                 uint32_t block_height, TextureFormat format,
                                 bool swap_channels, GLuint src_texture,
                                 Rect2D src_rect, Rect2D dest_rect) {
  return ConvertTexture(blitter, guest_address, logical_width, logical_height,
                        block_width, block_height, format, swap_channels,
                        src_texture, src_rect, dest_rect);
}

GLuint TextureCache::ConvertTexture(Blitter* blitter, uint32_t guest_address,
                                    uint32_t logical_width,
                                    uint32_t logical_height,
                                    uint32_t block_width, uint32_t block_height,
                                    TextureFormat format, bool swap_channels,
                                    GLuint src_texture, Rect2D src_rect,
                                    Rect2D dest_rect) {
  const auto& config = texture_configs[uint32_t(format)];
  if (config.format == GL_INVALID_ENUM) {
    assert_always("Unhandled destination texture format");
    return 0;
  }

  // See if we have used a texture at this address before. If we have, we can
  // reuse it.
  // TODO(benvanik): better lookup matching format/etc?
  auto texture_entry =
      LookupAddress(guest_address, block_width, block_height, format);
  if (texture_entry) {
    // Have existing texture.
    assert_false(texture_entry->pending_invalidation);
    if (config.format == GL_DEPTH_STENCIL) {
      blitter->CopyDepthTexture(src_texture, src_rect, texture_entry->handle,
                                dest_rect);
    } else {
      blitter->CopyColorTexture2D(src_texture, src_rect, texture_entry->handle,
                                  dest_rect, GL_LINEAR, swap_channels);
    }

    // Setup a read/write access watch. If the game tries to touch the memory
    // we were supposed to populate with this texture, then we'll actually
    // populate it.
    if (texture_entry->access_watch_handle) {
      memory_->CancelAccessWatch(texture_entry->access_watch_handle);
      texture_entry->access_watch_handle = 0;
    }

    texture_entry->access_watch_handle = memory_->AddPhysicalAccessWatch(
        guest_address, texture_entry->texture_info.input_length,
        cpu::MMIOHandler::kWatchReadWrite,
        [](void* context, void* data, uint32_t address) {
          auto touched_entry = reinterpret_cast<TextureEntry*>(data);
          touched_entry->access_watch_handle = 0;

          // This happens. RDR resolves to a texture then upsizes it, BF1943
          // writes to a resolved texture.
          // TODO (for Vulkan): Copy this texture back into system memory.
          // assert_always();
        },
        nullptr, texture_entry);

    return texture_entry->handle;
  }

  // Check pending read buffer textures (for multiple resolves with no
  // uploads inbetween).
  for (auto it = read_buffer_textures_.begin();
       it != read_buffer_textures_.end(); ++it) {
    const auto& entry = *it;
    if (entry->guest_address == guest_address &&
        entry->logical_width == logical_width &&
        entry->logical_height == logical_height && entry->format == format) {
      // Found an existing entry - just reupload.
      if (config.format == GL_DEPTH_STENCIL) {
        blitter->CopyDepthTexture(src_texture, src_rect, entry->handle,
                                  dest_rect);
      } else {
        blitter->CopyColorTexture2D(src_texture, src_rect, entry->handle,
                                    dest_rect, GL_LINEAR, swap_channels);
      }
      return entry->handle;
    }
  }

  // Need to create a new texture.
  // As we don't know anything about this texture, we'll add it to the
  // pending readbuffer list. If nobody claims it after a certain amount
  // of time we'll dump it.
  auto entry = std::make_unique<ReadBufferTexture>();
  entry->guest_address = guest_address;
  entry->logical_width = logical_width;
  entry->logical_height = logical_height;
  entry->block_width = block_width;
  entry->block_height = block_height;
  entry->format = format;

  entry->access_watch_handle = memory_->AddPhysicalAccessWatch(
      guest_address, block_height * block_width * 4,
      cpu::MMIOHandler::kWatchReadWrite,
      [](void* context, void* data, uint32_t address) {
        auto entry = reinterpret_cast<ReadBufferTexture*>(data);
        entry->access_watch_handle = 0;

        // This happens. RDR resolves to a texture then upsizes it, BF1943
        // writes to a resolved texture.
        // TODO (for Vulkan): Copy this texture back into system memory.
        // assert_always();
      },
      nullptr, entry.get());

  glCreateTextures(GL_TEXTURE_2D, 1, &entry->handle);
  glTextureParameteri(entry->handle, GL_TEXTURE_BASE_LEVEL, 0);
  glTextureParameteri(entry->handle, GL_TEXTURE_MAX_LEVEL, 1);
  glTextureStorage2D(entry->handle, 1, config.internal_format, logical_width,
                     logical_height);
  if (config.format == GL_DEPTH_STENCIL) {
    blitter->CopyDepthTexture(src_texture, src_rect, entry->handle, dest_rect);
  } else {
    blitter->CopyColorTexture2D(src_texture, src_rect, entry->handle, dest_rect,
                                GL_LINEAR, swap_channels);
  }

  GLuint handle = entry->handle;
  read_buffer_textures_.push_back(entry.release());
  return handle;
}

void TextureCache::EvictTexture(TextureEntry* entry) {
  if (entry->access_watch_handle) {
    memory_->CancelAccessWatch(entry->access_watch_handle);
    entry->access_watch_handle = 0;
  }

  for (auto& view : entry->views) {
    glMakeTextureHandleNonResidentARB(view->texture_sampler_handle);
  }
  glDeleteTextures(1, &entry->handle);

  uint64_t texture_hash = entry->texture_info.hash();
  for (auto it = texture_entries_.find(texture_hash);
       it != texture_entries_.end(); ++it) {
    if (it->second == entry) {
      texture_entries_.erase(it);
      break;
    }
  }

  delete entry;
}

struct HostTextureInfo {
  uint32_t output_length;

  union {
    struct {
      uint32_t output_width;
      uint32_t output_pitch;
    } size_1d;
    struct {
      uint32_t output_width;
      uint32_t output_height;
      uint32_t output_pitch;
    } size_2d;
    struct {
    } size_3d;
    struct {
      uint32_t output_width;
      uint32_t output_height;
      uint32_t output_pitch;
      uint32_t output_face_length;
    } size_cube;
  };

  static bool Setup(const TextureInfo& guest_info, HostTextureInfo* out_info) {
    auto& info = *out_info;
    auto format = guest_info.format_info();

    switch (guest_info.dimension) {
      case Dimension::k1D: {
        uint32_t bytes_per_block =
            format->block_width * format->bits_per_pixel / 8;
        uint32_t block_width = xe::round_up(guest_info.size_1d.logical_width,
                                            format->block_width) /
                               format->block_width;
        info.size_1d.output_width = block_width * format->block_width;
        info.size_1d.output_pitch = block_width * bytes_per_block;
        info.output_length = info.size_1d.output_pitch;
        return true;
      }
      case Dimension::k2D: {
        uint32_t bytes_per_block = format->block_width * format->block_height *
                                   format->bits_per_pixel / 8;
        uint32_t block_width = xe::round_up(guest_info.size_2d.logical_width,
                                            format->block_width) /
                               format->block_width;
        uint32_t block_height = xe::round_up(guest_info.size_2d.logical_height,
                                             format->block_height) /
                                format->block_height;
        info.size_2d.output_width = block_width * format->block_width;
        info.size_2d.output_height = block_height * format->block_height;
        info.size_2d.output_pitch = block_width * bytes_per_block;
        info.output_length = info.size_2d.output_pitch * block_height;
        return true;
      };
      case Dimension::k3D: {
        return false;
      }
      case Dimension::kCube: {
        uint32_t bytes_per_block = format->block_width * format->block_height *
                                   format->bits_per_pixel / 8;
        uint32_t block_width = xe::round_up(guest_info.size_cube.logical_width,
                                            format->block_width) /
                               format->block_width;
        uint32_t block_height =
            xe::round_up(guest_info.size_cube.logical_height,
                         format->block_height) /
            format->block_height;
        info.size_cube.output_width = block_width * format->block_width;
        info.size_cube.output_height = block_height * format->block_height;
        info.size_cube.output_pitch = block_width * bytes_per_block;
        info.size_cube.output_face_length =
            info.size_cube.output_pitch * block_height;
        info.output_length = info.size_cube.output_face_length * 6;
        return true;
      }
    }
    return false;
  }
};

void TextureSwap(Endian endianness, void* dest, const void* src,
                 size_t length) {
  switch (endianness) {
    case Endian::k8in16:
      xe::copy_and_swap_16_aligned(dest, src, length / 2);
      break;
    case Endian::k8in32:
      xe::copy_and_swap_32_aligned(dest, src, length / 4);
      break;
    case Endian::k16in32:  // Swap high and low 16 bits within a 32 bit word
      xe::copy_and_swap_16_in_32_aligned(dest, src, length);
      break;
    default:
    case Endian::kUnspecified:
      std::memcpy(dest, src, length);
      break;
  }
}

bool TextureCache::UploadTexture1D(GLuint texture,
                                   const TextureInfo& texture_info) {
  SCOPE_profile_cpu_f("gpu");
  const auto host_address =
      memory_->TranslatePhysical(texture_info.guest_address);

  const auto& config = texture_configs[uint32_t(texture_info.texture_format)];
  if (config.format == GL_INVALID_ENUM) {
    assert_always("Unhandled texture format");
    return false;
  }

  HostTextureInfo host_info;
  if (!HostTextureInfo::Setup(texture_info, &host_info)) {
    assert_always("Failed to set up host texture info");
    return false;
  }

  size_t unpack_length = host_info.output_length;
  glTextureStorage1D(texture, 1, config.internal_format,
                     host_info.size_1d.output_width);

  auto allocation = scratch_buffer_->Acquire(unpack_length);

  if (!texture_info.is_tiled) {
    if (texture_info.size_1d.input_pitch == host_info.size_1d.output_pitch) {
      TextureSwap(texture_info.endianness, allocation.host_ptr, host_address,
                  unpack_length);
    } else {
      assert_always();
    }
  } else {
    assert_always();
  }
  size_t unpack_offset = allocation.offset;
  scratch_buffer_->Commit(std::move(allocation));
  // TODO(benvanik): avoid flush on entire buffer by using another texture
  // buffer.
  scratch_buffer_->Flush();

  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, scratch_buffer_->handle());
  if (texture_info.is_compressed()) {
    glCompressedTextureSubImage1D(texture, 0, 0, host_info.size_1d.output_width,
                                  config.format,
                                  static_cast<GLsizei>(unpack_length),
                                  reinterpret_cast<void*>(unpack_offset));
  } else {
    // Most of these don't seem to have an effect on compressed images.
    // glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE);
    // glPixelStorei(GL_UNPACK_ALIGNMENT, texture_info.texel_pitch);
    // glPixelStorei(GL_UNPACK_ROW_LENGTH, texture_info.size_2d.input_width);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTextureSubImage1D(texture, 0, 0, host_info.size_1d.output_width,
                        config.format, config.type,
                        reinterpret_cast<void*>(unpack_offset));
  }
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  return true;
}

bool TextureCache::UploadTexture2D(GLuint texture,
                                   const TextureInfo& texture_info) {
  SCOPE_profile_cpu_f("gpu");
  const auto host_address =
      memory_->TranslatePhysical(texture_info.guest_address);

  const auto& config =
      texture_configs[uint32_t(texture_info.format_info()->format)];
  if (config.format == GL_INVALID_ENUM) {
    assert_always("Unhandled texture format");
    return false;
  }

  HostTextureInfo host_info;
  if (!HostTextureInfo::Setup(texture_info, &host_info)) {
    assert_always("Failed to set up host texture info");
    return false;
  }

  size_t unpack_length = host_info.output_length;
  glTextureStorage2D(texture, 1, config.internal_format,
                     host_info.size_2d.output_width,
                     host_info.size_2d.output_height);

  auto allocation = scratch_buffer_->Acquire(unpack_length);

  if (!texture_info.is_tiled) {
    uint32_t offset_x, offset_y;
    if (texture_info.has_packed_mips &&
        TextureInfo::GetPackedTileOffset(texture_info, &offset_x, &offset_y)) {
      uint32_t bytes_per_block = texture_info.format_info()->block_width *
                                 texture_info.format_info()->block_height *
                                 texture_info.format_info()->bits_per_pixel / 8;
      const uint8_t* src = host_address;
      // TODO(gibbed): this needs checking
      src += offset_y * texture_info.size_2d.input_pitch;
      src += offset_x * bytes_per_block;
      uint8_t* dest = reinterpret_cast<uint8_t*>(allocation.host_ptr);
      uint32_t pitch = std::min(texture_info.size_2d.input_pitch,
                                host_info.size_2d.output_pitch);
      for (uint32_t y = 0; y < std::min(texture_info.size_2d.block_height,
                                        texture_info.size_2d.logical_height);
           y++) {
        TextureSwap(texture_info.endianness, dest, src, pitch);
        src += texture_info.size_2d.input_pitch;
        dest += host_info.size_2d.output_pitch;
      }
    } else if (texture_info.size_2d.input_pitch ==
               host_info.size_2d.output_pitch) {
      // Fast path copy entire image.
      TextureSwap(texture_info.endianness, allocation.host_ptr, host_address,
                  unpack_length);
    } else {
      // Slow path copy row-by-row because strides differ.
      // UNPACK_ROW_LENGTH only works for uncompressed images, and likely does
      // this exact thing under the covers, so we just always do it here.
      const uint8_t* src = host_address;
      uint8_t* dest = reinterpret_cast<uint8_t*>(allocation.host_ptr);
      uint32_t pitch = std::min(texture_info.size_2d.input_pitch,
                                host_info.size_2d.output_pitch);
      for (uint32_t y = 0; y < std::min(texture_info.size_2d.block_height,
                                        texture_info.size_2d.logical_height);
           y++) {
        TextureSwap(texture_info.endianness, dest, src, pitch);
        src += texture_info.size_2d.input_pitch;
        dest += host_info.size_2d.output_pitch;
      }
    }
  } else {
    // Untile image.
    // We could do this in a shader to speed things up, as this is pretty slow.

    // TODO(benvanik): optimize this inner loop (or work by tiles).
    const uint8_t* src = host_address;
    uint8_t* dest = reinterpret_cast<uint8_t*>(allocation.host_ptr);
    uint32_t bytes_per_block = texture_info.format_info()->block_width *
                               texture_info.format_info()->block_height *
                               texture_info.format_info()->bits_per_pixel / 8;

    // Tiled textures can be packed; get the offset into the packed texture.
    uint32_t offset_x;
    uint32_t offset_y;
    TextureInfo::GetPackedTileOffset(texture_info, &offset_x, &offset_y);

    auto bpp = (bytes_per_block >> 2) +
               ((bytes_per_block >> 1) >> (bytes_per_block >> 2));
    for (uint32_t y = 0, output_base_offset = 0;
         y < std::min(texture_info.size_2d.block_height,
                      texture_info.size_2d.logical_height);
         y++, output_base_offset += host_info.size_2d.output_pitch) {
      auto input_base_offset = TextureInfo::TiledOffset2DOuter(
          offset_y + y,
          (texture_info.size_2d.input_width /
           texture_info.format_info()->block_width),
          bpp);
      for (uint32_t x = 0, output_offset = output_base_offset;
           x < texture_info.size_2d.block_width;
           x++, output_offset += bytes_per_block) {
        auto input_offset =
            TextureInfo::TiledOffset2DInner(offset_x + x, offset_y + y, bpp,
                                            input_base_offset) >>
            bpp;
        TextureSwap(texture_info.endianness, dest + output_offset,
                    src + input_offset * bytes_per_block, bytes_per_block);
      }
    }
  }
  size_t unpack_offset = allocation.offset;
  scratch_buffer_->Commit(std::move(allocation));
  // TODO(benvanik): avoid flush on entire buffer by using another texture
  // buffer.
  scratch_buffer_->Flush();

  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, scratch_buffer_->handle());
  if (texture_info.is_compressed()) {
    glCompressedTextureSubImage2D(
        texture, 0, 0, 0, host_info.size_2d.output_width,
        host_info.size_2d.output_height, config.format,
        static_cast<GLsizei>(unpack_length),
        reinterpret_cast<void*>(unpack_offset));
  } else {
    // Most of these don't seem to have an effect on compressed images.
    // glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE);
    // glPixelStorei(GL_UNPACK_ALIGNMENT, texture_info.texel_pitch);
    // glPixelStorei(GL_UNPACK_ROW_LENGTH, texture_info.size_2d.input_width);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTextureSubImage2D(texture, 0, 0, 0, host_info.size_2d.output_width,
                        host_info.size_2d.output_height, config.format,
                        config.type, reinterpret_cast<void*>(unpack_offset));
  }
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  return true;
}

bool TextureCache::UploadTextureCube(GLuint texture,
                                     const TextureInfo& texture_info) {
  SCOPE_profile_cpu_f("gpu");
  const auto host_address =
      memory_->TranslatePhysical(texture_info.guest_address);

  const auto& config =
      texture_configs[uint32_t(texture_info.format_info()->format)];
  if (config.format == GL_INVALID_ENUM) {
    assert_always("Unhandled texture format");
    return false;
  }

  HostTextureInfo host_info;
  if (!HostTextureInfo::Setup(texture_info, &host_info)) {
    assert_always("Failed to set up host texture info");
    return false;
  }

  size_t unpack_length = host_info.output_length;
  glTextureStorage2D(texture, 1, config.internal_format,
                     host_info.size_cube.output_width,
                     host_info.size_cube.output_height);

  auto allocation = scratch_buffer_->Acquire(unpack_length);
  if (!texture_info.is_tiled) {
    if (texture_info.size_cube.input_pitch ==
        host_info.size_cube.output_pitch) {
      // Fast path copy entire image.
      TextureSwap(texture_info.endianness, allocation.host_ptr, host_address,
                  unpack_length);
    } else {
      // Slow path copy row-by-row because strides differ.
      // UNPACK_ROW_LENGTH only works for uncompressed images, and likely does
      // this exact thing under the covers, so we just always do it here.
      const uint8_t* src = host_address;
      uint8_t* dest = reinterpret_cast<uint8_t*>(allocation.host_ptr);
      for (int face = 0; face < 6; ++face) {
        uint32_t pitch = std::min(texture_info.size_cube.input_pitch,
                                  host_info.size_cube.output_pitch);
        for (uint32_t y = 0; y < texture_info.size_cube.block_height; y++) {
          TextureSwap(texture_info.endianness, dest, src, pitch);
          src += texture_info.size_cube.input_pitch;
          dest += host_info.size_cube.output_pitch;
        }
      }
    }
  } else {
    // TODO(benvanik): optimize this inner loop (or work by tiles).
    const uint8_t* src = host_address;
    uint8_t* dest = reinterpret_cast<uint8_t*>(allocation.host_ptr);
    uint32_t bytes_per_block = texture_info.format_info()->block_width *
                               texture_info.format_info()->block_height *
                               texture_info.format_info()->bits_per_pixel / 8;
    // Tiled textures can be packed; get the offset into the packed texture.
    uint32_t offset_x;
    uint32_t offset_y;
    TextureInfo::GetPackedTileOffset(texture_info, &offset_x, &offset_y);
    auto bpp = (bytes_per_block >> 2) +
               ((bytes_per_block >> 1) >> (bytes_per_block >> 2));
    for (int face = 0; face < 6; ++face) {
      for (uint32_t y = 0, output_base_offset = 0;
           y < texture_info.size_cube.block_height;
           y++, output_base_offset += host_info.size_cube.output_pitch) {
        auto input_base_offset = TextureInfo::TiledOffset2DOuter(
            offset_y + y,
            (texture_info.size_cube.input_width /
             texture_info.format_info()->block_width),
            bpp);
        for (uint32_t x = 0, output_offset = output_base_offset;
             x < texture_info.size_cube.block_width;
             x++, output_offset += bytes_per_block) {
          auto input_offset =
              TextureInfo::TiledOffset2DInner(offset_x + x, offset_y + y, bpp,
                                              input_base_offset) >>
              bpp;
          TextureSwap(texture_info.endianness, dest + output_offset,
                      src + input_offset * bytes_per_block, bytes_per_block);
        }
      }
      src += texture_info.size_cube.input_face_length;
      dest += host_info.size_cube.output_face_length;
    }
  }
  size_t unpack_offset = allocation.offset;
  scratch_buffer_->Commit(std::move(allocation));
  // TODO(benvanik): avoid flush on entire buffer by using another texture
  // buffer.
  scratch_buffer_->Flush();

  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, scratch_buffer_->handle());
  if (texture_info.is_compressed()) {
    glCompressedTextureSubImage3D(
        texture, 0, 0, 0, 0, host_info.size_cube.output_width,
        host_info.size_cube.output_height, 6, config.format,
        static_cast<GLsizei>(unpack_length),
        reinterpret_cast<void*>(unpack_offset));
  } else {
    // Most of these don't seem to have an effect on compressed images.
    // glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE);
    // glPixelStorei(GL_UNPACK_ALIGNMENT, texture_info.texel_pitch);
    // glPixelStorei(GL_UNPACK_ROW_LENGTH, texture_info.size_2d.input_width);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTextureSubImage3D(texture, 0, 0, 0, 0, host_info.size_cube.output_width,
                        host_info.size_cube.output_height, 6, config.format,
                        config.type, reinterpret_cast<void*>(unpack_offset));
  }
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  return true;
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
