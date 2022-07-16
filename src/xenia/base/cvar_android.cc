/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <jni.h>

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/main_android.h"

namespace cvar {

void ParseLaunchArgumentsFromAndroidBundle(jobject bundle) {
  if (!ConfigVars) {
    return;
  }

  JNIEnv* jni_env = xe::GetAndroidThreadJniEnv();
  if (!jni_env) {
    return;
  }

  jclass bundle_class = jni_env->GetObjectClass(bundle);
  if (!bundle_class) {
    return;
  }
  bool bundle_methods_obtained = true;
  jmethodID bundle_get_boolean =
      jni_env->GetMethodID(bundle_class, "getBoolean", "(Ljava/lang/String;)Z");
  bundle_methods_obtained &= (bundle_get_boolean != nullptr);
  jmethodID bundle_get_double =
      jni_env->GetMethodID(bundle_class, "getDouble", "(Ljava/lang/String;)D");
  bundle_methods_obtained &= (bundle_get_double != nullptr);
  jmethodID bundle_get_int =
      jni_env->GetMethodID(bundle_class, "getInt", "(Ljava/lang/String;)I");
  bundle_methods_obtained &= (bundle_get_int != nullptr);
  jmethodID bundle_get_long =
      jni_env->GetMethodID(bundle_class, "getLong", "(Ljava/lang/String;)J");
  bundle_methods_obtained &= (bundle_get_long != nullptr);
  jmethodID bundle_get_string = jni_env->GetMethodID(
      bundle_class, "getString", "(Ljava/lang/String;)Ljava/lang/String;");
  bundle_methods_obtained &= (bundle_get_string != nullptr);
  jmethodID bundle_key_set_method_id =
      jni_env->GetMethodID(bundle_class, "keySet", "()Ljava/util/Set;");
  bundle_methods_obtained &= (bundle_key_set_method_id != nullptr);
  if (!bundle_methods_obtained) {
    jni_env->DeleteLocalRef(bundle_class);
    return;
  }

  jobject key_set = jni_env->CallObjectMethod(bundle, bundle_key_set_method_id);
  if (!key_set) {
    jni_env->DeleteLocalRef(bundle_class);
    return;
  }

  jclass set_class = jni_env->GetObjectClass(key_set);
  if (!set_class) {
    jni_env->DeleteLocalRef(key_set);
    jni_env->DeleteLocalRef(bundle_class);
    return;
  }
  bool set_methods_obtained = true;
  jmethodID set_iterator_method_id =
      jni_env->GetMethodID(set_class, "iterator", "()Ljava/util/Iterator;");
  set_methods_obtained &= (set_iterator_method_id != nullptr);
  if (!set_methods_obtained) {
    jni_env->DeleteLocalRef(set_class);
    jni_env->DeleteLocalRef(key_set);
    jni_env->DeleteLocalRef(bundle_class);
    return;
  }

  jobject key_set_iterator =
      jni_env->CallObjectMethod(key_set, set_iterator_method_id);
  if (!key_set_iterator) {
    jni_env->DeleteLocalRef(set_class);
    jni_env->DeleteLocalRef(key_set);
    jni_env->DeleteLocalRef(bundle_class);
    return;
  }

  jclass iterator_class = jni_env->GetObjectClass(key_set_iterator);
  if (!iterator_class) {
    jni_env->DeleteLocalRef(key_set_iterator);
    jni_env->DeleteLocalRef(set_class);
    jni_env->DeleteLocalRef(key_set);
    jni_env->DeleteLocalRef(bundle_class);
    return;
  }
  bool iterator_methods_obtained = true;
  jmethodID iterator_has_next =
      jni_env->GetMethodID(iterator_class, "hasNext", "()Z");
  iterator_methods_obtained &= (iterator_has_next != nullptr);
  jmethodID iterator_next =
      jni_env->GetMethodID(iterator_class, "next", "()Ljava/lang/Object;");
  iterator_methods_obtained &= (iterator_next != nullptr);
  if (!iterator_methods_obtained) {
    jni_env->DeleteLocalRef(iterator_class);
    jni_env->DeleteLocalRef(key_set_iterator);
    jni_env->DeleteLocalRef(set_class);
    jni_env->DeleteLocalRef(key_set);
    jni_env->DeleteLocalRef(bundle_class);
    return;
  }

  while (jni_env->CallBooleanMethod(key_set_iterator, iterator_has_next)) {
    jstring key = reinterpret_cast<jstring>(
        jni_env->CallObjectMethod(key_set_iterator, iterator_next));
    if (!key) {
      continue;
    }
    const char* key_utf = jni_env->GetStringUTFChars(key, nullptr);
    if (!key_utf) {
      jni_env->DeleteLocalRef(key);
      continue;
    }
    auto cvar_it = ConfigVars->find(key_utf);
    jni_env->ReleaseStringUTFChars(key, key_utf);
    // key_utf can't be used from now on.
    if (cvar_it == ConfigVars->end()) {
      jni_env->DeleteLocalRef(key);
      continue;
    }
    IConfigVar* cvar = cvar_it->second;
    auto cvar_bool = dynamic_cast<CommandVar<bool>*>(cvar);
    if (cvar_bool) {
      cvar_bool->SetCommandLineValue(
          bool(jni_env->CallBooleanMethod(bundle, bundle_get_boolean, key)));
      jni_env->DeleteLocalRef(key);
      continue;
    }
    auto cvar_int32 = dynamic_cast<CommandVar<int32_t>*>(cvar);
    if (cvar_int32) {
      cvar_int32->SetCommandLineValue(
          jni_env->CallIntMethod(bundle, bundle_get_int, key));
      jni_env->DeleteLocalRef(key);
      continue;
    }
    auto cvar_uint32 = dynamic_cast<CommandVar<uint32_t>*>(cvar);
    if (cvar_uint32) {
      cvar_uint32->SetCommandLineValue(
          uint32_t(jni_env->CallIntMethod(bundle, bundle_get_int, key)));
      jni_env->DeleteLocalRef(key);
      continue;
    }
    auto cvar_uint64 = dynamic_cast<CommandVar<uint64_t>*>(cvar);
    if (cvar_uint64) {
      cvar_uint64->SetCommandLineValue(
          uint64_t(jni_env->CallLongMethod(bundle, bundle_get_long, key)));
      jni_env->DeleteLocalRef(key);
      continue;
    }
    auto cvar_double = dynamic_cast<CommandVar<double>*>(cvar);
    if (cvar_double) {
      cvar_double->SetCommandLineValue(
          jni_env->CallDoubleMethod(bundle, bundle_get_double, key));
      jni_env->DeleteLocalRef(key);
      continue;
    }
    auto cvar_string = dynamic_cast<CommandVar<std::string>*>(cvar);
    if (cvar_string) {
      jstring cvar_string_value = reinterpret_cast<jstring>(
          jni_env->CallObjectMethod(bundle, bundle_get_string, key));
      if (cvar_string_value) {
        const char* cvar_string_value_utf =
            jni_env->GetStringUTFChars(cvar_string_value, nullptr);
        if (cvar_string_value_utf) {
          cvar_string->SetCommandLineValue(cvar_string_value_utf);
          jni_env->ReleaseStringUTFChars(cvar_string_value,
                                         cvar_string_value_utf);
        }
        jni_env->DeleteLocalRef(cvar_string_value);
      }
      jni_env->DeleteLocalRef(key);
      continue;
    }
    auto cvar_path = dynamic_cast<CommandVar<std::filesystem::path>*>(cvar);
    if (cvar_path) {
      jstring cvar_string_value = reinterpret_cast<jstring>(
          jni_env->CallObjectMethod(bundle, bundle_get_string, key));
      if (cvar_string_value) {
        const char* cvar_string_value_utf =
            jni_env->GetStringUTFChars(cvar_string_value, nullptr);
        if (cvar_string_value_utf) {
          cvar_path->SetCommandLineValue(cvar_string_value_utf);
          jni_env->ReleaseStringUTFChars(cvar_string_value,
                                         cvar_string_value_utf);
        }
        jni_env->DeleteLocalRef(cvar_string_value);
      }
      jni_env->DeleteLocalRef(key);
      continue;
    }
    assert_always("Unsupported type of cvar {}", cvar->name().c_str());
    jni_env->DeleteLocalRef(key);
  }

  jni_env->DeleteLocalRef(iterator_class);
  jni_env->DeleteLocalRef(key_set_iterator);
  jni_env->DeleteLocalRef(set_class);
  jni_env->DeleteLocalRef(key_set);
  jni_env->DeleteLocalRef(bundle_class);
}

}  // namespace cvar
