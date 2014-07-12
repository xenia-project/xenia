/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/ui/window.h>


using namespace xe;
using namespace xe::ui;


Window::Window(xe_run_loop_ref run_loop) :
    title_(0), is_visible_(true), is_cursor_visible_(true),
    width_(0), height_(0) {
  run_loop_ = xe_run_loop_retain(run_loop);
}

Window::~Window() {
  if (title_) {
    xe_free(title_);
  }
  xe_run_loop_release(run_loop_);
}

int Window::Initialize(const char* title, uint32_t width, uint32_t height) {
  title_ = xestrdupa(title);
  width_ = width;
  height_ = height;
  return 0;
}

bool Window::set_title(const char* title) {
  if (title == title_) {
    return false;
  }
  if (title_) {
    xe_free(title_);
  }
  title_ = xestrdupa(title);
  return true;
}

bool Window::set_cursor_visible(bool value) {
  if (value == is_cursor_visible_) {
    return false;
  }
  is_cursor_visible_ = value;
  return true;
}

void Window::OnShow() {
  if (is_visible_) {
    return;
  }
  is_visible_ = true;
  auto e = UIEvent(this);
  shown(e);
}

void Window::OnHide() {
  if (!is_visible_) {
    return;
  }
  is_visible_ = false;
  auto e = UIEvent(this);
  hidden(e);
}

void Window::Resize(uint32_t width, uint32_t height) {
  BeginResizing();
  SetSize(width, height);
  OnResize(width, height); // redundant?
  EndResizing();
}

void Window::BeginResizing() {
  if (resizing_) {
    return;
  }
  resizing_ = true;
  auto e = UIEvent(this);
  resizing(e);
}

bool Window::OnResize(uint32_t width, uint32_t height) {
  if (!resizing_) {
    BeginResizing();
  }
  if (width == width_ && height == height_) {
    return false;
  }
  width_ = width;
  height_ = height;
  return true;
}

void Window::EndResizing() {
  assert_true(resizing_);
  resizing_ = false;
  auto e = UIEvent(this);
  resized(e);
}

void Window::Close() {
  auto e = UIEvent(this);
  closing(e);

  OnClose();

  e = UIEvent(this);
  closed(e);
}

void Window::OnClose() {
}
