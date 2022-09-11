#pragma once
namespace xe {
/*
        a very simple freelist, intended to be used with HIRFunction/Arena to
   eliminate our last-level cache miss problems with HIR simplifications not
   thread safe, doesnt need to be
*/
template <typename T>
struct SimpleFreelist {
  union Node {
    union Node* next_;
    T entry_;
  };
  Node* head_;

  static_assert(sizeof(T) >= sizeof(void*));
  SimpleFreelist() : head_(nullptr) {}
  T* NewEntry() {
    Node* result_node = head_;
    if (!result_node) {
      return nullptr;
    } else {
      head_ = result_node->next_;

      memset(result_node, 0, sizeof(T));
      return &result_node->entry_;
      // return new (&result_node->entry_) T(args...);
    }
  }

  void DeleteEntry(T* value) {
    memset(value, 0, sizeof(T));
    Node* node = reinterpret_cast<Node*>(value);
    node->next_ = head_;
    head_ = node;
  }
  void Reset() { head_ = nullptr;
  }
};
}  // namespace xe