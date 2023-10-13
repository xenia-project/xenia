#ifndef XENIA_KERNEL_UTIL_KERNEL_FWD_H_
#define XENIA_KERNEL_UTIL_KERNEL_FWD_H_

namespace xe::kernel {
class Dispatcher;
class XHostThread;
class KernelModule;
class XModule;
class XNotifyListener;
class XThread;
class UserModule;
struct X_KPROCESS;
struct TerminateNotification;
struct X_TIME_STAMP_BUNDLE;
class KernelState;
struct XAPC;

struct X_KPCR;
struct X_KTHREAD;
struct X_OBJECT_CREATE_INFORMATION;

}  // namespace xe::kernel

namespace xe::kernel::util {
class NativeList;
class ObjectTable;
}
#endif