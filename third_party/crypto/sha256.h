// //////////////////////////////////////////////////////////
// sha256.h
// Copyright (c) 2014,2015 Stephan Brumme. All rights reserved.
// see http://create.stephan-brumme.com/disclaimer.html
//
// Altered for use in Xenia. Licensed under zlib license when acquired.

#pragma once

#include <cstdint>
#include <string>

namespace sha256 {

/// compute SHA256 hash
/** Usage:
    SHA256 sha256;
    std::string myHash  = sha256("Hello World");     // std::string
    std::string myHash2 = sha256("How are you", 11); // arbitrary data, 11 bytes

    // or in a streaming fashion:

    SHA256 sha256;
    while (more data available)
      sha256.add(pointer to fresh data, number of new bytes);
    std::string myHash3 = sha256.getHash();
  */
class SHA256 {
 public:
  /// split into 64 byte blocks (=> 512 bits), hash is 32 bytes long
  enum { BlockSize = 512 / 8, HashBytes = 32 };

  /// same as reset()
  SHA256();

  /// compute SHA256 of a memory block
  std::string operator()(const void* data, size_t numBytes);
  /// compute SHA256 of a string, excluding final zero
  std::string operator()(const std::string& text);

  /// add arbitrary number of bytes
  void add(const void* data, size_t numBytes);

  /// return latest hash as 64 hex characters
  std::string getHash();
  /// return latest hash as bytes
  void getHash(unsigned char buffer[HashBytes]);

  uint8_t* getBuffer() { return m_buffer; }
  uint32_t* getHashValues() { return m_hash; }

  size_t getTotalSize() const { return m_numBytes + m_bufferSize; }
  void setTotalSize(size_t size) {
    m_bufferSize = size & (BlockSize - 1);
    m_numBytes = size & ~(BlockSize - 1);
  }

  /// restart
  void reset();

 private:
  /// process 64 bytes
  void processBlock(const void* data);
  /// process everything left in the internal buffer
  void processBuffer();

  /// size of processed data in bytes
  uint64_t m_numBytes;
  /// valid bytes in m_buffer
  size_t m_bufferSize;
  /// bytes not processed yet
  uint8_t m_buffer[BlockSize];

  enum { HashValues = HashBytes / 4 };
  /// hash, stored as integers
  uint32_t m_hash[HashValues];
};

};  // namespace sha256