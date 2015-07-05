/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/ui/turbo_badger_control.h"

#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/debug/ui/turbo_badger_renderer.h"

// TODO(benvanik): remove this.
#include "xenia/debug/ui/application.h"

#include "third_party/turbobadger/src/tb/animation_manager.h"
#include "third_party/turbobadger/src/tb/parsing/parse_node.h"
#include "third_party/turbobadger/src/tb/resources/font_manager.h"
#include "third_party/turbobadger/src/tb/turbo_badger.h"
#include "third_party/turbobadger/src/tb/util/string.h"

#include "third_party/turbobadger/src/tb/tb_message_window.h"
#include "third_party/turbobadger/src/tb/tb_scroll_container.h"
#include "third_party/turbobadger/src/tb/tb_text_box.h"
#include "third_party/turbobadger/src/tb/tb_toggle_container.h"

namespace xe {
namespace debug {
namespace ui {

constexpr bool kContinuousRepaint = false;

// Enables long press behaviors (context menu, etc).
constexpr bool kTouch = false;

constexpr uint64_t kDoubleClickDelayMillis = 600;
constexpr double kDoubleClickDistance = 5;

constexpr int32_t kMouseWheelDetent = 120;

class RootElement : public tb::Element {
 public:
  RootElement(TurboBadgerControl* owner) : owner_(owner) {}
  void OnInvalid() override { owner_->Invalidate(); }

 private:
  TurboBadgerControl* owner_ = nullptr;
};

bool TurboBadgerControl::InitializeTurboBadger(
    tb::graphics::Renderer* renderer) {
  static bool has_initialized = false;
  if (has_initialized) {
    return true;
  }
  has_initialized = true;

  if (!tb::Initialize(
          renderer,
          "third_party/turbobadger/resources/language/lng_en.tb.txt")) {
    XELOGE("Failed to initialize turbobadger core");
    return false;
  }

  // Load the default skin, and override skin that contains the graphics
  // specific to the demo.
  if (!tb::resources::Skin::get()->Load(
          "third_party/turbobadger/resources/default_skin/skin.tb.txt",
          "third_party/turbobadger/Demo/demo01/skin/skin.tb.txt")) {
    XELOGE("Failed to load turbobadger skin");
    return false;
  }

// Register font renderers.
#ifdef TB_FONT_RENDERER_TBBF
  void register_tbbf_font_renderer();
  register_tbbf_font_renderer();
#endif
#ifdef TB_FONT_RENDERER_STB
  void register_stb_font_renderer();
  register_stb_font_renderer();
#endif
#ifdef TB_FONT_RENDERER_FREETYPE
  void register_freetype_font_renderer();
  register_freetype_font_renderer();
#endif
  auto font_manager = tb::resources::FontManager::get();

// Add fonts we can use to the font manager.
#if defined(TB_FONT_RENDERER_STB) || defined(TB_FONT_RENDERER_FREETYPE)
  font_manager->AddFontInfo("third_party/turbobadger/resources/vera.ttf",
                            "Default");
#endif
#ifdef TB_FONT_RENDERER_TBBF
  font_manager->AddFontInfo(
      "third_party/turbobadger/resources/default_font/"
      "segoe_white_with_shadow.tb.txt",
      "Default");
#endif

  // Set the default font description for elements to one of the fonts we just
  // added.
  tb::FontDescription fd;
  fd.set_id(TBIDC("Default"));
  fd.SetSize(tb::resources::Skin::get()->GetDimensionConverter()->DpToPx(14));
  font_manager->SetDefaultFontDescription(fd);

  // Create the font now.
  auto font =
      font_manager->CreateFontFace(font_manager->GetDefaultFontDescription());
  return true;
}

void TurboBadgerControl::ShutdownTurboBadger() { tb::Shutdown(); }

TurboBadgerControl::TurboBadgerControl(xe::ui::Loop* loop) : super(loop) {}

TurboBadgerControl::~TurboBadgerControl() = default;

using tb::parsing::ParseNode;

class TextNode {
 public:
  TextNode(const char* name,
           std::vector<std::pair<const char*, const char*>> properties = {},
           std::vector<TextNode> children = {}) {
    node_ = ParseNode::Create(name);
    for (auto& prop : properties) {
      set(prop.first, prop.second);
    }
    for (auto& child : children) {
      node_->Add(child.node_);
    }
  }

  TextNode& set(const char* key, int32_t value) {
    return set(key, tb::Value(value));
  }

  TextNode& set(const char* key, float value) {
    return set(key, tb::Value(value));
  }

  TextNode& set(const char* key, const char* value) {
    return set(key, tb::Value(value));
  }

  TextNode& set(const char* key, const std::string& value) {
    return set(key, tb::Value(value.c_str()));
  }

