/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/main_android.h"

#include <android/log.h>
#include <pthread.h>
#include <cstddef>
#include <cstdlib>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/base/system.h"
#include "xenia/base/threading.h"

namespace xe {

static size_t android_initializations_ = 0;

static int32_t android_api_level_ = __ANDROID_API__;

static JNIEnv* android_main_thread_jni_env_ = nullptr;
static JavaVM* android_java_vm_ = nullptr;
static pthread_key_t android_thread_jni_env_key_;
static jobject android_application_context_ = nullptr;

static void AndroidThreadJNIEnvDestructor(void* jni_env_pointer) {
  // The JNIEnv pointer for the main thread is taken externally, the lifetime of
  // the attachment is not managed by the key.
  JNIEnv* jni_env = static_cast<JNIEnv*>(jni_env_pointer);
  if (jni_env && jni_env != android_main_thread_jni_env_) {
    android_java_vm_->DetachCurrentThread();
  }
  // Multiple iterations of destructor invocations can be done - clear.
  pthread_setspecific(android_thread_jni_env_key_, nullptr);
}

void InitializeAndroidAppFromMainThread(int32_t api_level,
                                        JNIEnv* main_thread_jni_env,
                                        jobject application_context) {
  if (android_initializations_++) {
    // Already initialized for another component in the process.
    return;
  }

  // Set the API level before everything else if available - may be needed by
  // subsystem initialization itself.
  android_api_level_ = api_level;

  android_main_thread_jni_env_ = main_thread_jni_env;
  if (main_thread_jni_env) {
    // In a Java VM, not just in a process that runs an executable - set up
    // the attachment of threads to the Java VM.
    if (main_thread_jni_env->GetJavaVM(&android_java_vm_) < 0) {
      // Logging has not been initialized yet.
      __android_log_write(
          ANDROID_LOG_ERROR, "InitializeAndroidAppFromMainThread",
          "Failed to get the Java VM from the JNI environment of the main "
          "thread");
      std::abort();
    }
    if (pthread_key_create(&android_thread_jni_env_key_,
                           AndroidThreadJNIEnvDestructor)) {
      __android_log_write(
          ANDROID_LOG_ERROR, "InitializeAndroidAppFromMainThread",
          "Failed to create the thread-specific JNI environment key");
      std::abort();
    }
    if (pthread_setspecific(android_thread_jni_env_key_, main_thread_jni_env)) {
      __android_log_write(
          ANDROID_LOG_ERROR, "InitializeAndroidAppFromMainThread",
          "Failed to set the thread-specific JNI environment pointer for the "
          "main thread");
      std::abort();
    }
    if (application_context) {
      android_application_context_ =
          main_thread_jni_env->NewGlobalRef(application_context);
      if (!android_application_context_) {
        __android_log_write(
            ANDROID_LOG_ERROR, "InitializeAndroidAppFromMainThread",
            "Failed to create a global reference to the application context "
            "object");
        std::abort();
      }
    }
  }

  // Logging uses threading.
  xe::threading::AndroidInitialize();

  // Multiple apps can be launched within one process - don't pass the actual
  // app name.
  xe::InitializeLogging("xenia");

  xe::memory::AndroidInitialize();

  if (android_application_context_) {
    if (!xe::InitializeAndroidSystemForApplicationContext()) {
      __android_log_write(ANDROID_LOG_ERROR,
                          "InitializeAndroidAppFromMainThread",
                          "Failed to initialize system UI interaction");
      std::abort();
    }
  }
}

void ShutdownAndroidAppFromMainThread() {
  assert_not_zero(android_initializations_);
  if (!android_initializations_) {
    return;
  }
  if (--android_initializations_) {
    // Other components are still running.
    return;
  }

  xe::ShutdownAndroidSystem();

  xe::memory::AndroidShutdown();

  xe::ShutdownLogging();

  xe::threading::AndroidShutdown();

  if (android_application_context_) {
    android_main_thread_jni_env_->DeleteGlobalRef(android_application_context_);
    android_application_context_ = nullptr;
  }
  if (android_java_vm_) {
    android_java_vm_ = nullptr;
    pthread_key_delete(android_thread_jni_env_key_);
  }
  android_main_thread_jni_env_ = nullptr;

  android_api_level_ = __ANDROID_API__;
}

int32_t GetAndroidApiLevel() { return android_api_level_; }

JNIEnv* GetAndroidThreadJNIEnv() {
  if (!android_java_vm_) {
    return nullptr;
  }
  return static_cast<JNIEnv*>(pthread_getspecific(android_thread_jni_env_key_));
}

jobject GetAndroidApplicationContext() { return android_application_context_; }

}  // namespace xe
