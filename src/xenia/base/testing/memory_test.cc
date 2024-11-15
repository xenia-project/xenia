/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/memory.h"

#include "third_party/catch/include/catch.hpp"
#include "third_party/fmt/include/fmt/format.h"

#include "xenia/base/clock.h"

#include <array>

namespace xe {
namespace base {
namespace test {

TEST_CASE("copy_128_aligned", "[copy_and_swap]") {
  alignas(128) uint8_t src[256], dest[256];
  for (uint8_t i = 0; i < 255; ++i) {
    src[i] = 255 - i;
  }
  std::memset(dest, 0, sizeof(dest));
  copy_128_aligned(dest, src, 1);
  REQUIRE(std::memcmp(dest, src, 128));
  REQUIRE(dest[128] == 0);

  std::memset(dest, 0, sizeof(dest));
  copy_128_aligned(dest, src, 2);
  REQUIRE(std::memcmp(dest, src, 256));

  std::memset(dest, 0, sizeof(dest));
  copy_128_aligned(dest, src + 1, 1);
  REQUIRE(std::memcmp(dest, src + 1, 128));
}

TEST_CASE("copy_and_swap_16_aligned", "[copy_and_swap]") {
  alignas(16) uint16_t a = 0x1111, b = 0xABCD;
  copy_and_swap_16_aligned(&a, &b, 1);
  REQUIRE(a == 0xCDAB);
  REQUIRE(b == 0xABCD);

  alignas(16) uint16_t c[] = {0x0000, 0x0000, 0x0000, 0x0000};
  alignas(16) uint16_t d[] = {0x0123, 0x4567, 0x89AB, 0xCDEF};
  copy_and_swap_16_aligned(c, d, 1);
  REQUIRE(c[0] == 0x2301);
  REQUIRE(c[1] == 0x0000);
  REQUIRE(c[2] == 0x0000);
  REQUIRE(c[3] == 0x0000);

  copy_and_swap_16_aligned(c, d, 3);
  REQUIRE(c[0] == 0x2301);
  REQUIRE(c[1] == 0x6745);
  REQUIRE(c[2] == 0xAB89);
  REQUIRE(c[3] == 0x0000);

  copy_and_swap_16_aligned(c, d, 4);
  REQUIRE(c[0] == 0x2301);
  REQUIRE(c[1] == 0x6745);
  REQUIRE(c[2] == 0xAB89);
  REQUIRE(c[3] == 0xEFCD);

  alignas(16) uint64_t e;
  copy_and_swap_16_aligned(&e, d, 4);
  REQUIRE(e == 0xEFCDAB8967452301);

  alignas(16) char f[85] = {0x00};
  alignas(16) char g[] =
      "This is a 85 byte long string... "
      "It's supposed to be longer than standard alignment.";
  copy_and_swap_16_aligned(f, g, 42);
  REQUIRE(std::strcmp(f,
                      "hTsii  s a58b ty eolgns rtni.g..I 't susppsodet  oebl "
                      "noeg rhtnas atdnra dlagimnne.t") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_16_aligned(f, g + 16, 34);
  REQUIRE(std::strcmp(f,
                      " eolgns rtni.g..I 't susppsodet  oebl "
                      "noeg rhtnas atdnra dlagimnne.t") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_16_aligned(f, g + 32, 26);
  REQUIRE(std::strcmp(f,
                      "I 't susppsodet  oebl "
                      "noeg rhtnas atdnra dlagimnne.t") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_16_aligned(f, g + 64, 10);
  REQUIRE(std::strcmp(f, "s atdnra dlagimnne.t") == 0);
}

TEST_CASE("copy_and_swap_16_unaligned", "[copy_and_swap]") {
  uint16_t a = 0x1111, b = 0xABCD;
  copy_and_swap_16_unaligned(&a, &b, 1);
  REQUIRE(a == 0xCDAB);
  REQUIRE(b == 0xABCD);

  uint16_t c[] = {0x0000, 0x0000, 0x0000, 0x0000};
  uint16_t d[] = {0x0123, 0x4567, 0x89AB, 0xCDEF};
  copy_and_swap_16_unaligned(c, d, 1);
  REQUIRE(c[0] == 0x2301);
  REQUIRE(c[1] == 0x0000);
  REQUIRE(c[2] == 0x0000);
  REQUIRE(c[3] == 0x0000);

  copy_and_swap_16_unaligned(c, d, 4);
  REQUIRE(c[0] == 0x2301);
  REQUIRE(c[1] == 0x6745);
  REQUIRE(c[2] == 0xAB89);
  REQUIRE(c[3] == 0xEFCD);

  {
    constexpr size_t count = 100;
    std::array<uint8_t, count * 2> src{};
    std::array<uint8_t, count * 2> dst{};
    for (size_t i = 0; i < src.size(); ++i) {
      src[i] = static_cast<uint8_t>(i) + 1;  // no zero in array
    }
    copy_and_swap_16_unaligned(dst.data(), src.data(), count);
    for (size_t i = 0; i < src.size(); i += 2) {
      // Check src is untouched
      REQUIRE(static_cast<size_t>(src[i + 0]) == i + 1);
      REQUIRE(static_cast<size_t>(src[i + 1]) == i + 2);
      // Check swapped bytes
      REQUIRE(static_cast<size_t>(dst[i]) == static_cast<size_t>(src[i + 1]));
      REQUIRE(static_cast<size_t>(dst[i + 1]) == static_cast<size_t>(src[i]));
    }
  }

  uint64_t e;
  copy_and_swap_16_unaligned(&e, d, 4);
  REQUIRE(e == 0xEFCDAB8967452301);

  char f[85] = {0x00};
  char g[] =
      "This is a 85 byte long string... "
      "It's supposed to be longer than standard alignment.";
  copy_and_swap_16_unaligned(f, g, 42);
  REQUIRE(std::strcmp(f,
                      "hTsii  s a58b ty eolgns rtni.g..I 't susppsodet  oebl "
                      "noeg rhtnas atdnra dlagimnne.t") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_16_unaligned(f, g + 1, 41);
  REQUIRE(std::strcmp(f,
                      "ih ssia 8  5ybetl no gtsirgn.. .tIs's puopes dotb  "
                      "eolgnret ah ntsnaaddra ilngemtn") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_16_unaligned(f, g + 2, 41);
  REQUIRE(std::strcmp(f,
                      "sii  s a58b ty eolgns rtni.g..I 't susppsodet  oebl "
                      "noeg rhtnas atdnra dlagimnne.t") == 0);
}

TEST_CASE("copy_and_swap_32_aligned", "[copy_and_swap]") {
  alignas(32) uint32_t a = 0x11111111, b = 0x89ABCDEF;
  copy_and_swap_32_aligned(&a, &b, 1);
  REQUIRE(a == 0xEFCDAB89);
  REQUIRE(b == 0x89ABCDEF);

  alignas(32) uint32_t c[] = {0x00000000, 0x00000000, 0x00000000, 0x00000000};
  alignas(32) uint32_t d[] = {0x01234567, 0x89ABCDEF, 0xE887EEED, 0xD8514199};
  copy_and_swap_32_aligned(c, d, 1);
  REQUIRE(c[0] == 0x67452301);
  REQUIRE(c[1] == 0x00000000);
  REQUIRE(c[2] == 0x00000000);
  REQUIRE(c[3] == 0x00000000);

  copy_and_swap_32_aligned(c, d, 3);
  REQUIRE(c[0] == 0x67452301);
  REQUIRE(c[1] == 0xEFCDAB89);
  REQUIRE(c[2] == 0xEDEE87E8);
  REQUIRE(c[3] == 0x00000000);

  copy_and_swap_32_aligned(c, d, 4);
  REQUIRE(c[0] == 0x67452301);
  REQUIRE(c[1] == 0xEFCDAB89);
  REQUIRE(c[2] == 0xEDEE87E8);
  REQUIRE(c[3] == 0x994151D8);

  alignas(32) uint64_t e;
  copy_and_swap_32_aligned(&e, d, 2);
  REQUIRE(e == 0xEFCDAB8967452301);

  alignas(32) char f[85] = {0x00};
  alignas(32) char g[] =
      "This is a 85 byte long string... "
      "It's supposed to be longer than standard alignment.";
  copy_and_swap_32_aligned(f, g, 21);
  REQUIRE(std::strcmp(f,
                      "sihT si 58 atyb ol es gnnirt...g'tI us ssoppt deeb "
                      "onol  regnahtats radnla dmngi.tne") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_32_aligned(f, g + 16, 17);
  REQUIRE(std::strcmp(f,
                      "ol es gnnirt...g'tI us ssoppt deeb "
                      "onol  regnahtats radnla dmngi.tne") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_32_aligned(f, g + 32, 13);
  REQUIRE(std::strcmp(f,
                      "'tI us ssoppt deeb "
                      "onol  regnahtats radnla dmngi.tne") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_32_aligned(f, g + 64, 5);
  REQUIRE(std::strcmp(f, "ats radnla dmngi.tne") == 0);
}

TEST_CASE("copy_and_swap_32_unaligned", "[copy_and_swap]") {
  uint32_t a = 0x11111111, b = 0x89ABCDEF;
  copy_and_swap_32_unaligned(&a, &b, 1);
  REQUIRE(a == 0xEFCDAB89);
  REQUIRE(b == 0x89ABCDEF);

  uint32_t c[] = {0x00000000, 0x00000000, 0x00000000, 0x00000000};
  uint32_t d[] = {0x01234567, 0x89ABCDEF, 0xE887EEED, 0xD8514199};
  copy_and_swap_32_unaligned(c, d, 1);
  REQUIRE(c[0] == 0x67452301);
  REQUIRE(c[1] == 0x00000000);
  REQUIRE(c[2] == 0x00000000);
  REQUIRE(c[3] == 0x00000000);

  copy_and_swap_32_unaligned(c, d, 3);
  REQUIRE(c[0] == 0x67452301);
  REQUIRE(c[1] == 0xEFCDAB89);
  REQUIRE(c[2] == 0xEDEE87E8);
  REQUIRE(c[3] == 0x00000000);

  copy_and_swap_32_unaligned(c, d, 4);
  REQUIRE(c[0] == 0x67452301);
  REQUIRE(c[1] == 0xEFCDAB89);
  REQUIRE(c[2] == 0xEDEE87E8);
  REQUIRE(c[3] == 0x994151D8);

  {
    constexpr size_t count = 17;
    std::array<uint8_t, count * 4> src{};
    std::array<uint8_t, count * 4> dst{};
    for (size_t i = 0; i < src.size(); ++i) {
      src[i] = static_cast<uint8_t>(i) + 1;  // no zero in array
    }
    copy_and_swap_32_unaligned(dst.data(), src.data(), count);
    for (size_t i = 0; i < src.size(); i += 4) {
      // Check src is untouched
      REQUIRE(static_cast<size_t>(src[i + 0]) == i + 1);
      REQUIRE(static_cast<size_t>(src[i + 1]) == i + 2);
      REQUIRE(static_cast<size_t>(src[i + 2]) == i + 3);
      REQUIRE(static_cast<size_t>(src[i + 3]) == i + 4);
      // Check swapped bytes
      REQUIRE(static_cast<size_t>(dst[i + 0]) ==
              static_cast<size_t>(src[i + 3]));
      REQUIRE(static_cast<size_t>(dst[i + 1]) ==
              static_cast<size_t>(src[i + 2]));
      REQUIRE(static_cast<size_t>(dst[i + 2]) ==
              static_cast<size_t>(src[i + 1]));
      REQUIRE(static_cast<size_t>(dst[i + 3]) ==
              static_cast<size_t>(src[i + 0]));
    }
  }

  uint64_t e;
  copy_and_swap_32_unaligned(&e, d, 2);
  REQUIRE(e == 0xEFCDAB8967452301);

  char f[85] = {0x00};
  char g[] =
      "This is a 85 byte long string... "
      "It's supposed to be longer than standard alignment.";
  copy_and_swap_32_unaligned(f, g, 21);
  REQUIRE(std::strcmp(f,
                      "sihT si 58 atyb ol es gnnirt...g'tI us ssoppt deeb "
                      "onol  regnahtats radnla dmngi.tne") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_32_unaligned(f, g + 1, 20);
  REQUIRE(std::strcmp(f,
                      " siha si 58 etybnol ts ggnir ...s'tIpus esopot d eb "
                      "gnolt re nahnatsdradila emng") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_32_unaligned(f, g + 2, 20);
  REQUIRE(std::strcmp(f,
                      "i si a sb 58 etygnolrts .gniI .. s'tppusdeso ot l "
                      "ebegnoht rs nadnat dragilanemn") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_32_unaligned(f, g + 3, 20);
  REQUIRE(std::strcmp(f,
                      "si s8 a yb 5l et gnoirts..gntI .s s'oppu desb otol "
                      "eregnaht ts nadnaa drngiltnem") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_32_unaligned(f, g + 4, 20);
  REQUIRE(std::strcmp(f,
                      " si 58 atyb ol es gnnirt...g'tI us ssoppt deeb onol  "
                      "regnahtats radnla dmngi.tne") == 0);
}

TEST_CASE("copy_and_swap_64_aligned", "[copy_and_swap]") {
  alignas(64) uint64_t a = 0x1111111111111111, b = 0x0123456789ABCDEF;
  copy_and_swap_64_aligned(&a, &b, 1);
  REQUIRE(a == 0xEFCDAB8967452301);
  REQUIRE(b == 0x0123456789ABCDEF);

  alignas(64) uint64_t c[] = {0x0000000000000000, 0x0000000000000000,
                              0x0000000000000000, 0x0000000000000000};
  alignas(64) uint64_t d[] = {0x0123456789ABCDEF, 0xE887EEEDD8514199,
                              0x21D4745A1D4A7706, 0xA4174FED675766E3};
  copy_and_swap_64_aligned(c, d, 1);
  REQUIRE(c[0] == 0xEFCDAB8967452301);
  REQUIRE(c[1] == 0x0000000000000000);
  REQUIRE(c[2] == 0x0000000000000000);
  REQUIRE(c[3] == 0x0000000000000000);

  copy_and_swap_64_aligned(c, d, 3);
  REQUIRE(c[0] == 0xEFCDAB8967452301);
  REQUIRE(c[1] == 0x994151D8EDEE87E8);
  REQUIRE(c[2] == 0x06774A1D5A74D421);
  REQUIRE(c[3] == 0x0000000000000000);

  copy_and_swap_64_aligned(c, d, 4);
  REQUIRE(c[0] == 0xEFCDAB8967452301);
  REQUIRE(c[1] == 0x994151D8EDEE87E8);
  REQUIRE(c[2] == 0x06774A1D5A74D421);
  REQUIRE(c[3] == 0xE3665767ED4F17A4);

  alignas(64) uint64_t e;
  copy_and_swap_64_aligned(&e, d, 1);
  REQUIRE(e == 0xEFCDAB8967452301);

  alignas(64) char f[85] = {0x00};
  alignas(64) char g[] =
      "This is a 85 byte long string... "
      "It's supposed to be longer than standard alignment.";
  copy_and_swap_64_aligned(f, g, 10);
  REQUIRE(std::strcmp(f,
                      " si sihTtyb 58 as gnol e...gnirtus s'tI t desoppnol eb "
                      "onaht regradnats mngila d") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_64_aligned(f, g + 16, 8);
  REQUIRE(std::strcmp(f,
                      "s gnol e...gnirtus s'tI t desoppnol eb "
                      "onaht regradnats mngila d") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_64_aligned(f, g + 32, 6);
  REQUIRE(std::strcmp(f,
                      "us s'tI t desoppnol eb "
                      "onaht regradnats mngila d") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_64_aligned(f, g + 64, 2);
  REQUIRE(std::strcmp(f, "radnats mngila d") == 0);
}

TEST_CASE("copy_and_swap_64_unaligned", "[copy_and_swap]") {
  uint64_t a = 0x1111111111111111, b = 0x0123456789ABCDEF;
  copy_and_swap_64_unaligned(&a, &b, 1);
  REQUIRE(a == 0xEFCDAB8967452301);
  REQUIRE(b == 0x0123456789ABCDEF);

  uint64_t c[] = {0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
                  0x0000000000000000};
  uint64_t d[] = {0x0123456789ABCDEF, 0xE887EEEDD8514199, 0x21D4745A1D4A7706,
                  0xA4174FED675766E3};
  copy_and_swap_64_unaligned(c, d, 1);
  REQUIRE(c[0] == 0xEFCDAB8967452301);
  REQUIRE(c[1] == 0x0000000000000000);
  REQUIRE(c[2] == 0x0000000000000000);
  REQUIRE(c[3] == 0x0000000000000000);

  copy_and_swap_64_unaligned(c, d, 3);
  REQUIRE(c[0] == 0xEFCDAB8967452301);
  REQUIRE(c[1] == 0x994151D8EDEE87E8);
  REQUIRE(c[2] == 0x06774A1D5A74D421);
  REQUIRE(c[3] == 0x0000000000000000);

  copy_and_swap_64_unaligned(c, d, 4);
  REQUIRE(c[0] == 0xEFCDAB8967452301);
  REQUIRE(c[1] == 0x994151D8EDEE87E8);
  REQUIRE(c[2] == 0x06774A1D5A74D421);
  REQUIRE(c[3] == 0xE3665767ED4F17A4);

  uint64_t e;
  copy_and_swap_64_unaligned(&e, d, 1);
  REQUIRE(e == 0xEFCDAB8967452301);

  char f[85] = {0x00};
  char g[] =
      "This is a 85 byte long string... "
      "It's supposed to be longer than standard alignment.";
  copy_and_swap_64_unaligned(f, g, 10);
  REQUIRE(std::strcmp(f,
                      " si sihTtyb 58 as gnol e...gnirtus s'tI t desoppnol eb "
                      "onaht regradnats mngila d") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_64_unaligned(f, g + 1, 10);
  REQUIRE(std::strcmp(f,
                      "a si sihetyb 58 ts gnol  ...gnirpus s'tIot desopgnol "
                      "eb  naht redradnatsemngila ") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_64_unaligned(f, g + 2, 10);
  REQUIRE(std::strcmp(f,
                      " a si si etyb 58rts gnolI ...gnippus s't ot desoegnol "
                      "ebs naht r dradnatnemngila") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_64_unaligned(f, g + 3, 10);
  REQUIRE(std::strcmp(f,
                      "8 a si sl etyb 5irts gnotI ...gnoppus s'b ot desregnol "
                      "ets naht a dradnatnemngil") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_64_unaligned(f, g + 4, 10);
  REQUIRE(std::strcmp(f,
                      "58 a si ol etyb nirts gn'tI ...gsoppus seb ot de "
                      "regnol ats nahtla dradn.tnemngi") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_64_unaligned(f, g + 5, 9);
  REQUIRE(std::strcmp(f,
                      " 58 a sinol etybgnirts gs'tI ...esoppus  eb ot dt "
                      "regnolnats nahila drad") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_64_unaligned(f, g + 6, 9);
  REQUIRE(std::strcmp(f,
                      "b 58 a sgnol ety.gnirts  s'tI ..desoppusl eb ot ht "
                      "regnodnats nagila dra") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_64_unaligned(f, g + 7, 9);
  REQUIRE(std::strcmp(f,
                      "yb 58 a  gnol et..gnirtss s'tI . desoppuol eb otaht "
                      "regnadnats nngila dr") == 0);

  std::memset(f, 0, sizeof(f));
  copy_and_swap_64_unaligned(f, g + 8, 9);
  REQUIRE(std::strcmp(f,
                      "tyb 58 as gnol e...gnirtus s'tI t desoppnol eb onaht "
                      "regradnats mngila d") == 0);
}

TEST_CASE("copy_and_swap_16_in_32_aligned", "[copy_and_swap]") {
  constexpr size_t count = 17;
  alignas(16) std::array<uint8_t, count * 4> src{};
  alignas(16) std::array<uint8_t, count * 4> dst{};

  // Check alignment (if this fails, adjust allocation)
  REQUIRE((reinterpret_cast<uintptr_t>(src.data()) & 0xF) == 0);
  REQUIRE((reinterpret_cast<uintptr_t>(dst.data()) & 0xF) == 0);

  for (size_t i = 0; i < src.size(); ++i) {
    src[i] = static_cast<uint8_t>(i) + 1;  // no zero in array
  }

  copy_and_swap_16_in_32_aligned(dst.data(), src.data(), count);

  for (size_t i = 0; i < src.size(); i += 4) {
    // Check src is untouched
    REQUIRE(static_cast<size_t>(src[i + 0]) == i + 1);
    REQUIRE(static_cast<size_t>(src[i + 1]) == i + 2);
    REQUIRE(static_cast<size_t>(src[i + 2]) == i + 3);
    REQUIRE(static_cast<size_t>(src[i + 3]) == i + 4);
    // Check swapped bytes
    REQUIRE(static_cast<size_t>(dst[i + 0]) == static_cast<size_t>(src[i + 2]));
    REQUIRE(static_cast<size_t>(dst[i + 1]) == static_cast<size_t>(src[i + 3]));
    REQUIRE(static_cast<size_t>(dst[i + 2]) == static_cast<size_t>(src[i + 0]));
    REQUIRE(static_cast<size_t>(dst[i + 3]) == static_cast<size_t>(src[i + 1]));
  }
}

TEST_CASE("copy_and_swap_16_in_32_unaligned", "[copy_and_swap]") {
  constexpr size_t count = 17;
  std::array<uint8_t, count * 4> src{};
  std::array<uint8_t, count * 4> dst{};
  for (size_t i = 0; i < src.size(); ++i) {
    src[i] = static_cast<uint8_t>(i) + 1;  // no zero in array
  }

  copy_and_swap_16_in_32_unaligned(dst.data(), src.data(), count);

  for (size_t i = 0; i < src.size(); i += 4) {
    // Check src is untouched
    REQUIRE(static_cast<size_t>(src[i + 0]) == i + 1);
    REQUIRE(static_cast<size_t>(src[i + 1]) == i + 2);
    REQUIRE(static_cast<size_t>(src[i + 2]) == i + 3);
    REQUIRE(static_cast<size_t>(src[i + 3]) == i + 4);
    // Check swapped bytes
    REQUIRE(static_cast<size_t>(dst[i + 0]) == static_cast<size_t>(src[i + 2]));
    REQUIRE(static_cast<size_t>(dst[i + 1]) == static_cast<size_t>(src[i + 3]));
    REQUIRE(static_cast<size_t>(dst[i + 2]) == static_cast<size_t>(src[i + 0]));
    REQUIRE(static_cast<size_t>(dst[i + 3]) == static_cast<size_t>(src[i + 1]));
  }
}

TEST_CASE("create_and_close_file_mapping", "Virtual Memory Mapping") {
  auto path = fmt::format("xenia_test_{}", Clock::QueryHostTickCount());
  auto memory = xe::memory::CreateFileMappingHandle(
      path, 0x100, xe::memory::PageAccess::kReadWrite, true);
  REQUIRE(memory != xe::memory::kFileMappingHandleInvalid);
  xe::memory::CloseFileMappingHandle(memory, path);
}

TEST_CASE("map_view", "[virtual_memory_mapping]") {
  auto path = fmt::format("xenia_test_{}", Clock::QueryHostTickCount());
  const size_t length = 0x100;
  auto memory = xe::memory::CreateFileMappingHandle(
      path, length, xe::memory::PageAccess::kReadWrite, true);
  REQUIRE(memory != xe::memory::kFileMappingHandleInvalid);

  uintptr_t address = 0x200000000;
  auto view =
      xe::memory::MapFileView(memory, reinterpret_cast<void*>(address), length,
                              xe::memory::PageAccess::kReadWrite, 0);
  REQUIRE(reinterpret_cast<uintptr_t>(view) == address);

  xe::memory::UnmapFileView(memory, reinterpret_cast<void*>(address), length);
  xe::memory::CloseFileMappingHandle(memory, path);
}

TEST_CASE("read_write_view", "[virtual_memory_mapping]") {
  const size_t length = 0x100;
  auto path = fmt::format("xenia_test_{}", Clock::QueryHostTickCount());
  auto memory = xe::memory::CreateFileMappingHandle(
      path, length, xe::memory::PageAccess::kReadWrite, true);
  REQUIRE(memory != xe::memory::kFileMappingHandleInvalid);

  uintptr_t address = 0x200000000;
  auto view =
      xe::memory::MapFileView(memory, reinterpret_cast<void*>(address), length,
                              xe::memory::PageAccess::kReadWrite, 0);
  REQUIRE(reinterpret_cast<uintptr_t>(view) == address);

  for (uint32_t i = 0; i < length; i += sizeof(uint8_t)) {
    auto p_value = reinterpret_cast<uint8_t*>(address + i);
    *p_value = i;
  }
  for (uint32_t i = 0; i < length; i += sizeof(uint8_t)) {
    auto p_value = reinterpret_cast<uint8_t*>(address + i);
    uint8_t value = *p_value;
    REQUIRE(value == i);
  }

  xe::memory::UnmapFileView(memory, reinterpret_cast<void*>(address), length);
  xe::memory::CloseFileMappingHandle(memory, path);
}

TEST_CASE("make_fourcc", "[fourcc]") {
  SECTION("'1234'") {
    const uint32_t fourcc_host = 0x31323334;
    constexpr fourcc_t fourcc_1 = make_fourcc('1', '2', '3', '4');
    constexpr fourcc_t fourcc_2 = make_fourcc("1234");
    REQUIRE(fourcc_1 == fourcc_host);
    REQUIRE(fourcc_2 == fourcc_host);
    REQUIRE(fourcc_1 == fourcc_2);
    REQUIRE(fourcc_2 == fourcc_1);
  }

  SECTION("'ABcd'") {
    const uint32_t fourcc_host = 0x41426364;
    constexpr fourcc_t fourcc_1 = make_fourcc('A', 'B', 'c', 'd');
    constexpr fourcc_t fourcc_2 = make_fourcc("ABcd");
    REQUIRE(fourcc_1 == fourcc_host);
    REQUIRE(fourcc_2 == fourcc_host);
    REQUIRE(fourcc_1 == fourcc_2);
    REQUIRE(fourcc_2 == fourcc_1);
  }

  SECTION("'XEN\\0'") {
    const uint32_t fourcc_host = 0x58454E00;
    constexpr fourcc_t fourcc = make_fourcc('X', 'E', 'N', '\0');
    REQUIRE(fourcc == fourcc_host);
  }

  SECTION("length()!=4") {
    REQUIRE_THROWS(make_fourcc("AB\0\0"));
    REQUIRE_THROWS(make_fourcc("AB\0\0AB"));
    REQUIRE_THROWS(make_fourcc("ABCDEFGH"));
  }
}

}  // namespace test
}  // namespace base
}  // namespace xe
