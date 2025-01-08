/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_NATIVE_LIST_H_
#define XENIA_KERNEL_UTIL_NATIVE_LIST_H_

#include "xenia/memory.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace util {

// List is designed for storing pointers to objects in the guest heap.
// All values in the list should be assumed to be in big endian.

// Pass LIST_ENTRY pointers.
// struct MYOBJ {
//   uint32_t stuff;
//   LIST_ENTRY list_entry; <-- pass this
//   ...
// }

class NativeList {
 public:
  NativeList();
  explicit NativeList(Memory* memory);

  void Insert(uint32_t list_entry_ptr);
  bool IsQueued(uint32_t list_entry_ptr);
  void Remove(uint32_t list_entry_ptr);
  uint32_t Shift();
  bool HasPending();

  uint32_t head() const { return head_; }
  void set_head(uint32_t head) { head_ = head; }

  void set_memory(Memory* mem) { memory_ = mem; }

 private:
  const uint32_t kInvalidPointer = 0xE0FE0FFF;

 private:
  Memory* memory_ = nullptr;
  uint32_t head_;
};
template <typename VirtualTranslator>
static X_LIST_ENTRY* XeHostList(uint32_t ptr, VirtualTranslator context) {
  return context->template TranslateVirtual<X_LIST_ENTRY*>(ptr);
}
template <typename VirtualTranslator>
static uint32_t XeGuestList(X_LIST_ENTRY* ptr, VirtualTranslator context) {
  return context->HostToGuestVirtual(ptr);
}

// can either pass an object that adheres to the
// HostToGuestVirtual/TranslateVirtual interface, or the original guest ptr for
// arg 2
template <typename VirtualTranslator>
static void XeInitializeListHead(X_LIST_ENTRY* entry,
                                 VirtualTranslator context) {
  // is just a guest ptr?
  if constexpr (std::is_unsigned_v<VirtualTranslator>) {
    entry->blink_ptr = context;
    entry->flink_ptr = context;
  } else {
    uint32_t orig_ptr = XeGuestList(entry, context);
    entry->blink_ptr = orig_ptr;
    entry->flink_ptr = orig_ptr;
  }
}

template <typename VirtualTranslator>
static bool XeIsListEmpty(X_LIST_ENTRY* entry, VirtualTranslator context) {
  return XeHostList(entry->flink_ptr, context) == entry;
}
template <typename VirtualTranslator>
static void XeRemoveEntryList(X_LIST_ENTRY* entry, VirtualTranslator context) {
  uint32_t front = entry->flink_ptr;
  uint32_t back = entry->blink_ptr;
  XeHostList(back, context)->flink_ptr = front;
  XeHostList(front, context)->blink_ptr = back;
}
template <typename VirtualTranslator>
static void XeRemoveEntryList(uint32_t entry, VirtualTranslator context) {
  XeRemoveEntryList(XeHostList(entry, context), context);
}
template <typename VirtualTranslator>
static uint32_t XeRemoveHeadList(X_LIST_ENTRY* entry,
                                 VirtualTranslator context) {
  uint32_t result = entry->flink_ptr;
  XeRemoveEntryList(result, context);
  return result;
}
template <typename VirtualTranslator>
static uint32_t XeRemoveTailList(X_LIST_ENTRY* entry,
                                 VirtualTranslator context) {
  uint32_t result = entry->blink_ptr;
  XeRemoveEntryList(result, context);
  return result;
}
template <typename VirtualTranslator>
static void XeInsertTailList(X_LIST_ENTRY* list_head, uint32_t list_head_guest,
                             X_LIST_ENTRY* host_entry, uint32_t entry,
                             VirtualTranslator context) {
  uint32_t old_tail = list_head->blink_ptr;
  host_entry->flink_ptr = list_head_guest;
  host_entry->blink_ptr = old_tail;
  XeHostList(old_tail, context)->flink_ptr = entry;
  list_head->blink_ptr = entry;
}
template <typename VirtualTranslator>
static void XeInsertTailList(uint32_t list_head, uint32_t entry,
                             VirtualTranslator context) {
  XeInsertTailList(XeHostList(list_head, context), list_head,
                   XeHostList(entry, context), entry, context);
}
template <typename VirtualTranslator>
static void XeInsertTailList(X_LIST_ENTRY* list_head, uint32_t entry,
                             VirtualTranslator context) {
  XeInsertTailList(list_head, XeGuestList(list_head, context),
                   XeHostList(entry, context), entry, context);
}

template <typename VirtualTranslator>
static void XeInsertTailList(X_LIST_ENTRY* list_head, X_LIST_ENTRY* entry,
                             VirtualTranslator context) {
  XeInsertTailList(list_head, XeGuestList(list_head, context), entry,
                   XeGuestList(entry, context), context);
}

