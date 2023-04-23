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
struct ProcessInfoBlock;
struct TerminateNotification;
struct X_TIME_STAMP_BUNDLE;
class KernelState;
struct XAPC;

struct X_KPCR;
struct X_KTHREAD;
struct X_OBJECT_HEADER;
struct X_OBJECT_CREATE_INFORMATION;
struct X_OBJECT_TYPE;

}  // namespace xe::kernel

namespace xe::kernel::util {
class NativeList;
class ObjectTable;
}
#endif