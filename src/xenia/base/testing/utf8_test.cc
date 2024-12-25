/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <string>
#include <vector>

#include "xenia/base/string.h"

#include "third_party/catch/include/catch.hpp"

namespace xe::base::test {

// TODO(gibbed): bit messy?
// TODO(gibbed): predicate variant?

#define TEST_EXAMPLE(func, left, right) REQUIRE(func(left) == right)

#define TEST_EXAMPLES_1(func, language, results) \
  TEST_EXAMPLE(func, examples::k##language##Values[0], results.language[0])
#define TEST_EXAMPLES_2(func, language, results)                             \
  TEST_EXAMPLE(func, examples::k##language##Values[0], results.language[0]); \
  TEST_EXAMPLE(func, examples::k##language##Values[1], results.language[1])
#define TEST_EXAMPLES_3(func, language, results)                             \
  TEST_EXAMPLE(func, examples::k##language##Values[0], results.language[0]); \
  TEST_EXAMPLE(func, examples::k##language##Values[1], results.language[1]); \
  TEST_EXAMPLE(func, examples::k##language##Values[2], results.language[2])

namespace examples {

// https://www.cl.cam.ac.uk/~mgk25/ucs/examples/quickbrown.txt

const size_t kDanishCount = 1;
const char* kDanishValues[kDanishCount] = {
    "Quizdeltagerne spiste jordbær med fløde, mens cirkusklovnen Wolther "
    "spillede på xylofon.",
};
#define TEST_LANGUAGE_EXAMPLES_Danish(func, results) \
  TEST_EXAMPLES_1(func, Danish, results)

const size_t kGermanCount = 3;
const char* kGermanValues[kGermanCount] = {
    "Falsches Üben von Xylophonmusik quält jeden größeren Zwerg",
    "Zwölf Boxkämpfer jagten Eva quer über den Sylter Deich",
    "Heizölrückstoßabdämpfung",
};
#define TEST_LANGUAGE_EXAMPLES_German(func, results) \
  TEST_EXAMPLES_2(func, German, results)

const size_t kGreekCount = 2;
const char* kGreekValues[kGreekCount] = {
    "Γαζέες καὶ μυρτιὲς δὲν θὰ βρῶ πιὰ στὸ χρυσαφὶ ξέφωτο",
    "Ξεσκεπάζω τὴν ψυχοφθόρα βδελυγμία",
};
#define TEST_LANGUAGE_EXAMPLES_Greek(func, results) \
  TEST_EXAMPLES_2(func, Greek, results)

const size_t kEnglishCount = 1;
const char* kEnglishValues[kEnglishCount] = {
    "The quick brown fox jumps over the lazy dog",
};
#define TEST_LANGUAGE_EXAMPLES_English(func, results) \
  TEST_EXAMPLES_1(func, English, results)

const size_t kSpanishCount = 1;
const char* kSpanishValues[kSpanishCount] = {
    "El pingüino Wenceslao hizo kilómetros bajo exhaustiva lluvia y frío, "
    "añoraba a su querido cachorro.",
};
#define TEST_LANGUAGE_EXAMPLES_Spanish(func, results) \
  TEST_EXAMPLES_1(func, Spanish, results)

const size_t kFrenchCount = 3;
const char* kFrenchValues[kFrenchCount] = {
    "Portez ce vieux whisky au juge blond qui fume sur son île intérieure, à "
    "côté de l'alcôve ovoïde, où les bûches se consument dans l'âtre, ce qui "
    "lui permet de penser à la cænogenèse de l'être dont il est question "
    "dans la cause ambiguë entendue à Moÿ, dans un capharnaüm qui, "
    "pense-t-il, diminue çà et là la qualité de son œuvre.",
    "l'île exiguë\n"
    "Où l'obèse jury mûr\n"
    "Fête l'haï volapük,\n"
    "Âne ex aéquo au whist,\n"
    "Ôtez ce vœu déçu.",
    "Le cœur déçu mais l'âme plutôt naïve, Louÿs rêva de crapaüter en canoë "
    "au delà des îles, près du mälström où brûlent les novæ.",
};
#define TEST_LANGUAGE_EXAMPLES_French(func, results) \
  TEST_EXAMPLES_3(func, French, results)

const size_t kIrishGaelicCount = 1;
const char* kIrishGaelicValues[kIrishGaelicCount] = {
    "D'fhuascail Íosa, Úrmhac na hÓighe Beannaithe, pór Éava agus Ádhaimh",
};
#define TEST_LANGUAGE_EXAMPLES_IrishGaelic(func, results) \
  TEST_EXAMPLES_1(func, IrishGaelic, results)

const size_t kHungarianCount = 1;
const char* kHungarianValues[kHungarianCount] = {
    "Árvíztűrő tükörfúrógép",
};
#define TEST_LANGUAGE_EXAMPLES_Hungarian(func, results) \
  TEST_EXAMPLES_1(func, Hungarian, results)

const size_t kIcelandicCount = 2;
const char* kIcelandicValues[kIcelandicCount] = {
    "Kæmi ný öxi hér ykist þjófum nú bæði víl og ádrepa",
    "Sævör grét áðan því úlpan var ónýt",
};
#define TEST_LANGUAGE_EXAMPLES_Icelandic(func, results) \
  TEST_EXAMPLES_2(func, Icelandic, results)

const size_t kJapaneseCount = 2;
const char* kJapaneseValues[kJapaneseCount] = {
    "いろはにほへとちりぬるを\n"
    "わかよたれそつねならむ\n"
    "うゐのおくやまけふこえて\n"
    "あさきゆめみしゑひもせす\n",
    "イロハニホヘト チリヌルヲ ワカヨタレソ ツネナラム\n"
    "ウヰノオクヤマ ケフコエテ アサキユメミシ ヱヒモセスン",
};
#define TEST_LANGUAGE_EXAMPLES_Japanese(func, results) \
  TEST_EXAMPLES_2(func, Japanese, results)

const size_t kHebrewCount = 1;
const char* kHebrewValues[kHebrewCount] = {
    "? דג סקרן שט בים מאוכזב ולפתע מצא לו חברה איך הקליטה",
};
#define TEST_LANGUAGE_EXAMPLES_Hebrew(func, results) \
  TEST_EXAMPLES_1(func, Hebrew, results)

const size_t kPolishCount = 1;
const char* kPolishValues[kPolishCount] = {
    "Pchnąć w tę łódź jeża lub ośm skrzyń fig",
};
#define TEST_LANGUAGE_EXAMPLES_Polish(func, results) \
  TEST_EXAMPLES_1(func, Polish, results)

const size_t kRussianCount = 2;
const char* kRussianValues[kRussianCount] = {
    "В чащах юга жил бы цитрус? Да, но фальшивый экземпляр!",
    "Съешь же ещё этих мягких французских булок да выпей чаю",
};
#define TEST_LANGUAGE_EXAMPLES_Russian(func, results) \
  TEST_EXAMPLES_2(func, Russian, results)

const size_t kTurkishCount = 1;
const char* kTurkishValues[kTurkishCount] = {
    "Pijamalı hasta, yağız şoföre çabucak güvendi.",
};
#define TEST_LANGUAGE_EXAMPLES_Turkish(func, results) \
  TEST_EXAMPLES_1(func, Turkish, results)

#define TEST_LANGUAGE_EXAMPLES(func, results)        \
  TEST_LANGUAGE_EXAMPLES_Danish(func, results);      \
  TEST_LANGUAGE_EXAMPLES_German(func, results);      \
  TEST_LANGUAGE_EXAMPLES_Greek(func, results);       \
  TEST_LANGUAGE_EXAMPLES_English(func, results);     \
  TEST_LANGUAGE_EXAMPLES_Spanish(func, results);     \
  TEST_LANGUAGE_EXAMPLES_French(func, results);      \
  TEST_LANGUAGE_EXAMPLES_IrishGaelic(func, results); \
  TEST_LANGUAGE_EXAMPLES_Hungarian(func, results);   \
  TEST_LANGUAGE_EXAMPLES_Icelandic(func, results);   \
  TEST_LANGUAGE_EXAMPLES_Japanese(func, results);    \
  TEST_LANGUAGE_EXAMPLES_Hebrew(func, results);      \
  TEST_LANGUAGE_EXAMPLES_Polish(func, results);      \
  TEST_LANGUAGE_EXAMPLES_Russian(func, results);     \
  TEST_LANGUAGE_EXAMPLES_Turkish(func, results)

}  // namespace examples

#define TEST_EXAMPLE_RESULT(language) T language[examples::k##language##Count]
template <typename T>
struct example_results {
  TEST_EXAMPLE_RESULT(Danish);
  TEST_EXAMPLE_RESULT(German);
  TEST_EXAMPLE_RESULT(Greek);
  TEST_EXAMPLE_RESULT(English);
  TEST_EXAMPLE_RESULT(Spanish);
  TEST_EXAMPLE_RESULT(French);
  TEST_EXAMPLE_RESULT(IrishGaelic);
  TEST_EXAMPLE_RESULT(Hungarian);
  TEST_EXAMPLE_RESULT(Icelandic);
  TEST_EXAMPLE_RESULT(Japanese);
  TEST_EXAMPLE_RESULT(Hebrew);
  TEST_EXAMPLE_RESULT(Polish);
  TEST_EXAMPLE_RESULT(Russian);
  TEST_EXAMPLE_RESULT(Turkish);
};
#undef TEST_EXAMPLE_RESULT

TEST_CASE("UTF-8 Count", "[utf8]") {
  example_results<size_t> results = {};
  results.Danish[0] = 88;
  results.German[0] = 58;
  results.German[1] = 54;
  results.Greek[0] = 52;
  results.Greek[1] = 33;
  results.English[0] = 43;
  results.Spanish[0] = 99;
  results.French[0] = 327;
  results.French[1] = 93;
  results.French[2] = 126;
  results.IrishGaelic[0] = 68;
  results.Hungarian[0] = 22;
  results.Icelandic[0] = 50;
  results.Icelandic[1] = 34;
  results.Japanese[0] = 51;
  results.Japanese[1] = 55;
  results.Hebrew[0] = 52;
  results.Polish[0] = 40;
  results.Russian[0] = 54;
  results.Russian[1] = 55;
  results.Turkish[0] = 45;
  TEST_LANGUAGE_EXAMPLES(utf8::count, results);
}

// TODO(gibbed): lower_ascii
// TODO(gibbed): upper_ascii
// TODO(gibbed): hash_fnv1a
// TODO(gibbed): hash_fnv1a_case

TEST_CASE("UTF-8 Split", "[utf8]") {
  std::vector<std::string_view> parts;

  // Danish
  parts = utf8::split(examples::kDanishValues[0], "æcå");
  REQUIRE(parts.size() == 4);
  REQUIRE(parts[0] == "Quizdeltagerne spiste jordb");
  REQUIRE(parts[1] == "r med fløde, mens ");
  REQUIRE(parts[2] == "irkusklovnen Wolther spillede p");
  REQUIRE(parts[3] == " xylofon.");

  // German
  parts = utf8::split(examples::kGermanValues[0], "ßS");
  REQUIRE(parts.size() == 2);
  REQUIRE(parts[0] == "Falsches Üben von Xylophonmusik quält jeden grö");
  REQUIRE(parts[1] == "eren Zwerg");
  parts = utf8::split(examples::kGermanValues[1], "ßS");
  REQUIRE(parts.size() == 2);
  REQUIRE(parts[0] == "Zwölf Boxkämpfer jagten Eva quer über den ");
  REQUIRE(parts[1] == "ylter Deich");
  parts = utf8::split(examples::kGermanValues[2], "ßS");
  REQUIRE(parts.size() == 2);
  REQUIRE(parts[0] == "Heizölrücksto");
  REQUIRE(parts[1] == "abdämpfung");

  // Greek
  parts = utf8::split(examples::kGreekValues[0], "πφ");
  REQUIRE(parts.size() == 4);
  REQUIRE(parts[0] == "Γαζέες καὶ μυρτιὲς δὲν θὰ βρῶ ");
  REQUIRE(parts[1] == "ιὰ στὸ χρυσα");
  REQUIRE(parts[2] == "ὶ ξέ");
  REQUIRE(parts[3] == "ωτο");
  parts = utf8::split(examples::kGreekValues[1], "πφ");
  REQUIRE(parts.size() == 3);
  REQUIRE(parts[0] == "Ξεσκε");
  REQUIRE(parts[1] == "άζω τὴν ψυχο");
  REQUIRE(parts[2] == "θόρα βδελυγμία");

  // English
  parts = utf8::split(examples::kEnglishValues[0], "xy");
  REQUIRE(parts.size() == 3);
  REQUIRE(parts[0] == "The quick brown fo");
  REQUIRE(parts[1] == " jumps over the laz");
  REQUIRE(parts[2] == " dog");

  // Spanish
  parts = utf8::split(examples::kSpanishValues[0], "ójd");
  REQUIRE(parts.size() == 4);
  REQUIRE(parts[0] == "El pingüino Wenceslao hizo kil");
  REQUIRE(parts[1] == "metros ba");
  REQUIRE(parts[2] == "o exhaustiva lluvia y frío, añoraba a su queri");
  REQUIRE(parts[3] == "o cachorro.");

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

TEST_CASE("UTF-8 Equal Z", "[utf8]") {
  REQUIRE(utf8::equal_z("foo", "foo\0"));
  REQUIRE_FALSE(utf8::equal_z("bar", "baz\0"));
}

TEST_CASE("UTF-8 Equal Case", "[utf8]") {
  REQUIRE(utf8::equal_case("foo", "foo\0"));
  REQUIRE_FALSE(utf8::equal_case("bar", "baz\0"));
}

TEST_CASE("UTF-8 Equal Case Z", "[utf8]") {
  REQUIRE(utf8::equal_case_z("foo", "foo\0"));
  REQUIRE_FALSE(utf8::equal_case_z("bar", "baz\0"));
}

// TODO(gibbed): find_any_of
// TODO(gibbed): find_any_of_case
// TODO(gibbed): find_first_of
// TODO(gibbed): find_first_of_case
// TODO(gibbed): starts_with
// TODO(gibbed): starts_with_case
// TODO(gibbed): ends_with
// TODO(gibbed): ends_with_case
// TODO(gibbed): split_path

#define TEST_PATH(func, input, output)                                 \
  do {                                                                 \
    std::string input_value = input;                                   \
    std::string output_value = output;                                 \
    REQUIRE(func(input_value, '/') == output_value);                   \
    std::replace(input_value.begin(), input_value.end(), '/', '\\');   \
    std::replace(output_value.begin(), output_value.end(), '/', '\\'); \
    REQUIRE(func(input_value, '\\') == output_value);                  \
  } while (0)

#define TEST_PATH_RAW(func, input, output)                             \
  do {                                                                 \
    std::string output_value = output;                                 \
    REQUIRE(func(input, '/') == output_value);                         \
    std::replace(output_value.begin(), output_value.end(), '/', '\\'); \
    REQUIRE(func(input, '\\') == output_value);                        \
  } while (0)

#define TEST_PATHS(func, output, ...)                                      \
  do {                                                                     \
    std::vector<std::string> input_values = {__VA_ARGS__};                 \
    std::string output_value = output;                                     \
    REQUIRE(func(input_values, '/') == output_value);                      \
    for (auto it = input_values.begin(); it != input_values.end(); ++it) { \
      std::replace((*it).begin(), (*it).end(), '/', '\\');                 \
    }                                                                      \
    std::replace(output_value.begin(), output_value.end(), '/', '\\');     \
    REQUIRE(func(input_values, '\\') == output_value);                     \
  } while (0)

TEST_CASE("UTF-8 Join Paths", "[utf8]") {
  TEST_PATHS(utf8::join_paths, "");
  TEST_PATHS(utf8::join_paths, "foo", "foo");
  TEST_PATHS(utf8::join_paths, "foo/bar", "foo", "bar");
  TEST_PATHS(utf8::join_paths, "X:/foo/bar/baz/qux", "X:", "foo", "bar", "baz",
             "qux");
}

// TODO(gibbed): join_guest_paths

TEST_CASE("UTF-8 Fix Path Separators", "[utf8]") {
  TEST_PATH_RAW(utf8::fix_path_separators, "", "");
  TEST_PATH_RAW(utf8::fix_path_separators, "\\", "/");
  TEST_PATH_RAW(utf8::fix_path_separators, "/", "/");
  TEST_PATH_RAW(utf8::fix_path_separators, "\\foo", "/foo");
  TEST_PATH_RAW(utf8::fix_path_separators, "\\foo/", "/foo/");
  TEST_PATH_RAW(utf8::fix_path_separators, "/foo", "/foo");
  TEST_PATH_RAW(utf8::fix_path_separators, "\\foo/bar\\baz/qux",
                "/foo/bar/baz/qux");
  TEST_PATH_RAW(utf8::fix_path_separators, "\\\\foo//bar\\\\baz//qux",
                "/foo/bar/baz/qux");
  TEST_PATH_RAW(utf8::fix_path_separators, "foo", "foo");
  TEST_PATH_RAW(utf8::fix_path_separators, "foo/", "foo/");
  TEST_PATH_RAW(utf8::fix_path_separators, "foo/bar\\baz/qux",
                "foo/bar/baz/qux");
  TEST_PATH_RAW(utf8::fix_path_separators, "foo//bar\\\\baz//qux",
                "foo/bar/baz/qux");
  TEST_PATH_RAW(utf8::fix_path_separators, "X:", "X:");
  TEST_PATH_RAW(utf8::fix_path_separators, "X:\\", "X:/");
  TEST_PATH_RAW(utf8::fix_path_separators, "X:/", "X:/");
  TEST_PATH_RAW(utf8::fix_path_separators, "X:\\foo", "X:/foo");
  TEST_PATH_RAW(utf8::fix_path_separators, "X:\\foo/", "X:/foo/");
  TEST_PATH_RAW(utf8::fix_path_separators, "X:/foo", "X:/foo");
  TEST_PATH_RAW(utf8::fix_path_separators, "X:\\foo/bar\\baz/qux",
                "X:/foo/bar/baz/qux");
  TEST_PATH_RAW(utf8::fix_path_separators, "X:\\\\foo//bar\\\\baz//qux",
                "X:/foo/bar/baz/qux");
}

// TODO(gibbed): fix_guest_path_separators

TEST_CASE("UTF-8 Find Name From Path", "[utf8]") {
  TEST_PATH(utf8::find_name_from_path, "/", "");
  TEST_PATH(utf8::find_name_from_path, "//", "");
  TEST_PATH(utf8::find_name_from_path, "///", "");
  TEST_PATH(utf8::find_name_from_path, "C/", "C");
  TEST_PATH(utf8::find_name_from_path, "/C/", "C");
  TEST_PATH(utf8::find_name_from_path, "C/D/", "D");
  TEST_PATH(utf8::find_name_from_path, "/C/D/E/", "E");
  TEST_PATH(utf8::find_name_from_path, "foo/bar/D/", "D");
  TEST_PATH(utf8::find_name_from_path, "/foo/bar/E/qux/", "qux");
  TEST_PATH(utf8::find_name_from_path, "foo/bar/baz/qux/", "qux");
  TEST_PATH(utf8::find_name_from_path, "foo/bar/baz/qux//", "qux");
  TEST_PATH(utf8::find_name_from_path, "foo/bar/baz/qux///", "qux");
  TEST_PATH(utf8::find_name_from_path, "foo/bar/baz/qux.txt", "qux.txt");
  TEST_PATH(utf8::find_name_from_path, "ほげ/ぴよ/ふが/ほげら/ほげほげ/",
            "ほげほげ");
  TEST_PATH(utf8::find_name_from_path, "ほげ/ぴよ/ふが/ほげら/ほげほげ//",
            "ほげほげ");
  TEST_PATH(utf8::find_name_from_path, "ほげ/ぴよ/ふが/ほげら/ほげほげ///",
            "ほげほげ");
  TEST_PATH(utf8::find_name_from_path, "ほげ/ぴよ/ふが/ほげら/ほげほげ.txt",
            "ほげほげ.txt");
  TEST_PATH(utf8::find_name_from_path, "/foo", "foo");
  TEST_PATH(utf8::find_name_from_path, "//foo", "foo");
  TEST_PATH(utf8::find_name_from_path, "///foo", "foo");
  TEST_PATH(utf8::find_name_from_path, "/foo/bar/baz/qux.txt", "qux.txt");
  TEST_PATH(utf8::find_name_from_path, "/ほげ/ぴよ/ふが/ほげら/ほげほげ/",
            "ほげほげ");
  TEST_PATH(utf8::find_name_from_path, "/ほげ/ぴよ/ふが/ほげら/ほげほげ//",
            "ほげほげ");
  TEST_PATH(utf8::find_name_from_path, "/ほげ/ぴよ/ふが/ほげら/ほげほげ///",
            "ほげほげ");
  TEST_PATH(utf8::find_name_from_path, "/ほげ/ぴよ/ふが/ほげら/ほげほげ.txt",
            "ほげほげ.txt");
  TEST_PATH(utf8::find_name_from_path, "X:/foo/bar/baz/qux.txt", "qux.txt");
  TEST_PATH(utf8::find_name_from_path, "X:/ほげ/ぴよ/ふが/ほげら/ほげほげ/",
            "ほげほげ");
  TEST_PATH(utf8::find_name_from_path, "X:/ほげ/ぴよ/ふが/ほげら/ほげほげ//",
            "ほげほげ");
  TEST_PATH(utf8::find_name_from_path, "X:/ほげ/ぴよ/ふが/ほげら/ほげほげ///",
            "ほげほげ");
  TEST_PATH(utf8::find_name_from_path, "X:/ほげ/ぴよ/ふが/ほげら/ほげほげ.txt",
            "ほげほげ.txt");
  TEST_PATH(utf8::find_name_from_path, "X:/ほげ/ぴよ/ふが/ほげら.ほげほげ",
            "ほげら.ほげほげ");
}

// TODO(gibbed): find_name_from_guest_path

TEST_CASE("UTF-8 Find Base Name From Path", "[utf8]") {
  TEST_PATH(utf8::find_base_name_from_path, "foo/bar/baz/qux.txt", "qux");
  TEST_PATH(utf8::find_base_name_from_path, "foo/bar/baz/qux/", "qux");
  TEST_PATH(utf8::find_base_name_from_path, "foo/bar/baz/qux//", "qux");
  TEST_PATH(utf8::find_base_name_from_path, "foo/bar/baz/qux///", "qux");
  TEST_PATH(utf8::find_base_name_from_path, "C/", "C");
  TEST_PATH(utf8::find_base_name_from_path, "/C/", "C");
  TEST_PATH(utf8::find_base_name_from_path, "C/D/", "D");
  TEST_PATH(utf8::find_base_name_from_path, "/C/D/E/", "E");
  TEST_PATH(utf8::find_base_name_from_path, "foo/bar/D/", "D");
  TEST_PATH(utf8::find_base_name_from_path,
            "ほげ/ぴよ/ふが/ほげら/ほげほげ.txt", "ほげほげ");
  TEST_PATH(utf8::find_base_name_from_path, "ほげ/ぴよ/ふが/ほげら/ほげほげ/",
            "ほげほげ");
  TEST_PATH(utf8::find_base_name_from_path, "ほげ/ぴよ/ふが/ほげら/ほげほげ//",
            "ほげほげ");
  TEST_PATH(utf8::find_base_name_from_path, "ほげ/ぴよ/ふが/ほげら/ほげほげ///",
            "ほげほげ");
  TEST_PATH(utf8::find_base_name_from_path, "ほげ/ぴよ/ふが/ほげら.ほげほげ",
            "ほげら");
  TEST_PATH(utf8::find_base_name_from_path, "/foo/bar/baz/qux.txt", "qux");
  TEST_PATH(utf8::find_base_name_from_path, "/foo/bar/baz/qux/", "qux");
  TEST_PATH(utf8::find_base_name_from_path, "/foo/bar/baz/qux//", "qux");
  TEST_PATH(utf8::find_base_name_from_path, "/foo/bar/baz/qux///", "qux");
  TEST_PATH(utf8::find_base_name_from_path,
            "/ほげ/ぴよ/ふが/ほげら/ほげほげ.txt", "ほげほげ");
  TEST_PATH(utf8::find_base_name_from_path, "/ほげ/ぴよ/ふが/ほげら/ほげほげ/",
            "ほげほげ");
  TEST_PATH(utf8::find_base_name_from_path, "/ほげ/ぴよ/ふが/ほげら/ほげほげ//",
            "ほげほげ");
  TEST_PATH(utf8::find_base_name_from_path,
            "/ほげ/ぴよ/ふが/ほげら/ほげほげ///", "ほげほげ");
  TEST_PATH(utf8::find_base_name_from_path, "/ほげ/ぴよ/ふが/ほげら.ほげほげ",
            "ほげら");
  TEST_PATH(utf8::find_base_name_from_path, "X:/foo/bar/baz/qux.txt", "qux");
  TEST_PATH(utf8::find_base_name_from_path, "X:/foo/bar/baz/qux/", "qux");
  TEST_PATH(utf8::find_base_name_from_path, "X:/foo/bar/baz/qux//", "qux");
  TEST_PATH(utf8::find_base_name_from_path, "X:/foo/bar/baz/qux///", "qux");
  TEST_PATH(utf8::find_base_name_from_path,
            "X:/ほげ/ぴよ/ふが/ほげら/ほげほげ.txt", "ほげほげ");
  TEST_PATH(utf8::find_base_name_from_path,
            "X:/ほげ/ぴよ/ふが/ほげら/ほげほげ/", "ほげほげ");
  TEST_PATH(utf8::find_base_name_from_path,
            "X:/ほげ/ぴよ/ふが/ほげら/ほげほげ//", "ほげほげ");
  TEST_PATH(utf8::find_base_name_from_path,
            "X:/ほげ/ぴよ/ふが/ほげら/ほげほげ///", "ほげほげ");
  TEST_PATH(utf8::find_base_name_from_path, "X:/ほげ/ぴよ/ふが/ほげら.ほげほげ",
            "ほげら");
}

// TODO(gibbed): find_base_name_from_guest_path

TEST_CASE("UTF-8 Find Base Path", "[utf8]") {
  TEST_PATH(utf8::find_base_path, "", "");
  TEST_PATH(utf8::find_base_path, "/", "");
  TEST_PATH(utf8::find_base_path, "//", "");
  TEST_PATH(utf8::find_base_path, "///", "");
  TEST_PATH(utf8::find_base_path, "/foo", "");
  TEST_PATH(utf8::find_base_path, "//foo", "");
  TEST_PATH(utf8::find_base_path, "///foo", "");
  TEST_PATH(utf8::find_base_path, "/foo/", "");
  TEST_PATH(utf8::find_base_path, "/foo//", "");
  TEST_PATH(utf8::find_base_path, "/foo///", "");
  TEST_PATH(utf8::find_base_path, "//foo/", "");
  TEST_PATH(utf8::find_base_path, "//foo//", "");
  TEST_PATH(utf8::find_base_path, "//foo///", "");
  TEST_PATH(utf8::find_base_path, "///foo/", "");
  TEST_PATH(utf8::find_base_path, "///foo//", "");
  TEST_PATH(utf8::find_base_path, "///foo///", "");
  TEST_PATH(utf8::find_base_path, "/foo/bar", "/foo");
  TEST_PATH(utf8::find_base_path, "/foo/bar/", "/foo");
  TEST_PATH(utf8::find_base_path, "/foo/bar//", "/foo");
  TEST_PATH(utf8::find_base_path, "/foo/bar///", "/foo");
  TEST_PATH(utf8::find_base_path, "/foo/bar/baz/qux", "/foo/bar/baz");
  TEST_PATH(utf8::find_base_path, "/foo/bar/baz/qux/", "/foo/bar/baz");
  TEST_PATH(utf8::find_base_path, "/foo/bar/baz/qux//", "/foo/bar/baz");
  TEST_PATH(utf8::find_base_path, "/foo/bar/baz/qux///", "/foo/bar/baz");
  TEST_PATH(utf8::find_base_path, "/ほげ/ぴよ/ふが/ほげら/ほげほげ",
            "/ほげ/ぴよ/ふが/ほげら");
  TEST_PATH(utf8::find_base_path, "/ほげ/ぴよ/ふが/ほげら/ほげほげ/",
            "/ほげ/ぴよ/ふが/ほげら");
  TEST_PATH(utf8::find_base_path, "/ほげ/ぴよ/ふが/ほげら/ほげほげ//",
            "/ほげ/ぴよ/ふが/ほげら");
  TEST_PATH(utf8::find_base_path, "/ほげ/ぴよ/ふが/ほげら/ほげほげ///",
            "/ほげ/ぴよ/ふが/ほげら");
  TEST_PATH(utf8::find_base_path, "foo", "");
  TEST_PATH(utf8::find_base_path, "foo/", "");
  TEST_PATH(utf8::find_base_path, "foo//", "");
  TEST_PATH(utf8::find_base_path, "foo///", "");
  TEST_PATH(utf8::find_base_path, "foo/bar", "foo");
  TEST_PATH(utf8::find_base_path, "foo/bar/", "foo");
  TEST_PATH(utf8::find_base_path, "foo/bar//", "foo");
  TEST_PATH(utf8::find_base_path, "foo/bar///", "foo");
  TEST_PATH(utf8::find_base_path, "foo/bar/baz/qux", "foo/bar/baz");
  TEST_PATH(utf8::find_base_path, "foo/bar/baz/qux/", "foo/bar/baz");
  TEST_PATH(utf8::find_base_path, "foo/bar/baz/qux//", "foo/bar/baz");
  TEST_PATH(utf8::find_base_path, "foo/bar/baz/qux///", "foo/bar/baz");
  TEST_PATH(utf8::find_base_path, "ほげ/ぴよ/ふが/ほげら/ほげほげ",
            "ほげ/ぴよ/ふが/ほげら");
  TEST_PATH(utf8::find_base_path, "ほげ/ぴよ/ふが/ほげら/ほげほげ/",
            "ほげ/ぴよ/ふが/ほげら");
  TEST_PATH(utf8::find_base_path, "ほげ/ぴよ/ふが/ほげら/ほげほげ//",
            "ほげ/ぴよ/ふが/ほげら");
  TEST_PATH(utf8::find_base_path, "ほげ/ぴよ/ふが/ほげら/ほげほげ///",
            "ほげ/ぴよ/ふが/ほげら");
  TEST_PATH(utf8::find_base_path, "X:", "");
  TEST_PATH(utf8::find_base_path, "X:/", "");
  TEST_PATH(utf8::find_base_path, "X://", "");
  TEST_PATH(utf8::find_base_path, "X:///", "");
  TEST_PATH(utf8::find_base_path, "X:/foo", "X:");
  TEST_PATH(utf8::find_base_path, "X:/foo/", "X:");
  TEST_PATH(utf8::find_base_path, "X:/foo//", "X:");
  TEST_PATH(utf8::find_base_path, "X:/foo///", "X:");
  TEST_PATH(utf8::find_base_path, "X:/foo/bar", "X:/foo");
  TEST_PATH(utf8::find_base_path, "X:/foo/bar/", "X:/foo");
  TEST_PATH(utf8::find_base_path, "X:/foo/bar//", "X:/foo");
  TEST_PATH(utf8::find_base_path, "X:/foo/bar///", "X:/foo");
  TEST_PATH(utf8::find_base_path, "X:/foo/bar/baz/qux", "X:/foo/bar/baz");
  TEST_PATH(utf8::find_base_path, "X:/foo/bar/baz/qux/", "X:/foo/bar/baz");
  TEST_PATH(utf8::find_base_path, "X:/foo/bar/baz/qux//", "X:/foo/bar/baz");
  TEST_PATH(utf8::find_base_path, "X:/foo/bar/baz/qux///", "X:/foo/bar/baz");
  TEST_PATH(utf8::find_base_path, "X:/ほげ/ぴよ/ふが/ほげら/ほげほげ",
            "X:/ほげ/ぴよ/ふが/ほげら");
  TEST_PATH(utf8::find_base_path, "X:/ほげ/ぴよ/ふが/ほげら/ほげほげ/",
            "X:/ほげ/ぴよ/ふが/ほげら");
  TEST_PATH(utf8::find_base_path, "X:/ほげ/ぴよ/ふが/ほげら/ほげほげ//",
            "X:/ほげ/ぴよ/ふが/ほげら");
  TEST_PATH(utf8::find_base_path, "X:/ほげ/ぴよ/ふが/ほげら/ほげほげ///",
            "X:/ほげ/ぴよ/ふが/ほげら");
}

// TODO(gibbed): find_base_guest_path

TEST_CASE("UTF-8 Canonicalize Path", "[utf8]") {
  TEST_PATH(utf8::canonicalize_path, "foo/bar/baz/qux", "foo/bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "foo/bar/baz/qux/", "foo/bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "foo/bar/baz/qux//", "foo/bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "foo/bar/baz/qux///", "foo/bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "foo/./baz/qux", "foo/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "foo/./baz/qux/", "foo/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "foo/../baz/qux", "baz/qux");
  TEST_PATH(utf8::canonicalize_path, "foo/../baz/qux/", "baz/qux");
  TEST_PATH(utf8::canonicalize_path, "foo/./baz/../qux", "foo/qux");
  TEST_PATH(utf8::canonicalize_path, "foo/./baz/../qux/", "foo/qux");
  TEST_PATH(utf8::canonicalize_path, "foo/./../baz/qux", "baz/qux");
  TEST_PATH(utf8::canonicalize_path, "foo/./../baz/qux/", "baz/qux");
  TEST_PATH(utf8::canonicalize_path, "./bar/baz/qux", "bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "./bar/baz/qux/", "bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "../bar/baz/qux", "bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "../bar/baz/qux/", "bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "ほげ/ぴよ/./ふが/../ほげら/ほげほげ",
            "ほげ/ぴよ/ほげら/ほげほげ");
  TEST_PATH(utf8::canonicalize_path, "ほげ/ぴよ/./ふが/../ほげら/ほげほげ/",
            "ほげ/ぴよ/ほげら/ほげほげ");
  TEST_PATH(utf8::canonicalize_path, "/foo/bar/baz/qux", "/foo/bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "/foo/bar/baz/qux/", "/foo/bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "/foo/./baz/qux", "/foo/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "/foo/./baz/qux/", "/foo/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "/foo/../baz/qux", "/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "/foo/../baz/qux/", "/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "/foo/./baz/../qux", "/foo/qux");
  TEST_PATH(utf8::canonicalize_path, "/foo/./baz/../qux/", "/foo/qux");
  TEST_PATH(utf8::canonicalize_path, "/foo/./../baz/qux", "/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "/foo/./../baz/qux/", "/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "/./bar/baz/qux", "/bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "/./bar/baz/qux/", "/bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "/../bar/baz/qux", "/bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "/../bar/baz/qux/", "/bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "/ほげ/ぴよ/./ふが/../ほげら/ほげほげ",
            "/ほげ/ぴよ/ほげら/ほげほげ");
  TEST_PATH(utf8::canonicalize_path, "/ほげ/ぴよ/./ふが/../ほげら/ほげほげ/",
            "/ほげ/ぴよ/ほげら/ほげほげ");
  TEST_PATH(utf8::canonicalize_path, "X:/foo/bar/baz/qux",
            "X:/foo/bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "X:/foo/bar/baz/qux/",
            "X:/foo/bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "X:/foo/./baz/qux", "X:/foo/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "X:/foo/./baz/qux/", "X:/foo/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "X:/foo/../baz/qux", "X:/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "X:/foo/../baz/qux/", "X:/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "X:/foo/./baz/../qux", "X:/foo/qux");
  TEST_PATH(utf8::canonicalize_path, "X:/foo/./baz/../qux/", "X:/foo/qux");
  TEST_PATH(utf8::canonicalize_path, "X:/foo/./../baz/qux", "X:/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "X:/foo/./../baz/qux/", "X:/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "X:/./bar/baz/qux", "X:/bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "X:/./bar/baz/qux/", "X:/bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "X:/../bar/baz/qux", "X:/bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "X:/../bar/baz/qux/", "X:/bar/baz/qux");
  TEST_PATH(utf8::canonicalize_path, "X:/ほげ/ぴよ/./ふが/../ほげら/ほげほげ",
            "X:/ほげ/ぴよ/ほげら/ほげほげ");
  TEST_PATH(utf8::canonicalize_path, "X:/ほげ/ぴよ/./ふが/../ほげら/ほげほげ/",
            "X:/ほげ/ぴよ/ほげら/ほげほげ");
}

// TODO(gibbed): canonicalize_guest_path

}  // namespace xe::base::test