template <typename VirtualTranslator>
static void XeInsertHeadList(X_LIST_ENTRY* list_head, uint32_t list_head_guest,
                             X_LIST_ENTRY* host_entry, uint32_t entry,
                             VirtualTranslator context) {
  uint32_t old_list_head_flink = list_head->flink_ptr;
  host_entry->flink_ptr = old_list_head_flink;
  host_entry->blink_ptr = list_head_guest;
  XeHostList(old_list_head_flink, context)->blink_ptr = entry;
  list_head->flink_ptr = entry;
}

template <typename VirtualTranslator>
static void XeInsertHeadList(uint32_t list_head, uint32_t entry,
                             VirtualTranslator context) {
  XeInsertHeadList(XeHostList(list_head, context), list_head,
                   XeHostList(entry, context), entry, context);
}
template <typename VirtualTranslator>
static void XeInsertHeadList(X_LIST_ENTRY* list_head, uint32_t entry,
                             VirtualTranslator context) {
  XeInsertHeadList(list_head, XeGuestList(list_head, context),
                   XeHostList(entry, context), entry, context);
}

template <typename VirtualTranslator>
static void XeInsertHeadList(uint32_t list_head, X_LIST_ENTRY* entry,
                             VirtualTranslator context) {
  XeInsertHeadList(XeHostList(list_head, context), list_head, entry,
                   XeGuestList(entry, context), context);
}
template <typename VirtualTranslator>
static void XeInsertHeadList(X_LIST_ENTRY* list_head, X_LIST_ENTRY* entry,
                             VirtualTranslator context) {
  XeInsertHeadList(list_head, XeGuestList(list_head, context), entry,
                   XeGuestList(entry, context), context);
}

template <typename TObject, size_t EntryListOffset>
struct X_TYPED_LIST : public X_LIST_ENTRY {
 public:
  using this_type = X_TYPED_LIST<TObject, EntryListOffset>;

  static X_LIST_ENTRY* ObjectListEntry(TObject* obj) {
    return reinterpret_cast<X_LIST_ENTRY*>(
        &reinterpret_cast<char*>(obj)[static_cast<ptrdiff_t>(EntryListOffset)]);
  }
  static TObject* ListEntryObject(X_LIST_ENTRY* entry) {
    return reinterpret_cast<TObject*>(&reinterpret_cast<char*>(
        entry)[-static_cast<ptrdiff_t>(EntryListOffset)]);
  }

  template <typename VirtualTranslator>
  struct ForwardIterator {
    VirtualTranslator vt;
    uint32_t current_entry;

    inline ForwardIterator& operator++() {
      current_entry =
          vt->template TranslateVirtual<X_LIST_ENTRY*>(current_entry)
              ->flink_ptr;
      return *this;
    }
    inline bool operator!=(uint32_t other_ptr) const {
      return current_entry != other_ptr;
    }

    inline TObject& operator*() {
      return *ListEntryObject(
          vt->template TranslateVirtual<X_LIST_ENTRY*>(current_entry));
    }
  };
  template <typename VirtualTranslator>
  struct ForwardIteratorBegin {
    VirtualTranslator vt;
    this_type* const thiz;

    ForwardIterator<VirtualTranslator> begin() {
      return ForwardIterator<VirtualTranslator>{vt, thiz->flink_ptr};
    }

    uint32_t end() { return vt->HostToGuestVirtual(thiz); }
  };
  template <typename VirtualTranslator>
  ForwardIteratorBegin<VirtualTranslator> IterateForward(VirtualTranslator vt) {
    return ForwardIteratorBegin<VirtualTranslator>{vt, this};
  }

  template <typename VirtualTranslator>
  void Initialize(VirtualTranslator translator) {
    XeInitializeListHead(this, translator);
  }
  template <typename VirtualTranslator>
  void InsertHead(TObject* entry, VirtualTranslator translator) {
    XeInsertHeadList(static_cast<X_LIST_ENTRY*>(this), ObjectListEntry(entry),
                     translator);
  }
  template <typename VirtualTranslator>
  void InsertTail(TObject* entry, VirtualTranslator translator) {
    XeInsertTailList(this, ObjectListEntry(entry), translator);
  }
  template <typename VirtualTranslator>
  bool empty(VirtualTranslator vt) const {
    return vt->template TranslateVirtual<X_LIST_ENTRY*>(flink_ptr) == this;
  }
  template <typename VirtualTranslator>
  TObject* HeadObject(VirtualTranslator vt) {
    return ListEntryObject(
        vt->template TranslateVirtual<X_LIST_ENTRY*>(flink_ptr));
  }
  template <typename VirtualTranslator>
  TObject* TailObject(VirtualTranslator vt) {
    return ListEntryObject(
        vt->template TranslateVirtual<X_LIST_ENTRY*>(blink_ptr));
  }
};
}  // namespace util
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UTIL_NATIVE_LIST_H_
