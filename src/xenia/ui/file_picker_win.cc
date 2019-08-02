/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/file_picker.h"

#include "xenia/base/assert.h"
#include "xenia/base/platform_win.h"

namespace xe {
namespace ui {

class Win32FilePicker : public FilePicker {
 public:
  Win32FilePicker();
  ~Win32FilePicker() override;

  bool Show(void* parent_window_handle) override;

 private:
};

std::unique_ptr<FilePicker> FilePicker::Create() {
  return std::make_unique<Win32FilePicker>();
}

class CDialogEventHandler : public IFileDialogEvents,
                            public IFileDialogControlEvents {
 public:
  // IUnknown methods
  IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) {
    // dwOffset may be a DWORD or an int depending on compiler/SDK version.
    static const QITAB qit[] = {
        {&__uuidof(IFileDialogEvents),
         static_cast<decltype(qit[0].dwOffset)>(
             OFFSETOFCLASS(IFileDialogEvents, CDialogEventHandler))},
        {&__uuidof(IFileDialogControlEvents),
         static_cast<decltype(qit[1].dwOffset)>(
             OFFSETOFCLASS(IFileDialogControlEvents, CDialogEventHandler))},
        {0},
    };
    return QISearch(this, qit, riid, ppv);
  }

  IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&_cRef); }

  IFACEMETHODIMP_(ULONG) Release() {
    long cRef = InterlockedDecrement(&_cRef);
    if (!cRef) delete this;
    return cRef;
  }

  // IFileDialogEvents methods
  IFACEMETHODIMP OnFileOk(IFileDialog*) { return S_OK; }
  IFACEMETHODIMP OnFolderChange(IFileDialog*) { return S_OK; }
  IFACEMETHODIMP OnFolderChanging(IFileDialog*, IShellItem*) { return S_OK; }
  IFACEMETHODIMP OnHelp(IFileDialog*) { return S_OK; }
  IFACEMETHODIMP OnSelectionChange(IFileDialog*) { return S_OK; }
  IFACEMETHODIMP OnShareViolation(IFileDialog*, IShellItem*,
                                  FDE_SHAREVIOLATION_RESPONSE*) {
    return S_OK;
  }
  IFACEMETHODIMP OnTypeChange(IFileDialog* pfd) { return S_OK; }
  IFACEMETHODIMP OnOverwrite(IFileDialog*, IShellItem*,
                             FDE_OVERWRITE_RESPONSE*) {
    return S_OK;
  }

  // IFileDialogControlEvents methods
  IFACEMETHODIMP OnItemSelected(IFileDialogCustomize* pfdc, DWORD dwIDCtl,
                                DWORD dwIDItem) {
    return S_OK;
  }
  IFACEMETHODIMP OnButtonClicked(IFileDialogCustomize*, DWORD) { return S_OK; }
  IFACEMETHODIMP OnCheckButtonToggled(IFileDialogCustomize*, DWORD, BOOL) {
    return S_OK;
  }
  IFACEMETHODIMP OnControlActivating(IFileDialogCustomize*, DWORD) {
    return S_OK;
  }

  CDialogEventHandler() : _cRef(1) {}

 private:
  virtual ~CDialogEventHandler() = default;
  long _cRef;
};

HRESULT CDialogEventHandler_CreateInstance(REFIID riid, void** ppv) {
  *ppv = NULL;
  auto dialog_event_handler = new (std::nothrow) CDialogEventHandler();
  HRESULT hr = dialog_event_handler ? S_OK : E_OUTOFMEMORY;
  if (SUCCEEDED(hr)) {
    hr = dialog_event_handler->QueryInterface(riid, ppv);
    dialog_event_handler->Release();
  }
  return hr;
}

Win32FilePicker::Win32FilePicker() = default;

Win32FilePicker::~Win32FilePicker() = default;

template <typename T>
struct com_ptr {
  com_ptr() : value(nullptr) {}
  ~com_ptr() { reset(); }
  void reset() {
    if (value) {
      value->Release();
      value = nullptr;
    }
  }
  operator T*() { return value; }
  T* operator->() const { return value; }
  T** addressof() { return &value; }
  T* value;
};

bool Win32FilePicker::Show(void* parent_window_handle) {
  // TODO(benvanik): FileSaveDialog.
  assert_true(mode() == Mode::kOpen);
  // TODO(benvanik): folder dialogs.
  assert_true(type() == Type::kFile);

  com_ptr<IFileDialog> file_dialog;
  HRESULT hr =
      CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                       IID_PPV_ARGS(file_dialog.addressof()));
  if (!SUCCEEDED(hr)) {
    return false;
  }

  hr = file_dialog->SetTitle(title().c_str());
  if (!SUCCEEDED(hr)) {
    return false;
  }

  DWORD flags;
  hr = file_dialog->GetOptions(&flags);
  if (!SUCCEEDED(hr)) {
    return false;
  }
  // FOS_PICKFOLDERS
  // FOS_FILEMUSTEXIST
  // FOS_PATHMUSTEXIST
  flags |= FOS_FORCEFILESYSTEM;
  if (multi_selection()) {
    flags |= FOS_ALLOWMULTISELECT;
  }
  hr = file_dialog->SetOptions(flags);
  if (!SUCCEEDED(hr)) {
    return false;
  }

  // Set the file types to display only. Notice that this is a 1-based array.
  std::vector<COMDLG_FILTERSPEC> save_types;
  auto extensions = this->extensions();
  for (auto& extension : extensions) {
    save_types.push_back({extension.first.c_str(), extension.second.c_str()});
  }
  hr = file_dialog->SetFileTypes(static_cast<UINT>(save_types.size()),
                                 save_types.data());
  if (!SUCCEEDED(hr)) {
    return false;
  }

  hr = file_dialog->SetFileTypeIndex(1);
  if (!SUCCEEDED(hr)) {
    return false;
  }

  // Create an event handling object, and hook it up to the dialog.
  com_ptr<IFileDialogEvents> file_dialog_events;
  hr = CDialogEventHandler_CreateInstance(
      IID_PPV_ARGS(file_dialog_events.addressof()));
  if (!SUCCEEDED(hr)) {
    return false;
  }
  DWORD cookie;
  hr = file_dialog->Advise(file_dialog_events, &cookie);
  if (!SUCCEEDED(hr)) {
    return false;
  }

  // Show the dialog modally.
  hr = file_dialog->Show(static_cast<HWND>(parent_window_handle));
  file_dialog->Unadvise(cookie);
  if (!SUCCEEDED(hr)) {
    return false;
  }

  // Obtain the result once the user clicks the 'Open' button.
  // The result is an IShellItem object.
  com_ptr<IShellItem> shell_item;
  hr = file_dialog->GetResult(shell_item.addressof());
  if (!SUCCEEDED(hr)) {
    return false;
  }

  // We are just going to print out the name of the file for sample sake.
  PWSTR file_path = nullptr;
  hr = shell_item->GetDisplayName(SIGDN_FILESYSPATH, &file_path);
  if (!SUCCEEDED(hr)) {
    return false;
  }
  std::vector<std::wstring> selected_files;
  selected_files.push_back(std::wstring(file_path));
  set_selected_files(selected_files);
  CoTaskMemFree(file_path);

  return true;
}

}  // namespace ui
}  // namespace xe
