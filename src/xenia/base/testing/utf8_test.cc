/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <string>
#include <vector>

#include "xenia/base/string.h"

#include "third_party/catch/include/catch.hpp"

namespace xe::base::test {

// https://www.cl.cam.ac.uk/~mgk25/ucs/examples/quickbrown.txt

TEST_CASE("utf8::split", "UTF-8 Split") {
  std::vector<std::string_view> parts;

  // Danish
  parts = utf8::split(
      u8"Quizdeltagerne spiste jordbær med fløde, mens cirkusklovnen Wolther "
      u8"spillede på xylofon.",
      u8"æcå");
  REQUIRE(parts.size() == 4);
  REQUIRE(parts[0] == u8"Quizdeltagerne spiste jordb");
  REQUIRE(parts[1] == u8"r med fløde, mens ");
  REQUIRE(parts[2] == u8"irkusklovnen Wolther spillede p");
  REQUIRE(parts[3] == u8" xylofon.");

  // German
  parts = utf8::split(
      u8"Falsches Üben von Xylophonmusik quält jeden größeren Zwerg\n"
      u8"Zwölf Boxkämpfer jagten Eva quer über den Sylter Deich\n"
      u8"Heizölrückstoßabdämpfung",
      u8"ßS");
  REQUIRE(parts.size() == 4);
  REQUIRE(parts[0] == u8"Falsches Üben von Xylophonmusik quält jeden grö");
  REQUIRE(parts[1] ==
          u8"eren Zwerg\nZwölf Boxkämpfer jagten Eva quer über den ");
  REQUIRE(parts[2] == u8"ylter Deich\nHeizölrücksto");
  REQUIRE(parts[3] == u8"abdämpfung");

  // Greek
  parts = utf8::split(
      u8"Γαζέες καὶ μυρτιὲς δὲν θὰ βρῶ πιὰ στὸ χρυσαφὶ ξέφωτο\n"
      u8"Ξεσκεπάζω τὴν ψυχοφθόρα βδελυγμία",
      u8"πφ");
  REQUIRE(parts.size() == 6);
  REQUIRE(parts[0] == u8"Γαζέες καὶ μυρτιὲς δὲν θὰ βρῶ ");
  REQUIRE(parts[1] == u8"ιὰ στὸ χρυσα");
  REQUIRE(parts[2] == u8"ὶ ξέ");
  REQUIRE(parts[3] == u8"ωτο\nΞεσκε");
  REQUIRE(parts[4] == u8"άζω τὴν ψυχο");
  REQUIRE(parts[5] == u8"θόρα βδελυγμία");

  // English
  parts = utf8::split("The quick brown fox jumps over the lazy dog", "xy");
  REQUIRE(parts.size() == 3);
  REQUIRE(parts[0] == u8"The quick brown fo");
  REQUIRE(parts[1] == u8" jumps over the laz");
  REQUIRE(parts[2] == u8" dog");

  // Spanish
  parts = utf8::split(
      u8"El pingüino Wenceslao hizo kilómetros bajo exhaustiva lluvia y "
      u8"frío, añoraba a su querido cachorro.",
      u8"ójd");
  REQUIRE(parts.size() == 4);
  REQUIRE(parts[0] == u8"El pingüino Wenceslao hizo kil");
  REQUIRE(parts[1] == u8"metros ba");
  REQUIRE(parts[2] == u8"o exhaustiva lluvia y frío, añoraba a su queri");
  REQUIRE(parts[3] == u8"o cachorro.");

  // TODO(gibbed): French
  // TODO(gibbed): Irish Gaelic
  // TODO(gibbed): Hungarian
  // TODO(gibbed): Icelandic
  // TODO(gibbed): Japanese
  // TODO(gibbed): Hebrew
  // TODO(gibbed): Polish
  // TODO(gibbed): Russian
  // TODO(gibbed): Thai
  // TODO(gibbed): Turkish
}

TEST_CASE("utf8::equal_z", "UTF-8 Equal Z") {
  REQUIRE(utf8::equal_z(u8"foo", u8"foo\0"));
  REQUIRE_FALSE(utf8::equal_z(u8"bar", u8"baz\0"));
}

TEST_CASE("utf8::equal_case_z", "UTF-8 Equal Case Z") {
  REQUIRE(utf8::equal_z(u8"foo", u8"foo\0"));
  REQUIRE_FALSE(utf8::equal_z(u8"bar", u8"baz\0"));
}

TEST_CASE("utf8::join_paths", "UTF-8 Join Paths") {
  REQUIRE(utf8::join_paths({u8"X:", u8"foo", u8"bar", u8"baz", u8"qux"},
                           '\\') == "X:\\foo\\bar\\baz\\qux");
  REQUIRE(utf8::join_paths({u8"X:", u8"foo", u8"bar", u8"baz", u8"qux"}, '/') ==
          "X:/foo/bar/baz/qux");
}

TEST_CASE("utf8::fix_path_separators", "UTF-8 Fix Path Separators") {
  REQUIRE(utf8::fix_path_separators("X:\\foo/bar\\baz/qux", '\\') ==
          "X:\\foo\\bar\\baz\\qux");
  REQUIRE(utf8::fix_path_separators("X:\\foo/bar\\baz/qux", '/') ==
          "X:/foo/bar/baz/qux");
}

TEST_CASE("utf8::find_name_from_path", "UTF-8 Find Name From Path") {
  REQUIRE(utf8::find_name_from_path("X:\\foo\\bar\\baz\\qux", '\\') == "qux");
  REQUIRE(utf8::find_name_from_path("X:/foo/bar/baz/qux", '/') == "qux");
}

TEST_CASE("utf8::find_base_path", "UTF-8 Find Base Path") {
  REQUIRE(utf8::find_base_path("X:\\foo\\bar\\baz\\qux", '\\') ==
          "X:\\foo\\bar\\baz");
  REQUIRE(utf8::find_base_path("X:/foo/bar/baz/qux", '/') == "X:/foo/bar/baz");
}

TEST_CASE("utf8::canonicalize_path", "UTF-8 Canonicalize Path") {
  REQUIRE(utf8::canonicalize_path("X:\\foo\\bar\\baz\\qux", '\\') ==
          "X:\\foo\\bar\\baz\\qux");
  REQUIRE(utf8::canonicalize_path("X:\\foo\\.\\baz\\qux", '\\') ==
          "X:\\foo\\baz\\qux");
  REQUIRE(utf8::canonicalize_path("X:\\foo\\..\\baz\\qux", '\\') ==
          "X:\\baz\\qux");
  REQUIRE(utf8::canonicalize_path("X:\\.\\bar\\baz\\qux", '\\') ==
          "X:\\bar\\baz\\qux");
  REQUIRE(utf8::canonicalize_path("X:\\..\\bar\\baz\\qux", '\\') ==
          "X:\\bar\\baz\\qux");
}

}  // namespace xe::base::test
