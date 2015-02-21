/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <gflags/gflags.h>

#include "poly/main.h"
#include "poly/mapped_memory.h"
#include "third_party/imgui/imgui.h"
#include "xenia/gpu/gl4/gl_context.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/gpu/tracing.h"
#include "xenia/emulator.h"
#include "xenia/ui/main_window.h"

DEFINE_string(target_trace_file, "", "Specifies the trace file to load.");

namespace xe {
namespace gpu {

// TODO(benvanik): move to tracing.h/cc

class TraceReader {
 public:
  struct Frame {
    const uint8_t* start_ptr;
    const uint8_t* end_ptr;
    int command_count;
  };

  TraceReader() : trace_data_(nullptr), trace_size_(0) {}
  ~TraceReader() = default;

  const Frame* frame(int n) const { return &frames_[n]; }
  int frame_count() const { return int(frames_.size()); }

  bool Open(const std::wstring& path) {
    Close();

    mmap_ = poly::MappedMemory::Open(path, poly::MappedMemory::Mode::kRead);
    if (!mmap_) {
      return false;
    }

    trace_data_ = reinterpret_cast<const uint8_t*>(mmap_->data());
    trace_size_ = mmap_->size();

    ParseTrace();

    return true;
  }

  void Close() {
    mmap_.reset();
    trace_data_ = nullptr;
    trace_size_ = 0;
  }

  // void Foo() {
  //  auto trace_ptr = trace_data;
  //  while (trace_ptr < trace_data + trace_size) {
  //    auto cmd_type = *reinterpret_cast<const TraceCommandType*>(trace_ptr);
  //    switch (cmd_type) {
  //      case TraceCommandType::kPrimaryBufferStart:
  //        break;
  //      case TraceCommandType::kPrimaryBufferEnd:
  //        break;
  //      case TraceCommandType::kIndirectBufferStart:
  //        break;
  //      case TraceCommandType::kIndirectBufferEnd:
  //        break;
  //      case TraceCommandType::kPacketStart:
  //        break;
  //      case TraceCommandType::kPacketEnd:
  //        break;
  //      case TraceCommandType::kMemoryRead:
  //        break;
  //      case TraceCommandType::kMemoryWrite:
  //        break;
  //      case TraceCommandType::kEvent:
  //        break;
  //    }
  //    /*trace_ptr = graphics_system->PlayTrace(
  //    trace_ptr, trace_size - (trace_ptr - trace_data),
  //    GraphicsSystem::TracePlaybackMode::kBreakOnSwap);*/
  //  }
  //}

 protected:
  void ParseTrace() {
    auto trace_ptr = trace_data_;
    Frame current_frame = {
        trace_ptr, nullptr, 0,
    };
    bool pending_break = false;
    while (trace_ptr < trace_data_ + trace_size_) {
      ++current_frame.command_count;
      auto type =
          static_cast<TraceCommandType>(poly::load<uint32_t>(trace_ptr));
      switch (type) {
        case TraceCommandType::kPrimaryBufferStart: {
          auto cmd =
              reinterpret_cast<const PrimaryBufferStartCommand*>(trace_ptr);
          trace_ptr += sizeof(*cmd) + cmd->count * 4;
          break;
        }
        case TraceCommandType::kPrimaryBufferEnd: {
          auto cmd =
              reinterpret_cast<const PrimaryBufferEndCommand*>(trace_ptr);
          trace_ptr += sizeof(*cmd);
          break;
        }
        case TraceCommandType::kIndirectBufferStart: {
          auto cmd =
              reinterpret_cast<const IndirectBufferStartCommand*>(trace_ptr);
          trace_ptr += sizeof(*cmd) + cmd->count * 4;
          break;
        }
        case TraceCommandType::kIndirectBufferEnd: {
          auto cmd =
              reinterpret_cast<const IndirectBufferEndCommand*>(trace_ptr);
          trace_ptr += sizeof(*cmd);
          break;
        }
        case TraceCommandType::kPacketStart: {
          auto cmd = reinterpret_cast<const PacketStartCommand*>(trace_ptr);
          trace_ptr += sizeof(*cmd) + cmd->count * 4;
          break;
        }
        case TraceCommandType::kPacketEnd: {
          auto cmd = reinterpret_cast<const PacketEndCommand*>(trace_ptr);
          trace_ptr += sizeof(*cmd);
          if (pending_break) {
            current_frame.end_ptr = trace_ptr;
            frames_.push_back(std::move(current_frame));
            current_frame.start_ptr = trace_ptr;
            current_frame.end_ptr = nullptr;
            current_frame.command_count = 0;
            pending_break = false;
          }
          break;
        }
        case TraceCommandType::kMemoryRead: {
          auto cmd = reinterpret_cast<const MemoryReadCommand*>(trace_ptr);
          trace_ptr += sizeof(*cmd) + cmd->length;
          break;
        }
        case TraceCommandType::kMemoryWrite: {
          auto cmd = reinterpret_cast<const MemoryWriteCommand*>(trace_ptr);
          trace_ptr += sizeof(*cmd) + cmd->length;
          break;
        }
        case TraceCommandType::kEvent: {
          auto cmd = reinterpret_cast<const EventCommand*>(trace_ptr);
          trace_ptr += sizeof(*cmd);
          switch (cmd->event_type) {
            case EventType::kSwap: {
              pending_break = true;
              break;
            }
          }
          break;
        }
      }
    }
    if (pending_break) {
      current_frame.end_ptr = trace_ptr;
      frames_.push_back(std::move(current_frame));
    }
  }