  TextNode& set(const char* key, tb::Rect value) {
    auto va = new tb::ValueArray();
    va->AddInteger(value.x);
    va->AddInteger(value.y);
    va->AddInteger(value.w);
    va->AddInteger(value.h);
    auto node = node_->GetNode(key, ParseNode::MissingPolicy::kCreate);
    node->GetValue().set_array(va, tb::Value::Set::kTakeOwnership);
    return *this;
  }

  TextNode& set(const char* key, tb::Value& value) {
    auto node = node_->GetNode(key, ParseNode::MissingPolicy::kCreate);
    node->TakeValue(value);
    return *this;
  }

  TextNode& child_list(std::initializer_list<TextNode> children) {
    for (auto& child : children) {
      node_->Add(child.node_);
    }
    return *this;
  }

  ParseNode* node_ = nullptr;
};

namespace node {
using tb::Align;
using tb::Axis;
using tb::EditType;
using ElementState = tb::Element::State;
using tb::Gravity;
using tb::LayoutDistribution;
using tb::LayoutDistributionPosition;
using tb::LayoutOrder;
using tb::LayoutOverflow;
using tb::LayoutPosition;
using tb::LayoutSize;
using tb::Rect;
using tb::ScrollMode;
using tb::TextAlign;
using tb::ToggleAction;
using tb::Visibility;

std::string operator"" _px(uint64_t value) {
  return std::to_string(value) + "px";
}
std::string operator"" _dp(uint64_t value) {
  return std::to_string(value) + "dp";
}
std::string operator"" _mm(uint64_t value) {
  return std::to_string(value) + "mm";
}

struct Dimension {
  Dimension(int32_t value) : value(std::to_string(value) + "px") {}
  Dimension(const char* value) : value(value) {}
  Dimension(std::string value) : value(std::move(value)) {}
  operator std::string() { return value; }
  std::string value;
};
struct Id {
  Id(int32_t value) : is_int(true), int_value(value) {}
  Id(const char* value) : str_value(value) {}
  Id(std::string value) : str_value(std::move(value)) {}
  void set(TextNode* node, const char* key) {
    if (is_int) {
      node->set(key, int_value);
    } else {
      node->set(key, str_value);
    }
  }
  bool is_int = false;
  int32_t int_value = 0;
  std::string str_value;
};

template <typename T>
struct ElementNode : public TextNode {
  using R = T;
  ElementNode(const char* name,
              std::vector<std::pair<const char*, const char*>> properties = {},
              std::vector<TextNode> children = {})
      : TextNode(name, std::move(properties), std::move(children)) {}
  // TBID
  R& id(Id value) {
    value.set(this, "id");
    return *reinterpret_cast<R*>(this);
  }
  // TBID
  R& group_id(Id value) {
    value.set(this, "group-id");
    return *reinterpret_cast<R*>(this);
  }
  // TBID
  R& skin(Id value) {
    value.set(this, "skin");
    return *reinterpret_cast<R*>(this);
  }
  // Number
  R& data(int32_t value) {
    set("data", value);
    return *reinterpret_cast<R*>(this);
  }
  R& data(float value) {
    set("data", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& is_group_root(bool value) {
    set("is-group-root", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& is_focusable(bool value) {
    set("is-focusable", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& is_long_clickable(bool value) {
    set("want-long-click", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& ignore_input(bool value) {
    set("ignore-input", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& opacity(float value) {
    set("opacity", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& connection(std::string value) {
    set("connection", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& axis(Axis value) {
    set("axis", tb::to_string(value));
    return *reinterpret_cast<R*>(this);
  }
  //
  R& gravity(Gravity value) {
    set("gravity", tb::to_string(value));
    return *reinterpret_cast<R*>(this);
  }
  //
  R& visibility(Visibility value) {
    set("visibility", tb::to_string(value));
    return *reinterpret_cast<R*>(this);
  }
  //
  R& state(ElementState value) {
    set("state", tb::to_string(value));
    return *reinterpret_cast<R*>(this);
  }
  //
  R& is_enabled(bool value) {
    set("state",
        to_string(value ? ElementState::kNone : ElementState::kDisabled));
    return *reinterpret_cast<R*>(this);
  }
  // Rect
  R& rect(Rect value) {
    set("rect", std::move(value));
    return *reinterpret_cast<R*>(this);
  }
  //
  R& width(Dimension value) {
    set("lp>width", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& min_width(Dimension value) {
    set("lp>min-width", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& max_width(Dimension value) {
    set("lp>max-width", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& preferred_width(Dimension value) {
    set("lp>pref-width", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& height(Dimension value) {
    set("lp>height", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& min_height(Dimension value) {
    set("lp>min-height", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& max_height(Dimension value) {
    set("lp>max-height", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& preferred_height(Dimension value) {
    set("lp>pref-height", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& tooltip(std::string value) {
    set("tooltip", value);
    return *reinterpret_cast<R*>(this);
  }
  // The Element will be focused automatically the first time its Window is
  // activated.
  R& autofocus(bool value) {
    set("autofocus", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& font(const char* name, int32_t size_px) {
    set("font>name", name);
    set("font>size", size_px);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& font_name(std::string value) {
    set("font>name", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& font_size(Dimension value) {
    set("font>size", value);
    return *reinterpret_cast<R*>(this);
  }

  R& clone(TextNode node) {
    node_->Add(CloneNode(node).node_);
    return *reinterpret_cast<R*>(this);
  }
  R& child(TextNode node) {
    node_->Add(node.node_);
    return *reinterpret_cast<R*>(this);
  }
  R& children() { return *reinterpret_cast<R*>(this); }
  template <typename... C>
  R& children(TextNode child_node, C... other_children) {
    child(child_node);
    children(other_children...);
    return *reinterpret_cast<R*>(this);
  }
};
struct ButtonNode : public ElementNode<ButtonNode> {
  using R = ButtonNode;
  ButtonNode(const char* text = nullptr) : ElementNode("Button") {
    if (text) {
      this->text(text);
    }
  }
  //
  R& text(std::string value) {
    set("text", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& toggle_mode(bool value) {
    set("toggle-mode", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
};
struct SelectInlineNode : public ElementNode<SelectInlineNode> {
  using R = SelectInlineNode;
  SelectInlineNode() : ElementNode("SelectInline") {}
  SelectInlineNode(int32_t default_value, int32_t min_value, int32_t max_value)
      : ElementNode("SelectInline") {
    value(default_value);
    min(min_value);
    max(max_value);
  }
  //
  R& value(int32_t value) {
    set("value", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& min(int32_t value) {
    set("min", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& max(int32_t value) {
    set("max", value);
    return *reinterpret_cast<R*>(this);
  }
};
struct LabelContainerNode : public ElementNode<LabelContainerNode> {
  using R = LabelContainerNode;
  LabelContainerNode(const char* text, std::vector<TextNode> children = {})
      : ElementNode("LabelContainer", {}, std::move(children)) {
    if (text) {
      this->text(text);
    }
  }
  //
  R& text(std::string value) {
    set("text", value);
    return *reinterpret_cast<R*>(this);
  }
};
struct TextBoxNode : public ElementNode<TextBoxNode> {
  using R = TextBoxNode;
  TextBoxNode(const char* text = nullptr) : ElementNode("TextBox") {
    if (text) {
      this->text(text);
    }
  }
  //
  R& text(std::string value) {
    set("text", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& is_multiline(bool value) {
    set("multiline", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& is_styling(bool value) {
    set("styling", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& is_read_only(bool value) {
    set("readonly", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& is_wrapping(bool value) {
    set("wrap", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& adapt_to_content(bool value) {
    set("adapt-to-content", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& virtual_width(Dimension value) {
    set("virtual-width", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& placeholder(std::string value) {
    set("placeholder", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& type(EditType value) {
    set("type", tb::to_string(value));
    return *reinterpret_cast<R*>(this);
  }
};
struct LayoutNode : public ElementNode<LayoutNode> {
  using R = LayoutNode;
  LayoutNode(std::vector<TextNode> children = {})
      : ElementNode("Layout", {}, std::move(children)) {}
  //
  R& spacing(Dimension value) {
    set("spacing", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& size(LayoutSize value) {
    set("size", tb::to_string(value));
    return *reinterpret_cast<R*>(this);
  }
  //
  R& position(LayoutPosition value) {
    set("position", tb::to_string(value));
    return *reinterpret_cast<R*>(this);
  }
  //
  R& overflow(LayoutOverflow value) {
    set("overflow", tb::to_string(value));
    return *reinterpret_cast<R*>(this);
  }
  //
  R& distribution(LayoutDistribution value) {
    set("distribution", tb::to_string(value));
    return *reinterpret_cast<R*>(this);
  }
  //
  R& distribution_position(LayoutDistributionPosition value) {
    set("distribution-position", tb::to_string(value));
    return *reinterpret_cast<R*>(this);
  }
};
struct ScrollContainerNode : public ElementNode<ScrollContainerNode> {
  using R = ScrollContainerNode;
  ScrollContainerNode(std::vector<TextNode> children = {})
      : ElementNode("ScrollContainer", {}, std::move(children)) {}
  //
  R& adapt_content(bool value) {
    set("adapt-content", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& adapt_to_content(bool value) {
    set("adapt-to-content", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& scroll_mode(ScrollMode value) {
    set("scroll-mode", tb::to_string(value));
    return *reinterpret_cast<R*>(this);
  }
};
struct TabContainerNode : public ElementNode<TabContainerNode> {
  using R = TabContainerNode;
  TabContainerNode(std::vector<TextNode> children = {})
      : ElementNode("TabContainer", {}, std::move(children)) {}
  //
  R& value(int32_t value) {
    set("value", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& align(Align value) {
    set("align", tb::to_string(value));
    return *reinterpret_cast<R*>(this);
  }
  // content?
  // root?
  TabContainerNode& tab(TextNode tab_button, TextNode tab_content) {
    auto tabs_node = node_->GetNode("tabs", ParseNode::MissingPolicy::kCreate);
    tabs_node->Add(tab_button.node_);
    node_->Add(tab_content.node_);
    return *this;
  }
};
struct ScrollBarNode : public ElementNode<ScrollBarNode> {
  using R = ScrollBarNode;
  ScrollBarNode() : ElementNode("ScrollBar") {}
  //
  R& value(float value) {
    set("value", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& axis(Axis value) {
    set("axis", tb::to_string(value));
    return *reinterpret_cast<R*>(this);
  }
};
struct SliderNode : public ElementNode<SliderNode> {
  using R = SliderNode;
  SliderNode() : ElementNode("Slider") {}
  //
  R& value(float value) {
    set("value", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& min(float value) {
    set("min", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& max(float value) {
    set("max", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& axis(Axis value) {
    set("axis", tb::to_string(value));
    return *reinterpret_cast<R*>(this);
  }
};
struct Item {
  std::string id;
  std::string text;
  Item(std::string id, std::string text) : id(id), text(text) {}
};
template <typename T>
struct ItemListElementNode : public ElementNode<T> {
  ItemListElementNode(const char* name) : ElementNode(name) {}
  T& item(std::string text) {
    auto items_node =
        node_->GetNode("items", ParseNode::MissingPolicy::kCreate);
    auto node = ParseNode::Create("item");
    auto text_node = ParseNode::Create("text");
    text_node->TakeValue(tb::Value(text.c_str()));
    node->Add(text_node);
    items_node->Add(node);
    return *reinterpret_cast<T*>(this);
  }
  T& item(std::string id, std::string text) {
    auto items_node =
        node_->GetNode("items", ParseNode::MissingPolicy::kCreate);
    auto node = ParseNode::Create("item");
    auto id_node = ParseNode::Create("id");
    id_node->TakeValue(tb::Value(id.c_str()));
    node->Add(id_node);
    auto text_node = ParseNode::Create("text");
    text_node->TakeValue(tb::Value(text.c_str()));
    node->Add(text_node);
    items_node->Add(node);
    return *reinterpret_cast<T*>(this);
  }
  T& item(int32_t id, std::string text) {
    auto items_node =
        node_->GetNode("items", ParseNode::MissingPolicy::kCreate);
    auto node = ParseNode::Create("item");
    auto id_node = ParseNode::Create("id");
    id_node->TakeValue(tb::Value(id));
    node->Add(id_node);
    auto text_node = ParseNode::Create("text");
    text_node->TakeValue(tb::Value(text.c_str()));
    node->Add(text_node);
    items_node->Add(node);
    return *reinterpret_cast<T*>(this);
  }
  T& items(std::initializer_list<std::string> items) {
    auto items_node =
        node_->GetNode("items", ParseNode::MissingPolicy::kCreate);
    for (auto& item : items) {
      auto node = ParseNode::Create("item");
      auto text_node = ParseNode::Create("text");
      text_node->TakeValue(tb::Value(item.c_str()));
      node->Add(text_node);
      items_node->Add(node);
    }
    return *reinterpret_cast<T*>(this);
  }
  T& items(std::initializer_list<std::pair<int32_t, std::string>> items) {
    auto items_node =
        node_->GetNode("items", ParseNode::MissingPolicy::kCreate);
    for (auto& item : items) {
      auto node = ParseNode::Create("item");
      auto id_node = ParseNode::Create("id");
      id_node->TakeValue(tb::Value(item.first));
      node->Add(id_node);
      auto text_node = ParseNode::Create("text");
      text_node->TakeValue(tb::Value(item.second.c_str()));
      node->Add(text_node);
      items_node->Add(node);
    }
    return *reinterpret_cast<T*>(this);
  }
  T& items(std::initializer_list<Item> items) {
    auto items_node =
        node_->GetNode("items", ParseNode::MissingPolicy::kCreate);
    for (auto& item : items) {
      auto node = ParseNode::Create("item");
      auto id_node = ParseNode::Create("id");
      id_node->TakeValue(tb::Value(item.id.c_str()));
      node->Add(id_node);
      auto text_node = ParseNode::Create("text");
      text_node->TakeValue(tb::Value(item.text.c_str()));
      node->Add(text_node);
      items_node->Add(node);
    }
    return *reinterpret_cast<T*>(this);
  }
};
struct SelectListNode : public ItemListElementNode<SelectListNode> {
  using R = SelectListNode;
  SelectListNode() : ItemListElementNode("SelectList") {}
  //
  R& value(int32_t value) {
    set("value", value);
    return *reinterpret_cast<R*>(this);
  }
};
struct SelectDropdownNode : public ItemListElementNode<SelectDropdownNode> {
  using R = SelectDropdownNode;
  SelectDropdownNode() : ItemListElementNode("SelectDropdown") {}
  //
  R& value(int32_t value) {
    set("value", value);
    return *reinterpret_cast<R*>(this);
  }
};
struct CheckBoxNode : public ElementNode<CheckBoxNode> {
  using R = CheckBoxNode;
  CheckBoxNode() : ElementNode("CheckBox") {}
  //
  R& value(bool value) {
    set("value", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
};
struct RadioButtonNode : public ElementNode<RadioButtonNode> {
  using R = RadioButtonNode;
  RadioButtonNode() : ElementNode("RadioButton") {}
  //
  R& value(bool value) {
    set("value", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
};
struct LabelNode : public ElementNode<LabelNode> {
  using R = LabelNode;
  LabelNode(const char* text = nullptr) : ElementNode("Label") {
    if (text) {
      this->text(text);
    }
  }
  //
  R& text(std::string value) {
    set("text", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& text_align(TextAlign value) {
    set("text-align", tb::to_string(value));
    return *reinterpret_cast<R*>(this);
  }
};
struct SkinImageNode : public ElementNode<SkinImageNode> {
  using R = SkinImageNode;
  SkinImageNode() : ElementNode("SkinImage") {}
};
struct SeparatorNode : public ElementNode<SeparatorNode> {
  using R = SeparatorNode;
  SeparatorNode() : ElementNode("Separator") {}
};
struct ProgressSpinnerNode : public ElementNode<ProgressSpinnerNode> {
  using R = ProgressSpinnerNode;
  ProgressSpinnerNode() : ElementNode("ProgressSpinner") {}
  //
  R& value(int32_t value) {
    set("value", value);
    return *reinterpret_cast<R*>(this);
  }
};
struct ContainerNode : public ElementNode<ContainerNode> {
  using R = ContainerNode;
  ContainerNode() : ElementNode("Container") {}
};
struct SectionNode : public ElementNode<SectionNode> {
  using R = SectionNode;
  SectionNode(const char* text = nullptr) : ElementNode("Section") {
    if (text) {
      this->text(text);
    }
  }
  R& content(TextNode child) { return this->child(child); }
  //
  R& value(int32_t value) {
    set("value", value);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& text(std::string value) {
    set("text", value);
    return *reinterpret_cast<R*>(this);
  }
};
struct ToggleContainerNode : public ElementNode<ToggleContainerNode> {
  using R = ToggleContainerNode;
  ToggleContainerNode() : ElementNode("ToggleContainer") {}
  //
  R& value(bool value) {
    set("value", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
  //
  R& toggle_action(ToggleAction value) {
    set("toggle", tb::to_string(value));
    return *reinterpret_cast<R*>(this);
  }
  //
  R& invert(bool value) {
    set("invert", value ? 1 : 0);
    return *reinterpret_cast<R*>(this);
  }
};
struct ImageElementNode : public ElementNode<ImageElementNode> {
  using R = ImageElementNode;
  ImageElementNode(const char* filename = nullptr)
      : ElementNode("ImageElement") {
    if (filename) {
      this->filename(filename);
    }
  }
  //
  R& filename(std::string value) {
    set("filename", value);
    return *reinterpret_cast<R*>(this);
  }
};
struct CloneNode : public TextNode {
  using R = CloneNode;
  CloneNode(TextNode& source) : TextNode(source.node_->GetName()) {
    node_->GetValue().Copy(source.node_->GetValue());
    node_->CloneChildren(source.node_);
  }
};
}  // namespace node
TextNode BuildSomeControl() {
  using namespace node;
  return LayoutNode()
      .axis(Axis::kX)
      .child(LabelNode("foo"))
      .child(LabelNode("bar"));
}
TextNode BuildUI() {
  using namespace node;
  auto rep_tree = LayoutNode().axis(Axis::kX).child_list({
      LabelNode("item"), ButtonNode("button"),
  });
  return LayoutNode()
      .id("foo")
      .position(LayoutPosition::kLeftTop)
      .axis(Axis::kY)
      .children(
          TabContainerNode()
              .gravity(Gravity::kAll)
              .axis(Axis::kX)
              .tab(ButtonNode("Foo"), CloneNode(rep_tree))
              .tab(ButtonNode("Foo0"), CloneNode(rep_tree))
              .tab(ButtonNode("Foo1"), LabelNode("bar1")),
          SectionNode("controls:")
              .content(LayoutNode()
                           .child(BuildSomeControl())
                           .child(BuildSomeControl())),
          LabelNode("distribution: preferred").width(32_dp).font_size(3_mm),
          LayoutNode()
              .distribution(LayoutDistribution::kPreferred)
              .child(ButtonNode("tab 0"))
              .child(TextBoxNode("foo").type(EditType::kPassword))
              .children(ToggleContainerNode()
                            .value(true)
                            .toggle_action(ToggleAction::kExpanded)
                            .child(TextBoxNode()
                                       .placeholder("@search")
                                       .gravity(Gravity::kLeftRight)
                                       .type(EditType::kSearch)),
                        ButtonNode("fffoo").is_enabled(false))
              .child(ButtonNode("tab 0"))
              .child(ButtonNode("tab 0"))
              .clone(rep_tree)
              .children(ButtonNode("tab 1"), ButtonNode("tab 2"),
                        ButtonNode("tab 3"), ButtonNode("tab 4")),
          SelectInlineNode(4, 0, 40), SelectListNode().items({
                                          {1, "a"}, {2, "b"},
                                      }),
          SelectListNode().item("a").item("id", "b").item(5, "c"),
          SelectDropdownNode().value(1).items({
              Item("1", "a"), Item("2", "b"),
          }),
          SelectDropdownNode().value(1).items({
              {1, "a"}, {2, "b"},
          }),
          SelectDropdownNode().value(1).items({
              "a", "b", "c",
          }));
}
bool TurboBadgerControl::Create() {
  if (!super::Create()) {
    return false;
  }

  // TODO(benvanik): setup renderer?
  renderer_ = TBRendererGL4::Create(context());

  if (!InitializeTurboBadger(renderer_.get())) {
    XELOGE("Unable to initialize turbobadger");
    return false;
  }

  // TODO(benvanik): setup elements.
  root_element_ = std::make_unique<RootElement>(this);
  root_element_->SetSkinBg(TBIDC("background"));
  root_element_->set_rect({0, 0, 1000, 1000});

  // ParseNode node;
  ////node.ReadData("Label: text: \"distribution: preferred\"");
  // auto tf = ParseNode::Create("Label");
  // auto tt = ParseNode::Create("text");
  // tt->TakeValue(tb::Value("hello"));
  // tf->Add(tt);
  // node.Add(tf);
  // tb::g_elements_reader->LoadNodeTree(root_element_.get(), &node);
  /*
  Layout: position: left top, axis: y
    Label: text: "distribution: preferred"
    Layout: distribution: preferred
      Button: text: tab 1
      Button: text: tab 2
      Button: text: tab 3
      Button: text: tab 4
      TextBox: placeholder: @search, gravity: left right, type: "search"
    Label: text: "distribution: available"
    Layout: distribution: available
      Button: text: tab 1
      Button: text: tab 2
      Button: text: tab 3
      Button: text: tab 4
      TextBox: placeholder: @search, gravity: left right, type: "search"
  */

  auto n = BuildUI();
  ParseNode r;
  r.Add(n.node_);
  root_element_.get()->LoadNodeTree(&r);
  // n.node_->Clear();
  // delete n.node_;

  // Block animations during init.
  tb::AnimationBlocker anim_blocker;

  // TODO(benvanik): dummy UI.
  auto message_window = new tb::MessageWindow(root_element(), TBIDC(""));
  message_window->Show("Title", "Hello!");

  // tb::ShowDebugInfoSettingsWindow(root_element());

  return true;
}

void TurboBadgerControl::Destroy() {
  tb::Shutdown();
  super::Destroy();
}

void TurboBadgerControl::OnLayout(xe::ui::UIEvent& e) {
  super::OnLayout(e);
  if (!root_element()) {
    return;
  }
  // TODO(benvanik): subregion?
  root_element()->set_rect({0, 0, width(), height()});
}

void TurboBadgerControl::OnPaint(xe::ui::UIEvent& e) {
  super::OnPaint(e);
  if (!root_element()) {
    return;
  }

  ++frame_count_;
  ++fps_frame_count_;
  uint64_t now_ns = xe::Clock::QueryHostSystemTime();
  if (now_ns > fps_update_time_ + 1000 * 10000) {
    fps_ = uint32_t(fps_frame_count_ /
                    (double(now_ns - fps_update_time_) / 10000000.0));
    fps_update_time_ = now_ns;
    fps_frame_count_ = 0;
  }

  // Update TB (run animations, handle deferred input, etc).
  tb::AnimationManager::Update();
  root_element()->InvokeProcessStates();
  root_element()->InvokeProcess();

  renderer()->BeginPaint(width(), height());

  // Render entire control hierarchy.
  root_element()->InvokePaint(tb::Element::PaintProps());

  // Render debug overlay.
  root_element()->GetFont()->DrawString(
      5, 5, tb::Color(255, 0, 0),
      tb::util::format_string("Frame %lld", frame_count_));
  if (kContinuousRepaint) {
    root_element()->GetFont()->DrawString(
        5, 20, tb::Color(255, 0, 0), tb::util::format_string("FPS: %d", fps_));
  }

  renderer()->EndPaint();

  // If animations are running, reinvalidate immediately.
  if (tb::AnimationManager::HasAnimationsRunning()) {
    root_element()->Invalidate();
  }
  if (kContinuousRepaint) {
    // Force an immediate repaint, always.
    root_element()->Invalidate();
  }
}

void TurboBadgerControl::OnGotFocus(xe::ui::UIEvent& e) {
  super::OnGotFocus(e);
}

void TurboBadgerControl::OnLostFocus(xe::ui::UIEvent& e) {
  super::OnLostFocus(e);
  modifier_shift_pressed_ = false;
  modifier_cntrl_pressed_ = false;
  modifier_alt_pressed_ = false;
  modifier_super_pressed_ = false;
  last_click_time_ = 0;
}

tb::ModifierKeys TurboBadgerControl::GetModifierKeys() {
  auto modifiers = tb::ModifierKeys::kNone;
  if (modifier_shift_pressed_) {
    modifiers |= tb::ModifierKeys::kShift;
  }
  if (modifier_cntrl_pressed_) {
    modifiers |= tb::ModifierKeys::kCtrl;
  }
  if (modifier_alt_pressed_) {
    modifiers |= tb::ModifierKeys::kAlt;
  }
  if (modifier_super_pressed_) {
    modifiers |= tb::ModifierKeys::kSuper;
  }
  return modifiers;
}

void TurboBadgerControl::OnKeyPress(xe::ui::KeyEvent& e, bool is_down) {
  if (!root_element()) {
    return;
  }
  auto special_key = tb::SpecialKey::kUndefined;
  switch (e.key_code()) {
    case 38:
      special_key = tb::SpecialKey::kUp;
      break;
    case 39:
      special_key = tb::SpecialKey::kRight;
      break;
    case 40:
      special_key = tb::SpecialKey::kDown;
      break;
    case 37:
      special_key = tb::SpecialKey::kLeft;
      break;
    case 112:
      special_key = tb::SpecialKey::kF1;
      break;
    case 113:
      special_key = tb::SpecialKey::kF2;
      break;
    case 114:
      special_key = tb::SpecialKey::kF3;
      break;
    case 115:
      special_key = tb::SpecialKey::kF4;
      break;
    case 116:
      special_key = tb::SpecialKey::kF5;
      break;
    case 117:
      special_key = tb::SpecialKey::kF6;
      break;
    case 118:
      special_key = tb::SpecialKey::kF7;
      break;
    case 119:
      special_key = tb::SpecialKey::kF8;
      break;
    case 120:
      special_key = tb::SpecialKey::kF9;
      break;
    case 121:
      special_key = tb::SpecialKey::kF10;
      break;
    case 122:
      special_key = tb::SpecialKey::kF11;
      break;
    case 123:
      special_key = tb::SpecialKey::kF12;
      break;
    case 33:
      special_key = tb::SpecialKey::kPageUp;
      break;
    case 34:
      special_key = tb::SpecialKey::kPageDown;
      break;
    case 36:
      special_key = tb::SpecialKey::kHome;
      break;
    case 35:
      special_key = tb::SpecialKey::kEnd;
      break;
    case 45:
      special_key = tb::SpecialKey::kInsert;
      break;
    case 9:
      special_key = tb::SpecialKey::kTab;
      break;
    case 46:
      special_key = tb::SpecialKey::kDelete;
      break;
    case 8:
      special_key = tb::SpecialKey::kBackspace;
      break;
    case 13:
      special_key = tb::SpecialKey::kEnter;
      break;
    case 27:
      special_key = tb::SpecialKey::kEsc;
      break;
    case 93:
      if (!is_down && tb::Element::focused_element) {
        tb::ElementEvent ev(tb::EventType::kContextMenu);
        ev.modifierkeys = GetModifierKeys();
        tb::Element::focused_element->InvokeEvent(ev);
        e.set_handled(true);
        return;
      }
      break;
    case 16:
      modifier_shift_pressed_ = is_down;
      break;
    case 17:
      modifier_cntrl_pressed_ = is_down;
      break;
    // case xx:
    //  // alt ??
    //  modifier_alt_pressed_ = is_down;
    //  break;
    case 91:
      modifier_super_pressed_ = is_down;
      break;
  }

  if (!CheckShortcutKey(e, special_key, is_down)) {
    e.set_handled(root_element()->InvokeKey(
        special_key != tb::SpecialKey::kUndefined ? e.key_code() : 0,
        special_key, GetModifierKeys(), is_down));
  }
}

bool TurboBadgerControl::CheckShortcutKey(xe::ui::KeyEvent& e,
                                          tb::SpecialKey special_key,
                                          bool is_down) {
  bool shortcut_key = modifier_cntrl_pressed_;
  if (!tb::Element::focused_element || !is_down || !shortcut_key) {
    return false;
  }
  bool reverse_key = modifier_shift_pressed_;
  int upper_key = e.key_code();
  if (upper_key >= 'a' && upper_key <= 'z') {
    upper_key += 'A' - 'a';
  }
  tb::TBID id;
  if (upper_key == 'X') {
    id = TBIDC("cut");
  } else if (upper_key == 'C' || special_key == tb::SpecialKey::kInsert) {
    id = TBIDC("copy");
  } else if (upper_key == 'V' ||
             (special_key == tb::SpecialKey::kInsert && reverse_key)) {
    id = TBIDC("paste");
  } else if (upper_key == 'A') {
    id = TBIDC("selectall");
  } else if (upper_key == 'Z' || upper_key == 'Y') {
    bool undo = upper_key == 'Z';
    if (reverse_key) {
      undo = !undo;
    }
    id = undo ? TBIDC("undo") : TBIDC("redo");
  } else if (upper_key == 'N') {
    id = TBIDC("new");
  } else if (upper_key == 'O') {
    id = TBIDC("open");
  } else if (upper_key == 'S') {
    id = TBIDC("save");
  } else if (upper_key == 'W') {
    id = TBIDC("close");
  } else if (special_key == tb::SpecialKey::kPageUp) {
    id = TBIDC("prev_doc");
  } else if (special_key == tb::SpecialKey::kPageDown) {
    id = TBIDC("next_doc");
  } else {
    return false;
  }

  tb::ElementEvent ev(tb::EventType::kShortcut);
  ev.modifierkeys = GetModifierKeys();
  ev.ref_id = id;
  if (!tb::Element::focused_element->InvokeEvent(ev)) {
    return false;
  }
  e.set_handled(true);
  return true;
}

void TurboBadgerControl::OnKeyDown(xe::ui::KeyEvent& e) {
  super::OnKeyDown(e);
  OnKeyPress(e, true);
}

void TurboBadgerControl::OnKeyUp(xe::ui::KeyEvent& e) {
  super::OnKeyUp(e);
  OnKeyPress(e, false);
}

void TurboBadgerControl::OnMouseDown(xe::ui::MouseEvent& e) {
  super::OnMouseDown(e);
  if (!root_element()) {
    return;
  }
  // TODO(benvanik): more button types.
  if (e.button() == xe::ui::MouseEvent::Button::kLeft) {
    // Simulated click count support.
    // TODO(benvanik): move into Control?
    uint64_t now = xe::Clock::QueryHostUptimeMillis();
    if (now < last_click_time_ + kDoubleClickDelayMillis) {
      double distance_moved = std::sqrt(std::pow(e.x() - last_click_x_, 2.0) +
                                        std::pow(e.y() - last_click_y_, 2.0));
      if (distance_moved < kDoubleClickDistance) {
        ++last_click_counter_;
      } else {
        last_click_counter_ = 1;
      }
    } else {
      last_click_counter_ = 1;
    }
    last_click_x_ = e.x();
    last_click_y_ = e.y();
    last_click_time_ = now;

    e.set_handled(root_element()->InvokePointerDown(
        e.x(), e.y(), last_click_counter_, GetModifierKeys(), kTouch));
  }
}

void TurboBadgerControl::OnMouseMove(xe::ui::MouseEvent& e) {
  super::OnMouseMove(e);
  if (!root_element()) {
    return;
  }
  root_element()->InvokePointerMove(e.x(), e.y(), GetModifierKeys(), kTouch);
  e.set_handled(true);
}

void TurboBadgerControl::OnMouseUp(xe::ui::MouseEvent& e) {
  super::OnMouseUp(e);
  if (!root_element()) {
    return;
  }
  if (e.button() == xe::ui::MouseEvent::Button::kLeft) {
    e.set_handled(root_element()->InvokePointerUp(e.x(), e.y(),
                                                  GetModifierKeys(), kTouch));
  } else if (e.button() == xe::ui::MouseEvent::Button::kRight) {
    root_element()->InvokePointerMove(e.x(), e.y(), GetModifierKeys(), kTouch);
    if (tb::Element::hovered_element) {
      int x = e.x();
      int y = e.y();
      tb::Element::hovered_element->ConvertFromRoot(x, y);
      tb::ElementEvent ev(tb::EventType::kContextMenu, x, y, kTouch,
                          GetModifierKeys());
      tb::Element::hovered_element->InvokeEvent(ev);
    }
    e.set_handled(true);
  }
}

void TurboBadgerControl::OnMouseWheel(xe::ui::MouseEvent& e) {
  super::OnMouseWheel(e);
  if (!root_element()) {
    return;
  }
  e.set_handled(root_element()->InvokeWheel(
      e.x(), e.y(), e.dx(), -e.dy() / kMouseWheelDetent, GetModifierKeys()));
}

}  // namespace ui
}  // namespace debug
}  // namespace xe
