/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <jni.h>
#include <cstring>
#include <string>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/main_android.h"
#include "xenia/base/system.h"

namespace xe {

// To store method and field IDs persistently, global references to the classes
// are required to prevent the classes from being unloaded and reloaded,
// potentially changing the IDs.

static jclass android_system_application_context_class_ = nullptr;
static jmethodID android_system_application_context_start_activity_ = nullptr;

static jclass android_system_uri_class_ = nullptr;
static jmethodID android_system_uri_parse_ = nullptr;

static jclass android_system_intent_class_ = nullptr;
static jmethodID android_system_intent_init_action_uri_ = nullptr;
static jmethodID android_system_intent_add_flags_ = nullptr;
static jobject android_system_intent_action_view_ = nullptr;
static jint android_system_intent_flag_activity_new_task_;

static bool android_system_initialized_ = false;

bool InitializeAndroidSystemForApplicationContext() {
  assert_false(android_system_initialized_);

  JNIEnv* jni_env = GetAndroidThreadJniEnv();
  if (!jni_env) {
    return false;
  }
  jobject application_context = xe::GetAndroidApplicationContext();
  if (!application_context) {
    return false;
  }

  // Application context.
  {
    {
      jclass application_context_class_local_ref =
          jni_env->GetObjectClass(application_context);
      if (!application_context_class_local_ref) {
        XELOGE(
            "InitializeAndroidSystemForApplicationContext: Failed to get the "
            "class of the application context");
        ShutdownAndroidSystem();
        return false;
      }
      android_system_application_context_class_ =
          reinterpret_cast<jclass>(jni_env->NewGlobalRef(
              reinterpret_cast<jobject>(application_context_class_local_ref)));
      jni_env->DeleteLocalRef(application_context_class_local_ref);
    }
    if (!android_system_application_context_class_) {
      XELOGE(
          "InitializeAndroidSystemForApplicationContext: Failed to create a "
          "global reference to the class of the application context");
      ShutdownAndroidSystem();
      return false;
    }
    bool application_context_ids_obtained = true;
    application_context_ids_obtained &=
        (android_system_application_context_start_activity_ =
             jni_env->GetMethodID(android_system_application_context_class_,
                                  "startActivity",
                                  "(Landroid/content/Intent;)V")) != nullptr;
    if (!application_context_ids_obtained) {
      XELOGE(
          "InitializeAndroidSystemForApplicationContext: Failed to get the "
          "application context class IDs");
      ShutdownAndroidSystem();
      return false;
    }
  }

  // URI.
  {
    {
      jclass uri_class_local_ref = jni_env->FindClass("android/net/Uri");
      if (!uri_class_local_ref) {
        XELOGE(
            "InitializeAndroidSystemForApplicationContext: Failed to find the "
            "URI class");
        ShutdownAndroidSystem();
        return false;
      }
      android_system_uri_class_ =
          reinterpret_cast<jclass>(jni_env->NewGlobalRef(
              reinterpret_cast<jobject>(uri_class_local_ref)));
      jni_env->DeleteLocalRef(uri_class_local_ref);
    }
    if (!android_system_uri_class_) {
      XELOGE(
          "InitializeAndroidSystemForApplicationContext: Failed to create a "
          "global reference to the URI class");
      ShutdownAndroidSystem();
      return false;
    }
    bool uri_ids_obtained = true;
    uri_ids_obtained &=
        (android_system_uri_parse_ = jni_env->GetStaticMethodID(
             android_system_uri_class_, "parse",
             "(Ljava/lang/String;)Landroid/net/Uri;")) != nullptr;
    if (!uri_ids_obtained) {
      XELOGE(
          "InitializeAndroidSystemForApplicationContext: Failed to get the URI "
          "class IDs");
      ShutdownAndroidSystem();
      return false;
    }
  }

  // Intent.
  {
    {
      jclass intent_class_local_ref =
          jni_env->FindClass("android/content/Intent");
      if (!intent_class_local_ref) {
        XELOGE(
            "InitializeAndroidSystemForApplicationContext: Failed to find the "
            "intent class");
        ShutdownAndroidSystem();
        return false;
      }
      android_system_intent_class_ =
          reinterpret_cast<jclass>(jni_env->NewGlobalRef(
              reinterpret_cast<jobject>(intent_class_local_ref)));
      jni_env->DeleteLocalRef(intent_class_local_ref);
    }
    if (!android_system_intent_class_) {
      XELOGE(
          "InitializeAndroidSystemForApplicationContext: Failed to create a "
          "global reference to the intent class");
      ShutdownAndroidSystem();
      return false;
    }
    bool intent_ids_obtained = true;
    jfieldID intent_action_view_id;
    intent_ids_obtained &= (intent_action_view_id = jni_env->GetStaticFieldID(
                                android_system_intent_class_, "ACTION_VIEW",
                                "Ljava/lang/String;")) != nullptr;
    jfieldID intent_flag_activity_new_task_id;
    intent_ids_obtained &=
        (intent_flag_activity_new_task_id = jni_env->GetStaticFieldID(
             android_system_intent_class_, "FLAG_ACTIVITY_NEW_TASK", "I")) !=
        nullptr;
    intent_ids_obtained &=
        (android_system_intent_init_action_uri_ = jni_env->GetMethodID(
             android_system_intent_class_, "<init>",
             "(Ljava/lang/String;Landroid/net/Uri;)V")) != nullptr;
    intent_ids_obtained &=
        (android_system_intent_add_flags_ =
             jni_env->GetMethodID(android_system_intent_class_, "addFlags",
                                  "(I)Landroid/content/Intent;")) != nullptr;
    if (!intent_ids_obtained) {
      XELOGE(
          "InitializeAndroidSystemForApplicationContext: Failed to get the "
          "intent class IDs");
      ShutdownAndroidSystem();
      return false;
    }
    {
      jobject intent_action_view_local_ref = jni_env->GetStaticObjectField(
          android_system_intent_class_, intent_action_view_id);
      if (!intent_action_view_local_ref) {
        XELOGE(
            "InitializeAndroidSystemForApplicationContext: Failed to get the "
            "intent view action string");
        ShutdownAndroidSystem();
        return false;
      }
      android_system_intent_action_view_ =
          jni_env->NewGlobalRef(intent_action_view_local_ref);
      jni_env->DeleteLocalRef(intent_action_view_local_ref);
      if (!android_system_intent_action_view_) {
        XELOGE(
            "InitializeAndroidSystemForApplicationContext: Failed to create a "
            "global reference to the intent view action string");
        ShutdownAndroidSystem();
        return false;
      }
    }
    android_system_intent_flag_activity_new_task_ = jni_env->GetStaticIntField(
        android_system_intent_class_, intent_flag_activity_new_task_id);
  }

  android_system_initialized_ = true;
  return true;
}

void ShutdownAndroidSystem() {
  // May be called from InitializeAndroidSystemForApplicationContext as well.
  android_system_initialized_ = false;
  android_system_intent_add_flags_ = nullptr;
  android_system_intent_init_action_uri_ = nullptr;
  android_system_uri_parse_ = nullptr;
  android_system_application_context_start_activity_ = nullptr;
  JNIEnv* jni_env = GetAndroidThreadJniEnv();
  if (jni_env) {
    if (android_system_intent_action_view_) {
      jni_env->DeleteGlobalRef(android_system_intent_action_view_);
    }
    if (android_system_intent_class_) {
      jni_env->DeleteGlobalRef(android_system_intent_class_);
    }
    if (android_system_uri_class_) {
      jni_env->DeleteGlobalRef(android_system_uri_class_);
    }
    if (android_system_application_context_class_) {
      jni_env->DeleteGlobalRef(android_system_application_context_class_);
    }
  }
  android_system_intent_action_view_ = nullptr;
  android_system_intent_class_ = nullptr;
  android_system_uri_class_ = nullptr;
  android_system_application_context_class_ = nullptr;
}

void LaunchWebBrowser(const std::string_view url) {
  if (!android_system_initialized_) {
    return;
  }
  JNIEnv* jni_env = GetAndroidThreadJniEnv();
  if (!jni_env) {
    return;
  }
  jobject application_context = GetAndroidApplicationContext();
  if (!application_context) {
    return;
  }

  jstring uri_string = jni_env->NewStringUTF(std::string(url).c_str());
  if (!uri_string) {
    XELOGE("LaunchWebBrowser: Failed to create the URI string");
    return;
  }
  jobject uri = jni_env->CallStaticObjectMethod(
      android_system_uri_class_, android_system_uri_parse_, uri_string);
  jni_env->DeleteLocalRef(uri_string);
  if (!uri) {
    XELOGE("LaunchWebBrowser: Failed to parse the URI");
    return;
  }
  jobject intent = jni_env->NewObject(android_system_intent_class_,
                                      android_system_intent_init_action_uri_,
                                      android_system_intent_action_view_, uri);
  jni_env->DeleteLocalRef(uri);
  if (!intent) {
    XELOGE("LaunchWebBrowser: Failed to create the intent");
    return;
  }
  // Start a new task - the user may want to be able to switch between the
  // emulator and the newly opened web browser, without having to quit the web
  // browser to return to the emulator. Also, since the application context, not
  // the activity, is used, the new task flag is required.
  {
    jobject intent_add_flags_result_local_ref = jni_env->CallObjectMethod(
        intent, android_system_intent_add_flags_,
        android_system_intent_flag_activity_new_task_);
    if (intent_add_flags_result_local_ref) {
      jni_env->DeleteLocalRef(intent_add_flags_result_local_ref);
    }
  }
  jni_env->CallVoidMethod(application_context,
                          android_system_application_context_start_activity_,
                          intent);
  jni_env->DeleteLocalRef(intent);
}

void LaunchFileExplorer(const std::filesystem::path& path) { assert_always(); }

void ShowSimpleMessageBox(SimpleMessageBoxType type, std::string_view message) {
  // TODO(Triang3l): Likely not needed much at all. ShowSimpleMessageBox is a
  // concept pretty unfriendly to platforms like Android because it's blocking,
  // and because it can be called from threads other than the UI thread. In the
  // normal execution flow, dialogs should preferably be asynchronous, and used
  // only in the UI thread. However, non-blocking messages may be good for error
  // reporting - investigate the usage of Toasts with respect to threads, and
  // aborting the process immediately after showing a Toast. For a Toast, the
  // Java VM for the calling thread is needed.
}

bool SetProcessPriorityClass(const uint32_t priority_class) { return true; }

bool IsUseNexusForGameBarEnabled() { return false; }
}  // namespace xe
