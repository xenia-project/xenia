/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/file_picker.h"

#import <Cocoa/Cocoa.h>

#include <string>
#include <string_view>
#include <vector>

#include "xenia/base/assert.h"

namespace xe {
namespace ui {

namespace {

std::vector<std::string> ParseAllowedExtensions(std::string_view patterns) {
  // Input examples from callers:
  //   "*.iso;*.xex;*.*"
  //   "*.xtr"
  //   "*.*"
  std::vector<std::string> extensions;
  size_t start = 0;
  while (start < patterns.size()) {
    size_t end = patterns.find(';', start);
    if (end == std::string_view::npos) {
      end = patterns.size();
    }

    auto token = patterns.substr(start, end - start);
    while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) {
      token.remove_prefix(1);
    }
    while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
      token.remove_suffix(1);
    }

    // Ignore wildcard matches.
    if (token == "*" || token == "*.*") {
      start = end + 1;
      continue;
    }

    // Convert "*.ext" to "ext".
    if (token.size() >= 3 && token[0] == '*' && token[1] == '.') {
      token.remove_prefix(2);
    }

    // Strip leading dot (".ext" -> "ext").
    if (!token.empty() && token.front() == '.') {
      token.remove_prefix(1);
    }

    if (!token.empty()) {
      extensions.emplace_back(token);
    }

    start = end + 1;
  }

  return extensions;
}

}  // namespace

class MacFilePicker : public FilePicker {
 public:
  MacFilePicker() = default;
  ~MacFilePicker() override = default;

  bool Show(Window* parent_window) override {
    (void)parent_window;

    @autoreleasepool {
      set_selected_files({});

      if (mode() == Mode::kSave) {
        NSSavePanel* panel = [NSSavePanel savePanel];
        [panel setTitle:[NSString stringWithUTF8String:title().c_str()]];

        if (type() == Type::kDirectory) {
          [panel setCanCreateDirectories:YES];
          [panel setCanSelectHiddenExtension:YES];
        }

        // Apply file extension filtering when possible.
        if (type() == Type::kFile && !extensions().empty()) {
          const auto allowed = ParseAllowedExtensions(extensions().front().second);
          if (!allowed.empty()) {
            NSMutableArray<NSString*>* types = [NSMutableArray array];
            for (const auto& ext : allowed) {
              [types addObject:[NSString stringWithUTF8String:ext.c_str()]];
            }
            [panel setAllowedFileTypes:types];
          }
        }

        const NSInteger result = [panel runModal];
        if (result != NSModalResponseOK) {
          return false;
        }

        NSURL* url = [panel URL];
        if (!url) {
          return false;
        }

        std::vector<std::filesystem::path> selected;
        selected.emplace_back(std::filesystem::path([[url path] UTF8String]));
        set_selected_files(std::move(selected));
        return true;
      }

      // Open.
      NSOpenPanel* panel = [NSOpenPanel openPanel];
      [panel setTitle:[NSString stringWithUTF8String:title().c_str()]];

      if (type() == Type::kDirectory) {
        [panel setCanChooseFiles:NO];
        [panel setCanChooseDirectories:YES];
      } else {
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
      }

      [panel setAllowsMultipleSelection:multi_selection() ? YES : NO];

      // Apply file extension filtering when possible.
      if (type() == Type::kFile && !extensions().empty()) {
        const auto allowed = ParseAllowedExtensions(extensions().front().second);
        if (!allowed.empty()) {
          NSMutableArray<NSString*>* types = [NSMutableArray array];
          for (const auto& ext : allowed) {
            [types addObject:[NSString stringWithUTF8String:ext.c_str()]];
          }
          [panel setAllowedFileTypes:types];
        }
      }

      const NSInteger result = [panel runModal];
      if (result != NSModalResponseOK) {
        return false;
      }

      NSArray<NSURL*>* urls = [panel URLs];
      if (!urls || [urls count] == 0) {
        return false;
      }

      std::vector<std::filesystem::path> selected;
      selected.reserve([urls count]);
      for (NSURL* url in urls) {
        if (!url) {
          continue;
        }
        selected.emplace_back(std::filesystem::path([[url path] UTF8String]));
      }

      set_selected_files(std::move(selected));
      return !selected_files().empty();
    }
  }
};

std::unique_ptr<FilePicker> FilePicker::Create() {
  return std::make_unique<MacFilePicker>();
}

}  // namespace ui
}  // namespace xe