  std::unique_ptr<poly::MappedMemory> mmap_;
  const uint8_t* trace_data_;
  size_t trace_size_;
  std::vector<Frame> frames_;
};

class TracePlayer : public TraceReader {
 public:
  TracePlayer(poly::ui::Loop* loop, GraphicsSystem* graphics_system)
      : loop_(loop),
        graphics_system_(graphics_system),
        current_frame_index_(0),
        current_command_index_(-1) {}
  ~TracePlayer() = default;

  int current_frame_index() const { return current_frame_index_; }

  const Frame* current_frame() const {
    if (current_frame_index_ > frame_count()) {
      return nullptr;
    }
    return frame(current_frame_index_);
  }

  void SeekFrame(int target_frame) {
    if (current_frame_index_ == target_frame) {
      return;
    }
    current_frame_index_ = target_frame;
    auto frame = current_frame();
    current_command_index_ = frame->command_count - 1;

    graphics_system_->PlayTrace(
        frame->start_ptr, frame->end_ptr - frame->start_ptr,
        GraphicsSystem::TracePlaybackMode::kBreakOnSwap);
  }

  int current_command_index() const { return current_command_index_; }

  void SeekCommand(int target_command) {
    current_command_index_ = target_command;
  }

 private:
  poly::ui::Loop* loop_;
  GraphicsSystem* graphics_system_;
  int current_frame_index_;
  int current_command_index_;
};

void DrawUI(xe::ui::MainWindow* window, TracePlayer& player) {
  ImGui::ShowTestWindow();

  ImGui::SetNextWindowPos(ImVec2(5, 5), ImGuiSetCondition_FirstUseEver);
  if (ImGui::Begin("Controller", nullptr, ImVec2(0, 0), -1.0f,
                   ImGuiWindowFlags_AlwaysAutoResize)) {
    int target_frame = player.current_frame_index();
    if (ImGui::Button("|<<")) {
      target_frame = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button(">>", ImVec2(0, 0), true)) {
      if (target_frame + 1 < player.frame_count()) {
        ++target_frame;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button(">>|")) {
      target_frame = player.frame_count() - 1;
    }
    ImGui::SameLine();
    ImGui::SliderInt("", &target_frame, 0, player.frame_count() - 1);
    if (target_frame != player.current_frame_index()) {
      player.SeekFrame(target_frame);
    }
  }
  ImGui::End();

  ImGui::SetNextWindowPos(ImVec2(float(window->width()) - 500 - 5, 5),
                          ImGuiSetCondition_FirstUseEver);
  if (ImGui::Begin("Frame Inspector", nullptr, ImVec2(500, 300))) {
    ImGui::Columns(2, "frame_inspector");
    ImGui::Text("Frame #%d", player.current_frame_index());
    ImGui::Separator();
    ImGui::BeginChild("Sub1");
    ImGui::PushID(-1);
    bool is_selected = player.current_command_index() == -1;
    if (ImGui::Selectable("<start>", &is_selected)) {
      player.SeekCommand(-1);
    }
    ImGui::PopID();
    int column_width = int(ImGui::GetContentRegionMax().x);
    auto frame = player.current_frame();
    for (int i = 0; i < frame->command_count; ++i) {
      ImGui::PushID(i);
      is_selected = i == player.current_command_index();
      if (ImGui::Selectable("command", &is_selected)) {
        player.SeekCommand(i);
      }
      ImGui::SameLine(column_width - 30);
      ImGui::Text("bar");
      ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::NextColumn();
    ImGui::Text("right side");
    ImGui::Columns(1);
  }
  ImGui::End();
}

void ImImpl_Setup();
void ImImpl_Shutdown();

int trace_viewer_main(std::vector<std::wstring>& args) {
  // Create the emulator.
  auto emulator = std::make_unique<Emulator>(L"");
  X_STATUS result = emulator->Setup();
  if (XFAILED(result)) {
    XELOGE("Failed to setup emulator: %.8X", result);
    return 1;
  }

  // Grab path from the flag or unnamed argument.
  if (!FLAGS_target_trace_file.empty() || args.size() >= 2) {
    std::wstring path;
    if (!FLAGS_target_trace_file.empty()) {
      // Passed as a named argument.
      // TODO(benvanik): find something better than gflags that supports
      // unicode.
      path = poly::to_wstring(FLAGS_target_trace_file);
    } else {
      // Passed as an unnamed argument.
      path = args[1];
    }
    // Normalize the path and make absolute.
    auto abs_path = poly::to_absolute_path(path);

    auto window = emulator->main_window();
    auto loop = window->loop();
    auto file_name = poly::find_name_from_path(path);
    window->set_title(std::wstring(L"Xenia GPU Trace Viewer: ") + file_name);

    auto graphics_system = emulator->graphics_system();
    Profiler::set_display(nullptr);

    TracePlayer player(loop, emulator->graphics_system());
    if (!player.Open(abs_path)) {
      XELOGE("Could not load trace file");
      return 1;
    }

    auto control = window->child(0);
    control->on_key_char.AddListener([](poly::ui::KeyEvent& e) {
      auto& io = ImGui::GetIO();
      if (e.key_code() > 0 && e.key_code() < 0x10000) {
        io.AddInputCharacter(e.key_code());
      }
      e.set_handled(true);
    });
    control->on_mouse_down.AddListener([](poly::ui::MouseEvent& e) {
      auto& io = ImGui::GetIO();
      io.MousePos = ImVec2(float(e.x()), float(e.y()));
      switch (e.button()) {
        case poly::ui::MouseEvent::Button::kLeft:
          io.MouseDown[0] = true;
          break;
        case poly::ui::MouseEvent::Button::kRight:
          io.MouseDown[1] = true;
          break;
      }
    });
    control->on_mouse_move.AddListener([](poly::ui::MouseEvent& e) {
      auto& io = ImGui::GetIO();
      io.MousePos = ImVec2(float(e.x()), float(e.y()));
    });
    control->on_mouse_up.AddListener([](poly::ui::MouseEvent& e) {
      auto& io = ImGui::GetIO();
      io.MousePos = ImVec2(float(e.x()), float(e.y()));
      switch (e.button()) {
        case poly::ui::MouseEvent::Button::kLeft:
          io.MouseDown[0] = false;
          break;
        case poly::ui::MouseEvent::Button::kRight:
          io.MouseDown[1] = false;
          break;
      }
    });
    control->on_mouse_wheel.AddListener([](poly::ui::MouseEvent& e) {
      auto& io = ImGui::GetIO();
      io.MousePos = ImVec2(float(e.x()), float(e.y()));
      io.MouseWheel += float(e.dy() / 120.0f);
    });

    control->on_paint.AddListener([&](poly::ui::UIEvent& e) {
      static bool imgui_setup = false;
      if (!imgui_setup) {
        ImImpl_Setup();
        imgui_setup = true;
      }
      auto& io = ImGui::GetIO();
      auto current_ticks = poly::threading::ticks();
      static uint64_t last_ticks = 0;
      io.DeltaTime = (current_ticks - last_ticks) /
                     float(poly::threading::ticks_per_second());
      last_ticks = current_ticks;

      io.DisplaySize =
          ImVec2(float(e.control()->width()), float(e.control()->height()));

      BYTE keystate[256];
      GetKeyboardState(keystate);
      for (int i = 0; i < 256; i++) io.KeysDown[i] = (keystate[i] & 0x80) != 0;
      io.KeyCtrl = (keystate[VK_CONTROL] & 0x80) != 0;
      io.KeyShift = (keystate[VK_SHIFT] & 0x80) != 0;

      ImGui::NewFrame();

      DrawUI(window, player);

      glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
      ImGui::Render();

      graphics_system->RequestSwap();
    });
    graphics_system->RequestSwap();

    // Wait until we are exited.
    emulator->main_window()->loop()->AwaitQuit();

    ImImpl_Shutdown();
  }

  emulator.reset();
  return 0;
}

// TODO(benvanik): move to another file.

extern "C" GLEWContext* glewGetContext();
extern "C" WGLEWContext* wglewGetContext();

static int shader_handle, vert_handle, frag_handle;
static int texture_location, proj_mtx_location;
static int position_location, uv_location, colour_location;
static size_t vbo_max_size = 20000;
static unsigned int vbo_handle, vao_handle;
void ImImpl_RenderDrawLists(ImDrawList** const cmd_lists, int cmd_lists_count);
void ImImpl_Setup() {
  ImGuiIO& io = ImGui::GetIO();

  const GLchar* vertex_shader =
      "#version 330\n"
      "uniform mat4 ProjMtx;\n"
      "in vec2 Position;\n"
      "in vec2 UV;\n"
      "in vec4 Color;\n"
      "out vec2 Frag_UV;\n"
      "out vec4 Frag_Color;\n"
      "void main()\n"
      "{\n"
      "	Frag_UV = UV;\n"
      "	Frag_Color = Color;\n"
      "	gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
      "}\n";

  const GLchar* fragment_shader =
      "#version 330\n"
      "uniform sampler2D Texture;\n"
      "in vec2 Frag_UV;\n"
      "in vec4 Frag_Color;\n"
      "out vec4 Out_Color;\n"
      "void main()\n"
      "{\n"
      "	Out_Color = Frag_Color * texture( Texture, Frag_UV.st);\n"
      "}\n";

  shader_handle = glCreateProgram();
  vert_handle = glCreateShader(GL_VERTEX_SHADER);
  frag_handle = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(vert_handle, 1, &vertex_shader, 0);
  glShaderSource(frag_handle, 1, &fragment_shader, 0);
  glCompileShader(vert_handle);
  glCompileShader(frag_handle);
  glAttachShader(shader_handle, vert_handle);
  glAttachShader(shader_handle, frag_handle);
  glLinkProgram(shader_handle);

  texture_location = glGetUniformLocation(shader_handle, "Texture");
  proj_mtx_location = glGetUniformLocation(shader_handle, "ProjMtx");
  position_location = glGetAttribLocation(shader_handle, "Position");
  uv_location = glGetAttribLocation(shader_handle, "UV");
  colour_location = glGetAttribLocation(shader_handle, "Color");

  glGenBuffers(1, &vbo_handle);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_handle);
  glBufferData(GL_ARRAY_BUFFER, vbo_max_size, NULL, GL_DYNAMIC_DRAW);

  glGenVertexArrays(1, &vao_handle);
  glBindVertexArray(vao_handle);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_handle);
  glEnableVertexAttribArray(position_location);
  glEnableVertexAttribArray(uv_location);
  glEnableVertexAttribArray(colour_location);

  glVertexAttribPointer(position_location, 2, GL_FLOAT, GL_FALSE,
                        sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, pos));
  glVertexAttribPointer(uv_location, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert),
                        (GLvoid*)offsetof(ImDrawVert, uv));
  glVertexAttribPointer(colour_location, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                        sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, col));
  glBindVertexArray(0);
  glDisableVertexAttribArray(position_location);
  glDisableVertexAttribArray(uv_location);
  glDisableVertexAttribArray(colour_location);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  unsigned char* pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(
      &pixels, &width, &height);  // Load as RGBA 32-bits for OpenGL3 demo
  // because it is more likely to be compatible
  // with user's existing shader.

  GLuint tex_id;
  glCreateTextures(GL_TEXTURE_2D, 1, &tex_id);
  glTextureParameteri(tex_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(tex_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureStorage2D(tex_id, 1, GL_RGBA8, width, height);
  glTextureSubImage2D(tex_id, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                      pixels);

  // Store our identifier
  io.Fonts->TexID = (void*)(intptr_t)tex_id;

  io.DeltaTime = 1.0f / 60.0f;
  io.RenderDrawListsFn = ImImpl_RenderDrawLists;

  auto& style = ImGui::GetStyle();
  style.WindowRounding = 0;

  style.Colors[ImGuiCol_Text] = ImVec4(0.89f, 0.90f, 0.90f, 1.00f);
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_ChildWindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  style.Colors[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.22f);
  style.Colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 1.00f, 0.00f, 0.78f);
  style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.58f, 0.00f, 0.61f);
  style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.00f, 0.40f, 0.11f, 0.59f);
  style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.00f, 0.68f, 0.00f, 0.68f);
  style.Colors[ImGuiCol_ScrollbarGrabHovered] =
      ImVec4(0.00f, 1.00f, 0.15f, 0.62f);
  style.Colors[ImGuiCol_ScrollbarGrabActive] =
      ImVec4(0.00f, 0.91f, 0.09f, 0.40f);
  style.Colors[ImGuiCol_ComboBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.99f);
  style.Colors[ImGuiCol_CheckHovered] = ImVec4(0.23f, 0.64f, 0.13f, 0.45f);
  style.Colors[ImGuiCol_CheckActive] = ImVec4(0.21f, 0.93f, 0.13f, 0.55f);
  style.Colors[ImGuiCol_CheckMark] = ImVec4(0.74f, 0.90f, 0.72f, 0.50f);
  style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
  style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.34f, 0.75f, 0.11f, 1.00f);
  style.Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.56f, 0.11f, 0.60f);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.72f, 0.09f, 1.00f);
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.19f, 0.60f, 0.09f, 1.00f);
  style.Colors[ImGuiCol_Header] = ImVec4(0.00f, 0.40f, 0.00f, 0.71f);
  style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.60f, 0.26f, 0.80f);
  style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.00f, 0.75f, 0.00f, 0.80f);
  style.Colors[ImGuiCol_Column] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  style.Colors[ImGuiCol_ColumnHovered] = ImVec4(0.36f, 0.89f, 0.38f, 1.00f);
  style.Colors[ImGuiCol_ColumnActive] = ImVec4(0.13f, 0.50f, 0.11f, 1.00f);
  style.Colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
  style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
  style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
  style.Colors[ImGuiCol_CloseButton] = ImVec4(0.00f, 0.72f, 0.00f, 0.96f);
  style.Colors[ImGuiCol_CloseButtonHovered] =
      ImVec4(0.38f, 1.00f, 0.42f, 0.60f);
  style.Colors[ImGuiCol_CloseButtonActive] = ImVec4(0.56f, 1.00f, 0.64f, 1.00f);
  style.Colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_PlotHistogramHovered] =
      ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.00f, 0.00f, 1.00f, 0.35f);
  style.Colors[ImGuiCol_TooltipBg] = ImVec4(0.05f, 0.05f, 0.10f, 0.90f);

  io.KeyMap[ImGuiKey_Tab] = VK_TAB;
  io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
  io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
  io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
  io.KeyMap[ImGuiKey_DownArrow] = VK_UP;
  io.KeyMap[ImGuiKey_Home] = VK_HOME;
  io.KeyMap[ImGuiKey_End] = VK_END;
  io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
  io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
  io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
  io.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
  io.KeyMap[ImGuiKey_A] = 'A';
  io.KeyMap[ImGuiKey_C] = 'C';
  io.KeyMap[ImGuiKey_V] = 'V';
  io.KeyMap[ImGuiKey_X] = 'X';
  io.KeyMap[ImGuiKey_Y] = 'Y';
  io.KeyMap[ImGuiKey_Z] = 'Z';
}
void ImImpl_Shutdown() {
  ImGuiIO& io = ImGui::GetIO();
  if (vao_handle) glDeleteVertexArrays(1, &vao_handle);
  if (vbo_handle) glDeleteBuffers(1, &vbo_handle);
  glDetachShader(shader_handle, vert_handle);
  glDetachShader(shader_handle, frag_handle);
  glDeleteShader(vert_handle);
  glDeleteShader(frag_handle);
  glDeleteProgram(shader_handle);
  auto tex_id = static_cast<GLuint>(intptr_t(io.Fonts->TexID));
  glDeleteTextures(1, &tex_id);
  ImGui::Shutdown();
}
void ImImpl_RenderDrawLists(ImDrawList** const cmd_lists, int cmd_lists_count) {
  if (cmd_lists_count == 0) return;

  // Setup render state: alpha-blending enabled, no face culling, no depth
  // testing, scissor enabled
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_SCISSOR_TEST);
  glActiveTexture(GL_TEXTURE0);

  // Setup orthographic projection matrix
  const float width = ImGui::GetIO().DisplaySize.x;
  const float height = ImGui::GetIO().DisplaySize.y;
  const float ortho_projection[4][4] = {
      {2.0f / width, 0.0f, 0.0f, 0.0f},
      {0.0f, 2.0f / -height, 0.0f, 0.0f},
      {0.0f, 0.0f, -1.0f, 0.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
  };
  glProgramUniform1i(shader_handle, texture_location, 0);
  glProgramUniformMatrix4fv(shader_handle, proj_mtx_location, 1, GL_FALSE,
                            &ortho_projection[0][0]);

  // Grow our buffer according to what we need
  size_t total_vtx_count = 0;
  for (int n = 0; n < cmd_lists_count; n++)
    total_vtx_count += cmd_lists[n]->vtx_buffer.size();
  glBindBuffer(GL_ARRAY_BUFFER, vbo_handle);
  size_t neededBufferSize = total_vtx_count * sizeof(ImDrawVert);
  if (neededBufferSize > vbo_max_size) {
    vbo_max_size = neededBufferSize + 5000;  // Grow buffer
    glBufferData(GL_ARRAY_BUFFER, vbo_max_size, NULL, GL_STREAM_DRAW);
  }

  // Copy and convert all vertices into a single contiguous buffer
  unsigned char* buffer_data =
      (unsigned char*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
  if (!buffer_data) return;
  for (int n = 0; n < cmd_lists_count; n++) {
    const ImDrawList* cmd_list = cmd_lists[n];
    memcpy(buffer_data, &cmd_list->vtx_buffer[0],
           cmd_list->vtx_buffer.size() * sizeof(ImDrawVert));
    buffer_data += cmd_list->vtx_buffer.size() * sizeof(ImDrawVert);
  }
  glUnmapBuffer(GL_ARRAY_BUFFER);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(vao_handle);
  glUseProgram(shader_handle);

  int cmd_offset = 0;
  ImTextureID prev_texture_id = 0;
  for (int n = 0; n < cmd_lists_count; n++) {
    const ImDrawList* cmd_list = cmd_lists[n];
    int vtx_offset = cmd_offset;
    const ImDrawCmd* pcmd_end = cmd_list->commands.end();
    for (const ImDrawCmd* pcmd = cmd_list->commands.begin(); pcmd != pcmd_end;
         pcmd++) {
      if (pcmd->texture_id != prev_texture_id) {
        glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->texture_id);
        prev_texture_id = pcmd->texture_id;
      }
      glScissor((int)pcmd->clip_rect.x, (int)(height - pcmd->clip_rect.w),
                (int)(pcmd->clip_rect.z - pcmd->clip_rect.x),
                (int)(pcmd->clip_rect.w - pcmd->clip_rect.y));
      glDrawArrays(GL_TRIANGLES, vtx_offset, pcmd->vtx_count);
      vtx_offset += pcmd->vtx_count;
    }
    cmd_offset = vtx_offset;
  }

  // Restore modified state
  glBindVertexArray(0);
  glUseProgram(0);
  glDisable(GL_SCISSOR_TEST);
  glBindTexture(GL_TEXTURE_2D, 0);
}

}  // namespace gpu
}  // namespace xe

DEFINE_ENTRY_POINT(L"gpu_trace_viewer", L"gpu_trace_viewer some.trace",
                   xe::gpu::trace_viewer_main);
