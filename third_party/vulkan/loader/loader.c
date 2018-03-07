/*
 *
 * Copyright (c) 2014-2018 The Khronos Group Inc.
 * Copyright (c) 2014-2018 Valve Corporation
 * Copyright (c) 2014-2018 LunarG, Inc.
 * Copyright (C) 2015 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.

 *
 * Author: Jon Ashburn <jon@lunarg.com>
 * Author: Courtney Goeltzenleuchter <courtney@LunarG.com>
 * Author: Mark Young <marky@lunarg.com>
 * Author: Lenny Komow <lenny@lunarg.com>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>

#include <sys/types.h>
#if defined(_WIN32)
#include "dirent_on_windows.h"
#else  // _WIN32
#include <dirent.h>
#endif  // _WIN32
#include "vk_loader_platform.h"
#include "loader.h"
#include "gpa_helper.h"
#include "debug_utils.h"
#include "wsi.h"
#include "vulkan/vk_icd.h"
#include "cJSON.h"
#include "murmurhash.h"

#if defined(_WIN32)
#include <Cfgmgr32.h>
#include <initguid.h>
#include <Devpkey.h>
#endif

// This is a CMake generated file with #defines for any functions/includes
// that it found present.  This is currently necessary to properly determine
// if secure_getenv or __secure_getenv are present
#if !defined(VULKAN_NON_CMAKE_BUILD)
#include "loader_cmake_config.h"
#endif  // !defined(VULKAN_NON_CMAKE_BUILD)

// Generated file containing all the extension data
#include "vk_loader_extensions.c"

struct loader_struct loader = {0};
// TLS for instance for alloc/free callbacks
THREAD_LOCAL_DECL struct loader_instance *tls_instance;

static size_t loader_platform_combine_path(char *dest, size_t len, ...);

struct loader_phys_dev_per_icd {
    uint32_t count;
    VkPhysicalDevice *phys_devs;
    struct loader_icd_term *this_icd_term;
};

enum loader_debug {
    LOADER_INFO_BIT = 0x01,
    LOADER_WARN_BIT = 0x02,
    LOADER_PERF_BIT = 0x04,
    LOADER_ERROR_BIT = 0x08,
    LOADER_DEBUG_BIT = 0x10,
};

uint32_t g_loader_debug = 0;
uint32_t g_loader_log_msgs = 0;

// thread safety lock for accessing global data structures such as "loader"
// all entrypoints on the instance chain need to be locked except GPA
// additionally CreateDevice and DestroyDevice needs to be locked
loader_platform_thread_mutex loader_lock;
loader_platform_thread_mutex loader_json_lock;

LOADER_PLATFORM_THREAD_ONCE_DECLARATION(once_init);

// This loader supports Vulkan API version 1.1
uint32_t loader_major_version = 1;
uint32_t loader_minor_version = 1;

void *loader_instance_heap_alloc(const struct loader_instance *instance, size_t size, VkSystemAllocationScope alloc_scope) {
    void *pMemory = NULL;
#if (DEBUG_DISABLE_APP_ALLOCATORS == 1)
    {
#else
    if (instance && instance->alloc_callbacks.pfnAllocation) {
        // These are internal structures, so it's best to align everything to
        // the largest unit size which is the size of a uint64_t.
        pMemory = instance->alloc_callbacks.pfnAllocation(instance->alloc_callbacks.pUserData, size, sizeof(uint64_t), alloc_scope);
    } else {
#endif
        pMemory = malloc(size);
    }

    return pMemory;
}

void loader_instance_heap_free(const struct loader_instance *instance, void *pMemory) {
    if (pMemory != NULL) {
#if (DEBUG_DISABLE_APP_ALLOCATORS == 1)
        {
#else
        if (instance && instance->alloc_callbacks.pfnFree) {
            instance->alloc_callbacks.pfnFree(instance->alloc_callbacks.pUserData, pMemory);
        } else {
#endif
            free(pMemory);
        }
    }
}

void *loader_instance_heap_realloc(const struct loader_instance *instance, void *pMemory, size_t orig_size, size_t size,
                                   VkSystemAllocationScope alloc_scope) {
    void *pNewMem = NULL;
    if (pMemory == NULL || orig_size == 0) {
        pNewMem = loader_instance_heap_alloc(instance, size, alloc_scope);
    } else if (size == 0) {
        loader_instance_heap_free(instance, pMemory);
#if (DEBUG_DISABLE_APP_ALLOCATORS == 1)
#else
    } else if (instance && instance->alloc_callbacks.pfnReallocation) {
        // These are internal structures, so it's best to align everything to
        // the largest unit size which is the size of a uint64_t.
        pNewMem = instance->alloc_callbacks.pfnReallocation(instance->alloc_callbacks.pUserData, pMemory, size, sizeof(uint64_t),
                                                            alloc_scope);
#endif
    } else {
        pNewMem = realloc(pMemory, size);
    }
    return pNewMem;
}

void *loader_instance_tls_heap_alloc(size_t size) {
    return loader_instance_heap_alloc(tls_instance, size, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
}

void loader_instance_tls_heap_free(void *pMemory) { loader_instance_heap_free(tls_instance, pMemory); }

void *loader_device_heap_alloc(const struct loader_device *device, size_t size, VkSystemAllocationScope alloc_scope) {
    void *pMemory = NULL;
#if (DEBUG_DISABLE_APP_ALLOCATORS == 1)
    {
#else
    if (device && device->alloc_callbacks.pfnAllocation) {
        // These are internal structures, so it's best to align everything to
        // the largest unit size which is the size of a uint64_t.
        pMemory = device->alloc_callbacks.pfnAllocation(device->alloc_callbacks.pUserData, size, sizeof(uint64_t), alloc_scope);
    } else {
#endif
        pMemory = malloc(size);
    }
    return pMemory;
}

void loader_device_heap_free(const struct loader_device *device, void *pMemory) {
    if (pMemory != NULL) {
#if (DEBUG_DISABLE_APP_ALLOCATORS == 1)
        {
#else
        if (device && device->alloc_callbacks.pfnFree) {
            device->alloc_callbacks.pfnFree(device->alloc_callbacks.pUserData, pMemory);
        } else {
#endif
            free(pMemory);
        }
    }
}

void *loader_device_heap_realloc(const struct loader_device *device, void *pMemory, size_t orig_size, size_t size,
                                 VkSystemAllocationScope alloc_scope) {
    void *pNewMem = NULL;
    if (pMemory == NULL || orig_size == 0) {
        pNewMem = loader_device_heap_alloc(device, size, alloc_scope);
    } else if (size == 0) {
        loader_device_heap_free(device, pMemory);
#if (DEBUG_DISABLE_APP_ALLOCATORS == 1)
#else
    } else if (device && device->alloc_callbacks.pfnReallocation) {
        // These are internal structures, so it's best to align everything to
        // the largest unit size which is the size of a uint64_t.
        pNewMem = device->alloc_callbacks.pfnReallocation(device->alloc_callbacks.pUserData, pMemory, size, sizeof(uint64_t),
                                                          alloc_scope);
#endif
    } else {
        pNewMem = realloc(pMemory, size);
    }
    return pNewMem;
}

// Environment variables
#if defined(__linux__)

static inline char *loader_getenv(const char *name, const struct loader_instance *inst) {
    // No allocation of memory necessary for Linux, but we should at least touch
    // the inst pointer to get rid of compiler warnings.
    (void)inst;
    return getenv(name);
}

static inline char *loader_secure_getenv(const char *name, const struct loader_instance *inst) {
    // No allocation of memory necessary for Linux, but we should at least touch
    // the inst pointer to get rid of compiler warnings.
    (void)inst;

#ifdef HAVE_SECURE_GETENV
    return secure_getenv(name);
#elif defined(HAVE___SECURE_GETENV)
    return __secure_getenv(name);
#else
#pragma message(                                                                       \
    "Warning:  Falling back to non-secure getenv for environmental lookups!  Consider" \
    " updating to a different libc.")
    return loader_getenv(name, inst);
#endif
}

static inline void loader_free_getenv(char *val, const struct loader_instance *inst) {
    // No freeing of memory necessary for Linux, but we should at least touch
    // the val and inst pointers to get rid of compiler warnings.
    (void)val;
    (void)inst;
}

#elif defined(WIN32)

static inline char *loader_getenv(const char *name, const struct loader_instance *inst) {
    char *retVal;
    DWORD valSize;

    valSize = GetEnvironmentVariableA(name, NULL, 0);

    // valSize DOES include the null terminator, so for any set variable
    // will always be at least 1. If it's 0, the variable wasn't set.
    if (valSize == 0) return NULL;

    // Allocate the space necessary for the registry entry
    if (NULL != inst && NULL != inst->alloc_callbacks.pfnAllocation) {
        retVal = (char *)inst->alloc_callbacks.pfnAllocation(inst->alloc_callbacks.pUserData, valSize, sizeof(char *),
                                                             VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
    } else {
        retVal = (char *)malloc(valSize);
    }

    if (NULL != retVal) {
        GetEnvironmentVariableA(name, retVal, valSize);
    }

    return retVal;
}

static inline char *loader_secure_getenv(const char *name, const struct loader_instance *inst) {
    // No secure version for Windows as far as I know
    return loader_getenv(name, inst);
}

static inline void loader_free_getenv(char *val, const struct loader_instance *inst) {
    if (NULL != inst && NULL != inst->alloc_callbacks.pfnFree) {
        inst->alloc_callbacks.pfnFree(inst->alloc_callbacks.pUserData, val);
    } else {
        free((void *)val);
    }
}

#else

static inline char *loader_getenv(const char *name, const struct loader_instance *inst) {
    // stub func
    (void)inst;
    (void)name;
    return NULL;
}
static inline void loader_free_getenv(char *val, const struct loader_instance *inst) {
    // stub func
    (void)val;
    (void)inst;
}

#endif

void loader_log(const struct loader_instance *inst, VkFlags msg_type, int32_t msg_code, const char *format, ...) {
    char msg[512];
    char cmd_line_msg[512];
    size_t cmd_line_size = sizeof(cmd_line_msg);
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = vsnprintf(msg, sizeof(msg), format, ap);
    if ((ret >= (int)sizeof(msg)) || ret < 0) {
        msg[sizeof(msg) - 1] = '\0';
    }
    va_end(ap);

    if (inst) {
        VkDebugUtilsMessageSeverityFlagBitsEXT severity = 0;
        VkDebugUtilsMessageTypeFlagsEXT type;
        VkDebugUtilsMessengerCallbackDataEXT callback_data;
        VkDebugUtilsObjectNameInfoEXT object_name;

        if ((msg_type & LOADER_INFO_BIT) != 0) {
            severity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
        } else if ((msg_type & LOADER_WARN_BIT) != 0) {
            severity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        } else if ((msg_type & LOADER_ERROR_BIT) != 0) {
            severity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        } else if ((msg_type & LOADER_DEBUG_BIT) != 0) {
            severity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
        }

        if ((msg_type & LOADER_PERF_BIT) != 0) {
            type = VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        } else {
            type = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        }

        callback_data.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT;
        callback_data.pNext = NULL;
        callback_data.flags = 0;
        callback_data.pMessageIdName = "Loader Message";
        callback_data.messageIdNumber = 0;
        callback_data.pMessage = msg;
        callback_data.queueLabelCount = 0;
        callback_data.pQueueLabels = NULL;
        callback_data.cmdBufLabelCount = 0;
        callback_data.pCmdBufLabels = NULL;
        callback_data.objectCount = 1;
        callback_data.pObjects = &object_name;
        object_name.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        object_name.pNext = NULL;
        object_name.objectType = VK_OBJECT_TYPE_INSTANCE;
        object_name.objectHandle = (uint64_t)(uintptr_t)inst;
        object_name.pObjectName = NULL;

        util_SubmitDebugUtilsMessageEXT(inst, severity, type, &callback_data);
    }

    if (!(msg_type & g_loader_log_msgs)) {
        return;
    }

    cmd_line_msg[0] = '\0';
    cmd_line_size -= 1;
    size_t original_size = cmd_line_size;

    va_start(ap, format);
    if ((msg_type & LOADER_INFO_BIT) != 0) {
        strncat(cmd_line_msg, "INFO", cmd_line_size);
        cmd_line_size -= 4;
    }
    if ((msg_type & LOADER_WARN_BIT) != 0) {
        if (cmd_line_size != original_size) {
            strncat(cmd_line_msg, " | ", cmd_line_size);
            cmd_line_size -= 3;
        }
        strncat(cmd_line_msg, "WARNING", cmd_line_size);
        cmd_line_size -= 7;
    }
    if ((msg_type & LOADER_PERF_BIT) != 0) {
        if (cmd_line_size != original_size) {
            strncat(cmd_line_msg, " | ", cmd_line_size);
            cmd_line_size -= 3;
        }
        strncat(cmd_line_msg, "PERF", cmd_line_size);
        cmd_line_size -= 4;
    }
    if ((msg_type & LOADER_ERROR_BIT) != 0) {
        if (cmd_line_size != original_size) {
            strncat(cmd_line_msg, " | ", cmd_line_size);
            cmd_line_size -= 3;
        }
        strncat(cmd_line_msg, "ERROR", cmd_line_size);
        cmd_line_size -= 5;
    }
    if ((msg_type & LOADER_DEBUG_BIT) != 0) {
        if (cmd_line_size != original_size) {
            strncat(cmd_line_msg, " | ", cmd_line_size);
            cmd_line_size -= 3;
        }
        strncat(cmd_line_msg, "DEBUG", cmd_line_size);
        cmd_line_size -= 5;
    }
    if (cmd_line_size != original_size) {
        strncat(cmd_line_msg, ": ", cmd_line_size);
        cmd_line_size -= 2;
    }

    if (0 < cmd_line_size) {
        // If the message is too long, trim it down
        if (strlen(msg) > cmd_line_size) {
            msg[cmd_line_size - 1] = '\0';
        }
        strncat(cmd_line_msg, msg, cmd_line_size);
    } else {
        // Shouldn't get here, but check to make sure if we've already overrun
        // the string boundary
        assert(false);
    }

#if defined(WIN32)
    OutputDebugString(cmd_line_msg);
    OutputDebugString("\n");
#endif

    fputs(cmd_line_msg, stderr);
    fputc('\n', stderr);
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetInstanceDispatch(VkInstance instance, void *object) {
    struct loader_instance *inst = loader_get_instance(instance);
    if (!inst) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "vkSetInstanceDispatch: Can not retrieve Instance "
                   "dispatch table.");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    loader_set_dispatch(object, inst->disp);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetDeviceDispatch(VkDevice device, void *object) {
    struct loader_device *dev;
    struct loader_icd_term *icd_term = loader_get_icd_and_device(device, &dev, NULL);

    if (NULL == icd_term) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    loader_set_dispatch(object, &dev->loader_dispatch);
    return VK_SUCCESS;
}

#if defined(_WIN32)

// Append the JSON path data to the list and allocate/grow the list if it's not large enough.
// Function returns true if filename was appended to reg_data list.
// Caller should free reg_data.
static bool loaderAddJsonEntry(const struct loader_instance *inst,
                               char **reg_data,    // list of JSON files
                               PDWORD total_size,  // size of reg_data
                               LPCTSTR key_name,   // key name - used for debug prints - i.e. VulkanDriverName
                               DWORD key_type,     // key data type
                               LPSTR json_path,    // JSON string to add to the list reg_data
                               DWORD json_size,    // size in bytes of json_path
                               VkResult *result) {
    if (NULL == *reg_data) {
        *reg_data = loader_instance_heap_alloc(inst, *total_size, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (NULL == *reg_data) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loaderAddJsonEntry: Failed to allocate space for registry data for key %s", json_path);
            *result = VK_ERROR_OUT_OF_HOST_MEMORY;
            return false;
        }
        *reg_data[0] = '\0';
    } else if (strlen(*reg_data) + json_size + 1 > *total_size) {
        void *new_ptr =
            loader_instance_heap_realloc(inst, *reg_data, *total_size, *total_size * 2, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (NULL == new_ptr) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loaderAddJsonEntry: Failed to reallocate space for registry value of size %d for key %s", *total_size * 2,
                       json_path);
            *result = VK_ERROR_OUT_OF_HOST_MEMORY;
            return false;
        }
        *reg_data = new_ptr;
        *total_size *= 2;
    }

    for (char *curr_filename = json_path; curr_filename[0] != '\0'; curr_filename += strlen(curr_filename) + 1) {
        if (strlen(*reg_data) == 0) {
            (void)snprintf(*reg_data, json_size + 1, "%s", curr_filename);
        } else {
            (void)snprintf(*reg_data + strlen(*reg_data), json_size + 2, "%c%s", PATH_SEPARATOR, curr_filename);
        }
        loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0, "%s: Located json file \"%s\" from PnP registry: %s", __FUNCTION__,
                   curr_filename, key_name);

        if (key_type == REG_SZ) {
            break;
        }
    }
    return true;
}

// Find the list of registry files (names VulkanDriverName/VulkanDriverNameWow) in hkr.
//
// This function looks for filename in given device handle, filename is then added to return list
// function return true if filename was appended to reg_data list
// If error occures result is updated with failure reason
bool loaderGetDeviceRegistryEntry(const struct loader_instance *inst, char **reg_data, PDWORD total_size, DEVINST dev_id, LPCTSTR value_name, VkResult *result)
{
    HKEY hkrKey = INVALID_HANDLE_VALUE;
    DWORD requiredSize, data_type;
    char *manifest_path = NULL;
    bool found = false;

    if (NULL == total_size || NULL == reg_data) {
        *result = VK_ERROR_INITIALIZATION_FAILED;
        return false;
    }

    CONFIGRET status = CM_Open_DevNode_Key(dev_id, KEY_QUERY_VALUE, 0, RegDisposition_OpenExisting, &hkrKey, CM_REGISTRY_SOFTWARE);
    if (status != CR_SUCCESS) {
        loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
            "loaderGetDeviceRegistryEntry: Failed to open registry key for DeviceID(%d)", dev_id);
        *result = VK_ERROR_INITIALIZATION_FAILED;
        return false;
    }

    // query value
    LSTATUS ret = RegQueryValueEx(
        hkrKey,
        value_name,
        NULL,
        NULL,
        NULL,
        &requiredSize);

    if (ret != ERROR_SUCCESS) {
        if (ret == ERROR_FILE_NOT_FOUND) {
            loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                "loaderGetDeviceRegistryEntry: Device ID(%d) Does not contain a value for \"%s\"", dev_id, value_name);
        } else {
            loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                "loaderGetDeviceRegistryEntry: DeviceID(%d) Failed to obtain %s size", dev_id, value_name);
        }
        goto out;
    }

    manifest_path = loader_instance_heap_alloc(inst, requiredSize, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (manifest_path == NULL) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
            "loaderGetDeviceRegistryEntry: Failed to allocate space for DriverName.");
        *result = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }

    ret = RegQueryValueEx(
        hkrKey,
        value_name,
        NULL,
        &data_type,
        (BYTE *)manifest_path,
        &requiredSize
    );

    if (ret != ERROR_SUCCESS) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
            "loaderGetDeviceRegistryEntry: DeviceID(%d) Failed to obtain %s", value_name);

        *result = VK_ERROR_INITIALIZATION_FAILED;
        goto out;
    }

    if (data_type != REG_SZ && data_type != REG_MULTI_SZ) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
            "loaderGetDeviceRegistryEntry: Invalid %s data type. Expected REG_SZ or REG_MULTI_SZ.", value_name);
        *result = VK_ERROR_INITIALIZATION_FAILED;
        goto out;
    }

    found = loaderAddJsonEntry(inst, reg_data, total_size, value_name, data_type, manifest_path, requiredSize, result);

out:
    if (manifest_path != NULL) {
        loader_instance_heap_free(inst, manifest_path);
    }
    RegCloseKey(hkrKey);
    return found;
}

// Find the list of registry files (names VulkanDriverName/VulkanDriverNameWow) in hkr .
//
// This function looks for display devices and childish software components
// for a list of files which are added to a returned list (function return
// value).
// Function return is a string with a ';'  separated list of filenames.
// Function return is NULL if no valid name/value pairs  are found in the key,
// or the key is not found.
//
// *reg_data contains a string list of filenames as pointer.
// When done using the returned string list, the caller should free the pointer.
VkResult loaderGetDeviceRegistryFiles(const struct loader_instance *inst, char **reg_data, PDWORD reg_data_size, LPCTSTR value_name) {
    static const wchar_t *softwareComponentGUID = L"{5c4c3332-344d-483c-8739-259e934c9cc8}";
    static const wchar_t *displayGUID = L"{4d36e968-e325-11ce-bfc1-08002be10318}";
    const ULONG flags = CM_GETIDLIST_FILTER_CLASS | CM_GETIDLIST_FILTER_PRESENT;

    wchar_t childGuid[MAX_GUID_STRING_LEN + 2];  // +2 for brackets {}
    ULONG childGuidSize = sizeof(childGuid);

    DEVINST devID = 0, childID = 0;
    wchar_t *pDeviceNames = NULL;
    ULONG deviceNamesSize = 0;
    VkResult result = VK_SUCCESS;
    bool found = false;

    if (NULL == reg_data) {
        result = VK_ERROR_INITIALIZATION_FAILED;
        return result;
    }

    // if after obtaining the DeviceNameSize, new device is added start over
    do {
        CM_Get_Device_ID_List_SizeW(&deviceNamesSize, displayGUID, flags);

        if (pDeviceNames != NULL) {
            loader_instance_heap_free(inst, pDeviceNames);
        }

        pDeviceNames = loader_instance_heap_alloc(inst, deviceNamesSize * sizeof(wchar_t), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (pDeviceNames == NULL) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                "loaderGetDeviceRegistryFiles: Failed to allocate space for display device names.");
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            return result;
        }
    } while (CM_Get_Device_ID_ListW(displayGUID, pDeviceNames, deviceNamesSize, flags) == CR_BUFFER_SMALL);

    if (pDeviceNames) {
        for (wchar_t *deviceName = pDeviceNames; *deviceName; deviceName += wcslen(deviceName) + 1) {
            CONFIGRET status = CM_Locate_DevNodeW(&devID, deviceName, CM_LOCATE_DEVNODE_NORMAL);
            if (CR_SUCCESS != status) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0, "loaderGetDeviceRegistryFiles: failed to open DevNode %s",
                           deviceName);
                continue;
            }
            ULONG ulStatus, ulProblem;
            status = CM_Get_DevNode_Status(&ulStatus, &ulProblem, devID, 0);

            if (CR_SUCCESS != status)
            {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0, "loaderGetDeviceRegistryFiles: failed to probe device status %s",
                           deviceName);
                continue;
            }
            if ((ulStatus & DN_HAS_PROBLEM) && (ulProblem == CM_PROB_NEED_RESTART || ulProblem == DN_NEED_RESTART))
            {
                loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                           "loaderGetDeviceRegistryFiles: device %s is pending reboot, skipping ...", deviceName);
                continue;
            }

            loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0, "loaderGetDeviceRegistryFiles: opening device %s", deviceName);

            if (loaderGetDeviceRegistryEntry(inst, reg_data, reg_data_size, devID, value_name, &result)) {
                found = true;
                continue;
            }
            else if (result == VK_ERROR_OUT_OF_HOST_MEMORY) {
                break;
            }

            status = CM_Get_Child(&childID, devID, 0);
            if (status != CR_SUCCESS) {
                loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                           "loaderGetDeviceRegistryFiles: unable to open child-device error:%d", status);
                continue;
            }

            do {
                wchar_t buffer[MAX_DEVICE_ID_LEN];
                CM_Get_Device_IDW(childID, buffer, MAX_DEVICE_ID_LEN, 0);

                loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                           "loaderGetDeviceRegistryFiles: Opening child device %d - %s", childID, buffer);

                status = CM_Get_DevNode_Registry_PropertyW(childID, CM_DRP_CLASSGUID, NULL, &childGuid, &childGuidSize, 0);
                if (status != CR_SUCCESS) {
                    loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                               "loaderGetDeviceRegistryFiles: unable to obtain GUID for:%d error:%d", childID, status);

                    result = VK_ERROR_INITIALIZATION_FAILED;
                    continue;
                }

                if (wcscmp(childGuid, softwareComponentGUID) != 0) {
                    loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0,
                               "loaderGetDeviceRegistryFiles: GUID for %d is not SoftwareComponent skipping", childID);
                    continue;
                }

                if (loaderGetDeviceRegistryEntry(inst, reg_data, reg_data_size, childID, value_name, &result)) {
                    found = true;
                    break; // check next-display-device
                }

            } while (CM_Get_Sibling(&childID, childID, 0) == CR_SUCCESS);
        }

        loader_instance_heap_free(inst, pDeviceNames);
    }

    if (!found && result != VK_ERROR_OUT_OF_HOST_MEMORY) {
        result = VK_ERROR_INITIALIZATION_FAILED;
    }

    return result;
}

static char *loader_get_next_path(char *path);

// Find the list of registry files (names within a key) in key "location".
//
// This function looks in the registry (hive = DEFAULT_VK_REGISTRY_HIVE) key as
// given in "location"
// for a list or name/values which are added to a returned list (function return
// value).
// The DWORD values within the key must be 0 or they are skipped.
// Function return is a string with a ';'  separated list of filenames.
// Function return is NULL if no valid name/value pairs  are found in the key,
// or the key is not found.
//
// *reg_data contains a string list of filenames as pointer.
// When done using the returned string list, the caller should free the pointer.
VkResult loaderGetRegistryFiles(const struct loader_instance *inst, char *location, bool use_secondary_hive, char **reg_data, PDWORD reg_data_size) {
    LONG rtn_value;
    HKEY hive = DEFAULT_VK_REGISTRY_HIVE, key;
    DWORD access_flags;
    char name[2048];
    char *loc = location;
    char *next;
    DWORD idx;
    DWORD name_size = sizeof(name);
    DWORD value;
    DWORD value_size = sizeof(value);
    VkResult result = VK_SUCCESS;
    bool found = false;

    if (NULL == reg_data) {
        result = VK_ERROR_INITIALIZATION_FAILED;
        goto out;
    }

    while (*loc) {
        next = loader_get_next_path(loc);
        access_flags = KEY_QUERY_VALUE;
        rtn_value = RegOpenKeyEx(hive, loc, 0, access_flags, &key);
        if (ERROR_SUCCESS == rtn_value) {
            idx = 0;
            while ((rtn_value = RegEnumValue(key, idx++, name, &name_size, NULL, NULL, (LPBYTE)&value, &value_size)) ==
                   ERROR_SUCCESS) {
                if (value_size == sizeof(value) && value == 0) {
                    if (NULL == *reg_data) {
                        *reg_data = loader_instance_heap_alloc(inst, *reg_data_size, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
                        if (NULL == *reg_data) {
                            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                                       "loaderGetRegistryFiles: Failed to allocate space for registry data for key %s", name);
                            RegCloseKey(key);
                            result = VK_ERROR_OUT_OF_HOST_MEMORY;
                            goto out;
                        }
                        *reg_data[0] = '\0';
                    } else if (strlen(*reg_data) + name_size + 1 > *reg_data_size) {
                        void *new_ptr = loader_instance_heap_realloc(inst, *reg_data, *reg_data_size, *reg_data_size * 2,
                                                                     VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
                        if (NULL == new_ptr) {
                            loader_log(
                                inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                                "loaderGetRegistryFiles: Failed to reallocate space for registry value of size %d for key %s",
                                *reg_data_size * 2, name);
                            RegCloseKey(key);
                            result = VK_ERROR_OUT_OF_HOST_MEMORY;
                            goto out;
                        }
                        *reg_data = new_ptr;
                        *reg_data_size *= 2;
                    }
                    loader_log(
                        inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0, "Located json file \"%s\" from registry \"%s\\%s\"", name,
                        hive == DEFAULT_VK_REGISTRY_HIVE ? DEFAULT_VK_REGISTRY_HIVE_STR : SECONDARY_VK_REGISTRY_HIVE_STR, location);
                    if (strlen(*reg_data) == 0) {
                        // The list is emtpy. Add the first entry.
                        (void)snprintf(*reg_data, name_size + 1, "%s", name);
                        found = true;
                    } else {
                        // At this point the reg_data variable contains other JSON paths, likely from the PNP/device section
                        // of the registry that we want to have precendence over this non-device specific section of the registry.
                        // To make sure we avoid enumerating old JSON files/drivers that might be present in the non-device specific
                        // area of the registry when a newer device specific JSON file is present, do a check before adding.
                        // Find the file name, without path, of the JSON file found in the non-device specific registry location.
                        // If the same JSON file name is already found in the list, don't add it again.
                        bool foundDuplicate = false;
                        char *pLastSlashName = strrchr(name, '\\');
                        if (pLastSlashName != NULL) {
                            char *foundMatch = strstr(*reg_data, pLastSlashName + 1);
                            if (foundMatch != NULL) {
                                foundDuplicate = true;
                            }
                        }

                        if (foundDuplicate == false) {
                            // Add the new entry to the list.
                            (void)snprintf(*reg_data + strlen(*reg_data), name_size + 2, "%c%s", PATH_SEPARATOR, name);
                            found = true;
                        } else {
                            loader_log(
                                inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                                "Skipping adding of json file \"%s\" from registry \"%s\\%s\" to the list due to duplication", name,
                                hive == DEFAULT_VK_REGISTRY_HIVE ? DEFAULT_VK_REGISTRY_HIVE_STR : SECONDARY_VK_REGISTRY_HIVE_STR,
                                location);
                        }
                    }
                }
                name_size = 2048;
            }
            RegCloseKey(key);
        }

        // Advance the location - if the next location is in the secondary hive, then reset the locations and advance the hive
        if (use_secondary_hive && (hive == DEFAULT_VK_REGISTRY_HIVE) && (*next == '\0')) {
            loc = location;
            hive = SECONDARY_VK_REGISTRY_HIVE;
        } else {
            loc = next;
        }
    }

    if (!found && result != VK_ERROR_OUT_OF_HOST_MEMORY) {
        result = VK_ERROR_INITIALIZATION_FAILED;
    }

out:

    return result;
}

#endif  // WIN32

// Combine path elements, separating each element with the platform-specific
// directory separator, and save the combined string to a destination buffer,
// not exceeding the given length. Path elements are given as variable args,
// with a NULL element terminating the list.
//
// \returns the total length of the combined string, not including an ASCII
// NUL termination character. This length may exceed the available storage:
// in this case, the written string will be truncated to avoid a buffer
// overrun, and the return value will greater than or equal to the storage
// size. A NULL argument may be provided as the destination buffer in order
// to determine the required string length without actually writing a string.
static size_t loader_platform_combine_path(char *dest, size_t len, ...) {
    size_t required_len = 0;
    va_list ap;
    const char *component;

    va_start(ap, len);

    while ((component = va_arg(ap, const char *))) {
        if (required_len > 0) {
            // This path element is not the first non-empty element; prepend
            // a directory separator if space allows
            if (dest && required_len + 1 < len) {
                (void)snprintf(dest + required_len, len - required_len, "%c", DIRECTORY_SYMBOL);
            }
            required_len++;
        }

        if (dest && required_len < len) {
            strncpy(dest + required_len, component, len - required_len);
        }
        required_len += strlen(component);
    }

    va_end(ap);

    // strncpy(3) won't add a NUL terminating byte in the event of truncation.
    if (dest && required_len >= len) {
        dest[len - 1] = '\0';
    }

    return required_len;
}

// Given string of three part form "maj.min.pat" convert to a vulkan version number.
static uint32_t loader_make_version(char *vers_str) {
    uint32_t vers = 0, major = 0, minor = 0, patch = 0;
    char *vers_tok;

    if (!vers_str) {
        return vers;
    }

    vers_tok = strtok(vers_str, ".\"\n\r");
    if (NULL != vers_tok) {
        major = (uint16_t)atoi(vers_tok);
        vers_tok = strtok(NULL, ".\"\n\r");
        if (NULL != vers_tok) {
            minor = (uint16_t)atoi(vers_tok);
            vers_tok = strtok(NULL, ".\"\n\r");
            if (NULL != vers_tok) {
                patch = (uint16_t)atoi(vers_tok);
            }
        }
    }

    return VK_MAKE_VERSION(major, minor, patch);
}

bool compare_vk_extension_properties(const VkExtensionProperties *op1, const VkExtensionProperties *op2) {
    return strcmp(op1->extensionName, op2->extensionName) == 0 ? true : false;
}

// Search the given ext_array for an extension matching the given vk_ext_prop
bool has_vk_extension_property_array(const VkExtensionProperties *vk_ext_prop, const uint32_t count,
                                     const VkExtensionProperties *ext_array) {
    for (uint32_t i = 0; i < count; i++) {
        if (compare_vk_extension_properties(vk_ext_prop, &ext_array[i])) return true;
    }
    return false;
}

// Search the given ext_list for an extension matching the given vk_ext_prop
bool has_vk_extension_property(const VkExtensionProperties *vk_ext_prop, const struct loader_extension_list *ext_list) {
    for (uint32_t i = 0; i < ext_list->count; i++) {
        if (compare_vk_extension_properties(&ext_list->list[i], vk_ext_prop)) return true;
    }
    return false;
}

// Search the given ext_list for a device extension matching the given ext_prop
bool has_vk_dev_ext_property(const VkExtensionProperties *ext_prop, const struct loader_device_extension_list *ext_list) {
    for (uint32_t i = 0; i < ext_list->count; i++) {
        if (compare_vk_extension_properties(&ext_list->list[i].props, ext_prop)) return true;
    }
    return false;
}

// Search the given layer list for a layer matching the given layer name
static struct loader_layer_properties *loader_get_layer_property(const char *name, const struct loader_layer_list *layer_list) {
    for (uint32_t i = 0; i < layer_list->count; i++) {
        const VkLayerProperties *item = &layer_list->list[i].info;
        if (strcmp(name, item->layerName) == 0) return &layer_list->list[i];
    }
    return NULL;
}

// Get the next unused layer property in the list. Init the property to zero.
static struct loader_layer_properties *loader_get_next_layer_property(const struct loader_instance *inst,
                                                                      struct loader_layer_list *layer_list) {
    if (layer_list->capacity == 0) {
        layer_list->list =
            loader_instance_heap_alloc(inst, sizeof(struct loader_layer_properties) * 64, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (layer_list->list == NULL) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_get_next_layer_property: Out of memory can "
                       "not add any layer properties to list");
            return NULL;
        }
        memset(layer_list->list, 0, sizeof(struct loader_layer_properties) * 64);
        layer_list->capacity = sizeof(struct loader_layer_properties) * 64;
    }

    // Ensure enough room to add an entry
    if ((layer_list->count + 1) * sizeof(struct loader_layer_properties) > layer_list->capacity) {
        void *new_ptr = loader_instance_heap_realloc(inst, layer_list->list, layer_list->capacity, layer_list->capacity * 2,
                                                     VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (NULL == new_ptr) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0, "loader_get_next_layer_property: realloc failed for layer list");
            return NULL;
        }
        layer_list->list = new_ptr;
        layer_list->capacity *= 2;
    }

    layer_list->count++;
    return &(layer_list->list[layer_list->count - 1]);
}

// Remove all layer properties entries from the list
void loader_delete_layer_properties(const struct loader_instance *inst, struct loader_layer_list *layer_list) {
    uint32_t i, j, k;
    struct loader_device_extension_list *dev_ext_list;
    struct loader_dev_ext_props *ext_props;
    if (!layer_list) return;

    for (i = 0; i < layer_list->count; i++) {
        if (NULL != layer_list->list[i].component_layer_names) {
            loader_instance_heap_free(inst, layer_list->list[i].component_layer_names);
            layer_list->list[i].component_layer_names = NULL;
        }
        loader_destroy_generic_list(inst, (struct loader_generic_list *)&layer_list->list[i].instance_extension_list);
        dev_ext_list = &layer_list->list[i].device_extension_list;
        if (dev_ext_list->capacity > 0 && NULL != dev_ext_list->list) {
            for (j = 0; j < dev_ext_list->count; j++) {
                ext_props = &dev_ext_list->list[j];
                if (ext_props->entrypoint_count > 0) {
                    for (k = 0; k < ext_props->entrypoint_count; k++) {
                        loader_instance_heap_free(inst, ext_props->entrypoints[k]);
                    }
                    loader_instance_heap_free(inst, ext_props->entrypoints);
                }
            }
        }
        loader_destroy_generic_list(inst, (struct loader_generic_list *)dev_ext_list);
    }
    layer_list->count = 0;

    if (layer_list->capacity > 0) {
        layer_list->capacity = 0;
        loader_instance_heap_free(inst, layer_list->list);
    }
}

static VkResult loader_add_instance_extensions(const struct loader_instance *inst,
                                               const PFN_vkEnumerateInstanceExtensionProperties fp_get_props, const char *lib_name,
                                               struct loader_extension_list *ext_list) {
    uint32_t i, count = 0;
    VkExtensionProperties *ext_props;
    VkResult res = VK_SUCCESS;

    if (!fp_get_props) {
        // No EnumerateInstanceExtensionProperties defined
        goto out;
    }

    res = fp_get_props(NULL, &count, NULL);
    if (res != VK_SUCCESS) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_add_instance_extensions: Error getting Instance "
                   "extension count from %s",
                   lib_name);
        goto out;
    }

    if (count == 0) {
        // No ExtensionProperties to report
        goto out;
    }

    ext_props = loader_stack_alloc(count * sizeof(VkExtensionProperties));
    if (NULL == ext_props) {
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }

    res = fp_get_props(NULL, &count, ext_props);
    if (res != VK_SUCCESS) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_add_instance_extensions: Error getting Instance "
                   "extensions from %s",
                   lib_name);
        goto out;
    }

    for (i = 0; i < count; i++) {
        char spec_version[64];

        bool ext_unsupported = wsi_unsupported_instance_extension(&ext_props[i]);
        if (!ext_unsupported) {
            (void)snprintf(spec_version, sizeof(spec_version), "%d.%d.%d", VK_VERSION_MAJOR(ext_props[i].specVersion),
                           VK_VERSION_MINOR(ext_props[i].specVersion), VK_VERSION_PATCH(ext_props[i].specVersion));
            loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0, "Instance Extension: %s (%s) version %s", ext_props[i].extensionName,
                       lib_name, spec_version);

            res = loader_add_to_ext_list(inst, ext_list, 1, &ext_props[i]);
            if (res != VK_SUCCESS) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "loader_add_instance_extensions: Failed to add %s "
                           "to Instance extension list",
                           lib_name);
                goto out;
            }
        }
    }

out:
    return res;
}

// Initialize ext_list with the physical device extensions.
// The extension properties are passed as inputs in count and ext_props.
static VkResult loader_init_device_extensions(const struct loader_instance *inst, struct loader_physical_device_term *phys_dev_term,
                                              uint32_t count, VkExtensionProperties *ext_props,
                                              struct loader_extension_list *ext_list) {
    VkResult res;
    uint32_t i;

    res = loader_init_generic_list(inst, (struct loader_generic_list *)ext_list, sizeof(VkExtensionProperties));
    if (VK_SUCCESS != res) {
        return res;
    }

    for (i = 0; i < count; i++) {
        char spec_version[64];
        (void)snprintf(spec_version, sizeof(spec_version), "%d.%d.%d", VK_VERSION_MAJOR(ext_props[i].specVersion),
                       VK_VERSION_MINOR(ext_props[i].specVersion), VK_VERSION_PATCH(ext_props[i].specVersion));
        loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0, "Device Extension: %s (%s) version %s", ext_props[i].extensionName,
                   phys_dev_term->this_icd_term->scanned_icd->lib_name, spec_version);
        res = loader_add_to_ext_list(inst, ext_list, 1, &ext_props[i]);
        if (res != VK_SUCCESS) return res;
    }

    return VK_SUCCESS;
}

VkResult loader_add_device_extensions(const struct loader_instance *inst,
                                      PFN_vkEnumerateDeviceExtensionProperties fpEnumerateDeviceExtensionProperties,
                                      VkPhysicalDevice physical_device, const char *lib_name,
                                      struct loader_extension_list *ext_list) {
    uint32_t i, count;
    VkResult res;
    VkExtensionProperties *ext_props;

    res = fpEnumerateDeviceExtensionProperties(physical_device, NULL, &count, NULL);
    if (res == VK_SUCCESS && count > 0) {
        ext_props = loader_stack_alloc(count * sizeof(VkExtensionProperties));
        if (!ext_props) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_add_device_extensions: Failed to allocate space"
                       " for device extension properties.");
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        res = fpEnumerateDeviceExtensionProperties(physical_device, NULL, &count, ext_props);
        if (res != VK_SUCCESS) {
            return res;
        }
        for (i = 0; i < count; i++) {
            char spec_version[64];
            (void)snprintf(spec_version, sizeof(spec_version), "%d.%d.%d", VK_VERSION_MAJOR(ext_props[i].specVersion),
                           VK_VERSION_MINOR(ext_props[i].specVersion), VK_VERSION_PATCH(ext_props[i].specVersion));
            loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0, "Device Extension: %s (%s) version %s", ext_props[i].extensionName,
                       lib_name, spec_version);
            res = loader_add_to_ext_list(inst, ext_list, 1, &ext_props[i]);
            if (res != VK_SUCCESS) {
                return res;
            }
        }
    } else {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_add_device_extensions: Error getting physical "
                   "device extension info count from library %s",
                   lib_name);
        return res;
    }

    return VK_SUCCESS;
}

VkResult loader_init_generic_list(const struct loader_instance *inst, struct loader_generic_list *list_info, size_t element_size) {
    size_t capacity = 32 * element_size;
    list_info->count = 0;
    list_info->capacity = 0;
    list_info->list = loader_instance_heap_alloc(inst, capacity, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (list_info->list == NULL) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_init_generic_list: Failed to allocate space "
                   "for generic list");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memset(list_info->list, 0, capacity);
    list_info->capacity = capacity;
    return VK_SUCCESS;
}

void loader_destroy_generic_list(const struct loader_instance *inst, struct loader_generic_list *list) {
    loader_instance_heap_free(inst, list->list);
    list->count = 0;
    list->capacity = 0;
}

// Append non-duplicate extension properties defined in props to the given ext_list.
// Return - Vk_SUCCESS on success
VkResult loader_add_to_ext_list(const struct loader_instance *inst, struct loader_extension_list *ext_list,
                                uint32_t prop_list_count, const VkExtensionProperties *props) {
    uint32_t i;
    const VkExtensionProperties *cur_ext;

    if (ext_list->list == NULL || ext_list->capacity == 0) {
        VkResult res = loader_init_generic_list(inst, (struct loader_generic_list *)ext_list, sizeof(VkExtensionProperties));
        if (VK_SUCCESS != res) {
            return res;
        }
    }

    for (i = 0; i < prop_list_count; i++) {
        cur_ext = &props[i];

        // look for duplicates
        if (has_vk_extension_property(cur_ext, ext_list)) {
            continue;
        }

        // add to list at end
        // check for enough capacity
        if (ext_list->count * sizeof(VkExtensionProperties) >= ext_list->capacity) {
            void *new_ptr = loader_instance_heap_realloc(inst, ext_list->list, ext_list->capacity, ext_list->capacity * 2,
                                                         VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
            if (new_ptr == NULL) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "loader_add_to_ext_list: Failed to reallocate "
                           "space for extension list");
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            ext_list->list = new_ptr;

            // double capacity
            ext_list->capacity *= 2;
        }

        memcpy(&ext_list->list[ext_list->count], cur_ext, sizeof(VkExtensionProperties));
        ext_list->count++;
    }
    return VK_SUCCESS;
}

// Append one extension property defined in props with entrypoints defined in entries to the given
// ext_list. Do not append if a duplicate.
// Return - Vk_SUCCESS on success
VkResult loader_add_to_dev_ext_list(const struct loader_instance *inst, struct loader_device_extension_list *ext_list,
                                    const VkExtensionProperties *props, uint32_t entry_count, char **entrys) {
    uint32_t idx;
    if (ext_list->list == NULL || ext_list->capacity == 0) {
        VkResult res = loader_init_generic_list(inst, (struct loader_generic_list *)ext_list, sizeof(struct loader_dev_ext_props));
        if (VK_SUCCESS != res) {
            return res;
        }
    }

    // look for duplicates
    if (has_vk_dev_ext_property(props, ext_list)) {
        return VK_SUCCESS;
    }

    idx = ext_list->count;
    // add to list at end
    // check for enough capacity
    if (idx * sizeof(struct loader_dev_ext_props) >= ext_list->capacity) {
        void *new_ptr = loader_instance_heap_realloc(inst, ext_list->list, ext_list->capacity, ext_list->capacity * 2,
                                                     VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

        if (NULL == new_ptr) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_add_to_dev_ext_list: Failed to reallocate space for device extension list");
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        ext_list->list = new_ptr;

        // double capacity
        ext_list->capacity *= 2;
    }

    memcpy(&ext_list->list[idx].props, props, sizeof(*props));
    ext_list->list[idx].entrypoint_count = entry_count;
    if (entry_count == 0) {
        ext_list->list[idx].entrypoints = NULL;
    } else {
        ext_list->list[idx].entrypoints =
            loader_instance_heap_alloc(inst, sizeof(char *) * entry_count, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (ext_list->list[idx].entrypoints == NULL) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_add_to_dev_ext_list: Failed to allocate space "
                       "for device extension entrypoint list in list %d",
                       idx);
            ext_list->list[idx].entrypoint_count = 0;
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        for (uint32_t i = 0; i < entry_count; i++) {
            ext_list->list[idx].entrypoints[i] =
                loader_instance_heap_alloc(inst, strlen(entrys[i]) + 1, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
            if (ext_list->list[idx].entrypoints[i] == NULL) {
                for (uint32_t j = 0; j < i; j++) {
                    loader_instance_heap_free(inst, ext_list->list[idx].entrypoints[j]);
                }
                loader_instance_heap_free(inst, ext_list->list[idx].entrypoints);
                ext_list->list[idx].entrypoint_count = 0;
                ext_list->list[idx].entrypoints = NULL;
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "loader_add_to_dev_ext_list: Failed to allocate space "
                           "for device extension entrypoint %d name",
                           i);
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            strcpy(ext_list->list[idx].entrypoints[i], entrys[i]);
        }
    }
    ext_list->count++;

    return VK_SUCCESS;
}

// Prototype of loader_add_meta_layer function since we use it in the loader_add_implicit_layer, but can also
// call loader_add_implicit_layer from loader_add_meta_layer.
bool loader_add_meta_layer(const struct loader_instance *inst, const struct loader_layer_properties *prop,
                           struct loader_layer_list *target_list, struct loader_layer_list *expanded_target_list,
                           const struct loader_layer_list *source_list);

// Search the given layer list for a list matching the given VkLayerProperties
bool has_vk_layer_property(const VkLayerProperties *vk_layer_prop, const struct loader_layer_list *list) {
    for (uint32_t i = 0; i < list->count; i++) {
        if (strcmp(vk_layer_prop->layerName, list->list[i].info.layerName) == 0) return true;
    }
    return false;
}

// Search the given layer list for a layer matching the given name
bool has_layer_name(const char *name, const struct loader_layer_list *list) {
    for (uint32_t i = 0; i < list->count; i++) {
        if (strcmp(name, list->list[i].info.layerName) == 0) return true;
    }
    return false;
}

// Search the given search_list for any layers in the props list.  Add these to the
// output layer_list.  Don't add duplicates to the output layer_list.
static VkResult loader_add_layer_names_to_list(const struct loader_instance *inst, struct loader_layer_list *output_list,
                                               struct loader_layer_list *expanded_output_list, uint32_t name_count,
                                               const char *const *names, const struct loader_layer_list *source_list) {
    struct loader_layer_properties *layer_prop;
    VkResult err = VK_SUCCESS;

    for (uint32_t i = 0; i < name_count; i++) {
        const char *source_name = names[i];
        layer_prop = loader_get_layer_property(source_name, source_list);
        if (NULL == layer_prop) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_add_layer_names_to_list: Unable to find layer"
                       " %s",
                       source_name);
            err = VK_ERROR_LAYER_NOT_PRESENT;
            continue;
        }

        // If not a meta-layer, simply add it.
        if (0 == (layer_prop->type_flags & VK_LAYER_TYPE_FLAG_META_LAYER)) {
            if (!has_vk_layer_property(&layer_prop->info, output_list)) {
                loader_add_to_layer_list(inst, output_list, 1, layer_prop);
            }
            if (!has_vk_layer_property(&layer_prop->info, expanded_output_list)) {
                loader_add_to_layer_list(inst, expanded_output_list, 1, layer_prop);
            }
        } else {
            if (!has_vk_layer_property(&layer_prop->info, output_list) ||
                !has_vk_layer_property(&layer_prop->info, expanded_output_list)) {
                loader_add_meta_layer(inst, layer_prop, output_list, expanded_output_list, source_list);
            }
        }
    }

    return err;
}

// Manage lists of VkLayerProperties
static bool loader_init_layer_list(const struct loader_instance *inst, struct loader_layer_list *list) {
    list->capacity = 32 * sizeof(struct loader_layer_properties);
    list->list = loader_instance_heap_alloc(inst, list->capacity, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (list->list == NULL) {
        return false;
    }
    memset(list->list, 0, list->capacity);
    list->count = 0;
    return true;
}

void loader_destroy_layer_list(const struct loader_instance *inst, struct loader_device *device,
                               struct loader_layer_list *layer_list) {
    if (device) {
        loader_device_heap_free(device, layer_list->list);
    } else {
        loader_instance_heap_free(inst, layer_list->list);
    }
    layer_list->count = 0;
    layer_list->capacity = 0;
}

// Append non-duplicate layer properties defined in prop_list to the given layer_info list
VkResult loader_add_to_layer_list(const struct loader_instance *inst, struct loader_layer_list *list, uint32_t prop_list_count,
                                  const struct loader_layer_properties *props) {
    uint32_t i;
    uint16_t layer_api_major_version;
    uint16_t layer_api_minor_version;
    struct loader_layer_properties *layer;

    if (list->list == NULL || list->capacity == 0) {
        loader_init_layer_list(inst, list);
    }

    if (list->list == NULL) return VK_SUCCESS;

    for (i = 0; i < prop_list_count; i++) {
        layer = (struct loader_layer_properties *)&props[i];

        // Look for duplicates, and skip
        if (has_vk_layer_property(&layer->info, list)) {
            continue;
        }

        // Check for enough capacity
        if (((list->count + 1) * sizeof(struct loader_layer_properties)) >= list->capacity) {
            size_t new_capacity = list->capacity * 2;
            void *new_ptr =
                loader_instance_heap_realloc(inst, list->list, list->capacity, new_capacity, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
            if (NULL == new_ptr) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "loader_add_to_layer_list: Realloc failed for when attempting to add new layer");
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            list->list = new_ptr;
            list->capacity = new_capacity;
        }

        // Verify that the layer api version is at least that of the application's request, if not, throw a warning since
        // undefined behavior could occur.
        layer_api_major_version = VK_VERSION_MAJOR(props[i].info.specVersion);
        layer_api_minor_version = VK_VERSION_MINOR(props[i].info.specVersion);
        if (inst->app_api_major_version > layer_api_major_version ||
            (inst->app_api_major_version == layer_api_major_version && inst->app_api_minor_version > layer_api_minor_version)) {
            loader_log(
                inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                "loader_add_to_layer_list: Explicit layer %s is using an old API version %d.%d versus application requested %d.%d",
                props[i].info.layerName, layer_api_major_version, layer_api_minor_version, inst->app_api_major_version,-                inst->app_api_minor_version);
        }

        memcpy(&list->list[list->count], layer, sizeof(struct loader_layer_properties));
        list->count++;
    }

    return VK_SUCCESS;
}

// Check the individual implicit layer for the enable/disable environment variable settings.  Only add it after
// every check has passed indicating it should be used.
static void loader_add_implicit_layer(const struct loader_instance *inst, const struct loader_layer_properties *prop,
                                      struct loader_layer_list *target_list, struct loader_layer_list *expanded_target_list,
                                      const struct loader_layer_list *source_list) {
    bool enable = loader_is_implicit_layer_enabled(inst, prop);

    // If the implicit layer is supposed to be enable, make sure the layer supports at least the same API version
    // that the application is asking (i.e. layer's API >= app's API).  If it's not, disable this layer.
    if (enable) {
        uint16_t layer_api_major_version = VK_VERSION_MAJOR(prop->info.specVersion);
        uint16_t layer_api_minor_version = VK_VERSION_MINOR(prop->info.specVersion);
        if (inst->app_api_major_version > layer_api_major_version ||
            (inst->app_api_major_version == layer_api_major_version && inst->app_api_minor_version > layer_api_minor_version)) {
            loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                       "loader_add_implicit_layer: Disabling implicit layer %s for using an old API version %d.%d versus "
                       "application requested %d.%d",
                       prop->info.layerName, layer_api_major_version, layer_api_minor_version, inst->app_api_major_version,
                       inst->app_api_minor_version);
            enable = false;
        }
    }

    if (enable) {
        if (0 == (prop->type_flags & VK_LAYER_TYPE_FLAG_META_LAYER)) {
            if (NULL != target_list && !has_vk_layer_property(&prop->info, target_list)) {
                loader_add_to_layer_list(inst, target_list, 1, prop);
            }
            if (NULL != expanded_target_list && !has_vk_layer_property(&prop->info, expanded_target_list)) {
                loader_add_to_layer_list(inst, expanded_target_list, 1, prop);
            }
        } else {
            if (!has_vk_layer_property(&prop->info, target_list) ||
                (NULL != expanded_target_list && !has_vk_layer_property(&prop->info, expanded_target_list))) {
                loader_add_meta_layer(inst, prop, target_list, expanded_target_list, source_list);
            }
        }
    }
}

// Add the component layers of a meta-layer to the active list of layers
bool loader_add_meta_layer(const struct loader_instance *inst, const struct loader_layer_properties *prop,
                           struct loader_layer_list *target_list, struct loader_layer_list *expanded_target_list,
                           const struct loader_layer_list *source_list) {
    bool found = true;

    // We need to add all the individual component layers
    for (uint32_t comp_layer = 0; comp_layer < prop->num_component_layers; comp_layer++) {
        bool found_comp = false;
        const struct loader_layer_properties *search_prop =
            loader_get_layer_property(prop->component_layer_names[comp_layer], source_list);
        if (search_prop != NULL) {
            found_comp = true;

            // If the component layer is itself an implicit layer, we need to do the implicit layer enable
            // checks
            if (0 == (search_prop->type_flags & VK_LAYER_TYPE_FLAG_EXPLICIT_LAYER)) {
                loader_add_implicit_layer(inst, search_prop, target_list, expanded_target_list, source_list);
            } else {
                if (NULL != expanded_target_list && !has_vk_layer_property(&search_prop->info, expanded_target_list)) {
                    loader_add_to_layer_list(inst, expanded_target_list, 1, search_prop);
                }
            }
        }
        if (!found_comp) {
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "loader_add_meta_layer: Failed to find layer name %s component layer "
                       "%s to activate",
                       search_prop->info.layerName, prop->component_layer_names[comp_layer]);
            found = false;
        }
    }

    // Add this layer to the overall target list (not the expanded one)
    if (found && !has_vk_layer_property(&prop->info, target_list)) {
        loader_add_to_layer_list(inst, target_list, 1, prop);
    }

    return found;
}

// Search the source_list for any layer with a name that matches the given name and a type
// that matches the given type.  Add all matching layers to the target_list.
// Do not add if found loader_layer_properties is already on the target_list.
void loader_find_layer_name_add_list(const struct loader_instance *inst, const char *name, const enum layer_type_flags type_flags,
                                     const struct loader_layer_list *source_list, struct loader_layer_list *target_list,
                                     struct loader_layer_list *expanded_target_list) {
    bool found = false;
    for (uint32_t i = 0; i < source_list->count; i++) {
        struct loader_layer_properties *source_prop = &source_list->list[i];
        if (0 == strcmp(source_prop->info.layerName, name) && (source_prop->type_flags & type_flags) == type_flags) {
            // If not a meta-layer, simply add it.
            if (0 == (source_prop->type_flags & VK_LAYER_TYPE_FLAG_META_LAYER)) {
                if (NULL != target_list && !has_vk_layer_property(&source_prop->info, target_list) &&
                    VK_SUCCESS == loader_add_to_layer_list(inst, target_list, 1, source_prop)) {
                    found = true;
                }
                if (NULL != expanded_target_list && !has_vk_layer_property(&source_prop->info, expanded_target_list) &&
                    VK_SUCCESS == loader_add_to_layer_list(inst, expanded_target_list, 1, source_prop)) {
                    found = true;
                }
            } else {
                found = loader_add_meta_layer(inst, source_prop, target_list, expanded_target_list, source_list);
            }
        }
    }
    if (!found) {
        loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                   "loader_find_layer_name_add_list: Failed to find layer name %s to activate", name);
    }
}

static VkExtensionProperties *get_extension_property(const char *name, const struct loader_extension_list *list) {
    for (uint32_t i = 0; i < list->count; i++) {
        if (strcmp(name, list->list[i].extensionName) == 0) return &list->list[i];
    }
    return NULL;
}

static VkExtensionProperties *get_dev_extension_property(const char *name, const struct loader_device_extension_list *list) {
    for (uint32_t i = 0; i < list->count; i++) {
        if (strcmp(name, list->list[i].props.extensionName) == 0) return &list->list[i].props;
    }
    return NULL;
}

// For Instance extensions implemented within the loader (i.e. DEBUG_REPORT
// the extension must provide two entry points for the loader to use:
// - "trampoline" entry point - this is the address returned by GetProcAddr
//                              and will always do what's necessary to support a
//                              global call.
// - "terminator" function    - this function will be put at the end of the
//                              instance chain and will contain the necessary logic
//                              to call / process the extension for the appropriate
//                              ICDs that are available.
// There is no generic mechanism for including these functions, the references
// must be placed into the appropriate loader entry points.
// GetInstanceProcAddr: call extension GetInstanceProcAddr to check for GetProcAddr
// requests
// loader_coalesce_extensions(void) - add extension records to the list of global
//                                    extension available to the app.
// instance_disp                    - add function pointer for terminator function
//                                    to this array.
// The extension itself should be in a separate file that will be linked directly
// with the loader.
VkResult loader_get_icd_loader_instance_extensions(const struct loader_instance *inst, struct loader_icd_tramp_list *icd_tramp_list,
                                                   struct loader_extension_list *inst_exts) {
    struct loader_extension_list icd_exts;
    VkResult res = VK_SUCCESS;
    char *env_value;
    bool filter_extensions = true;

    loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0, "Build ICD instance extension list");

    // Check if a user wants to disable the instance extension filtering behavior
    env_value = loader_getenv("VK_LOADER_DISABLE_INST_EXT_FILTER", inst);
    if (NULL != env_value && atoi(env_value) != 0) {
        filter_extensions = false;
    }
    loader_free_getenv(env_value, inst);

    // traverse scanned icd list adding non-duplicate extensions to the list
    for (uint32_t i = 0; i < icd_tramp_list->count; i++) {
        res = loader_init_generic_list(inst, (struct loader_generic_list *)&icd_exts, sizeof(VkExtensionProperties));
        if (VK_SUCCESS != res) {
            goto out;
        }
        res = loader_add_instance_extensions(inst, icd_tramp_list->scanned_list[i].EnumerateInstanceExtensionProperties,
                                             icd_tramp_list->scanned_list[i].lib_name, &icd_exts);
        if (VK_SUCCESS == res) {
            if (filter_extensions) {
                // Remove any extensions not recognized by the loader
                for (int32_t j = 0; j < (int32_t)icd_exts.count; j++) {
                    // See if the extension is in the list of supported extensions
                    bool found = false;
                    for (uint32_t k = 0; LOADER_INSTANCE_EXTENSIONS[k] != NULL; k++) {
                        if (strcmp(icd_exts.list[j].extensionName, LOADER_INSTANCE_EXTENSIONS[k]) == 0) {
                            found = true;
                            break;
                        }
                    }

                    // If it isn't in the list, remove it
                    if (!found) {
                        for (uint32_t k = j + 1; k < icd_exts.count; k++) {
                            icd_exts.list[k - 1] = icd_exts.list[k];
                        }
                        --icd_exts.count;
                        --j;
                    }
                }
            }

            res = loader_add_to_ext_list(inst, inst_exts, icd_exts.count, icd_exts.list);
        }
        loader_destroy_generic_list(inst, (struct loader_generic_list *)&icd_exts);
        if (VK_SUCCESS != res) {
            goto out;
        }
    };

    // Traverse loader's extensions, adding non-duplicate extensions to the list
    debug_utils_AddInstanceExtensions(inst, inst_exts);

out:
    return res;
}

struct loader_icd_term *loader_get_icd_and_device(const VkDevice device, struct loader_device **found_dev, uint32_t *icd_index) {
    *found_dev = NULL;
    for (struct loader_instance *inst = loader.instances; inst; inst = inst->next) {
        uint32_t index = 0;
        for (struct loader_icd_term *icd_term = inst->icd_terms; icd_term; icd_term = icd_term->next) {
            for (struct loader_device *dev = icd_term->logical_device_list; dev; dev = dev->next)
                // Value comparison of device prevents object wrapping by layers
                if (loader_get_dispatch(dev->icd_device) == loader_get_dispatch(device) ||
                    loader_get_dispatch(dev->chain_device) == loader_get_dispatch(device)) {
                    *found_dev = dev;
                    if (NULL != icd_index) {
                        *icd_index = index;
                    }
                    return icd_term;
                }
            index++;
        }
    }
    return NULL;
}

void loader_destroy_logical_device(const struct loader_instance *inst, struct loader_device *dev,
                                   const VkAllocationCallbacks *pAllocator) {
    if (pAllocator) {
        dev->alloc_callbacks = *pAllocator;
    }
    if (NULL != dev->expanded_activated_layer_list.list) {
        loader_deactivate_layers(inst, dev, &dev->expanded_activated_layer_list);
    }
    if (NULL != dev->app_activated_layer_list.list) {
        loader_destroy_layer_list(inst, dev, &dev->app_activated_layer_list);
    }
    loader_device_heap_free(dev, dev);
}

struct loader_device *loader_create_logical_device(const struct loader_instance *inst, const VkAllocationCallbacks *pAllocator) {
    struct loader_device *new_dev;
#if (DEBUG_DISABLE_APP_ALLOCATORS == 1)
    {
#else
    if (pAllocator) {
        new_dev = (struct loader_device *)pAllocator->pfnAllocation(pAllocator->pUserData, sizeof(struct loader_device),
                                                                    sizeof(int *), VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
    } else {
#endif
        new_dev = (struct loader_device *)malloc(sizeof(struct loader_device));
    }

    if (!new_dev) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_create_logical_device: Failed to alloc struct "
                   "loader_device");
        return NULL;
    }

    memset(new_dev, 0, sizeof(struct loader_device));
    if (pAllocator) {
        new_dev->alloc_callbacks = *pAllocator;
    }

    return new_dev;
}

void loader_add_logical_device(const struct loader_instance *inst, struct loader_icd_term *icd_term, struct loader_device *dev) {
    dev->next = icd_term->logical_device_list;
    icd_term->logical_device_list = dev;
}

void loader_remove_logical_device(const struct loader_instance *inst, struct loader_icd_term *icd_term,
                                  struct loader_device *found_dev, const VkAllocationCallbacks *pAllocator) {
    struct loader_device *dev, *prev_dev;

    if (!icd_term || !found_dev) return;

    prev_dev = NULL;
    dev = icd_term->logical_device_list;
    while (dev && dev != found_dev) {
        prev_dev = dev;
        dev = dev->next;
    }

    if (prev_dev)
        prev_dev->next = found_dev->next;
    else
        icd_term->logical_device_list = found_dev->next;
    loader_destroy_logical_device(inst, found_dev, pAllocator);
}

static void loader_icd_destroy(struct loader_instance *ptr_inst, struct loader_icd_term *icd_term,
                               const VkAllocationCallbacks *pAllocator) {
    ptr_inst->total_icd_count--;
    for (struct loader_device *dev = icd_term->logical_device_list; dev;) {
        struct loader_device *next_dev = dev->next;
        loader_destroy_logical_device(ptr_inst, dev, pAllocator);
        dev = next_dev;
    }

    loader_instance_heap_free(ptr_inst, icd_term);
}

static struct loader_icd_term *loader_icd_create(const struct loader_instance *inst) {
    struct loader_icd_term *icd_term;

    icd_term = loader_instance_heap_alloc(inst, sizeof(struct loader_icd_term), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (!icd_term) {
        return NULL;
    }

    memset(icd_term, 0, sizeof(struct loader_icd_term));

    return icd_term;
}

static struct loader_icd_term *loader_icd_add(struct loader_instance *ptr_inst, const struct loader_scanned_icd *scanned_icd) {
    struct loader_icd_term *icd_term;

    icd_term = loader_icd_create(ptr_inst);
    if (!icd_term) {
        return NULL;
    }

    icd_term->scanned_icd = scanned_icd;
    icd_term->this_instance = ptr_inst;

    // Prepend to the list
    icd_term->next = ptr_inst->icd_terms;
    ptr_inst->icd_terms = icd_term;
    ptr_inst->total_icd_count++;

    return icd_term;
}

// Determine the ICD interface version to use.
//     @param icd
//     @param pVersion Output parameter indicating which version to use or 0 if
//            the negotiation API is not supported by the ICD
//     @return  bool indicating true if the selected interface version is supported
//            by the loader, false indicates the version is not supported
bool loader_get_icd_interface_version(PFN_vkNegotiateLoaderICDInterfaceVersion fp_negotiate_icd_version, uint32_t *pVersion) {
    if (fp_negotiate_icd_version == NULL) {
        // ICD does not support the negotiation API, it supports version 0 or 1
        // calling code must determine if it is version 0 or 1
        *pVersion = 0;
    } else {
        // ICD supports the negotiation API, so call it with the loader's
        // latest version supported
        *pVersion = CURRENT_LOADER_ICD_INTERFACE_VERSION;
        VkResult result = fp_negotiate_icd_version(pVersion);

        if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
            // ICD no longer supports the loader's latest interface version so
            // fail loading the ICD
            return false;
        }
    }

#if MIN_SUPPORTED_LOADER_ICD_INTERFACE_VERSION > 0
    if (*pVersion < MIN_SUPPORTED_LOADER_ICD_INTERFACE_VERSION) {
        // Loader no longer supports the ICD's latest interface version so fail
        // loading the ICD
        return false;
    }
#endif
    return true;
}

void loader_scanned_icd_clear(const struct loader_instance *inst, struct loader_icd_tramp_list *icd_tramp_list) {
    if (0 != icd_tramp_list->capacity) {
        for (uint32_t i = 0; i < icd_tramp_list->count; i++) {
            loader_platform_close_library(icd_tramp_list->scanned_list[i].handle);
            loader_instance_heap_free(inst, icd_tramp_list->scanned_list[i].lib_name);
        }
        loader_instance_heap_free(inst, icd_tramp_list->scanned_list);
        icd_tramp_list->capacity = 0;
        icd_tramp_list->count = 0;
        icd_tramp_list->scanned_list = NULL;
    }
}

static VkResult loader_scanned_icd_init(const struct loader_instance *inst, struct loader_icd_tramp_list *icd_tramp_list) {
    VkResult err = VK_SUCCESS;
    loader_scanned_icd_clear(inst, icd_tramp_list);
    icd_tramp_list->capacity = 8 * sizeof(struct loader_scanned_icd);
    icd_tramp_list->scanned_list = loader_instance_heap_alloc(inst, icd_tramp_list->capacity, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (NULL == icd_tramp_list->scanned_list) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_scanned_icd_init: Realloc failed for layer list when "
                   "attempting to add new layer");
        err = VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    return err;
}

static VkResult loader_scanned_icd_add(const struct loader_instance *inst, struct loader_icd_tramp_list *icd_tramp_list,
                                       const char *filename, uint32_t api_version) {
    loader_platform_dl_handle handle;
    PFN_vkCreateInstance fp_create_inst;
    PFN_vkEnumerateInstanceExtensionProperties fp_get_inst_ext_props;
    PFN_vkGetInstanceProcAddr fp_get_proc_addr;
    PFN_GetPhysicalDeviceProcAddr fp_get_phys_dev_proc_addr = NULL;
    PFN_vkNegotiateLoaderICDInterfaceVersion fp_negotiate_icd_version;
    struct loader_scanned_icd *new_scanned_icd;
    uint32_t interface_vers;
    VkResult res = VK_SUCCESS;

    // TODO implement smarter opening/closing of libraries. For now this
    // function leaves libraries open and the scanned_icd_clear closes them
    handle = loader_platform_open_library(filename);
    if (NULL == handle) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0, loader_platform_open_library_error(filename));
        goto out;
    }

    // Get and settle on an ICD interface version
    fp_negotiate_icd_version = loader_platform_get_proc_address(handle, "vk_icdNegotiateLoaderICDInterfaceVersion");

    if (!loader_get_icd_interface_version(fp_negotiate_icd_version, &interface_vers)) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_scanned_icd_add: ICD %s doesn't support interface"
                   " version compatible with loader, skip this ICD.",
                   filename);
        goto out;
    }

    fp_get_proc_addr = loader_platform_get_proc_address(handle, "vk_icdGetInstanceProcAddr");
    if (NULL == fp_get_proc_addr) {
        assert(interface_vers == 0);
        // Use deprecated interface from version 0
        fp_get_proc_addr = loader_platform_get_proc_address(handle, "vkGetInstanceProcAddr");
        if (NULL == fp_get_proc_addr) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_scanned_icd_add: Attempt to retrieve either "
                       "\'vkGetInstanceProcAddr\' or "
                       "\'vk_icdGetInstanceProcAddr\' from ICD %s failed.",
                       filename);
            goto out;
        } else {
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "loader_scanned_icd_add: Using deprecated ICD "
                       "interface of \'vkGetInstanceProcAddr\' instead of "
                       "\'vk_icdGetInstanceProcAddr\' for ICD %s",
                       filename);
        }
        fp_create_inst = loader_platform_get_proc_address(handle, "vkCreateInstance");
        if (NULL == fp_create_inst) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_scanned_icd_add:  Failed querying "
                       "\'vkCreateInstance\' via dlsym/loadlibrary for "
                       "ICD %s",
                       filename);
            goto out;
        }
        fp_get_inst_ext_props = loader_platform_get_proc_address(handle, "vkEnumerateInstanceExtensionProperties");
        if (NULL == fp_get_inst_ext_props) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_scanned_icd_add: Could not get \'vkEnumerate"
                       "InstanceExtensionProperties\' via dlsym/loadlibrary "
                       "for ICD %s",
                       filename);
            goto out;
        }
    } else {
        // Use newer interface version 1 or later
        if (interface_vers == 0) {
            interface_vers = 1;
        }

        fp_create_inst = (PFN_vkCreateInstance)fp_get_proc_addr(NULL, "vkCreateInstance");
        if (NULL == fp_create_inst) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_scanned_icd_add: Could not get "
                       "\'vkCreateInstance\' via \'vk_icdGetInstanceProcAddr\'"
                       " for ICD %s",
                       filename);
            goto out;
        }
        fp_get_inst_ext_props =
            (PFN_vkEnumerateInstanceExtensionProperties)fp_get_proc_addr(NULL, "vkEnumerateInstanceExtensionProperties");
        if (NULL == fp_get_inst_ext_props) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_scanned_icd_add: Could not get \'vkEnumerate"
                       "InstanceExtensionProperties\' via "
                       "\'vk_icdGetInstanceProcAddr\' for ICD %s",
                       filename);
            goto out;
        }
        fp_get_phys_dev_proc_addr = loader_platform_get_proc_address(handle, "vk_icdGetPhysicalDeviceProcAddr");
    }

    // check for enough capacity
    if ((icd_tramp_list->count * sizeof(struct loader_scanned_icd)) >= icd_tramp_list->capacity) {
        void *new_ptr = loader_instance_heap_realloc(inst, icd_tramp_list->scanned_list, icd_tramp_list->capacity,
                                                     icd_tramp_list->capacity * 2, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (NULL == new_ptr) {
            res = VK_ERROR_OUT_OF_HOST_MEMORY;
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_scanned_icd_add: Realloc failed on icd library list for ICD %s", filename);
            goto out;
        }
        icd_tramp_list->scanned_list = new_ptr;

        // double capacity
        icd_tramp_list->capacity *= 2;
    }

    new_scanned_icd = &(icd_tramp_list->scanned_list[icd_tramp_list->count]);
    new_scanned_icd->handle = handle;
    new_scanned_icd->api_version = api_version;
    new_scanned_icd->GetInstanceProcAddr = fp_get_proc_addr;
    new_scanned_icd->GetPhysicalDeviceProcAddr = fp_get_phys_dev_proc_addr;
    new_scanned_icd->EnumerateInstanceExtensionProperties = fp_get_inst_ext_props;
    new_scanned_icd->CreateInstance = fp_create_inst;
    new_scanned_icd->interface_version = interface_vers;

    new_scanned_icd->lib_name = (char *)loader_instance_heap_alloc(inst, strlen(filename) + 1, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (NULL == new_scanned_icd->lib_name) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0, "loader_scanned_icd_add: Out of memory can't add ICD %s", filename);
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }
    strcpy(new_scanned_icd->lib_name, filename);
    icd_tramp_list->count++;

out:

    return res;
}

static void loader_debug_init(void) {
    char *env, *orig;

    if (g_loader_debug > 0) return;

    g_loader_debug = 0;

    // Parse comma-separated debug options
    orig = env = loader_getenv("VK_LOADER_DEBUG", NULL);
    while (env) {
        char *p = strchr(env, ',');
        size_t len;

        if (p)
            len = p - env;
        else
            len = strlen(env);

        if (len > 0) {
            if (strncmp(env, "all", len) == 0) {
                g_loader_debug = ~0u;
                g_loader_log_msgs = ~0u;
            } else if (strncmp(env, "warn", len) == 0) {
                g_loader_debug |= LOADER_WARN_BIT;
                g_loader_log_msgs |= VK_DEBUG_REPORT_WARNING_BIT_EXT;
            } else if (strncmp(env, "info", len) == 0) {
                g_loader_debug |= LOADER_INFO_BIT;
                g_loader_log_msgs |= VK_DEBUG_REPORT_INFORMATION_BIT_EXT;
            } else if (strncmp(env, "perf", len) == 0) {
                g_loader_debug |= LOADER_PERF_BIT;
                g_loader_log_msgs |= VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
            } else if (strncmp(env, "error", len) == 0) {
                g_loader_debug |= LOADER_ERROR_BIT;
                g_loader_log_msgs |= VK_DEBUG_REPORT_ERROR_BIT_EXT;
            } else if (strncmp(env, "debug", len) == 0) {
                g_loader_debug |= LOADER_DEBUG_BIT;
                g_loader_log_msgs |= VK_DEBUG_REPORT_DEBUG_BIT_EXT;
            }
        }

        if (!p) break;

        env = p + 1;
    }

    loader_free_getenv(orig, NULL);
}

void loader_initialize(void) {
    // initialize mutexs
    loader_platform_thread_create_mutex(&loader_lock);
    loader_platform_thread_create_mutex(&loader_json_lock);

    // initialize logging
    loader_debug_init();

    // initial cJSON to use alloc callbacks
    cJSON_Hooks alloc_fns = {
        .malloc_fn = loader_instance_tls_heap_alloc, .free_fn = loader_instance_tls_heap_free,
    };
    cJSON_InitHooks(&alloc_fns);
}

struct loader_manifest_files {
    uint32_t count;
    char **filename_list;
};

void loader_release() {
    // release mutexs
    loader_platform_thread_delete_mutex(&loader_lock);
    loader_platform_thread_delete_mutex(&loader_json_lock);
}

// Get next file or dirname given a string list or registry key path
//
// \returns
// A pointer to first char in the next path.
// The next path (or NULL) in the list is returned in next_path.
// Note: input string is modified in some cases. PASS IN A COPY!
static char *loader_get_next_path(char *path) {
    uint32_t len;
    char *next;

    if (path == NULL) return NULL;
    next = strchr(path, PATH_SEPARATOR);
    if (next == NULL) {
        len = (uint32_t)strlen(path);
        next = path + len;
    } else {
        *next = '\0';
        next++;
    }

    return next;
}

// Given a path which is absolute or relative, expand the path if relative or
// leave the path unmodified if absolute. The base path to prepend to relative
// paths is given in rel_base.
//
// @return - A string in out_fullpath of the full absolute path
static void loader_expand_path(const char *path, const char *rel_base, size_t out_size, char *out_fullpath) {
    if (loader_platform_is_path_absolute(path)) {
        // do not prepend a base to an absolute path
        rel_base = "";
    }

    loader_platform_combine_path(out_fullpath, out_size, rel_base, path, NULL);
}

// Given a filename (file)  and a list of paths (dir), try to find an existing
// file in the paths.  If filename already is a path then no searching in the given paths.
//
// @return - A string in out_fullpath of either the full path or file.
static void loader_get_fullpath(const char *file, const char *dirs, size_t out_size, char *out_fullpath) {
    if (!loader_platform_is_path(file) && *dirs) {
        char *dirs_copy, *dir, *next_dir;

        dirs_copy = loader_stack_alloc(strlen(dirs) + 1);
        strcpy(dirs_copy, dirs);

        // find if file exists after prepending paths in given list
        for (dir = dirs_copy; *dir && (next_dir = loader_get_next_path(dir)); dir = next_dir) {
            loader_platform_combine_path(out_fullpath, out_size, dir, file, NULL);
            if (loader_platform_file_exists(out_fullpath)) {
                return;
            }
        }
    }

    (void)snprintf(out_fullpath, out_size, "%s", file);
}

// Read a JSON file into a buffer.
//
// @return -  A pointer to a cJSON object representing the JSON parse tree.
//            This returned buffer should be freed by caller.
static VkResult loader_get_json(const struct loader_instance *inst, const char *filename, cJSON **json) {
    FILE *file = NULL;
    char *json_buf;
    size_t len;
    VkResult res = VK_SUCCESS;

    if (NULL == json) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0, "loader_get_json: Received invalid JSON file");
        res = VK_ERROR_INITIALIZATION_FAILED;
        goto out;
    }

    *json = NULL;

    file = fopen(filename, "rb");
    if (!file) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0, "loader_get_json: Failed to open JSON file %s", filename);
        res = VK_ERROR_INITIALIZATION_FAILED;
        goto out;
    }
    fseek(file, 0, SEEK_END);
    len = ftell(file);
    fseek(file, 0, SEEK_SET);
    json_buf = (char *)loader_stack_alloc(len + 1);
    if (json_buf == NULL) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_get_json: Failed to allocate space for "
                   "JSON file %s buffer of length %d",
                   filename, len);
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }
    if (fread(json_buf, sizeof(char), len, file) != len) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0, "loader_get_json: Failed to read JSON file %s.", filename);
        res = VK_ERROR_INITIALIZATION_FAILED;
        goto out;
    }
    json_buf[len] = '\0';

    // Parse text from file
    *json = cJSON_Parse(json_buf);
    if (*json == NULL) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_get_json: Failed to parse JSON file %s, "
                   "this is usually because something ran out of "
                   "memory.",
                   filename);
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }

out:
    if (NULL != file) {
        fclose(file);
    }

    return res;
}

// Do a deep copy of the loader_layer_properties structure.
VkResult loader_copy_layer_properties(const struct loader_instance *inst, struct loader_layer_properties *dst,
                                      struct loader_layer_properties *src) {
    uint32_t cnt, i;
    memcpy(dst, src, sizeof(*src));
    dst->instance_extension_list.list = loader_instance_heap_alloc(
        inst, sizeof(VkExtensionProperties) * src->instance_extension_list.count, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (NULL == dst->instance_extension_list.list) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_copy_layer_properties: Failed to allocate space "
                   "for instance extension list of size %d.",
                   src->instance_extension_list.count);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    dst->instance_extension_list.capacity = sizeof(VkExtensionProperties) * src->instance_extension_list.count;
    memcpy(dst->instance_extension_list.list, src->instance_extension_list.list, dst->instance_extension_list.capacity);
    dst->device_extension_list.list = loader_instance_heap_alloc(
        inst, sizeof(struct loader_dev_ext_props) * src->device_extension_list.count, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (NULL == dst->device_extension_list.list) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_copy_layer_properties: Failed to allocate space "
                   "for device extension list of size %d.",
                   src->device_extension_list.count);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memset(dst->device_extension_list.list, 0, sizeof(struct loader_dev_ext_props) * src->device_extension_list.count);

    dst->device_extension_list.capacity = sizeof(struct loader_dev_ext_props) * src->device_extension_list.count;
    memcpy(dst->device_extension_list.list, src->device_extension_list.list, dst->device_extension_list.capacity);
    if (src->device_extension_list.count > 0 && src->device_extension_list.list->entrypoint_count > 0) {
        cnt = src->device_extension_list.list->entrypoint_count;
        dst->device_extension_list.list->entrypoints =
            loader_instance_heap_alloc(inst, sizeof(char *) * cnt, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (NULL == dst->device_extension_list.list->entrypoints) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_copy_layer_properties: Failed to allocate space "
                       "for device extension entrypoint list of size %d.",
                       cnt);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        memset(dst->device_extension_list.list->entrypoints, 0, sizeof(char *) * cnt);

        for (i = 0; i < cnt; i++) {
            dst->device_extension_list.list->entrypoints[i] = loader_instance_heap_alloc(
                inst, strlen(src->device_extension_list.list->entrypoints[i]) + 1, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
            if (NULL == dst->device_extension_list.list->entrypoints[i]) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "loader_copy_layer_properties: Failed to "
                           "allocate space for device extension entrypoint "
                           "%d name of length",
                           i);
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            strcpy(dst->device_extension_list.list->entrypoints[i], src->device_extension_list.list->entrypoints[i]);
        }
    }

    return VK_SUCCESS;
}

static bool loader_find_layer_name_list(const char *name, const struct loader_layer_list *layer_list) {
    if (NULL == layer_list) {
        return false;
    }
    for (uint32_t j = 0; j < layer_list->count; j++) {
        if (!strcmp(name, layer_list->list[j].info.layerName)) {
            return true;
        }
    }
    return false;
}

bool loader_find_layer_name_array(const char *name, uint32_t layer_count, const char layer_list[][VK_MAX_EXTENSION_NAME_SIZE]) {
    if (!layer_list) return false;
    for (uint32_t j = 0; j < layer_count; j++)
        if (!strcmp(name, layer_list[j])) return true;
    return false;
}

const char *std_validation_str = "VK_LAYER_LUNARG_standard_validation";

// Adds the legacy VK_LAYER_LUNARG_standard_validation as a meta-layer if it
// fails to find it in the list already.  This is usually an indication that a
// newer loader is being used with an older layer set.
static bool loader_add_legacy_std_val_layer(const struct loader_instance *inst, struct loader_layer_list *layer_instance_list) {
    uint32_t i;
    bool success = true;
    struct loader_layer_properties *props = loader_get_next_layer_property(inst, layer_instance_list);
    const char std_validation_names[6][VK_MAX_EXTENSION_NAME_SIZE] = {
        "VK_LAYER_GOOGLE_threading", "VK_LAYER_LUNARG_parameter_validation", "VK_LAYER_LUNARG_object_tracker",
        "VK_LAYER_LUNARG_core_validation", "VK_LAYER_GOOGLE_unique_objects"};
    uint32_t layer_count = sizeof(std_validation_names) / sizeof(std_validation_names[0]);

    loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0,
               "Adding VK_LAYER_LUNARG_standard_validation using the loader legacy path.  This is"
               " not an error.");

    if (NULL == props) {
        goto out;
    }

    memset(props, 0, sizeof(struct loader_layer_properties));
    props->type_flags = VK_LAYER_TYPE_FLAG_INSTANCE_LAYER | VK_LAYER_TYPE_FLAG_EXPLICIT_LAYER | VK_LAYER_TYPE_FLAG_META_LAYER;
    strncpy(props->info.description, "LunarG Standard Validation Layer", sizeof(props->info.description));
    props->info.implementationVersion = 1;
    strncpy(props->info.layerName, std_validation_str, sizeof(props->info.layerName));
    props->info.specVersion = VK_MAKE_VERSION(1, 0, VK_HEADER_VERSION);

    props->component_layer_names =
        loader_instance_heap_alloc(inst, sizeof(char[MAX_STRING_SIZE]) * layer_count, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (NULL == props->component_layer_names) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "Failed to allocate space for legacy VK_LAYER_LUNARG_standard_validation"
                   " meta-layer component_layers information.");
        success = false;
        goto out;
    }
    for (i = 0; i < layer_count; i++) {
        strncpy(props->component_layer_names[i], std_validation_names[i], MAX_STRING_SIZE - 1);
        props->component_layer_names[i][MAX_STRING_SIZE - 1] = '\0';
    }

out:

    if (!success && NULL != props && NULL != props->component_layer_names) {
        loader_instance_heap_free(inst, props->component_layer_names);
        props->component_layer_names = NULL;
    }

    return success;
}

// Verify that all component layers in a meta-layer are valid.
static bool verify_meta_layer_comp_layers(const struct loader_instance *inst, struct loader_layer_properties *prop,
                                          struct loader_layer_list *instance_layers) {
    bool success = true;
    const uint32_t expected_major = VK_VERSION_MAJOR(prop->info.specVersion);
    const uint32_t expected_minor = VK_VERSION_MINOR(prop->info.specVersion);

    for (uint32_t comp_layer = 0; comp_layer < prop->num_component_layers; comp_layer++) {
        if (!loader_find_layer_name_list(prop->component_layer_names[comp_layer], instance_layers)) {
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "Meta-layer %s can't find component layer %s at index %d."
                       "  Skipping this layer.",
                       prop->info.layerName, prop->component_layer_names[comp_layer], comp_layer);
            success = false;
            break;
        } else {
            struct loader_layer_properties *comp_prop =
                loader_get_layer_property(prop->component_layer_names[comp_layer], instance_layers);
            if (comp_prop == NULL) {
                loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                           "Meta-layer %s can't find property for component layer %s at index %d."
                           "  Skipping this layer.",
                           prop->info.layerName, prop->component_layer_names[comp_layer], comp_layer);
                success = false;
                break;
            }

            // Check the version of each layer, they need to at least match MAJOR and MINOR
            uint32_t cur_major = VK_VERSION_MAJOR(comp_prop->info.specVersion);
            uint32_t cur_minor = VK_VERSION_MINOR(comp_prop->info.specVersion);
            if (cur_major != expected_major || cur_minor != expected_minor) {
                loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                           "Meta-layer uses API version %d.%d, but component layer %d uses API "
                           "version %d.%d.  Skipping this layer.",
                           expected_major, expected_minor, comp_layer, cur_major, cur_minor);
                success = false;
                break;
            }

            // Make sure the layer isn't using it's own name
            if (!strcmp(prop->info.layerName, prop->component_layer_names[comp_layer])) {
                loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                           "Meta-layer %s lists itself in its component layer list at index %d."
                           "  Skipping this layer.",
                           prop->info.layerName, comp_layer);
                success = false;
                break;
            }
            if (comp_prop->type_flags & VK_LAYER_TYPE_FLAG_META_LAYER) {
                loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                           "verify_meta_layer_comp_layers: Adding meta-layer %s which also contains meta-layer %s",
                           prop->info.layerName, comp_prop->info.layerName);

                // Make sure if the layer is using a meta-layer in its component list that we also verify that.
                if (!verify_meta_layer_comp_layers(inst, comp_prop, instance_layers)) {
                    loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                               "Meta-layer %s component layer %s can not find all component layers."
                               "  Skipping this layer.",
                               prop->info.layerName, prop->component_layer_names[comp_layer]);
                    success = false;
                    break;
                }
            }

            // Add any instance and device extensions from component layers to this layer
            // list, so that anyone querying extensions will only need to look at the meta-layer
            for (uint32_t ext = 0; ext < comp_prop->instance_extension_list.count; ext++) {
                loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0, "Meta-layer %s component layer %s adding instance extension %s",
                           prop->info.layerName, prop->component_layer_names[comp_layer],
                           comp_prop->instance_extension_list.list[ext].extensionName);
                if (!has_vk_extension_property(&comp_prop->instance_extension_list.list[ext], &prop->instance_extension_list)) {
                    loader_add_to_ext_list(inst, &prop->instance_extension_list, 1, &comp_prop->instance_extension_list.list[ext]);
                }
            }

            for (uint32_t ext = 0; ext < comp_prop->device_extension_list.count; ext++) {
                loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0, "Meta-layer %s component layer %s adding device extension %s",
                           prop->info.layerName, prop->component_layer_names[comp_layer],
                           comp_prop->device_extension_list.list[ext].props.extensionName);
                if (!has_vk_dev_ext_property(&comp_prop->device_extension_list.list[ext].props, &prop->device_extension_list)) {
                    loader_add_to_dev_ext_list(inst, &prop->device_extension_list,
                                               &comp_prop->device_extension_list.list[ext].props, 0, NULL);
                }
            }
        }
    }
    if (success) {
        loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0, "Meta-layer %s all %d component layers appear to be valid.",
                   prop->info.layerName, prop->num_component_layers);
    }
    return success;
}

// Verify that all meta-layers in a layer list are valid.
static void verify_all_meta_layers(const struct loader_instance *inst, struct loader_layer_list *instance_layers) {
    for (int32_t i = 0; i < (int32_t)instance_layers->count; i++) {
        struct loader_layer_properties *prop = &instance_layers->list[i];

        // If this is a meta-layer, make sure it is valid
        if ((prop->type_flags & VK_LAYER_TYPE_FLAG_META_LAYER) && !verify_meta_layer_comp_layers(inst, prop, instance_layers)) {
            loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0,
                       "Removing meta-layer %s from instance layer list since it appears invalid.", prop->info.layerName);

            // Delete the component layers
            loader_instance_heap_free(inst, prop->component_layer_names);

            // Remove the current invalid meta-layer from the layer list.  Use memmove since we are
            // overlapping the source and destination addresses.
            memmove(&instance_layers->list[i], &instance_layers->list[i + 1],
                    sizeof(struct loader_layer_properties) * (instance_layers->count - 1 - i));

            // Decrement the count (because we now have one less) and decrement the loop index since we need to
            // re-check this index.
            instance_layers->count--;
            i--;
        }
    }
}

// This structure is used to store the json file version
// in a more manageable way.
typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} layer_json_version;

static inline bool is_valid_layer_json_version(const layer_json_version *layer_json) {
    // Supported versions are: 1.0.0, 1.0.1, 1.1.0, 1.1.1, and 1.1.2.
    if ((layer_json->major == 1 && layer_json->minor == 1 && layer_json->patch < 3) ||
        (layer_json->major == 1 && layer_json->minor == 0 && layer_json->patch < 2)) {
        return true;
    }
    return false;
}

static inline bool layer_json_supports_layers_tag(const layer_json_version *layer_json) {
    // Supported versions started in 1.0.1, so anything newer
    if ((layer_json->major > 1 || layer_json->minor > 0 || layer_json->patch > 1)) {
        return true;
    }
    return false;
}

static inline bool layer_json_supports_pre_instance_tag(const layer_json_version *layer_json) {
    // Supported versions started in 1.1.2, so anything newer
    return layer_json->major > 1 || layer_json->minor > 1 || (layer_json->minor == 1 && layer_json->patch > 1);
}

static VkResult loader_read_json_layer(const struct loader_instance *inst, struct loader_layer_list *layer_instance_list,
                                       cJSON *layer_node, layer_json_version version, cJSON *item, cJSON *disable_environment,
                                       bool is_implicit, char *filename) {
    char *temp;
    char *name, *type, *library_path_str, *api_version;
    char *implementation_version, *description;
    cJSON *ext_item, *library_path, *component_layers;
    VkExtensionProperties ext_prop;
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    struct loader_layer_properties *props = NULL;
    int i, j;

// The following are required in the "layer" object:
// (required) "name"
// (required) "type"
// (required) "library_path"
// (required) "api_version"
// (required) "implementation_version"
// (required) "description"
// (required for implicit layers) "disable_environment"
#define GET_JSON_OBJECT(node, var)                                         \
    {                                                                      \
        var = cJSON_GetObjectItem(node, #var);                             \
        if (var == NULL) {                                                 \
            layer_node = layer_node->next;                                 \
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,           \
                       "Didn't find required layer object %s in manifest " \
                       "JSON file, skipping this layer",                   \
                       #var);                                              \
            goto out;                                                      \
        }                                                                  \
    }
#define GET_JSON_ITEM(node, var)                                               \
    {                                                                          \
        item = cJSON_GetObjectItem(node, #var);                                \
        if (item == NULL) {                                                    \
            layer_node = layer_node->next;                                     \
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,               \
                       "Didn't find required layer value %s in manifest JSON " \
                       "file, skipping this layer",                            \
                       #var);                                                  \
            goto out;                                                          \
        }                                                                      \
        temp = cJSON_Print(item);                                              \
        if (temp == NULL) {                                                    \
            layer_node = layer_node->next;                                     \
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,               \
                       "Problem accessing layer value %s in manifest JSON "    \
                       "file, skipping this layer",                            \
                       #var);                                                  \
            result = VK_ERROR_OUT_OF_HOST_MEMORY;                              \
            goto out;                                                          \
        }                                                                      \
        temp[strlen(temp) - 1] = '\0';                                         \
        var = loader_stack_alloc(strlen(temp) + 1);                            \
        strcpy(var, &temp[1]);                                                 \
        cJSON_Free(temp);                                                      \
    }
    GET_JSON_ITEM(layer_node, name)
    GET_JSON_ITEM(layer_node, type)
    GET_JSON_ITEM(layer_node, api_version)
    GET_JSON_ITEM(layer_node, implementation_version)
    GET_JSON_ITEM(layer_node, description)

    // Add list entry
    if (!strcmp(type, "DEVICE")) {
        loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0, "Device layers are deprecated skipping this layer");
        layer_node = layer_node->next;
        goto out;
    }

    // Allow either GLOBAL or INSTANCE type interchangeably to handle
    // layers that must work with older loaders
    if (!strcmp(type, "INSTANCE") || !strcmp(type, "GLOBAL")) {
        if (layer_instance_list == NULL) {
            layer_node = layer_node->next;
            goto out;
        }
        props = loader_get_next_layer_property(inst, layer_instance_list);
        if (NULL == props) {
            // Error already triggered in loader_get_next_layer_property.
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto out;
        }
        props->type_flags = VK_LAYER_TYPE_FLAG_INSTANCE_LAYER;
        if (!is_implicit) {
            props->type_flags |= VK_LAYER_TYPE_FLAG_EXPLICIT_LAYER;
        }
    } else {
        layer_node = layer_node->next;
        goto out;
    }

    // Library path no longer required unless component_layers is also not defined
    library_path = cJSON_GetObjectItem(layer_node, "library_path");
    component_layers = cJSON_GetObjectItem(layer_node, "component_layers");
    if (NULL != library_path) {
        if (NULL != component_layers) {
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "Indicating meta-layer-specific component_layers, but also "
                       "defining layer library path.  Both are not compatible, so "
                       "skipping this layer");
            goto out;
        }
        props->num_component_layers = 0;
        props->component_layer_names = NULL;

        temp = cJSON_Print(library_path);
        if (NULL == temp) {
            layer_node = layer_node->next;
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "Problem accessing layer value library_path in manifest JSON "
                       "file, skipping this layer");
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto out;
        }
        temp[strlen(temp) - 1] = '\0';
        library_path_str = loader_stack_alloc(strlen(temp) + 1);
        strcpy(library_path_str, &temp[1]);
        cJSON_Free(temp);

        char *fullpath = props->lib_name;
        char *rel_base;
        if (NULL != library_path_str) {
            if (loader_platform_is_path(library_path_str)) {
                // A relative or absolute path
                char *name_copy = loader_stack_alloc(strlen(filename) + 1);
                strcpy(name_copy, filename);
                rel_base = loader_platform_dirname(name_copy);
                loader_expand_path(library_path_str, rel_base, MAX_STRING_SIZE, fullpath);
            } else {
                // A filename which is assumed in a system directory
                loader_get_fullpath(library_path_str, DEFAULT_VK_LAYERS_PATH, MAX_STRING_SIZE, fullpath);
            }
        }
    } else if (NULL != component_layers) {
        if (version.major == 1 && (version.minor < 1 || version.patch < 1)) {
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "Indicating meta-layer-specific component_layers, but using older "
                       "JSON file version.");
        }
        int count = cJSON_GetArraySize(component_layers);
        props->num_component_layers = count;

        // Allocate buffer for layer names
        props->component_layer_names =
            loader_instance_heap_alloc(inst, sizeof(char[MAX_STRING_SIZE]) * count, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (NULL == props->component_layer_names) {
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto out;
        }

        // Copy the component layers into the array
        for (i = 0; i < count; i++) {
            cJSON *comp_layer = cJSON_GetArrayItem(component_layers, i);
            if (NULL != comp_layer) {
                temp = cJSON_Print(comp_layer);
                if (NULL == temp) {
                    result = VK_ERROR_OUT_OF_HOST_MEMORY;
                    goto out;
                }
                temp[strlen(temp) - 1] = '\0';
                strncpy(props->component_layer_names[i], temp + 1, MAX_STRING_SIZE - 1);
                props->component_layer_names[i][MAX_STRING_SIZE - 1] = '\0';
                cJSON_Free(temp);
            }
        }

        // This is now, officially, a meta-layer
        props->type_flags |= VK_LAYER_TYPE_FLAG_META_LAYER;
        loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0, "Encountered meta-layer %s", name);

        // Make sure we set up other things so we head down the correct branches below
        library_path_str = NULL;
    } else {
        loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                   "Layer missing both library_path and component_layers fields.  One or the "
                   "other MUST be defined.  Skipping this layer");
        goto out;
    }

    if (is_implicit) {
        GET_JSON_OBJECT(layer_node, disable_environment)
    }
#undef GET_JSON_ITEM
#undef GET_JSON_OBJECT

    strncpy(props->info.layerName, name, sizeof(props->info.layerName));
    props->info.layerName[sizeof(props->info.layerName) - 1] = '\0';
    props->info.specVersion = loader_make_version(api_version);
    props->info.implementationVersion = atoi(implementation_version);
    strncpy((char *)props->info.description, description, sizeof(props->info.description));
    props->info.description[sizeof(props->info.description) - 1] = '\0';
    if (is_implicit) {
        if (!disable_environment || !disable_environment->child) {
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "Didn't find required layer child value disable_environment"
                       "in manifest JSON file, skipping this layer");
            layer_node = layer_node->next;
            goto out;
        }
        strncpy(props->disable_env_var.name, disable_environment->child->string, sizeof(props->disable_env_var.name));
        props->disable_env_var.name[sizeof(props->disable_env_var.name) - 1] = '\0';
        strncpy(props->disable_env_var.value, disable_environment->child->valuestring, sizeof(props->disable_env_var.value));
        props->disable_env_var.value[sizeof(props->disable_env_var.value) - 1] = '\0';
    }

// Now get all optional items and objects and put in list:
// functions
// instance_extensions
// device_extensions
// enable_environment (implicit layers only)
#define GET_JSON_OBJECT(node, var) \
    { var = cJSON_GetObjectItem(node, #var); }
#define GET_JSON_ITEM(node, var)                            \
    {                                                       \
        item = cJSON_GetObjectItem(node, #var);             \
        if (item != NULL) {                                 \
            temp = cJSON_Print(item);                       \
            if (temp != NULL) {                             \
                temp[strlen(temp) - 1] = '\0';              \
                var = loader_stack_alloc(strlen(temp) + 1); \
                strcpy(var, &temp[1]);                      \
                cJSON_Free(temp);                           \
            } else {                                        \
                result = VK_ERROR_OUT_OF_HOST_MEMORY;       \
                goto out;                                   \
            }                                               \
        }                                                   \
    }

    cJSON *instance_extensions, *device_extensions, *functions, *enable_environment;
    cJSON *entrypoints = NULL;
    char *vkGetInstanceProcAddr = NULL;
    char *vkGetDeviceProcAddr = NULL;
    char *vkNegotiateLoaderLayerInterfaceVersion = NULL;
    char *spec_version = NULL;
    char **entry_array = NULL;

    // Layer interface functions
    //    vkGetInstanceProcAddr
    //    vkGetDeviceProcAddr
    //    vkNegotiateLoaderLayerInterfaceVersion (starting with JSON file 1.1.0)
    GET_JSON_OBJECT(layer_node, functions)
    if (functions != NULL) {
        if (version.major > 1 || version.minor >= 1) {
            GET_JSON_ITEM(functions, vkNegotiateLoaderLayerInterfaceVersion)
            if (vkNegotiateLoaderLayerInterfaceVersion != NULL)
                strncpy(props->functions.str_negotiate_interface, vkNegotiateLoaderLayerInterfaceVersion,
                        sizeof(props->functions.str_negotiate_interface));
            props->functions.str_negotiate_interface[sizeof(props->functions.str_negotiate_interface) - 1] = '\0';
        } else {
            props->functions.str_negotiate_interface[0] = '\0';
        }
        GET_JSON_ITEM(functions, vkGetInstanceProcAddr)
        GET_JSON_ITEM(functions, vkGetDeviceProcAddr)
        if (vkGetInstanceProcAddr != NULL) {
            strncpy(props->functions.str_gipa, vkGetInstanceProcAddr, sizeof(props->functions.str_gipa));
            if (version.major > 1 || version.minor >= 1) {
                loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                           "Indicating layer-specific vkGetInstanceProcAddr "
                           "function is deprecated starting with JSON file "
                           "version 1.1.0.  Instead, use the new "
                           "vkNegotiateLayerInterfaceVersion function to "
                           "return the GetInstanceProcAddr function for this"
                           "layer");
            }
        }
        props->functions.str_gipa[sizeof(props->functions.str_gipa) - 1] = '\0';
        if (vkGetDeviceProcAddr != NULL) {
            strncpy(props->functions.str_gdpa, vkGetDeviceProcAddr, sizeof(props->functions.str_gdpa));
            if (version.major > 1 || version.minor >= 1) {
                loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                           "Indicating layer-specific vkGetDeviceProcAddr "
                           "function is deprecated starting with JSON file "
                           "version 1.1.0.  Instead, use the new "
                           "vkNegotiateLayerInterfaceVersion function to "
                           "return the GetDeviceProcAddr function for this"
                           "layer");
            }
        }
        props->functions.str_gdpa[sizeof(props->functions.str_gdpa) - 1] = '\0';
    }

    // instance_extensions
    //   array of {
    //     name
    //     spec_version
    //   }
    GET_JSON_OBJECT(layer_node, instance_extensions)
    if (instance_extensions != NULL) {
        int count = cJSON_GetArraySize(instance_extensions);
        for (i = 0; i < count; i++) {
            ext_item = cJSON_GetArrayItem(instance_extensions, i);
            GET_JSON_ITEM(ext_item, name)
            if (name != NULL) {
                strncpy(ext_prop.extensionName, name, sizeof(ext_prop.extensionName));
                ext_prop.extensionName[sizeof(ext_prop.extensionName) - 1] = '\0';
            }
            GET_JSON_ITEM(ext_item, spec_version)
            if (NULL != spec_version) {
                ext_prop.specVersion = atoi(spec_version);
            } else {
                ext_prop.specVersion = 0;
            }
            bool ext_unsupported = wsi_unsupported_instance_extension(&ext_prop);
            if (!ext_unsupported) {
                loader_add_to_ext_list(inst, &props->instance_extension_list, 1, &ext_prop);
            }
        }
    }

    // device_extensions
    //   array of {
    //     name
    //     spec_version
    //     entrypoints
    //   }
    GET_JSON_OBJECT(layer_node, device_extensions)
    if (device_extensions != NULL) {
        int count = cJSON_GetArraySize(device_extensions);
        for (i = 0; i < count; i++) {
            ext_item = cJSON_GetArrayItem(device_extensions, i);
            GET_JSON_ITEM(ext_item, name)
            GET_JSON_ITEM(ext_item, spec_version)
            if (name != NULL) {
                strncpy(ext_prop.extensionName, name, sizeof(ext_prop.extensionName));
                ext_prop.extensionName[sizeof(ext_prop.extensionName) - 1] = '\0';
            }
            if (NULL != spec_version) {
                ext_prop.specVersion = atoi(spec_version);
            } else {
                ext_prop.specVersion = 0;
            }
            // entrypoints = cJSON_GetObjectItem(ext_item, "entrypoints");
            GET_JSON_OBJECT(ext_item, entrypoints)
            int entry_count;
            if (entrypoints == NULL) {
                loader_add_to_dev_ext_list(inst, &props->device_extension_list, &ext_prop, 0, NULL);
                continue;
            }
            entry_count = cJSON_GetArraySize(entrypoints);
            if (entry_count) {
                entry_array = (char **)loader_stack_alloc(sizeof(char *) * entry_count);
            }
            for (j = 0; j < entry_count; j++) {
                ext_item = cJSON_GetArrayItem(entrypoints, j);
                if (ext_item != NULL) {
                    temp = cJSON_Print(ext_item);
                    if (NULL == temp) {
                        entry_array[j] = NULL;
                        result = VK_ERROR_OUT_OF_HOST_MEMORY;
                        goto out;
                    }
                    temp[strlen(temp) - 1] = '\0';
                    entry_array[j] = loader_stack_alloc(strlen(temp) + 1);
                    strcpy(entry_array[j], &temp[1]);
                    cJSON_Free(temp);
                }
            }
            loader_add_to_dev_ext_list(inst, &props->device_extension_list, &ext_prop, entry_count, entry_array);
        }
    }
    if (is_implicit) {
        GET_JSON_OBJECT(layer_node, enable_environment)

        // enable_environment is optional
        if (enable_environment) {
            strncpy(props->enable_env_var.name, enable_environment->child->string, sizeof(props->enable_env_var.name));
            props->enable_env_var.name[sizeof(props->enable_env_var.name) - 1] = '\0';
            strncpy(props->enable_env_var.value, enable_environment->child->valuestring, sizeof(props->enable_env_var.value));
            props->enable_env_var.value[sizeof(props->enable_env_var.value) - 1] = '\0';
        }
    }

    // Read in the pre-instance stuff
    cJSON *pre_instance = cJSON_GetObjectItem(layer_node, "pre_instance_functions");
    if (pre_instance) {
        if (!layer_json_supports_pre_instance_tag(&version)) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "Found pre_instance_functions section in layer from \"%s\". "
                       "This section is only valid in manifest version 1.1.2 or later. The section will be ignored",
                       filename);
        } else if (!is_implicit) {
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "Found pre_instance_functions section in explicit layer from "
                       "\"%s\". This section is only valid in implicit layers. The section will be ignored",
                       filename);
        } else {
            cJSON *inst_ext_json = cJSON_GetObjectItem(pre_instance, "vkEnumerateInstanceExtensionProperties");
            if (inst_ext_json) {
                char *inst_ext_name = cJSON_Print(inst_ext_json);
                size_t len = strlen(inst_ext_name) >= MAX_STRING_SIZE ? MAX_STRING_SIZE - 3 : strlen(inst_ext_name) - 2;
                strncpy(props->pre_instance_functions.enumerate_instance_extension_properties, inst_ext_name + 1, len);
                props->pre_instance_functions.enumerate_instance_extension_properties[len] = '\0';
                cJSON_Free(inst_ext_name);
            }

            cJSON *inst_layer_json = cJSON_GetObjectItem(pre_instance, "vkEnumerateInstanceLayerProperties");
            if (inst_layer_json) {
                char *inst_layer_name = cJSON_Print(inst_layer_json);
                size_t len = strlen(inst_layer_name) >= MAX_STRING_SIZE ? MAX_STRING_SIZE - 3 : strlen(inst_layer_name) - 2;
                strncpy(props->pre_instance_functions.enumerate_instance_layer_properties, inst_layer_name + 1, len);
                props->pre_instance_functions.enumerate_instance_layer_properties[len] = '\0';
                cJSON_Free(inst_layer_name);
            }

            cJSON *inst_version_json = cJSON_GetObjectItem(pre_instance, "vkEnumerateInstanceVersion");
            if (inst_version_json) {
                char *inst_version_name = cJSON_Print(inst_version_json);
                size_t len = strlen(inst_version_name) >= MAX_STRING_SIZE ? MAX_STRING_SIZE - 3 : strlen(inst_version_name) - 2;
                strncpy(props->pre_instance_functions.enumerate_instance_version, inst_version_name + 1, len);
                props->pre_instance_functions.enumerate_instance_version[len] = '\0';
                cJSON_Free(inst_version_name);
            }
        }
    }

    result = VK_SUCCESS;

out:

#undef GET_JSON_ITEM
#undef GET_JSON_OBJECT

    if (VK_SUCCESS != result && NULL != props) {
        props->num_component_layers = 0;
        if (NULL != props->component_layer_names) {
            loader_instance_heap_free(inst, props->component_layer_names);
        }
        props->component_layer_names = NULL;
    }

    return result;
}

// Given a cJSON struct (json) of the top level JSON object from layer manifest
// file, add entry to the layer_list. Fill out the layer_properties in this list
// entry from the input cJSON object.
//
// \returns
// void
// layer_list has a new entry and initialized accordingly.
// If the json input object does not have all the required fields no entry
// is added to the list.
static VkResult loader_add_layer_properties(const struct loader_instance *inst, struct loader_layer_list *layer_instance_list,
                                            cJSON *json, bool is_implicit, char *filename) {
    // The following Fields in layer manifest file that are required:
    //   - "file_format_version"
    //   - If more than one "layer" object are used, then the "layers" array is
    //     required
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    cJSON *item, *layers_node, *layer_node;
    layer_json_version json_version = {0, 0, 0};
    char *vers_tok;
    cJSON *disable_environment = NULL;
    item = cJSON_GetObjectItem(json, "file_format_version");
    if (item == NULL) {
        goto out;
    }
    char *file_vers = cJSON_PrintUnformatted(item);
    if (NULL == file_vers) {
        goto out;
    }
    loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0, "Found manifest file %s, version %s", filename, file_vers);
    // Get the major/minor/and patch as integers for easier comparison
    vers_tok = strtok(file_vers, ".\"\n\r");
    if (NULL != vers_tok) {
        json_version.major = (uint16_t)atoi(vers_tok);
        vers_tok = strtok(NULL, ".\"\n\r");
        if (NULL != vers_tok) {
            json_version.minor = (uint16_t)atoi(vers_tok);
            vers_tok = strtok(NULL, ".\"\n\r");
            if (NULL != vers_tok) {
                json_version.patch = (uint16_t)atoi(vers_tok);
            }
        }
    }

    if (!is_valid_layer_json_version(&json_version)) {
        loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                   "loader_add_layer_properties: %s invalid layer "
                   "manifest file version %d.%d.%d.  May cause errors.",
                   filename, json_version.major, json_version.minor, json_version.patch);
    }
    cJSON_Free(file_vers);

    // If "layers" is present, read in the array of layer objects
    layers_node = cJSON_GetObjectItem(json, "layers");
    if (layers_node != NULL) {
        int numItems = cJSON_GetArraySize(layers_node);
        if (!layer_json_supports_layers_tag(&json_version)) {
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "loader_add_layer_properties: \'layers\' tag not "
                       "supported until file version 1.0.1, but %s is "
                       "reporting version %s",
                       filename, file_vers);
        }
        for (int curLayer = 0; curLayer < numItems; curLayer++) {
            layer_node = cJSON_GetArrayItem(layers_node, curLayer);
            if (layer_node == NULL) {
                loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                           "loader_add_layer_properties: Can not find "
                           "\'layers\' array element %d object in manifest "
                           "JSON file %s.  Skipping this file",
                           curLayer, filename);
                goto out;
            }
            result = loader_read_json_layer(inst, layer_instance_list, layer_node, json_version, item, disable_environment,
                                            is_implicit, filename);
        }
    } else {
        // Otherwise, try to read in individual layers
        layer_node = cJSON_GetObjectItem(json, "layer");
        if (layer_node == NULL) {
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "loader_add_layer_properties: Can not find \'layer\' "
                       "object in manifest JSON file %s.  Skipping this file.",
                       filename);
            goto out;
        }
        // Loop through all "layer" objects in the file to get a count of them
        // first.
        uint16_t layer_count = 0;
        cJSON *tempNode = layer_node;
        do {
            tempNode = tempNode->next;
            layer_count++;
        } while (tempNode != NULL);

        // Throw a warning if we encounter multiple "layer" objects in file
        // versions newer than 1.0.0.  Having multiple objects with the same
        // name at the same level is actually a JSON standard violation.
        if (layer_count > 1 && layer_json_supports_layers_tag(&json_version)) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_add_layer_properties: Multiple \'layer\' nodes"
                       " are deprecated starting in file version \"1.0.1\".  "
                       "Please use \'layers\' : [] array instead in %s.",
                       filename);
        } else {
            do {
                result = loader_read_json_layer(inst, layer_instance_list, layer_node, json_version, item, disable_environment,
                                                is_implicit, filename);
                layer_node = layer_node->next;
            } while (layer_node != NULL);
        }
    }

out:

    return result;
}

// Find the Vulkan library manifest files.
//
// This function scans the "location" or "env_override" directories/files
// for a list of JSON manifest files.  If env_override is non-NULL
// and has a valid value. Then the location is ignored.  Otherwise
// location is used to look for manifest files. The location
// is interpreted as  Registry path on Windows and a directory path(s)
// on Linux. "home_location" is an additional directory in the users home
// directory to look at. It is expanded into the dir path
// $XDG_DATA_HOME/home_location or $HOME/.local/share/home_location depending
// on environment variables. This "home_location" is only used on Linux.
//
// \returns
// VKResult
// A string list of manifest files to be opened in out_files param.
// List has a pointer to string for each manifest filename.
// When done using the list in out_files, pointers should be freed.
// Location or override  string lists can be either files or directories as
// follows:
//            | location | override
// --------------------------------
// Win ICD    | files    | files
// Win Layer  | files    | dirs
// Linux ICD  | dirs     | files
// Linux Layer| dirs     | dirs
static VkResult loader_get_manifest_files(const struct loader_instance *inst, const char *env_override, const char *source_override,
                                          bool is_layer, bool warn_if_not_present, const char *location,
                                          const char *relative_location, struct loader_manifest_files *out_files) {
    const char *override = NULL;
    char *override_getenv = NULL;
    char *loc, *orig_loc = NULL;
    char *reg = NULL;
    char *file, *next_file, *name;
    size_t alloced_count = 64;
    char full_path[2048];
    DIR *sysdir = NULL;
    bool list_is_dirs = false;
    struct dirent *dent;
    VkResult res = VK_SUCCESS;

    out_files->count = 0;
    out_files->filename_list = NULL;

    if (source_override != NULL) {
        override = source_override;
    } else if (env_override != NULL) {
#if !defined(_WIN32)
        if (geteuid() != getuid() || getegid() != getgid()) {
            // Don't allow setuid apps to use the env var:
            env_override = NULL;
        }
#endif
        if (env_override != NULL) {
            override = override_getenv = loader_secure_getenv(env_override, inst);
        }
    }

#if !defined(_WIN32)
    if (relative_location == NULL) {
#else
    relative_location = NULL;
    if (location == NULL) {
#endif
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_get_manifest_files: Can not get manifest files with "
                   "NULL location, env_override=%s",
                   (env_override != NULL) ? env_override : "");
        res = VK_ERROR_INITIALIZATION_FAILED;
        goto out;
    }

#if defined(_WIN32)
    list_is_dirs = (is_layer && override != NULL) ? true : false;
#else
    list_is_dirs = (override == NULL || is_layer) ? true : false;
#endif
    // Make a copy of the input we are using so it is not modified
    // Also handle getting the location(s) from registry on Windows
    if (override == NULL) {
        size_t loc_size = 0;
#if !defined(_WIN32)
        const char *xdgconfdirs = loader_secure_getenv("XDG_CONFIG_DIRS", inst);
        const char *xdgdatadirs = loader_secure_getenv("XDG_DATA_DIRS", inst);
        if (xdgconfdirs == NULL || xdgconfdirs[0] == '\0') xdgconfdirs = FALLBACK_CONFIG_DIRS;
        if (xdgdatadirs == NULL || xdgdatadirs[0] == '\0') xdgdatadirs = FALLBACK_DATA_DIRS;
        const size_t rel_size = strlen(relative_location);
        // Leave space for trailing separators
        loc_size += strlen(xdgconfdirs) + strlen(xdgdatadirs) + 2 * rel_size + 2;
        for (const char *x = xdgconfdirs; *x; ++x)
            if (*x == PATH_SEPARATOR) loc_size += rel_size;
        for (const char *x = xdgdatadirs; *x; ++x)
            if (*x == PATH_SEPARATOR) loc_size += rel_size;
        loc_size += strlen(SYSCONFDIR) + rel_size + 1;
#if defined(EXTRASYSCONFDIR)
        loc_size += strlen(EXTRASYSCONFDIR) + rel_size + 1;
#endif
#else
        loc_size += strlen(location) + 1;
#endif
        loc = loader_stack_alloc(loc_size);
        if (loc == NULL) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_get_manifest_files: Failed to allocate "
                       "%d bytes for manifest file location.",
                       loc_size);
            res = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto out;
        }
        char *loc_write = loc;
#if !defined(_WIN32)
        const char *loc_read;
        size_t start, stop;

        loc_read = &xdgconfdirs[0];
        start = 0;
        while (loc_read[start] != '\0') {
            while (loc_read[start] == PATH_SEPARATOR) {
                start++;
            }
            stop = start;
            while (loc_read[stop] != PATH_SEPARATOR && loc_read[stop] != '\0') {
                stop++;
            }
            const size_t s = stop - start;
            if (s) {
                memcpy(loc_write, &loc_read[start], s);
                loc_write += s;
                memcpy(loc_write, relative_location, rel_size);
                loc_write += rel_size;
                *loc_write++ = PATH_SEPARATOR;
                start = stop;
            }
        }

        memcpy(loc_write, SYSCONFDIR, strlen(SYSCONFDIR));
        loc_write += strlen(SYSCONFDIR);
        memcpy(loc_write, relative_location, rel_size);
        loc_write += rel_size;
        *loc_write++ = PATH_SEPARATOR;

#if defined(EXTRASYSCONFDIR)
        memcpy(loc_write, EXTRASYSCONFDIR, strlen(EXTRASYSCONFDIR));
        loc_write += strlen(EXTRASYSCONFDIR);
        memcpy(loc_write, relative_location, rel_size);
        loc_write += rel_size;
        *loc_write++ = PATH_SEPARATOR;
#endif

        loc_read = &xdgdatadirs[0];
        start = 0;
        while (loc_read[start] != '\0') {
            while (loc_read[start] == PATH_SEPARATOR) {
                start++;
            }
            stop = start;
            while (loc_read[stop] != PATH_SEPARATOR && loc_read[stop] != '\0') {
                stop++;
            }
            const size_t s = stop - start;
            if (s) {
                memcpy(loc_write, &loc_read[start], s);
                loc_write += s;
                memcpy(loc_write, relative_location, rel_size);
                loc_write += rel_size;
                *loc_write++ = PATH_SEPARATOR;
                start = stop;
            }
        }

        --loc_write;
#else
        memcpy(loc_write, location, strlen(location));
        loc_write += strlen(location);
#endif
        assert(loc_write - loc < (ptrdiff_t)loc_size);
        *loc_write = '\0';

#if defined(_WIN32)
        VkResult regHKR_result = VK_SUCCESS;

        DWORD reg_size = 4096;

        // These calls look at the PNP/Device section of the registry.
        if (!strncmp(loc, DEFAULT_VK_DRIVERS_INFO, sizeof(DEFAULT_VK_DRIVERS_INFO))) {
            regHKR_result = loaderGetDeviceRegistryFiles(inst, &reg, &reg_size, LoaderPnpDriverRegistry());
        } else if (!strncmp(loc, DEFAULT_VK_ELAYERS_INFO, sizeof(DEFAULT_VK_ELAYERS_INFO))) {
            regHKR_result = loaderGetDeviceRegistryFiles(inst, &reg, &reg_size, LoaderPnpELayerRegistry());
        } else if (!strncmp(loc, DEFAULT_VK_ILAYERS_INFO, sizeof(DEFAULT_VK_ILAYERS_INFO))) {
            regHKR_result = loaderGetDeviceRegistryFiles(inst, &reg, &reg_size, LoaderPnpILayerRegistry());
        }

        // This call looks into the Khronos non-device specific section of the registry.
        VkResult reg_result = loaderGetRegistryFiles(inst, loc, is_layer, &reg, &reg_size);

        if ((VK_SUCCESS != reg_result && VK_SUCCESS != regHKR_result) || NULL == reg) {
            if (!is_layer) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "loader_get_manifest_files: Registry lookup failed "
                           "to get ICD manifest files.  Possibly missing Vulkan"
                           " driver?");
                if (VK_SUCCESS == regHKR_result || VK_ERROR_OUT_OF_HOST_MEMORY == regHKR_result) {
                    res = regHKR_result;
                } else if (VK_SUCCESS == reg_result || VK_ERROR_OUT_OF_HOST_MEMORY == reg_result) {
                    res = reg_result;
                } else {
                    res = VK_ERROR_INCOMPATIBLE_DRIVER;
                }
            } else {
                if (warn_if_not_present) {
                    // This is only a warning for layers
                    loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                               "loader_get_manifest_files: Registry lookup failed "
                               "to get layer manifest files.");
                }
                if (reg_result == VK_ERROR_OUT_OF_HOST_MEMORY) {
                    res = reg_result;
                } else {
                    // Return success for now since it's not critical for layers
                    res = VK_SUCCESS;
                }
            }
            goto out;
        }
        orig_loc = loc;
        loc = reg;
#endif
    } else {
        loc = loader_stack_alloc(strlen(override) + 1);
        if (loc == NULL) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_get_manifest_files: Failed to allocate space for "
                       "override environment variable of length %d",
                       strlen(override) + 1);
            res = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto out;
        }
        strcpy(loc, override);
    }

    // Print out the paths being searched if debugging is enabled
    loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0, "Searching the following paths for manifest files: %s\n", loc);

    file = loc;
    while (*file) {
        next_file = loader_get_next_path(file);
        if (list_is_dirs) {
            sysdir = opendir(file);
            name = NULL;
            if (sysdir) {
                dent = readdir(sysdir);
                if (dent == NULL) break;
                name = &(dent->d_name[0]);
                loader_get_fullpath(name, file, sizeof(full_path), full_path);
                name = full_path;
            }
        } else {
#if defined(_WIN32)
            name = file;
#else
            // only Linux has relative paths
            char *dir;
            // make a copy of location so it isn't modified
            dir = loader_stack_alloc(strlen(loc) + 1);
            if (dir == NULL) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "loader_get_manifest_files: Failed to allocate "
                           "space for relative location path length %d",
                           strlen(loc) + 1);
                goto out;
            }
            strcpy(dir, loc);

            loader_get_fullpath(file, dir, sizeof(full_path), full_path);

            name = full_path;
#endif
        }
        while (name) {
            // Look for files ending with ".json" suffix
            uint32_t nlen = (uint32_t)strlen(name);
            const char *suf = name + nlen - 5;

            // Check if the file is already present
            bool file_already_loaded = false;
            for (uint32_t i = 0; i < out_files->count; ++i) {
                if (!strcmp(out_files->filename_list[i], name)) {
                    file_already_loaded = true;
                }
            }

            if (!file_already_loaded && (nlen > 5) && !strncmp(suf, ".json", 5)) {
                if (out_files->count == 0) {
                    out_files->filename_list =
                        loader_instance_heap_alloc(inst, alloced_count * sizeof(char *), VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
                    if (NULL == out_files->filename_list) {
                        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                                   "loader_get_manifest_files: Failed to allocate space for manifest file name list");
                        res = VK_ERROR_OUT_OF_HOST_MEMORY;
                        goto out;
                    }
                } else if (out_files->count == alloced_count) {
                    void *new_ptr =
                        loader_instance_heap_realloc(inst, out_files->filename_list, alloced_count * sizeof(char *),
                                                     alloced_count * sizeof(char *) * 2, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
                    if (NULL == new_ptr) {
                        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                                   "loader_get_manifest_files: Failed to reallocate space for manifest file name list");
                        res = VK_ERROR_OUT_OF_HOST_MEMORY;
                        goto out;
                    }
                    out_files->filename_list = new_ptr;
                    alloced_count *= 2;
                }
                out_files->filename_list[out_files->count] =
                    loader_instance_heap_alloc(inst, strlen(name) + 1, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
                if (out_files->filename_list[out_files->count] == NULL) {
                    loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                               "loader_get_manifest_files: Failed to allocate "
                               "space for manifest file %d list",
                               out_files->count);
                    res = VK_ERROR_OUT_OF_HOST_MEMORY;
                    goto out;
                }
                strcpy(out_files->filename_list[out_files->count], name);
                out_files->count++;
            } else if(file_already_loaded) {
                loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                    "Skipping manifest file %s - The file has already been read once", name);
            } else if (!list_is_dirs) {
                loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0, "Skipping manifest file %s, file name must end in .json",
                           name);
            }
            if (list_is_dirs) {
                dent = readdir(sysdir);
                if (dent == NULL) {
                    break;
                }
                name = &(dent->d_name[0]);
                loader_get_fullpath(name, file, sizeof(full_path), full_path);
                name = full_path;
            } else {
                break;
            }
        }
        if (sysdir) {
            closedir(sysdir);
            sysdir = NULL;
        }
        file = next_file;
#if !defined(_WIN32)
        if (relative_location != NULL && (next_file == NULL || *next_file == '\0') && override == NULL) {
            char *xdgdatahome = loader_secure_getenv("XDG_DATA_HOME", inst);
            size_t len;
            if (xdgdatahome != NULL) {
                size_t alloc_len = strlen(xdgdatahome) + 2 + strlen(relative_location);
                char *home_loc = loader_stack_alloc(alloc_len);
                if (home_loc == NULL) {
                    loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                               "loader_get_manifest_files: Failed to allocate "
                               "space for manifest file XDG Home location");
                    res = VK_ERROR_OUT_OF_HOST_MEMORY;
                    goto out;
                }
                strcpy(home_loc, xdgdatahome);
                // Add directory separator if needed
                if (relative_location[0] != DIRECTORY_SYMBOL) {
                    len = strlen(home_loc);
                    home_loc[len] = DIRECTORY_SYMBOL;
                    home_loc[len + 1] = '\0';
                }
                strncat(home_loc, relative_location, alloc_len);
                file = home_loc;
                next_file = loader_get_next_path(file);
                relative_location = NULL;

                loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0, "Searching the following path for manifest files: %s\n",
                           home_loc);
                list_is_dirs = true;

            } else {
                char *home = loader_secure_getenv("HOME", inst);
                if (home != NULL) {
                    size_t alloc_len = strlen(home) + 16 + strlen(relative_location);
                    char *home_loc = loader_stack_alloc(alloc_len);
                    if (home_loc == NULL) {
                        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                                   "loader_get_manifest_files: Failed to allocate "
                                   "space for manifest file Home location");
                        res = VK_ERROR_OUT_OF_HOST_MEMORY;
                        goto out;
                    }
                    strncpy(home_loc, home, alloc_len);

                    len = strlen(home);
                    if (home[len] != DIRECTORY_SYMBOL) {
                        home_loc[len] = DIRECTORY_SYMBOL;
                        home_loc[len + 1] = '\0';
                    }
                    strncat(home_loc, ".local/share", alloc_len);

                    if (relative_location[0] != DIRECTORY_SYMBOL) {
                        len = strlen(home_loc);
                        home_loc[len] = DIRECTORY_SYMBOL;
                        home_loc[len + 1] = '\0';
                    }
                    strncat(home_loc, relative_location, alloc_len);
                    file = home_loc;
                    next_file = loader_get_next_path(file);
                    relative_location = NULL;

                    loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0, "Searching the following path for manifest files: %s\n",
                               home_loc);
                    list_is_dirs = true;
                } else {
                    // without knowing HOME, we just.. give up
                }
            }
        }
#endif
    }

out:
    if (VK_SUCCESS != res && NULL != out_files->filename_list) {
        for (uint32_t remove = 0; remove < out_files->count; remove++) {
            loader_instance_heap_free(inst, out_files->filename_list[remove]);
        }
        loader_instance_heap_free(inst, out_files->filename_list);
        out_files->count = 0;
        out_files->filename_list = NULL;
    }

    if (NULL != sysdir) {
        closedir(sysdir);
    }

    if (override_getenv != NULL) {
        loader_free_getenv(override_getenv, inst);
    }

    if (NULL != reg && reg != orig_loc) {
        loader_instance_heap_free(inst, reg);
    }
    return res;
}

void loader_init_icd_lib_list() {}

void loader_destroy_icd_lib_list() {}

// Try to find the Vulkan ICD driver(s).
//
// This function scans the default system loader path(s) or path
// specified by the \c VK_ICD_FILENAMES environment variable in
// order to find loadable VK ICDs manifest files. From these
// manifest files it finds the ICD libraries.
//
// \returns
// Vulkan result
// (on result == VK_SUCCESS) a list of icds that were discovered
VkResult loader_icd_scan(const struct loader_instance *inst, struct loader_icd_tramp_list *icd_tramp_list) {
    char *file_str;
    uint16_t file_major_vers = 0;
    uint16_t file_minor_vers = 0;
    uint16_t file_patch_vers = 0;
    char *vers_tok;
    struct loader_manifest_files manifest_files;
    VkResult res = VK_SUCCESS;
    bool lockedMutex = false;
    cJSON *json = NULL;
    uint32_t num_good_icds = 0;

    memset(&manifest_files, 0, sizeof(struct loader_manifest_files));

    res = loader_scanned_icd_init(inst, icd_tramp_list);
    if (VK_SUCCESS != res) {
        goto out;
    }

    // Get a list of manifest files for ICDs
    res = loader_get_manifest_files(inst, "VK_ICD_FILENAMES", NULL, false, true, DEFAULT_VK_DRIVERS_INFO, RELATIVE_VK_DRIVERS_INFO,
                                    &manifest_files);
    if (VK_SUCCESS != res || manifest_files.count == 0) {
        goto out;
    }

    loader_platform_thread_lock_mutex(&loader_json_lock);
    lockedMutex = true;
    for (uint32_t i = 0; i < manifest_files.count; i++) {
        file_str = manifest_files.filename_list[i];
        if (file_str == NULL) {
            continue;
        }

        VkResult temp_res = loader_get_json(inst, file_str, &json);
        if (NULL == json || temp_res != VK_SUCCESS) {
            if (NULL != json) {
                cJSON_Delete(json);
                json = NULL;
            }
            // If we haven't already found an ICD, copy this result to
            // the returned result.
            if (num_good_icds == 0) {
                res = temp_res;
            }
            if (temp_res == VK_ERROR_OUT_OF_HOST_MEMORY) {
                break;
            } else {
                continue;
            }
        }
        res = temp_res;

        cJSON *item, *itemICD;
        item = cJSON_GetObjectItem(json, "file_format_version");
        if (item == NULL) {
            if (num_good_icds == 0) {
                res = VK_ERROR_INITIALIZATION_FAILED;
            }
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "loader_icd_scan: ICD JSON %s does not have a"
                       " \'file_format_version\' field. Skipping ICD JSON.",
                       file_str);
            cJSON_Delete(json);
            json = NULL;
            continue;
        }

        char *file_vers = cJSON_Print(item);
        if (NULL == file_vers) {
            // Only reason the print can fail is if there was an allocation issue
            if (num_good_icds == 0) {
                res = VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "loader_icd_scan: Failed retrieving ICD JSON %s"
                       " \'file_format_version\' field.  Skipping ICD JSON",
                       file_str);
            cJSON_Delete(json);
            json = NULL;
            continue;
        }
        loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0, "Found ICD manifest file %s, version %s", file_str, file_vers);

        // Get the major/minor/and patch as integers for easier comparison
        vers_tok = strtok(file_vers, ".\"\n\r");
        if (NULL != vers_tok) {
            file_major_vers = (uint16_t)atoi(vers_tok);
            vers_tok = strtok(NULL, ".\"\n\r");
            if (NULL != vers_tok) {
                file_minor_vers = (uint16_t)atoi(vers_tok);
                vers_tok = strtok(NULL, ".\"\n\r");
                if (NULL != vers_tok) {
                    file_patch_vers = (uint16_t)atoi(vers_tok);
                }
            }
        }

        if (file_major_vers != 1 || file_minor_vers != 0 || file_patch_vers > 1) {
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "loader_icd_scan: Unexpected manifest file version "
                       "(expected 1.0.0 or 1.0.1), may cause errors");
        }
        cJSON_Free(file_vers);

        itemICD = cJSON_GetObjectItem(json, "ICD");
        if (itemICD != NULL) {
            item = cJSON_GetObjectItem(itemICD, "library_path");
            if (item != NULL) {
                char *temp = cJSON_Print(item);
                if (!temp || strlen(temp) == 0) {
                    if (num_good_icds == 0) {
                        res = VK_ERROR_OUT_OF_HOST_MEMORY;
                    }
                    loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                               "loader_icd_scan: Failed retrieving ICD JSON %s"
                               " \'library_path\' field.  Skipping ICD JSON.",
                               file_str);
                    cJSON_Free(temp);
                    cJSON_Delete(json);
                    json = NULL;
                    continue;
                }
                // strip out extra quotes
                temp[strlen(temp) - 1] = '\0';
                char *library_path = loader_stack_alloc(strlen(temp) + 1);
                if (NULL == library_path) {
                    loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                               "loader_icd_scan: Failed to allocate space for "
                               "ICD JSON %s \'library_path\' value.  Skipping "
                               "ICD JSON.",
                               file_str);
                    res = VK_ERROR_OUT_OF_HOST_MEMORY;
                    cJSON_Free(temp);
                    cJSON_Delete(json);
                    json = NULL;
                    goto out;
                }
                strcpy(library_path, &temp[1]);
                cJSON_Free(temp);
                if (strlen(library_path) == 0) {
                    loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                               "loader_icd_scan: ICD JSON %s \'library_path\'"
                               " field is empty.  Skipping ICD JSON.",
                               file_str);
                    cJSON_Delete(json);
                    json = NULL;
                    continue;
                }
                char fullpath[MAX_STRING_SIZE];
                // Print out the paths being searched if debugging is enabled
                loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0, "Searching for ICD drivers named %s, using default dir %s",
                           library_path, DEFAULT_VK_DRIVERS_PATH);
                if (loader_platform_is_path(library_path)) {
                    // a relative or absolute path
                    char *name_copy = loader_stack_alloc(strlen(file_str) + 1);
                    char *rel_base;
                    strcpy(name_copy, file_str);
                    rel_base = loader_platform_dirname(name_copy);
                    loader_expand_path(library_path, rel_base, sizeof(fullpath), fullpath);
                } else {
                    // a filename which is assumed in a system directory
                    loader_get_fullpath(library_path, DEFAULT_VK_DRIVERS_PATH, sizeof(fullpath), fullpath);
                }

                uint32_t vers = 0;
                item = cJSON_GetObjectItem(itemICD, "api_version");
                if (item != NULL) {
                    temp = cJSON_Print(item);
                    if (NULL == temp) {
                        loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                                   "loader_icd_scan: Failed retrieving ICD JSON %s"
                                   " \'api_version\' field.  Skipping ICD JSON.",
                                   file_str);

                        // Only reason the print can fail is if there was an
                        // allocation issue
                        if (num_good_icds == 0) {
                            res = VK_ERROR_OUT_OF_HOST_MEMORY;
                        }

                        cJSON_Free(temp);
                        cJSON_Delete(json);
                        json = NULL;
                        continue;
                    }
                    vers = loader_make_version(temp);
                    cJSON_Free(temp);
                } else {
                    loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                               "loader_icd_scan: ICD JSON %s does not have an"
                               " \'api_version\' field.",
                               file_str);
                }

                res = loader_scanned_icd_add(inst, icd_tramp_list, fullpath, vers);
                if (VK_SUCCESS != res) {
                    loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                               "loader_icd_scan: Failed to add ICD JSON %s. "
                               " Skipping ICD JSON.",
                               fullpath);
                    cJSON_Delete(json);
                    json = NULL;
                    continue;
                }
                num_good_icds++;
            } else {
                loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                           "loader_icd_scan: Failed to find \'library_path\' "
                           "object in ICD JSON file %s.  Skipping ICD JSON.",
                           file_str);
            }
        } else {
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "loader_icd_scan: Can not find \'ICD\' object in ICD JSON "
                       "file %s.  Skipping ICD JSON",
                       file_str);
        }

        cJSON_Delete(json);
        json = NULL;
    }

out:

    if (NULL != json) {
        cJSON_Delete(json);
    }

    if (NULL != manifest_files.filename_list) {
        for (uint32_t i = 0; i < manifest_files.count; i++) {
            if (NULL != manifest_files.filename_list[i]) {
                loader_instance_heap_free(inst, manifest_files.filename_list[i]);
            }
        }
        loader_instance_heap_free(inst, manifest_files.filename_list);
    }
    if (lockedMutex) {
        loader_platform_thread_unlock_mutex(&loader_json_lock);
    }

    return res;
}

void loader_layer_scan(const struct loader_instance *inst, struct loader_layer_list *instance_layers) {
    char *file_str;
    struct loader_manifest_files manifest_files[2];  // [0] = explicit, [1] = implicit
    cJSON *json;
    uint32_t implicit;
    bool lockedMutex = false;

    memset(manifest_files, 0, sizeof(struct loader_manifest_files) * 2);

    // Get a list of manifest files for explicit layers
    if (VK_SUCCESS != loader_get_manifest_files(inst, LAYERS_PATH_ENV, LAYERS_SOURCE_PATH, true, true, DEFAULT_VK_ELAYERS_INFO,
                                                RELATIVE_VK_ELAYERS_INFO, &manifest_files[0])) {
        goto out;
    }

    // Get a list of manifest files for any implicit layers
    // Pass NULL for environment variable override - implicit layers are not
    // overridden by LAYERS_PATH_ENV
    if (VK_SUCCESS != loader_get_manifest_files(inst, NULL, NULL, true, false, DEFAULT_VK_ILAYERS_INFO, RELATIVE_VK_ILAYERS_INFO,
                                                &manifest_files[1])) {
        goto out;
    }

    // Make sure we have at least one layer, if not, go ahead and return
    if (manifest_files[0].count == 0 && manifest_files[1].count == 0) {
        goto out;
    }

    // cleanup any previously scanned libraries
    loader_delete_layer_properties(inst, instance_layers);

    loader_platform_thread_lock_mutex(&loader_json_lock);
    lockedMutex = true;
    for (implicit = 0; implicit < 2; implicit++) {
        for (uint32_t i = 0; i < manifest_files[implicit].count; i++) {
            file_str = manifest_files[implicit].filename_list[i];
            if (file_str == NULL) continue;

            // parse file into JSON struct
            VkResult res = loader_get_json(inst, file_str, &json);
            if (VK_ERROR_OUT_OF_HOST_MEMORY == res) {
                break;
            } else if (VK_SUCCESS != res || NULL == json) {
                continue;
            }

            VkResult local_res = loader_add_layer_properties(inst, instance_layers, json, (implicit == 1), file_str);
            cJSON_Delete(json);

            // If the error is anything other than out of memory we still want to try to load the other layers
            if (VK_ERROR_OUT_OF_HOST_MEMORY == local_res) {
                goto out;
            }
        }
    }

    // See if "VK_LAYER_LUNARG_standard_validation" already in list.
    bool found_std_val = false;
    for (uint32_t i = 0; i < instance_layers->count; i++) {
        struct loader_layer_properties *props = &instance_layers->list[i];
        if (strcmp(props->info.layerName, std_validation_str) == 0) {
            found_std_val = true;
            break;
        }
    }

    // If we didn't find the VK_LAYER_LUNARG_standard_validation meta-layer in
    // the list, then we need to add it manually.  This is likely because we're
    // dealing with a new loader, but an old layer folder.
    if (!found_std_val && !loader_add_legacy_std_val_layer(inst, instance_layers)) {
        goto out;
    }

    // Verify any meta-layers in the list are valid and all the component layers are
    // actually present in the available layer list
    verify_all_meta_layers(inst, instance_layers);

out:

    for (uint32_t manFile = 0; manFile < 2; manFile++) {
        if (NULL != manifest_files[manFile].filename_list) {
            for (uint32_t i = 0; i < manifest_files[manFile].count; i++) {
                if (NULL != manifest_files[manFile].filename_list[i]) {
                    loader_instance_heap_free(inst, manifest_files[manFile].filename_list[i]);
                }
            }
            loader_instance_heap_free(inst, manifest_files[manFile].filename_list);
        }
    }
    if (lockedMutex) {
        loader_platform_thread_unlock_mutex(&loader_json_lock);
    }
}

void loader_implicit_layer_scan(const struct loader_instance *inst, struct loader_layer_list *instance_layers) {
    char *file_str;
    struct loader_manifest_files manifest_files;
    cJSON *json;
    uint32_t i;

    // Pass NULL for environment variable override - implicit layers are not
    // overridden by LAYERS_PATH_ENV
    VkResult res = loader_get_manifest_files(inst, NULL, NULL, true, false, DEFAULT_VK_ILAYERS_INFO, RELATIVE_VK_ILAYERS_INFO,
                                             &manifest_files);
    if (VK_SUCCESS != res || manifest_files.count == 0) {
        return;
    }

    // Cleanup any previously scanned libraries
    loader_delete_layer_properties(inst, instance_layers);

    loader_platform_thread_lock_mutex(&loader_json_lock);

    for (i = 0; i < manifest_files.count; i++) {
        file_str = manifest_files.filename_list[i];
        if (file_str == NULL) {
            continue;
        }

        // parse file into JSON struct
        res = loader_get_json(inst, file_str, &json);
        if (VK_ERROR_OUT_OF_HOST_MEMORY == res) {
            break;
        } else if (VK_SUCCESS != res || NULL == json) {
            continue;
        }

        res = loader_add_layer_properties(inst, instance_layers, json, true, file_str);

        loader_instance_heap_free(inst, file_str);
        cJSON_Delete(json);

        if (VK_ERROR_OUT_OF_HOST_MEMORY == res) {
            break;
        }
    }
    loader_instance_heap_free(inst, manifest_files.filename_list);
    loader_platform_thread_unlock_mutex(&loader_json_lock);
}

// Check if an implicit layer should be enabled.
bool loader_is_implicit_layer_enabled(const struct loader_instance *inst, const struct loader_layer_properties *prop) {
    bool enable = false;
    char *env_value = NULL;

    // if no enable_environment variable is specified, this implicit layer
    // should always be enabled. Otherwise check if the variable is set
    if (prop->enable_env_var.name[0] == 0) {
        enable = true;
    } else {
        env_value = loader_secure_getenv(prop->enable_env_var.name, inst);
        if (env_value && !strcmp(prop->enable_env_var.value, env_value)) enable = true;
        loader_free_getenv(env_value, inst);
    }

    // disable_environment has priority, i.e. if both enable and disable
    // environment variables are set, the layer is disabled. Implicit
    // layers are required to have a disable_environment variables
    env_value = loader_secure_getenv(prop->disable_env_var.name, inst);
    if (env_value && !strcmp(prop->disable_env_var.value, env_value)) enable = false;
    loader_free_getenv(env_value, inst);

    return enable;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL loader_gpdpa_instance_internal(VkInstance inst, const char *pName) {
    // inst is not wrapped
    if (inst == VK_NULL_HANDLE) {
        return NULL;
    }
    VkLayerInstanceDispatchTable *disp_table = *(VkLayerInstanceDispatchTable **)inst;
    void *addr;

    if (disp_table == NULL) return NULL;

    bool found_name;
    addr = loader_lookup_instance_dispatch_table(disp_table, pName, &found_name);
    if (found_name) {
        return addr;
    }

    if (loader_phys_dev_ext_gpa(loader_get_instance(inst), pName, true, NULL, &addr)) return addr;

    // Don't call down the chain, this would be an infinite loop
    loader_log(NULL, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0, "loader_gpdpa_instance_internal() unrecognized name %s", pName);
    return NULL;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL loader_gpdpa_instance_terminator(VkInstance inst, const char *pName) {
    // inst is not wrapped
    if (inst == VK_NULL_HANDLE) {
        return NULL;
    }
    VkLayerInstanceDispatchTable *disp_table = *(VkLayerInstanceDispatchTable **)inst;
    void *addr;

    if (disp_table == NULL) return NULL;

    bool found_name;
    addr = loader_lookup_instance_dispatch_table(disp_table, pName, &found_name);
    if (found_name) {
        return addr;
    }

    // Get the terminator, but don't perform checking since it should already
    // have been setup if we get here.
    if (loader_phys_dev_ext_gpa(loader_get_instance(inst), pName, false, NULL, &addr)) {
        return addr;
    }

    // Don't call down the chain, this would be an infinite loop
    loader_log(NULL, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0, "loader_gpdpa_instance_terminator() unrecognized name %s", pName);
    return NULL;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL loader_gpa_instance_internal(VkInstance inst, const char *pName) {
    if (!strcmp(pName, "vkGetInstanceProcAddr")) {
        return (PFN_vkVoidFunction)loader_gpa_instance_internal;
    }
    if (!strcmp(pName, "vk_layerGetPhysicalDeviceProcAddr")) {
        return (PFN_vkVoidFunction)loader_gpdpa_instance_terminator;
    }
    if (!strcmp(pName, "vkCreateInstance")) {
        return (PFN_vkVoidFunction)terminator_CreateInstance;
    }
    if (!strcmp(pName, "vkCreateDevice")) {
        return (PFN_vkVoidFunction)terminator_CreateDevice;
    }

    // inst is not wrapped
    if (inst == VK_NULL_HANDLE) {
        return NULL;
    }
    VkLayerInstanceDispatchTable *disp_table = *(VkLayerInstanceDispatchTable **)inst;
    void *addr;

    if (disp_table == NULL) return NULL;

    bool found_name;
    addr = loader_lookup_instance_dispatch_table(disp_table, pName, &found_name);
    if (found_name) {
        return addr;
    }

    // Don't call down the chain, this would be an infinite loop
    loader_log(NULL, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0, "loader_gpa_instance_internal() unrecognized name %s", pName);
    return NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL loader_gpa_device_internal(VkDevice device, const char *pName) {
    struct loader_device *dev;
    struct loader_icd_term *icd_term = loader_get_icd_and_device(device, &dev, NULL);

    // Return this function if a layer above here is asking for the vkGetDeviceProcAddr.
    // This is so we can properly intercept any device commands needing a terminator.
    if (!strcmp(pName, "vkGetDeviceProcAddr")) {
        return (PFN_vkVoidFunction)loader_gpa_device_internal;
    }

    // NOTE: Device Funcs needing Trampoline/Terminator.
    // Overrides for device functions needing a trampoline and
    // a terminator because certain device entry-points still need to go
    // through a terminator before hitting the ICD.  This could be for
    // several reasons, but the main one is currently unwrapping an
    // object before passing the appropriate info along to the ICD.
    // This is why we also have to override the direct ICD call to
    // vkGetDeviceProcAddr to intercept those calls.
    PFN_vkVoidFunction addr = get_extension_device_proc_terminator(pName);
    if (NULL != addr) {
        return addr;
    }

    return icd_term->dispatch.GetDeviceProcAddr(device, pName);
}

// Initialize device_ext dispatch table entry as follows:
// If dev == NULL find all logical devices created within this instance and
//  init the entry (given by idx) in the ext dispatch table.
// If dev != NULL only initialize the entry in the given dev's dispatch table.
// The initialization value is gotten by calling down the device chain with
// GDPA.
// If GDPA returns NULL then don't initialize the dispatch table entry.
static void loader_init_dispatch_dev_ext_entry(struct loader_instance *inst, struct loader_device *dev, uint32_t idx,
                                               const char *funcName)

{
    void *gdpa_value;
    if (dev != NULL) {
        gdpa_value = dev->loader_dispatch.core_dispatch.GetDeviceProcAddr(dev->chain_device, funcName);
        if (gdpa_value != NULL) dev->loader_dispatch.ext_dispatch.dev_ext[idx] = (PFN_vkDevExt)gdpa_value;
    } else {
        for (struct loader_icd_term *icd_term = inst->icd_terms; icd_term != NULL; icd_term = icd_term->next) {
            struct loader_device *ldev = icd_term->logical_device_list;
            while (ldev) {
                gdpa_value = ldev->loader_dispatch.core_dispatch.GetDeviceProcAddr(ldev->chain_device, funcName);
                if (gdpa_value != NULL) ldev->loader_dispatch.ext_dispatch.dev_ext[idx] = (PFN_vkDevExt)gdpa_value;
                ldev = ldev->next;
            }
        }
    }
}

// Find all dev extension in the hash table  and initialize the dispatch table
// for dev  for each of those extension entrypoints found in hash table.
void loader_init_dispatch_dev_ext(struct loader_instance *inst, struct loader_device *dev) {
    for (uint32_t i = 0; i < MAX_NUM_UNKNOWN_EXTS; i++) {
        if (inst->dev_ext_disp_hash[i].func_name != NULL)
            loader_init_dispatch_dev_ext_entry(inst, dev, i, inst->dev_ext_disp_hash[i].func_name);
    }
}

static bool loader_check_icds_for_dev_ext_address(struct loader_instance *inst, const char *funcName) {
    struct loader_icd_term *icd_term;
    icd_term = inst->icd_terms;
    while (NULL != icd_term) {
        if (icd_term->scanned_icd->GetInstanceProcAddr(icd_term->instance, funcName))
            // this icd supports funcName
            return true;
        icd_term = icd_term->next;
    }

    return false;
}

static bool loader_check_layer_list_for_dev_ext_address(const struct loader_layer_list *const layers, const char *funcName) {
    // Iterate over the layers.
    for (uint32_t layer = 0; layer < layers->count; ++layer) {
        // Iterate over the extensions.
        const struct loader_device_extension_list *const extensions = &(layers->list[layer].device_extension_list);
        for (uint32_t extension = 0; extension < extensions->count; ++extension) {
            // Iterate over the entry points.
            const struct loader_dev_ext_props *const property = &(extensions->list[extension]);
            for (uint32_t entry = 0; entry < property->entrypoint_count; ++entry) {
                if (strcmp(property->entrypoints[entry], funcName) == 0) {
                    return true;
                }
            }
        }
    }

    return false;
}

static void loader_free_dev_ext_table(struct loader_instance *inst) {
    for (uint32_t i = 0; i < MAX_NUM_UNKNOWN_EXTS; i++) {
        loader_instance_heap_free(inst, inst->dev_ext_disp_hash[i].func_name);
        loader_instance_heap_free(inst, inst->dev_ext_disp_hash[i].list.index);
    }
    memset(inst->dev_ext_disp_hash, 0, sizeof(inst->dev_ext_disp_hash));
}

static bool loader_add_dev_ext_table(struct loader_instance *inst, uint32_t *ptr_idx, const char *funcName) {
    uint32_t i;
    uint32_t idx = *ptr_idx;
    struct loader_dispatch_hash_list *list = &inst->dev_ext_disp_hash[idx].list;

    if (!inst->dev_ext_disp_hash[idx].func_name) {
        // no entry here at this idx, so use it
        assert(list->capacity == 0);
        inst->dev_ext_disp_hash[idx].func_name =
            (char *)loader_instance_heap_alloc(inst, strlen(funcName) + 1, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (inst->dev_ext_disp_hash[idx].func_name == NULL) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_add_dev_ext_table: Failed to allocate memory "
                       "for func_name %s",
                       funcName);
            return false;
        }
        strncpy(inst->dev_ext_disp_hash[idx].func_name, funcName, strlen(funcName) + 1);
        return true;
    }

    // check for enough capacity
    if (list->capacity == 0) {
        list->index = loader_instance_heap_alloc(inst, 8 * sizeof(*(list->index)), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (list->index == NULL) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0, "loader_add_dev_ext_table: Failed to allocate memory for list index",
                       funcName);
            return false;
        }
        list->capacity = 8 * sizeof(*(list->index));
    } else if (list->capacity < (list->count + 1) * sizeof(*(list->index))) {
        void *new_ptr = loader_instance_heap_realloc(inst, list->index, list->capacity, list->capacity * 2,
                                                     VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (NULL == new_ptr) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_add_dev_ext_table: Failed to reallocate memory for list index", funcName);
            return false;
        }
        list->index = new_ptr;
        list->capacity *= 2;
    }

    // find an unused index in the hash table and use it
    i = (idx + 1) % MAX_NUM_UNKNOWN_EXTS;
    do {
        if (!inst->dev_ext_disp_hash[i].func_name) {
            assert(inst->dev_ext_disp_hash[i].list.capacity == 0);
            inst->dev_ext_disp_hash[i].func_name =
                (char *)loader_instance_heap_alloc(inst, strlen(funcName) + 1, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
            if (inst->dev_ext_disp_hash[i].func_name == NULL) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "loader_add_dev_ext_table: Failed to allocate memory "
                           "for func_name %s",
                           funcName);
                return false;
            }
            strncpy(inst->dev_ext_disp_hash[i].func_name, funcName, strlen(funcName) + 1);
            list->index[list->count] = i;
            list->count++;
            *ptr_idx = i;
            return true;
        }
        i = (i + 1) % MAX_NUM_UNKNOWN_EXTS;
    } while (i != idx);

    loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
               "loader_add_dev_ext_table:  Could not insert into hash table; is "
               "it full?");

    return false;
}

static bool loader_name_in_dev_ext_table(struct loader_instance *inst, uint32_t *idx, const char *funcName) {
    uint32_t alt_idx;
    if (inst->dev_ext_disp_hash[*idx].func_name && !strcmp(inst->dev_ext_disp_hash[*idx].func_name, funcName)) return true;

    // funcName wasn't at the primary spot in the hash table
    // search the list of secondary locations (shallow search, not deep search)
    for (uint32_t i = 0; i < inst->dev_ext_disp_hash[*idx].list.count; i++) {
        alt_idx = inst->dev_ext_disp_hash[*idx].list.index[i];
        if (!strcmp(inst->dev_ext_disp_hash[*idx].func_name, funcName)) {
            *idx = alt_idx;
            return true;
        }
    }

    return false;
}

// This function returns generic trampoline code address for unknown entry
// points.
// Presumably, these unknown entry points (as given by funcName) are device
// extension entrypoints.  A hash table is used to keep a list of unknown entry
// points and their mapping to the device extension dispatch table
// (struct loader_dev_ext_dispatch_table).
// \returns
// For a given entry point string (funcName), if an existing mapping is found
// the
// trampoline address for that mapping is returned. Otherwise, this unknown
// entry point
// has not been seen yet. Next check if a layer or ICD supports it.  If so then
// a
// new entry in the hash table is initialized and that trampoline address for
// the new entry is returned. Null is returned if the hash table is full or
// if no discovered layer or ICD returns a non-NULL GetProcAddr for it.
void *loader_dev_ext_gpa(struct loader_instance *inst, const char *funcName) {
    uint32_t idx;
    uint32_t seed = 0;

    idx = murmurhash(funcName, strlen(funcName), seed) % MAX_NUM_UNKNOWN_EXTS;

    if (loader_name_in_dev_ext_table(inst, &idx, funcName))
        // found funcName already in hash
        return loader_get_dev_ext_trampoline(idx);

    // Check if funcName is supported in either ICDs or a layer library
    if (!loader_check_icds_for_dev_ext_address(inst, funcName) &&
        !loader_check_layer_list_for_dev_ext_address(&inst->app_activated_layer_list, funcName)) {
        // if support found in layers continue on
        return NULL;
    }

    if (loader_add_dev_ext_table(inst, &idx, funcName)) {
        // successfully added new table entry
        // init any dev dispatch table entries as needed
        loader_init_dispatch_dev_ext_entry(inst, NULL, idx, funcName);
        return loader_get_dev_ext_trampoline(idx);
    }

    return NULL;
}

static bool loader_check_icds_for_phys_dev_ext_address(struct loader_instance *inst, const char *funcName) {
    struct loader_icd_term *icd_term;
    icd_term = inst->icd_terms;
    while (NULL != icd_term) {
        if (icd_term->scanned_icd->interface_version >= MIN_PHYS_DEV_EXTENSION_ICD_INTERFACE_VERSION &&
            icd_term->scanned_icd->GetPhysicalDeviceProcAddr(icd_term->instance, funcName))
            // this icd supports funcName
            return true;
        icd_term = icd_term->next;
    }

    return false;
}

static bool loader_check_layer_list_for_phys_dev_ext_address(struct loader_instance *inst, const char *funcName) {
    struct loader_layer_properties *layer_prop_list = inst->expanded_activated_layer_list.list;
    for (uint32_t layer = 0; layer < inst->expanded_activated_layer_list.count; ++layer) {
        // If this layer supports the vk_layerGetPhysicalDeviceProcAddr, then call
        // it and see if it returns a valid pointer for this function name.
        if (layer_prop_list[layer].interface_version > 1) {
            const struct loader_layer_functions *const functions = &(layer_prop_list[layer].functions);
            if (NULL != functions->get_physical_device_proc_addr &&
                NULL != functions->get_physical_device_proc_addr((VkInstance)inst->instance, funcName)) {
                return true;
            }
        }
    }

    return false;
}

static void loader_free_phys_dev_ext_table(struct loader_instance *inst) {
    for (uint32_t i = 0; i < MAX_NUM_UNKNOWN_EXTS; i++) {
        loader_instance_heap_free(inst, inst->phys_dev_ext_disp_hash[i].func_name);
        loader_instance_heap_free(inst, inst->phys_dev_ext_disp_hash[i].list.index);
    }
    memset(inst->phys_dev_ext_disp_hash, 0, sizeof(inst->phys_dev_ext_disp_hash));
}

static bool loader_add_phys_dev_ext_table(struct loader_instance *inst, uint32_t *ptr_idx, const char *funcName) {
    uint32_t i;
    uint32_t idx = *ptr_idx;
    struct loader_dispatch_hash_list *list = &inst->phys_dev_ext_disp_hash[idx].list;

    if (!inst->phys_dev_ext_disp_hash[idx].func_name) {
        // no entry here at this idx, so use it
        assert(list->capacity == 0);
        inst->phys_dev_ext_disp_hash[idx].func_name =
            (char *)loader_instance_heap_alloc(inst, strlen(funcName) + 1, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (inst->phys_dev_ext_disp_hash[idx].func_name == NULL) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_add_phys_dev_ext_table() can't allocate memory for "
                       "func_name");
            return false;
        }
        strncpy(inst->phys_dev_ext_disp_hash[idx].func_name, funcName, strlen(funcName) + 1);
        return true;
    }

    // check for enough capacity
    if (list->capacity == 0) {
        list->index = loader_instance_heap_alloc(inst, 8 * sizeof(*(list->index)), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (list->index == NULL) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0, "loader_add_phys_dev_ext_table() can't allocate list memory");
            return false;
        }
        list->capacity = 8 * sizeof(*(list->index));
    } else if (list->capacity < (list->count + 1) * sizeof(*(list->index))) {
        void *new_ptr = loader_instance_heap_realloc(inst, list->index, list->capacity, list->capacity * 2,
                                                     VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (NULL == new_ptr) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0, "loader_add_phys_dev_ext_table() can't reallocate list memory");
            return false;
        }
        list->index = new_ptr;
        list->capacity *= 2;
    }

    // find an unused index in the hash table and use it
    i = (idx + 1) % MAX_NUM_UNKNOWN_EXTS;
    do {
        if (!inst->phys_dev_ext_disp_hash[i].func_name) {
            assert(inst->phys_dev_ext_disp_hash[i].list.capacity == 0);
            inst->phys_dev_ext_disp_hash[i].func_name =
                (char *)loader_instance_heap_alloc(inst, strlen(funcName) + 1, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
            if (inst->phys_dev_ext_disp_hash[i].func_name == NULL) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "loader_add_dev_ext_table() can't reallocate "
                           "func_name memory");
                return false;
            }
            strncpy(inst->phys_dev_ext_disp_hash[i].func_name, funcName, strlen(funcName) + 1);
            list->index[list->count] = i;
            list->count++;
            *ptr_idx = i;
            return true;
        }
        i = (i + 1) % MAX_NUM_UNKNOWN_EXTS;
    } while (i != idx);

    loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
               "loader_add_phys_dev_ext_table() couldn't insert into hash table; is "
               "it full?");
    return false;
}

static bool loader_name_in_phys_dev_ext_table(struct loader_instance *inst, uint32_t *idx, const char *funcName) {
    uint32_t alt_idx;
    if (inst->phys_dev_ext_disp_hash[*idx].func_name && !strcmp(inst->phys_dev_ext_disp_hash[*idx].func_name, funcName))
        return true;

    // funcName wasn't at the primary spot in the hash table
    // search the list of secondary locations (shallow search, not deep search)
    for (uint32_t i = 0; i < inst->phys_dev_ext_disp_hash[*idx].list.count; i++) {
        alt_idx = inst->phys_dev_ext_disp_hash[*idx].list.index[i];
        if (!strcmp(inst->phys_dev_ext_disp_hash[*idx].func_name, funcName)) {
            *idx = alt_idx;
            return true;
        }
    }

    return false;
}

// This function returns a generic trampoline and/or terminator function
// address for any unknown physical device extension commands.  A hash
// table is used to keep a list of unknown entry points and their
// mapping to the physical device extension dispatch table (struct
// loader_phys_dev_ext_dispatch_table).
// For a given entry point string (funcName), if an existing mapping is
// found, then the trampoline address for that mapping is returned in
// tramp_addr (if it is not NULL) and the terminator address for that
// mapping is returned in term_addr (if it is not NULL). Otherwise,
// this unknown entry point has not been seen yet.
// If it has not been seen before, and perform_checking is 'true',
// check if a layer or and ICD supports it.  If so then a new entry in
// the hash table is initialized and the trampoline and/or terminator
// addresses are returned.
// Null is returned if the hash table is full or if no discovered layer or
// ICD returns a non-NULL GetProcAddr for it.
bool loader_phys_dev_ext_gpa(struct loader_instance *inst, const char *funcName, bool perform_checking, void **tramp_addr,
                             void **term_addr) {
    uint32_t idx;
    uint32_t seed = 0;
    bool success = false;

    if (inst == NULL) {
        goto out;
    }

    if (NULL != tramp_addr) {
        *tramp_addr = NULL;
    }
    if (NULL != term_addr) {
        *term_addr = NULL;
    }

    // We should always check to see if any ICD supports it.
    if (!loader_check_icds_for_phys_dev_ext_address(inst, funcName)) {
        // If we're not checking layers, or we are and it's not in a layer, just
        // return
        if (!perform_checking || !loader_check_layer_list_for_phys_dev_ext_address(inst, funcName)) {
            goto out;
        }
    }

    idx = murmurhash(funcName, strlen(funcName), seed) % MAX_NUM_UNKNOWN_EXTS;
    if (perform_checking && !loader_name_in_phys_dev_ext_table(inst, &idx, funcName)) {
        uint32_t i;
        bool added = false;

        // Only need to add first one to get index in Instance.  Others will use
        // the same index.
        if (!added && loader_add_phys_dev_ext_table(inst, &idx, funcName)) {
            added = true;
        }

        // Setup the ICD function pointers
        struct loader_icd_term *icd_term = inst->icd_terms;
        while (NULL != icd_term) {
            if (MIN_PHYS_DEV_EXTENSION_ICD_INTERFACE_VERSION <= icd_term->scanned_icd->interface_version &&
                NULL != icd_term->scanned_icd->GetPhysicalDeviceProcAddr) {
                icd_term->phys_dev_ext[idx] =
                    (PFN_PhysDevExt)icd_term->scanned_icd->GetPhysicalDeviceProcAddr(icd_term->instance, funcName);

                // Make sure we set the instance dispatch to point to the
                // loader's terminator now since we can at least handle it
                // in one ICD.
                inst->disp->phys_dev_ext[idx] = loader_get_phys_dev_ext_termin(idx);
            } else {
                icd_term->phys_dev_ext[idx] = NULL;
            }

            icd_term = icd_term->next;
        }

        // Now, search for the first layer attached and query using it to get
        // the first entry point.
        for (i = 0; i < inst->expanded_activated_layer_list.count; i++) {
            struct loader_layer_properties *layer_prop = &inst->expanded_activated_layer_list.list[i];
            if (layer_prop->interface_version > 1 && NULL != layer_prop->functions.get_physical_device_proc_addr) {
                inst->disp->phys_dev_ext[idx] =
                    (PFN_PhysDevExt)layer_prop->functions.get_physical_device_proc_addr((VkInstance)inst->instance, funcName);
                if (NULL != inst->disp->phys_dev_ext[idx]) {
                    break;
                }
            }
        }
    }

    if (NULL != tramp_addr) {
        *tramp_addr = loader_get_phys_dev_ext_tramp(idx);
    }

    if (NULL != term_addr) {
        *term_addr = loader_get_phys_dev_ext_termin(idx);
    }

    success = true;

out:
    return success;
}

struct loader_instance *loader_get_instance(const VkInstance instance) {
    // look up the loader_instance in our list by comparing dispatch tables, as
    // there is no guarantee the instance is still a loader_instance* after any
    // layers which wrap the instance object.
    const VkLayerInstanceDispatchTable *disp;
    struct loader_instance *ptr_instance = NULL;
    disp = loader_get_instance_layer_dispatch(instance);
    for (struct loader_instance *inst = loader.instances; inst; inst = inst->next) {
        if (&inst->disp->layer_inst_disp == disp) {
            ptr_instance = inst;
            break;
        }
    }
    return ptr_instance;
}

static loader_platform_dl_handle loader_open_layer_lib(const struct loader_instance *inst, const char *chain_type,
                                                       struct loader_layer_properties *prop) {
    if ((prop->lib_handle = loader_platform_open_library(prop->lib_name)) == NULL) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0, loader_platform_open_library_error(prop->lib_name));
    } else {
        loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0, "Loading layer library %s", prop->lib_name);
    }

    return prop->lib_handle;
}

static void loader_close_layer_lib(const struct loader_instance *inst, struct loader_layer_properties *prop) {
    if (prop->lib_handle) {
        loader_platform_close_library(prop->lib_handle);
        loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0, "Unloading layer library %s", prop->lib_name);
        prop->lib_handle = NULL;
    }
}

void loader_deactivate_layers(const struct loader_instance *instance, struct loader_device *device,
                              struct loader_layer_list *list) {
    // Delete instance list of enabled layers and close any layer libraries
    for (uint32_t i = 0; i < list->count; i++) {
        struct loader_layer_properties *layer_prop = &list->list[i];

        loader_close_layer_lib(instance, layer_prop);
    }
    loader_destroy_layer_list(instance, device, list);
}

// Go through the search_list and find any layers which match type. If layer
// type match is found in then add it to ext_list.
static void loader_add_implicit_layers(const struct loader_instance *inst, struct loader_layer_list *target_list,
                                       struct loader_layer_list *expanded_target_list,
                                       const struct loader_layer_list *source_list) {
    for (uint32_t src_layer = 0; src_layer < source_list->count; src_layer++) {
        const struct loader_layer_properties *prop = &source_list->list[src_layer];
        if (0 == (prop->type_flags & VK_LAYER_TYPE_FLAG_EXPLICIT_LAYER)) {
            loader_add_implicit_layer(inst, prop, target_list, expanded_target_list, source_list);
        }
    }
}

// Get the layer name(s) from the env_name environment variable. If layer is found in
// search_list then add it to layer_list.  But only add it to layer_list if type_flags matches.
static void loader_add_env_layers(const struct loader_instance *inst, const enum layer_type_flags type_flags, const char *env_name,
                                  struct loader_layer_list *target_list, struct loader_layer_list *expanded_target_list,
                                  const struct loader_layer_list *source_list) {
    char *next, *name;
    char *layer_env = loader_secure_getenv(env_name, inst);
    if (layer_env == NULL) {
        goto out;
    }
    name = loader_stack_alloc(strlen(layer_env) + 1);
    if (name == NULL) {
        goto out;
    }
    strcpy(name, layer_env);

    while (name && *name) {
        next = loader_get_next_path(name);
        loader_find_layer_name_add_list(inst, name, type_flags, source_list, target_list, expanded_target_list);
        name = next;
    }

out:

    if (layer_env != NULL) {
        loader_free_getenv(layer_env, inst);
    }

    return;
}

VkResult loader_enable_instance_layers(struct loader_instance *inst, const VkInstanceCreateInfo *pCreateInfo,
                                       const struct loader_layer_list *instance_layers) {
    VkResult err;

    assert(inst && "Cannot have null instance");

    if (!loader_init_layer_list(inst, &inst->app_activated_layer_list)) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_enable_instance_layers: Failed to initialize"
                   " application version of the layer list");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    if (!loader_init_layer_list(inst, &inst->expanded_activated_layer_list)) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_enable_instance_layers: Failed to initialize"
                   " expanded version of the layer list");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    // Add any implicit layers first
    loader_add_implicit_layers(inst, &inst->app_activated_layer_list, &inst->expanded_activated_layer_list, instance_layers);

    // Add any layers specified via environment variable next
    loader_add_env_layers(inst, VK_LAYER_TYPE_FLAG_EXPLICIT_LAYER, ENABLED_LAYERS_ENV, &inst->app_activated_layer_list,
                          &inst->expanded_activated_layer_list, instance_layers);

    // Add layers specified by the application
    err = loader_add_layer_names_to_list(inst, &inst->app_activated_layer_list, &inst->expanded_activated_layer_list,
                                         pCreateInfo->enabledLayerCount, pCreateInfo->ppEnabledLayerNames, instance_layers);

    return err;
}

// Determine the layer interface version to use.
bool loader_get_layer_interface_version(PFN_vkNegotiateLoaderLayerInterfaceVersion fp_negotiate_layer_version,
                                        VkNegotiateLayerInterface *interface_struct) {
    memset(interface_struct, 0, sizeof(VkNegotiateLayerInterface));
    interface_struct->sType = LAYER_NEGOTIATE_INTERFACE_STRUCT;
    interface_struct->loaderLayerInterfaceVersion = 1;

    if (fp_negotiate_layer_version != NULL) {
        // Layer supports the negotiation API, so call it with the loader's
        // latest version supported
        interface_struct->loaderLayerInterfaceVersion = CURRENT_LOADER_LAYER_INTERFACE_VERSION;
        VkResult result = fp_negotiate_layer_version(interface_struct);

        if (result != VK_SUCCESS) {
            // Layer no longer supports the loader's latest interface version so
            // fail loading the Layer
            return false;
        }
    }

    if (interface_struct->loaderLayerInterfaceVersion < MIN_SUPPORTED_LOADER_LAYER_INTERFACE_VERSION) {
        // Loader no longer supports the layer's latest interface version so
        // fail loading the layer
        return false;
    }

    return true;
}

// Given the list of layers to activate in the loader_instance
// structure. This function will add a VkLayerInstanceCreateInfo
// structure to the VkInstanceCreateInfo.pNext pointer.
// Each activated layer will have it's own VkLayerInstanceLink
// structure that tells the layer what Get*ProcAddr to call to
// get function pointers to the next layer down.
// Once the chain info has been created this function will
// execute the CreateInstance call chain. Each layer will
// then have an opportunity in it's CreateInstance function
// to setup it's dispatch table when the lower layer returns
// successfully.
// Each layer can wrap or not-wrap the returned VkInstance object
// as it sees fit.
// The instance chain is terminated by a loader function
// that will call CreateInstance on all available ICD's and
// cache those VkInstance objects for future use.
VkResult loader_create_instance_chain(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                                      struct loader_instance *inst, VkInstance *created_instance) {
    uint32_t activated_layers = 0;
    VkLayerInstanceCreateInfo chain_info;
    VkLayerInstanceLink *layer_instance_link_info = NULL;
    VkInstanceCreateInfo loader_create_info;
    VkResult res;

    PFN_vkGetInstanceProcAddr next_gipa = loader_gpa_instance_internal;
    PFN_vkGetInstanceProcAddr cur_gipa = loader_gpa_instance_internal;
    PFN_GetPhysicalDeviceProcAddr next_gpdpa = loader_gpdpa_instance_internal;
    PFN_GetPhysicalDeviceProcAddr cur_gpdpa = loader_gpdpa_instance_internal;

    memcpy(&loader_create_info, pCreateInfo, sizeof(VkInstanceCreateInfo));

    if (inst->expanded_activated_layer_list.count > 0) {
        chain_info.u.pLayerInfo = NULL;
        chain_info.pNext = pCreateInfo->pNext;
        chain_info.sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO;
        chain_info.function = VK_LAYER_LINK_INFO;
        loader_create_info.pNext = &chain_info;

        layer_instance_link_info = loader_stack_alloc(sizeof(VkLayerInstanceLink) * inst->expanded_activated_layer_list.count);
        if (!layer_instance_link_info) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_create_instance_chain: Failed to alloc Instance"
                       " objects for layer");
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }

        // Create instance chain of enabled layers
        for (int32_t i = inst->expanded_activated_layer_list.count - 1; i >= 0; i--) {
            struct loader_layer_properties *layer_prop = &inst->expanded_activated_layer_list.list[i];
            loader_platform_dl_handle lib_handle;

            lib_handle = loader_open_layer_lib(inst, "instance", layer_prop);
            if (!lib_handle) {
                continue;
            }

            if (NULL == layer_prop->functions.negotiate_layer_interface) {
                PFN_vkNegotiateLoaderLayerInterfaceVersion negotiate_interface = NULL;
                bool functions_in_interface = false;
                if (strlen(layer_prop->functions.str_negotiate_interface) == 0) {
                    negotiate_interface = (PFN_vkNegotiateLoaderLayerInterfaceVersion)loader_platform_get_proc_address(
                        lib_handle, "vkNegotiateLoaderLayerInterfaceVersion");
                } else {
                    negotiate_interface = (PFN_vkNegotiateLoaderLayerInterfaceVersion)loader_platform_get_proc_address(
                        lib_handle, layer_prop->functions.str_negotiate_interface);
                }

                // If we can negotiate an interface version, then we can also
                // get everything we need from the one function call, so try
                // that first, and see if we can get all the function pointers
                // necessary from that one call.
                if (NULL != negotiate_interface) {
                    layer_prop->functions.negotiate_layer_interface = negotiate_interface;

                    VkNegotiateLayerInterface interface_struct;

                    if (loader_get_layer_interface_version(negotiate_interface, &interface_struct)) {
                        // Go ahead and set the properties version to the
                        // correct value.
                        layer_prop->interface_version = interface_struct.loaderLayerInterfaceVersion;

                        // If the interface is 2 or newer, we have access to the
                        // new GetPhysicalDeviceProcAddr function, so grab it,
                        // and the other necessary functions, from the
                        // structure.
                        if (interface_struct.loaderLayerInterfaceVersion > 1) {
                            cur_gipa = interface_struct.pfnGetInstanceProcAddr;
                            cur_gpdpa = interface_struct.pfnGetPhysicalDeviceProcAddr;
                            if (cur_gipa != NULL) {
                                // We've set the functions, so make sure we
                                // don't do the unnecessary calls later.
                                functions_in_interface = true;
                            }
                        }
                    }
                }

                if (!functions_in_interface) {
                    if ((cur_gipa = layer_prop->functions.get_instance_proc_addr) == NULL) {
                        if (strlen(layer_prop->functions.str_gipa) == 0) {
                            cur_gipa =
                                (PFN_vkGetInstanceProcAddr)loader_platform_get_proc_address(lib_handle, "vkGetInstanceProcAddr");
                            layer_prop->functions.get_instance_proc_addr = cur_gipa;
                        } else {
                            cur_gipa = (PFN_vkGetInstanceProcAddr)loader_platform_get_proc_address(lib_handle,
                                                                                                   layer_prop->functions.str_gipa);
                        }

                        if (NULL == cur_gipa) {
                            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                                       "loader_create_instance_chain: Failed to"
                                       " find \'vkGetInstanceProcAddr\' in "
                                       "layer %s",
                                       layer_prop->lib_name);
                            continue;
                        }
                    }
                }
            }

            layer_instance_link_info[activated_layers].pNext = chain_info.u.pLayerInfo;
            layer_instance_link_info[activated_layers].pfnNextGetInstanceProcAddr = next_gipa;
            layer_instance_link_info[activated_layers].pfnNextGetPhysicalDeviceProcAddr = next_gpdpa;
            next_gipa = cur_gipa;
            if (layer_prop->interface_version > 1 && cur_gpdpa != NULL) {
                layer_prop->functions.get_physical_device_proc_addr = cur_gpdpa;
                next_gpdpa = cur_gpdpa;
            }

            chain_info.u.pLayerInfo = &layer_instance_link_info[activated_layers];

            loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0, "Insert instance layer %s (%s)", layer_prop->info.layerName,
                       layer_prop->lib_name);

            activated_layers++;
        }
    }

    PFN_vkCreateInstance fpCreateInstance = (PFN_vkCreateInstance)next_gipa(*created_instance, "vkCreateInstance");
    if (fpCreateInstance) {
        VkLayerInstanceCreateInfo create_info_disp;

        create_info_disp.sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO;
        create_info_disp.function = VK_LOADER_DATA_CALLBACK;

        create_info_disp.u.pfnSetInstanceLoaderData = vkSetInstanceDispatch;

        create_info_disp.pNext = loader_create_info.pNext;
        loader_create_info.pNext = &create_info_disp;
        res = fpCreateInstance(&loader_create_info, pAllocator, created_instance);
    } else {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_create_instance_chain: Failed to find "
                   "\'vkCreateInstance\'");
        // Couldn't find CreateInstance function!
        res = VK_ERROR_INITIALIZATION_FAILED;
    }

    if (res == VK_SUCCESS) {
        loader_init_instance_core_dispatch_table(&inst->disp->layer_inst_disp, next_gipa, *created_instance);
        inst->instance = *created_instance;
    }

    return res;
}

void loader_activate_instance_layer_extensions(struct loader_instance *inst, VkInstance created_inst) {
    loader_init_instance_extension_dispatch_table(&inst->disp->layer_inst_disp, inst->disp->layer_inst_disp.GetInstanceProcAddr,
                                                  created_inst);
}

VkResult loader_create_device_chain(const struct loader_physical_device_tramp *pd, const VkDeviceCreateInfo *pCreateInfo,
                                    const VkAllocationCallbacks *pAllocator, const struct loader_instance *inst,
                                    struct loader_device *dev) {
    uint32_t activated_layers = 0;
    VkLayerDeviceLink *layer_device_link_info;
    VkLayerDeviceCreateInfo chain_info;
    VkDeviceCreateInfo loader_create_info;
    VkResult res;

    PFN_vkGetDeviceProcAddr fpGDPA = NULL, nextGDPA = loader_gpa_device_internal;
    PFN_vkGetInstanceProcAddr fpGIPA = NULL, nextGIPA = loader_gpa_instance_internal;

    memcpy(&loader_create_info, pCreateInfo, sizeof(VkDeviceCreateInfo));

    // Before we continue, we need to find out if the KHR_device_group extension is in the enabled list.  If it is, we then
    // need to look for the corresponding VkDeviceGroupDeviceCreateInfoKHR struct in the device list.  This is because we
    // need to replace all the incoming physical device values (which are really loader trampoline physical device values)
    // with the layer/ICD version.
    {
        struct VkStructureHeader *pNext = (struct VkStructureHeader *)loader_create_info.pNext;
        struct VkStructureHeader *pPrev = (struct VkStructureHeader *)&loader_create_info;
        while (NULL != pNext) {
            if (VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO == pNext->sType) {
                VkDeviceGroupDeviceCreateInfoKHR *cur_struct = (VkDeviceGroupDeviceCreateInfoKHR *)pNext;
                if (0 < cur_struct->physicalDeviceCount && NULL != cur_struct->pPhysicalDevices) {
                    VkDeviceGroupDeviceCreateInfoKHR *temp_struct = loader_stack_alloc(sizeof(VkDeviceGroupDeviceCreateInfoKHR));
                    VkPhysicalDevice *phys_dev_array = NULL;
                    if (NULL == temp_struct) {
                        return VK_ERROR_OUT_OF_HOST_MEMORY;
                    }
                    memcpy(temp_struct, cur_struct, sizeof(VkDeviceGroupDeviceCreateInfoKHR));
                    phys_dev_array = loader_stack_alloc(sizeof(VkPhysicalDevice) * cur_struct->physicalDeviceCount);
                    if (NULL == phys_dev_array) {
                        return VK_ERROR_OUT_OF_HOST_MEMORY;
                    }

                    // Before calling down, replace the incoming physical device values (which are really loader trampoline
                    // physical devices) with the next layer (or possibly even the terminator) physical device values.
                    struct loader_physical_device_tramp *cur_tramp;
                    for (uint32_t phys_dev = 0; phys_dev < cur_struct->physicalDeviceCount; phys_dev++) {
                        cur_tramp = (struct loader_physical_device_tramp *)cur_struct->pPhysicalDevices[phys_dev];
                        phys_dev_array[phys_dev] = cur_tramp->phys_dev;
                    }
                    temp_struct->pPhysicalDevices = phys_dev_array;

                    // Replace the old struct in the pNext chain with this one.
                    pPrev->pNext = (const void *)temp_struct;
                    pNext = (struct VkStructureHeader *)(temp_struct);
                }
                break;
            }

            pPrev = pNext;
            pNext = (struct VkStructureHeader *)(pPrev->pNext);
        }
    }

    layer_device_link_info = loader_stack_alloc(sizeof(VkLayerDeviceLink) * dev->expanded_activated_layer_list.count);
    if (!layer_device_link_info) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_create_device_chain: Failed to alloc Device objects"
                   " for layer.  Skipping Layer.");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    if (dev->expanded_activated_layer_list.count > 0) {
        chain_info.sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;
        chain_info.function = VK_LAYER_LINK_INFO;
        chain_info.u.pLayerInfo = NULL;
        chain_info.pNext = loader_create_info.pNext;
        loader_create_info.pNext = &chain_info;

        // Create instance chain of enabled layers
        for (int32_t i = dev->expanded_activated_layer_list.count - 1; i >= 0; i--) {
            struct loader_layer_properties *layer_prop = &dev->expanded_activated_layer_list.list[i];
            loader_platform_dl_handle lib_handle;
            bool functions_in_interface = false;

            lib_handle = loader_open_layer_lib(inst, "device", layer_prop);
            if (!lib_handle) {
                continue;
            }

            // If we can negotiate an interface version, then we can also get everything we need from the one function
            // call, so try that first, and see if we can get all the function pointers necessary from that one call.
            if (NULL == layer_prop->functions.negotiate_layer_interface) {
                PFN_vkNegotiateLoaderLayerInterfaceVersion negotiate_interface = NULL;
                if (strlen(layer_prop->functions.str_negotiate_interface) == 0) {
                    negotiate_interface = (PFN_vkNegotiateLoaderLayerInterfaceVersion)loader_platform_get_proc_address(
                        lib_handle, "vkNegotiateLoaderLayerInterfaceVersion");
                } else {
                    negotiate_interface = (PFN_vkNegotiateLoaderLayerInterfaceVersion)loader_platform_get_proc_address(
                        lib_handle, layer_prop->functions.str_negotiate_interface);
                }

                if (NULL != negotiate_interface) {
                    layer_prop->functions.negotiate_layer_interface = negotiate_interface;

                    VkNegotiateLayerInterface interface_struct;

                    if (loader_get_layer_interface_version(negotiate_interface, &interface_struct)) {
                        // Go ahead and set the properties version to the correct value.
                        layer_prop->interface_version = interface_struct.loaderLayerInterfaceVersion;

                        // If the interface is 2 or newer, we have access to the new GetPhysicalDeviceProcAddr
                        // function, so grab it, and the other necessary functions, from the structure.
                        if (interface_struct.loaderLayerInterfaceVersion > 1) {
                            fpGIPA = interface_struct.pfnGetInstanceProcAddr;
                            fpGDPA = interface_struct.pfnGetDeviceProcAddr;
                            if (fpGIPA != NULL && fpGDPA) {
                                // We've set the functions, so make sure we
                                // don't do the unnecessary calls later.
                                functions_in_interface = true;
                            }
                        }
                    }
                }
            }

            if (!functions_in_interface) {
                if ((fpGIPA = layer_prop->functions.get_instance_proc_addr) == NULL) {
                    if (strlen(layer_prop->functions.str_gipa) == 0) {
                        fpGIPA = (PFN_vkGetInstanceProcAddr)loader_platform_get_proc_address(lib_handle, "vkGetInstanceProcAddr");
                        layer_prop->functions.get_instance_proc_addr = fpGIPA;
                    } else
                        fpGIPA =
                            (PFN_vkGetInstanceProcAddr)loader_platform_get_proc_address(lib_handle, layer_prop->functions.str_gipa);
                    if (!fpGIPA) {
                        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                                   "loader_create_device_chain: Failed to find "
                                   "\'vkGetInstanceProcAddr\' in layer %s.  Skipping"
                                   " layer.",
                                   layer_prop->lib_name);
                        continue;
                    }
                }
                if ((fpGDPA = layer_prop->functions.get_device_proc_addr) == NULL) {
                    if (strlen(layer_prop->functions.str_gdpa) == 0) {
                        fpGDPA = (PFN_vkGetDeviceProcAddr)loader_platform_get_proc_address(lib_handle, "vkGetDeviceProcAddr");
                        layer_prop->functions.get_device_proc_addr = fpGDPA;
                    } else
                        fpGDPA =
                            (PFN_vkGetDeviceProcAddr)loader_platform_get_proc_address(lib_handle, layer_prop->functions.str_gdpa);
                    if (!fpGDPA) {
                        loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0, "Failed to find vkGetDeviceProcAddr in layer %s",
                                   layer_prop->lib_name);
                        continue;
                    }
                }
            }
            layer_device_link_info[activated_layers].pNext = chain_info.u.pLayerInfo;
            layer_device_link_info[activated_layers].pfnNextGetInstanceProcAddr = nextGIPA;
            layer_device_link_info[activated_layers].pfnNextGetDeviceProcAddr = nextGDPA;
            chain_info.u.pLayerInfo = &layer_device_link_info[activated_layers];
            nextGIPA = fpGIPA;
            nextGDPA = fpGDPA;

            loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0, "Inserted device layer %s (%s)", layer_prop->info.layerName,
                       layer_prop->lib_name);

            activated_layers++;
        }
    }

    VkDevice created_device = (VkDevice)dev;
    PFN_vkCreateDevice fpCreateDevice = (PFN_vkCreateDevice)nextGIPA(inst->instance, "vkCreateDevice");
    if (fpCreateDevice) {
        VkLayerDeviceCreateInfo create_info_disp;

        create_info_disp.sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;
        create_info_disp.function = VK_LOADER_DATA_CALLBACK;

        create_info_disp.u.pfnSetDeviceLoaderData = vkSetDeviceDispatch;

        create_info_disp.pNext = loader_create_info.pNext;
        loader_create_info.pNext = &create_info_disp;
        res = fpCreateDevice(pd->phys_dev, &loader_create_info, pAllocator, &created_device);
        if (res != VK_SUCCESS) {
            return res;
        }
        dev->chain_device = created_device;
    } else {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader_create_device_chain: Failed to find \'vkCreateDevice\' "
                   "in layer %s");
        // Couldn't find CreateDevice function!
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Initialize device dispatch table
    loader_init_device_dispatch_table(&dev->loader_dispatch, nextGDPA, dev->chain_device);

    return res;
}

VkResult loader_validate_layers(const struct loader_instance *inst, const uint32_t layer_count,
                                const char *const *ppEnabledLayerNames, const struct loader_layer_list *list) {
    struct loader_layer_properties *prop;

    for (uint32_t i = 0; i < layer_count; i++) {
        VkStringErrorFlags result = vk_string_validate(MaxLoaderStringLength, ppEnabledLayerNames[i]);
        if (result != VK_STRING_ERROR_NONE) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_validate_layers: Device ppEnabledLayerNames "
                       "contains string that is too long or is badly formed");
            return VK_ERROR_LAYER_NOT_PRESENT;
        }

        prop = loader_get_layer_property(ppEnabledLayerNames[i], list);
        if (NULL == prop) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_validate_layers: Layer %d does not exist in the list of available layers", i);
            return VK_ERROR_LAYER_NOT_PRESENT;
        }
    }
    return VK_SUCCESS;
}

VkResult loader_validate_instance_extensions(const struct loader_instance *inst, const struct loader_extension_list *icd_exts,
                                             const struct loader_layer_list *instance_layers,
                                             const VkInstanceCreateInfo *pCreateInfo) {
    VkExtensionProperties *extension_prop;
    char *env_value;
    bool check_if_known = true;
    VkResult res = VK_SUCCESS;

    struct loader_layer_list active_layers;
    struct loader_layer_list expanded_layers;
    memset(&active_layers, 0, sizeof(active_layers));
    memset(&expanded_layers, 0, sizeof(expanded_layers));
    if (!loader_init_layer_list(inst, &active_layers)) {
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }
    if (!loader_init_layer_list(inst, &expanded_layers)) {
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }

    // Build the lists of active layers (including metalayers) and expanded layers (with metalayers resolved to their components)
    loader_add_implicit_layers(inst, &active_layers, &expanded_layers, instance_layers);
    loader_add_env_layers(inst, VK_LAYER_TYPE_FLAG_EXPLICIT_LAYER, ENABLED_LAYERS_ENV, &active_layers, &expanded_layers,
                          instance_layers);
    res = loader_add_layer_names_to_list(inst, &active_layers, &expanded_layers, pCreateInfo->enabledLayerCount,
                                         pCreateInfo->ppEnabledLayerNames, instance_layers);
    if (VK_SUCCESS != res) {
        goto out;
    }

    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        VkStringErrorFlags result = vk_string_validate(MaxLoaderStringLength, pCreateInfo->ppEnabledExtensionNames[i]);
        if (result != VK_STRING_ERROR_NONE) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_validate_instance_extensions: Instance ppEnabledExtensionNames contains "
                       "string that is too long or is badly formed");
            res = VK_ERROR_EXTENSION_NOT_PRESENT;
            goto out;
        }

        // Check if a user wants to disable the instance extension filtering behavior
        env_value = loader_getenv("VK_LOADER_DISABLE_INST_EXT_FILTER", inst);
        if (NULL != env_value && atoi(env_value) != 0) {
            check_if_known = false;
        }
        loader_free_getenv(env_value, inst);

        if (check_if_known) {
            // See if the extension is in the list of supported extensions
            bool found = false;
            for (uint32_t j = 0; LOADER_INSTANCE_EXTENSIONS[j] != NULL; j++) {
                if (strcmp(pCreateInfo->ppEnabledExtensionNames[i], LOADER_INSTANCE_EXTENSIONS[j]) == 0) {
                    found = true;
                    break;
                }
            }

            // If it isn't in the list, return an error
            if (!found) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "loader_validate_instance_extensions: Extension %s not found in list of known instance extensions.",
                           pCreateInfo->ppEnabledExtensionNames[i]);
                res = VK_ERROR_EXTENSION_NOT_PRESENT;
                goto out;
            }
        }

        extension_prop = get_extension_property(pCreateInfo->ppEnabledExtensionNames[i], icd_exts);

        if (extension_prop) {
            continue;
        }

        extension_prop = NULL;

        // Not in global list, search expanded layer extension list
        for (uint32_t j = 0; NULL == extension_prop && j < expanded_layers.count; ++j) {
            extension_prop =
                get_extension_property(pCreateInfo->ppEnabledExtensionNames[i], &expanded_layers.list[j].instance_extension_list);
        }

        if (!extension_prop) {
            // Didn't find extension name in any of the global layers, error out
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_validate_instance_extensions: Instance extension %s not supported by available ICDs or enabled "
                       "layers.",
                       pCreateInfo->ppEnabledExtensionNames[i]);
            res = VK_ERROR_EXTENSION_NOT_PRESENT;
            goto out;
        }
    }

out:
    loader_destroy_layer_list(inst, NULL, &active_layers);
    loader_destroy_layer_list(inst, NULL, &expanded_layers);
    return res;
}

VkResult loader_validate_device_extensions(struct loader_physical_device_tramp *phys_dev,
                                           const struct loader_layer_list *activated_device_layers,
                                           const struct loader_extension_list *icd_exts, const VkDeviceCreateInfo *pCreateInfo) {
    VkExtensionProperties *extension_prop;
    struct loader_layer_properties *layer_prop;

    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        VkStringErrorFlags result = vk_string_validate(MaxLoaderStringLength, pCreateInfo->ppEnabledExtensionNames[i]);
        if (result != VK_STRING_ERROR_NONE) {
            loader_log(phys_dev->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_validate_device_extensions: Device ppEnabledExtensionNames contains "
                       "string that is too long or is badly formed");
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }

        const char *extension_name = pCreateInfo->ppEnabledExtensionNames[i];
        extension_prop = get_extension_property(extension_name, icd_exts);

        if (extension_prop) {
            continue;
        }

        // Not in global list, search activated layer extension lists
        for (uint32_t j = 0; j < activated_device_layers->count; j++) {
            layer_prop = &activated_device_layers->list[j];

            extension_prop = get_dev_extension_property(extension_name, &layer_prop->device_extension_list);
            if (extension_prop) {
                // Found the extension in one of the layers enabled by the app.
                break;
            }
        }

        if (!extension_prop) {
            // Didn't find extension name in any of the device layers, error out
            loader_log(phys_dev->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_validate_device_extensions: Device extension %s not supported by selected physical device "
                       "or enabled layers.",
                       pCreateInfo->ppEnabledExtensionNames[i]);
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }
    return VK_SUCCESS;
}

// Terminator functions for the Instance chain
// All named terminator_<Vulakn API name>
VKAPI_ATTR VkResult VKAPI_CALL terminator_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                                                         const VkAllocationCallbacks *pAllocator, VkInstance *pInstance) {
    struct loader_icd_term *icd_term;
    VkExtensionProperties *prop;
    char **filtered_extension_names = NULL;
    VkInstanceCreateInfo icd_create_info;
    VkResult res = VK_SUCCESS;
    bool one_icd_successful = false;

    struct loader_instance *ptr_instance = (struct loader_instance *)*pInstance;
    memcpy(&icd_create_info, pCreateInfo, sizeof(icd_create_info));

    icd_create_info.enabledLayerCount = 0;
    icd_create_info.ppEnabledLayerNames = NULL;

    // NOTE: Need to filter the extensions to only those supported by the ICD.
    //       No ICD will advertise support for layers. An ICD library could
    //       support a layer, but it would be independent of the actual ICD,
    //       just in the same library.
    filtered_extension_names = loader_stack_alloc(pCreateInfo->enabledExtensionCount * sizeof(char *));
    if (!filtered_extension_names) {
        loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "terminator_CreateInstance: Failed create extension name array for %d extensions",
                   pCreateInfo->enabledExtensionCount);
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }
    icd_create_info.ppEnabledExtensionNames = (const char *const *)filtered_extension_names;

    for (uint32_t i = 0; i < ptr_instance->icd_tramp_list.count; i++) {
        icd_term = loader_icd_add(ptr_instance, &ptr_instance->icd_tramp_list.scanned_list[i]);
        if (NULL == icd_term) {
            loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "terminator_CreateInstance: Failed to add ICD %d to ICD trampoline list.", i);
            res = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto out;
        }

        // If any error happens after here, we need to remove the ICD from the list,
        // because we've already added it, but haven't validated it

        icd_create_info.enabledExtensionCount = 0;
        struct loader_extension_list icd_exts;

        loader_log(ptr_instance, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0, "Build ICD instance extension list");
        // traverse scanned icd list adding non-duplicate extensions to the list
        res = loader_init_generic_list(ptr_instance, (struct loader_generic_list *)&icd_exts, sizeof(VkExtensionProperties));
        if (VK_ERROR_OUT_OF_HOST_MEMORY == res) {
            // If out of memory, bail immediately.
            goto out;
        } else if (VK_SUCCESS != res) {
            // Something bad happened with this ICD, so free it and try the
            // next.
            ptr_instance->icd_terms = icd_term->next;
            icd_term->next = NULL;
            loader_icd_destroy(ptr_instance, icd_term, pAllocator);
            continue;
        }

        res = loader_add_instance_extensions(ptr_instance, icd_term->scanned_icd->EnumerateInstanceExtensionProperties,
                                             icd_term->scanned_icd->lib_name, &icd_exts);
        if (VK_SUCCESS != res) {
            loader_destroy_generic_list(ptr_instance, (struct loader_generic_list *)&icd_exts);
            if (VK_ERROR_OUT_OF_HOST_MEMORY == res) {
                // If out of memory, bail immediately.
                goto out;
            } else {
                // Something bad happened with this ICD, so free it and try the next.
                ptr_instance->icd_terms = icd_term->next;
                icd_term->next = NULL;
                loader_icd_destroy(ptr_instance, icd_term, pAllocator);
                continue;
            }
        }

        for (uint32_t j = 0; j < pCreateInfo->enabledExtensionCount; j++) {
            prop = get_extension_property(pCreateInfo->ppEnabledExtensionNames[j], &icd_exts);
            if (prop) {
                filtered_extension_names[icd_create_info.enabledExtensionCount] = (char *)pCreateInfo->ppEnabledExtensionNames[j];
                icd_create_info.enabledExtensionCount++;
            }
        }

        loader_destroy_generic_list(ptr_instance, (struct loader_generic_list *)&icd_exts);

        // Get the driver version from vkEnumerateInstanceVersion
        uint32_t icd_version = VK_API_VERSION_1_0;
        PFN_vkEnumerateInstanceVersion icd_enumerate_instance_version = (PFN_vkEnumerateInstanceVersion)
            icd_term->scanned_icd->GetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion");
        VkResult icd_result = VK_SUCCESS;
        if (icd_enumerate_instance_version != NULL) {
            icd_result = icd_enumerate_instance_version(&icd_version);
            if (icd_result != VK_SUCCESS) {
                icd_version = VK_API_VERSION_1_0;
                loader_log(ptr_instance, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0, "terminator_CreateInstance: ICD \"%s\" "
                    "vkEnumerateInstanceVersion returned error. The ICD will be treated as a 1.0 ICD",
                    icd_term->scanned_icd->lib_name);
            }
        }

        // Create an instance, substituting the version to 1.0 if necessary
        VkApplicationInfo icd_app_info;
        uint32_t requested_version = pCreateInfo == NULL || pCreateInfo->pApplicationInfo == NULL ? VK_API_VERSION_1_0 : pCreateInfo->pApplicationInfo->apiVersion;
        if (requested_version > icd_version) {
            if (icd_create_info.pApplicationInfo == NULL) {
                memset(&icd_app_info, 0, sizeof(icd_app_info));
            } else {
                memcpy(&icd_app_info, icd_create_info.pApplicationInfo, sizeof(icd_app_info));
            }
            icd_app_info.apiVersion = icd_term->scanned_icd->api_version;
            icd_create_info.pApplicationInfo = &icd_app_info;
        }
        icd_result = ptr_instance->icd_tramp_list.scanned_list[i].CreateInstance(&icd_create_info, pAllocator, &(icd_term->instance));
        if (VK_ERROR_OUT_OF_HOST_MEMORY == icd_result) {
            // If out of memory, bail immediately.
            res = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto out;
        } else if (VK_SUCCESS != icd_result) {
            loader_log(ptr_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "terminator_CreateInstance: Failed to CreateInstance in "
                       "ICD %d.  Skipping ICD.",
                       i);
            ptr_instance->icd_terms = icd_term->next;
            icd_term->next = NULL;
            loader_icd_destroy(ptr_instance, icd_term, pAllocator);
            continue;
        }

        if (!loader_icd_init_entries(icd_term, icd_term->instance,
                                     ptr_instance->icd_tramp_list.scanned_list[i].GetInstanceProcAddr)) {
            loader_log(ptr_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "terminator_CreateInstance: Failed to CreateInstance and find "
                       "entrypoints with ICD.  Skipping ICD.");
            ptr_instance->icd_terms = icd_term->next;
            icd_term->next = NULL;
            loader_icd_destroy(ptr_instance, icd_term, pAllocator);
            continue;
        }

        // If we made it this far, at least one ICD was successful
        one_icd_successful = true;
    }

    // If no ICDs were added to instance list and res is unchanged from it's initial value, the loader was unable to
    // find a suitable ICD.
    if (VK_SUCCESS == res && (ptr_instance->icd_terms == NULL || !one_icd_successful)) {
        res = VK_ERROR_INCOMPATIBLE_DRIVER;
    }

out:

    if (VK_SUCCESS != res) {
        while (NULL != ptr_instance->icd_terms) {
            icd_term = ptr_instance->icd_terms;
            ptr_instance->icd_terms = icd_term->next;
            if (NULL != icd_term->instance) {
                icd_term->dispatch.DestroyInstance(icd_term->instance, pAllocator);
            }
            loader_icd_destroy(ptr_instance, icd_term, pAllocator);
        }
    }

    return res;
}

VKAPI_ATTR void VKAPI_CALL terminator_DestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator) {
    struct loader_instance *ptr_instance = loader_instance(instance);
    if (NULL == ptr_instance) {
        return;
    }
    struct loader_icd_term *icd_terms = ptr_instance->icd_terms;
    struct loader_icd_term *next_icd_term;

    // Remove this instance from the list of instances:
    struct loader_instance *prev = NULL;
    struct loader_instance *next = loader.instances;
    while (next != NULL) {
        if (next == ptr_instance) {
            // Remove this instance from the list:
            if (prev)
                prev->next = next->next;
            else
                loader.instances = next->next;
            break;
        }
        prev = next;
        next = next->next;
    }

    while (NULL != icd_terms) {
        if (icd_terms->instance) {
            icd_terms->dispatch.DestroyInstance(icd_terms->instance, pAllocator);
        }
        next_icd_term = icd_terms->next;
        icd_terms->instance = VK_NULL_HANDLE;
        loader_icd_destroy(ptr_instance, icd_terms, pAllocator);

        icd_terms = next_icd_term;
    }

    loader_delete_layer_properties(ptr_instance, &ptr_instance->instance_layer_list);
    loader_scanned_icd_clear(ptr_instance, &ptr_instance->icd_tramp_list);
    loader_destroy_generic_list(ptr_instance, (struct loader_generic_list *)&ptr_instance->ext_list);
    if (NULL != ptr_instance->phys_devs_term) {
        for (uint32_t i = 0; i < ptr_instance->phys_dev_count_term; i++) {
            loader_instance_heap_free(ptr_instance, ptr_instance->phys_devs_term[i]);
        }
        loader_instance_heap_free(ptr_instance, ptr_instance->phys_devs_term);
    }
    if (NULL != ptr_instance->phys_dev_groups_term) {
        for (uint32_t i = 0; i < ptr_instance->phys_dev_group_count_term; i++) {
            loader_instance_heap_free(ptr_instance, ptr_instance->phys_dev_groups_term[i]);
        }
        loader_instance_heap_free(ptr_instance, ptr_instance->phys_dev_groups_term);
    }
    loader_free_dev_ext_table(ptr_instance);
    loader_free_phys_dev_ext_table(ptr_instance);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
                                                       const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) {
    VkResult res = VK_SUCCESS;
    struct loader_physical_device_term *phys_dev_term;
    phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    struct loader_device *dev = (struct loader_device *)*pDevice;
    PFN_vkCreateDevice fpCreateDevice = icd_term->dispatch.CreateDevice;
    struct loader_extension_list icd_exts;

    struct VkStructureHeader *caller_dgci_container = NULL;
    VkDeviceGroupDeviceCreateInfoKHR *caller_dgci = NULL;

    dev->phys_dev_term = phys_dev_term;

    icd_exts.list = NULL;

    if (fpCreateDevice == NULL) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "terminator_CreateDevice: No vkCreateDevice command exposed "
                   "by ICD %s",
                   icd_term->scanned_icd->lib_name);
        res = VK_ERROR_INITIALIZATION_FAILED;
        goto out;
    }

    VkDeviceCreateInfo localCreateInfo;
    memcpy(&localCreateInfo, pCreateInfo, sizeof(localCreateInfo));

    // NOTE: Need to filter the extensions to only those supported by the ICD.
    //       No ICD will advertise support for layers. An ICD library could support a layer,
    //       but it would be independent of the actual ICD, just in the same library.
    char **filtered_extension_names = NULL;
    if (0 < pCreateInfo->enabledExtensionCount) {
        filtered_extension_names = loader_stack_alloc(pCreateInfo->enabledExtensionCount * sizeof(char *));
        if (NULL == filtered_extension_names) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "terminator_CreateDevice: Failed to create extension name "
                       "storage for %d extensions %d",
                       pCreateInfo->enabledExtensionCount);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
    }

    localCreateInfo.enabledLayerCount = 0;
    localCreateInfo.ppEnabledLayerNames = NULL;

    localCreateInfo.enabledExtensionCount = 0;
    localCreateInfo.ppEnabledExtensionNames = (const char *const *)filtered_extension_names;

    // Get the physical device (ICD) extensions
    res = loader_init_generic_list(icd_term->this_instance, (struct loader_generic_list *)&icd_exts, sizeof(VkExtensionProperties));
    if (VK_SUCCESS != res) {
        goto out;
    }

    res = loader_add_device_extensions(icd_term->this_instance, icd_term->dispatch.EnumerateDeviceExtensionProperties,
                                       phys_dev_term->phys_dev, icd_term->scanned_icd->lib_name, &icd_exts);
    if (res != VK_SUCCESS) {
        goto out;
    }

    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        const char *extension_name = pCreateInfo->ppEnabledExtensionNames[i];
        VkExtensionProperties *prop = get_extension_property(extension_name, &icd_exts);
        if (prop) {
            filtered_extension_names[localCreateInfo.enabledExtensionCount] = (char *)extension_name;
            localCreateInfo.enabledExtensionCount++;
        } else {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0,
                       "vkCreateDevice extension %s not available for "
                       "devices associated with ICD %s",
                       extension_name, icd_term->scanned_icd->lib_name);
        }
    }

    // Before we continue, If KHX_device_group is the list of enabled and viable extensions, then we then need to look for the
    // corresponding VkDeviceGroupDeviceCreateInfo struct in the device list and replace all the physical device values (which
    // are really loader physical device terminator values) with the ICD versions.
    //if (icd_term->this_instance->enabled_known_extensions.khr_device_group_creation == 1) {
    {
        struct VkStructureHeader *pNext = (struct VkStructureHeader *)localCreateInfo.pNext;
        struct VkStructureHeader *pPrev = (struct VkStructureHeader *)&localCreateInfo;
        while (NULL != pNext) {
            if (VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO == pNext->sType) {
                VkDeviceGroupDeviceCreateInfo *cur_struct = (VkDeviceGroupDeviceCreateInfo *)pNext;
                if (0 < cur_struct->physicalDeviceCount && NULL != cur_struct->pPhysicalDevices) {
                    VkDeviceGroupDeviceCreateInfo *temp_struct = loader_stack_alloc(sizeof(VkDeviceGroupDeviceCreateInfo));
                    VkPhysicalDevice *phys_dev_array = NULL;
                    if (NULL == temp_struct) {
                        return VK_ERROR_OUT_OF_HOST_MEMORY;
                    }
                    memcpy(temp_struct, cur_struct, sizeof(VkDeviceGroupDeviceCreateInfo));
                    phys_dev_array = loader_stack_alloc(sizeof(VkPhysicalDevice) * cur_struct->physicalDeviceCount);
                    if (NULL == phys_dev_array) {
                        return VK_ERROR_OUT_OF_HOST_MEMORY;
                    }

                    // Before calling down, replace the incoming physical device values (which are really loader terminator
                    // physical devices) with the ICDs physical device values.
                    struct loader_physical_device_term *cur_term;
                    for (uint32_t phys_dev = 0; phys_dev < cur_struct->physicalDeviceCount; phys_dev++) {
                        cur_term = (struct loader_physical_device_term *)cur_struct->pPhysicalDevices[phys_dev];
                        phys_dev_array[phys_dev] = cur_term->phys_dev;
                    }
                    temp_struct->pPhysicalDevices = phys_dev_array;

                    // Keep track of pointers to restore pNext chain before returning
                    caller_dgci_container = pPrev;
                    caller_dgci = cur_struct;

                    // Replace the old struct in the pNext chain with this one.
                    pPrev->pNext = (const void *)temp_struct;
                    pNext = (struct VkStructureHeader *)(temp_struct);
                }
                break;
            }

            pPrev = pNext;
            pNext = (struct VkStructureHeader *)(pPrev->pNext);
        }
    }

    // Handle loader emulation for structs that are not supported by the ICD:
    // Presently, the emulation leaves the pNext chain alone. This means that the ICD will receive items in the chain which
    // are not recognized by the ICD. If this causes the ICD to fail, then the items would have to be removed here. The current
    // implementation does not remove them because copying the pNext chain would be impossible if the loader does not recognize
    // the any of the struct types, as the loader would not know the size to allocate and copy.
    //if (icd_term->dispatch.GetPhysicalDeviceFeatures2 == NULL && icd_term->dispatch.GetPhysicalDeviceFeatures2KHR == NULL) {
    {
        const void *pNext = localCreateInfo.pNext;
        while (pNext != NULL) {
            switch (*(VkStructureType *)pNext) {
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2: {
                    const VkPhysicalDeviceFeatures2KHR *features = pNext;

                    if (icd_term->dispatch.GetPhysicalDeviceFeatures2 == NULL && icd_term->dispatch.GetPhysicalDeviceFeatures2KHR == NULL) {
                        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                            "vkCreateDevice: Emulating handling of VkPhysicalDeviceFeatures2 in pNext chain for ICD \"%s\"",
                            icd_term->scanned_icd->lib_name);

                        // Verify that VK_KHR_get_physical_device_properties2 is enabled
                        if (icd_term->this_instance->enabled_known_extensions.khr_get_physical_device_properties2) {
                            localCreateInfo.pEnabledFeatures = &features->features;
                        }
                    }

                    // Leave this item in the pNext chain for now

                    pNext = features->pNext;
                    break;
                }

                case VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO: {
                    const VkDeviceGroupDeviceCreateInfoKHR *group_info = pNext;

                    if (icd_term->dispatch.EnumeratePhysicalDeviceGroups == NULL && icd_term->dispatch.EnumeratePhysicalDeviceGroupsKHR == NULL) {
                        loader_log(
                            icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                            "vkCreateDevice: Emulating handling of VkPhysicalDeviceGroupProperties in pNext chain for ICD \"%s\"",
                            icd_term->scanned_icd->lib_name);

                        // The group must contain only this one device, since physical device groups aren't actually supported
                        if (group_info->physicalDeviceCount != 1) {
                            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                                   "vkCreateDevice: Emulation failed to create device from device group info");
                            res = VK_ERROR_INITIALIZATION_FAILED;
                            goto out;
                        }
                    }

                    // Nothing needs to be done here because we're leaving the item in the pNext chain and because the spec states
                    // that the physicalDevice argument must be included in the device group, and we've already checked that it is

                    pNext = group_info->pNext;
                    break;
                }

                // Multiview properties are also allowed, but since VK_KHX_multiview is a device extension, we'll just let the ICD
                // handle that error when the user enables the extension here
                default: {
                    const struct VkStructureHeader *header = pNext;
                    pNext = header->pNext;
                    break;
                }
            }
        }
    }

    res = fpCreateDevice(phys_dev_term->phys_dev, &localCreateInfo, pAllocator, &dev->icd_device);
    if (res != VK_SUCCESS) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "terminator_CreateDevice: Failed in ICD %s vkCreateDevice"
                   "call",
                   icd_term->scanned_icd->lib_name);
        goto out;
    }

    *pDevice = dev->icd_device;
    loader_add_logical_device(icd_term->this_instance, icd_term, dev);

    // Init dispatch pointer in new device object
    loader_init_dispatch(*pDevice, &dev->loader_dispatch);

out:
    if (NULL != icd_exts.list) {
        loader_destroy_generic_list(icd_term->this_instance, (struct loader_generic_list *)&icd_exts);
    }

    // Restore pNext pointer to old VkDeviceGroupDeviceCreateInfoKHX
    // in the chain to maintain consistency for the caller.
    if (caller_dgci_container != NULL) {
        caller_dgci_container->pNext = caller_dgci;
    }

    return res;
}

VkResult setupLoaderTrampPhysDevs(VkInstance instance) {
    VkResult res = VK_SUCCESS;
    VkPhysicalDevice *local_phys_devs = NULL;
    struct loader_instance *inst;
    uint32_t total_count = 0;
    struct loader_physical_device_tramp **new_phys_devs = NULL;

    inst = loader_get_instance(instance);
    if (NULL == inst) {
        res = VK_ERROR_INITIALIZATION_FAILED;
        goto out;
    }

    // Query how many GPUs there
    res = inst->disp->layer_inst_disp.EnumeratePhysicalDevices(instance, &total_count, NULL);
    if (res != VK_SUCCESS) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "setupLoaderTrampPhysDevs:  Failed during dispatch call "
                   "of \'vkEnumeratePhysicalDevices\' to lower layers or "
                   "loader to get count.");
        goto out;
    }

    // Really use what the total GPU count is since Optimus and other layers may mess
    // the count up.
    total_count = inst->total_gpu_count;

    // Create an array for the new physical devices, which will be stored
    // in the instance for the trampoline code.
    new_phys_devs = (struct loader_physical_device_tramp **)loader_instance_heap_alloc(
        inst, total_count * sizeof(struct loader_physical_device_tramp *), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (NULL == new_phys_devs) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "setupLoaderTrampPhysDevs:  Failed to allocate new physical device"
                   " array of size %d",
                   total_count);
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }
    memset(new_phys_devs, 0, total_count * sizeof(struct loader_physical_device_tramp *));

    // Create a temporary array (on the stack) to keep track of the
    // returned VkPhysicalDevice values.
    local_phys_devs = loader_stack_alloc(sizeof(VkPhysicalDevice) * total_count);
    if (NULL == local_phys_devs) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "setupLoaderTrampPhysDevs:  Failed to allocate local "
                   "physical device array of size %d",
                   total_count);
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }
    memset(local_phys_devs, 0, sizeof(VkPhysicalDevice) * total_count);

    res = inst->disp->layer_inst_disp.EnumeratePhysicalDevices(instance, &total_count, local_phys_devs);
    if (VK_SUCCESS != res) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "setupLoaderTrampPhysDevs:  Failed during dispatch call "
                   "of \'vkEnumeratePhysicalDevices\' to lower layers or "
                   "loader to get content.");
        goto out;
    }

    // Copy or create everything to fill the new array of physical devices
    for (uint32_t new_idx = 0; new_idx < total_count; new_idx++) {
        // Check if this physical device is already in the old buffer
        for (uint32_t old_idx = 0; old_idx < inst->phys_dev_count_tramp; old_idx++) {
            if (local_phys_devs[new_idx] == inst->phys_devs_tramp[old_idx]->phys_dev) {
                new_phys_devs[new_idx] = inst->phys_devs_tramp[old_idx];
                break;
            }
        }

        // If this physical device isn't in the old buffer, create it
        if (NULL == new_phys_devs[new_idx]) {
            new_phys_devs[new_idx] = (struct loader_physical_device_tramp *)loader_instance_heap_alloc(
                inst, sizeof(struct loader_physical_device_tramp), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
            if (NULL == new_phys_devs[new_idx]) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "setupLoaderTrampPhysDevs:  Failed to allocate "
                           "physical device trampoline object %d",
                           new_idx);
                total_count = new_idx;
                res = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto out;
            }

            // Initialize the new physicalDevice object
            loader_set_dispatch((void *)new_phys_devs[new_idx], inst->disp);
            new_phys_devs[new_idx]->this_instance = inst;
            new_phys_devs[new_idx]->phys_dev = local_phys_devs[new_idx];
        }
    }

out:

    if (VK_SUCCESS != res) {
        if (NULL != new_phys_devs) {
            for (uint32_t i = 0; i < total_count; i++) {
                loader_instance_heap_free(inst, new_phys_devs[i]);
            }
            loader_instance_heap_free(inst, new_phys_devs);
        }
        total_count = 0;
    } else {
        // Free everything that didn't carry over to the new array of
        // physical devices
        if (NULL != inst->phys_devs_tramp) {
            for (uint32_t i = 0; i < inst->phys_dev_count_tramp; i++) {
                bool found = false;
                for (uint32_t j = 0; j < total_count; j++) {
                    if (inst->phys_devs_tramp[i] == new_phys_devs[j]) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    loader_instance_heap_free(inst, inst->phys_devs_tramp[i]);
                }
            }
            loader_instance_heap_free(inst, inst->phys_devs_tramp);
        }

        // Swap in the new physical device list
        inst->phys_dev_count_tramp = total_count;
        inst->phys_devs_tramp = new_phys_devs;
    }

    return res;
}

VkResult setupLoaderTermPhysDevs(struct loader_instance *inst) {
    VkResult res = VK_SUCCESS;
    struct loader_icd_term *icd_term;
    struct loader_phys_dev_per_icd *icd_phys_dev_array = NULL;
    struct loader_physical_device_term **new_phys_devs = NULL;

    inst->total_gpu_count = 0;

    // Allocate something to store the physical device characteristics
    // that we read from each ICD.
    icd_phys_dev_array =
        (struct loader_phys_dev_per_icd *)loader_stack_alloc(sizeof(struct loader_phys_dev_per_icd) * inst->total_icd_count);
    if (NULL == icd_phys_dev_array) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "setupLoaderTermPhysDevs:  Failed to allocate temporary "
                   "ICD Physical device info array of size %d",
                   inst->total_gpu_count);
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }
    memset(icd_phys_dev_array, 0, sizeof(struct loader_phys_dev_per_icd) * inst->total_icd_count);
    icd_term = inst->icd_terms;

    // For each ICD, query the number of physical devices, and then get an
    // internal value for those physical devices.
    for (uint32_t icd_idx = 0; NULL != icd_term; icd_term = icd_term->next, icd_idx++) {
        res = icd_term->dispatch.EnumeratePhysicalDevices(icd_term->instance, &icd_phys_dev_array[icd_idx].count, NULL);
        if (VK_SUCCESS != res) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "setupLoaderTermPhysDevs:  Call to "
                       "ICD %d's \'vkEnumeratePhysicalDevices\' failed with"
                       " error 0x%08x",
                       icd_idx, res);
            goto out;
        }

        icd_phys_dev_array[icd_idx].phys_devs =
            (VkPhysicalDevice *)loader_stack_alloc(icd_phys_dev_array[icd_idx].count * sizeof(VkPhysicalDevice));
        if (NULL == icd_phys_dev_array[icd_idx].phys_devs) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "setupLoaderTermPhysDevs:  Failed to allocate temporary "
                       "ICD Physical device array for ICD %d of size %d",
                       icd_idx, inst->total_gpu_count);
            res = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto out;
        }

        res = icd_term->dispatch.EnumeratePhysicalDevices(icd_term->instance, &(icd_phys_dev_array[icd_idx].count),
                                                          icd_phys_dev_array[icd_idx].phys_devs);
        if (VK_SUCCESS != res) {
            goto out;
        }
        inst->total_gpu_count += icd_phys_dev_array[icd_idx].count;
        icd_phys_dev_array[icd_idx].this_icd_term = icd_term;
    }

    if (0 == inst->total_gpu_count) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "setupLoaderTermPhysDevs:  Failed to detect any valid"
                   " GPUs in the current config");
        res = VK_ERROR_INITIALIZATION_FAILED;
        goto out;
    }

    new_phys_devs = loader_instance_heap_alloc(inst, sizeof(struct loader_physical_device_term *) * inst->total_gpu_count,
                                               VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (NULL == new_phys_devs) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "setupLoaderTermPhysDevs:  Failed to allocate new physical"
                   " device array of size %d",
                   inst->total_gpu_count);
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }
    memset(new_phys_devs, 0, sizeof(struct loader_physical_device_term *) * inst->total_gpu_count);

    // Copy or create everything to fill the new array of physical devices
    uint32_t idx = 0;
    for (uint32_t icd_idx = 0; icd_idx < inst->total_icd_count; icd_idx++) {
        for (uint32_t pd_idx = 0; pd_idx < icd_phys_dev_array[icd_idx].count; pd_idx++) {
            // Check if this physical device is already in the old buffer
            if (NULL != inst->phys_devs_term) {
                for (uint32_t old_idx = 0; old_idx < inst->phys_dev_count_term; old_idx++) {
                    if (icd_phys_dev_array[icd_idx].phys_devs[pd_idx] == inst->phys_devs_term[old_idx]->phys_dev) {
                        new_phys_devs[idx] = inst->phys_devs_term[old_idx];
                        break;
                    }
                }
            }
            // If this physical device isn't in the old buffer, then we
            // need to create it.
            if (NULL == new_phys_devs[idx]) {
                new_phys_devs[idx] = loader_instance_heap_alloc(inst, sizeof(struct loader_physical_device_term),
                                                                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
                if (NULL == new_phys_devs[idx]) {
                    loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                               "setupLoaderTermPhysDevs:  Failed to allocate "
                               "physical device terminator object %d",
                               idx);
                    inst->total_gpu_count = idx;
                    res = VK_ERROR_OUT_OF_HOST_MEMORY;
                    goto out;
                }

                loader_set_dispatch((void *)new_phys_devs[idx], inst->disp);
                new_phys_devs[idx]->this_icd_term = icd_phys_dev_array[icd_idx].this_icd_term;
                new_phys_devs[idx]->icd_index = (uint8_t)(icd_idx);
                new_phys_devs[idx]->phys_dev = icd_phys_dev_array[icd_idx].phys_devs[pd_idx];
            }
            idx++;
        }
    }

out:

    if (VK_SUCCESS != res) {
        if (NULL != new_phys_devs) {
            // We've encountered an error, so we should free the new buffers.
            for (uint32_t i = 0; i < inst->total_gpu_count; i++) {
                loader_instance_heap_free(inst, new_phys_devs[i]);
            }
            loader_instance_heap_free(inst, new_phys_devs);
        }
        inst->total_gpu_count = 0;
    } else {
        // Free everything that didn't carry over to the new array of
        // physical devices.  Everything else will have been copied over
        // to the new array.
        if (NULL != inst->phys_devs_term) {
            for (uint32_t cur_pd = 0; cur_pd < inst->phys_dev_count_term; cur_pd++) {
                bool found = false;
                for (uint32_t new_pd_idx = 0; new_pd_idx < inst->total_gpu_count; new_pd_idx++) {
                    if (inst->phys_devs_term[cur_pd] == new_phys_devs[new_pd_idx]) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    loader_instance_heap_free(inst, inst->phys_devs_term[cur_pd]);
                }
            }
            loader_instance_heap_free(inst, inst->phys_devs_term);
        }

        // Swap out old and new devices list
        inst->phys_dev_count_term = inst->total_gpu_count;
        inst->phys_devs_term = new_phys_devs;
    }

    return res;
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_EnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount,
                                                                   VkPhysicalDevice *pPhysicalDevices) {
    struct loader_instance *inst = (struct loader_instance *)instance;
    VkResult res = VK_SUCCESS;

    // Always call the setup loader terminator physical devices because they may
    // have changed at any point.
    res = setupLoaderTermPhysDevs(inst);
    if (VK_SUCCESS != res) {
        goto out;
    }

    uint32_t copy_count = inst->total_gpu_count;
    if (NULL != pPhysicalDevices) {
        if (copy_count > *pPhysicalDeviceCount) {
            copy_count = *pPhysicalDeviceCount;
            res = VK_INCOMPLETE;
        }

        for (uint32_t i = 0; i < copy_count; i++) {
            pPhysicalDevices[i] = (VkPhysicalDevice)inst->phys_devs_term[i];
        }
    }

    *pPhysicalDeviceCount = copy_count;

out:

    return res;
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                                                  VkPhysicalDeviceProperties *pProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL != icd_term->dispatch.GetPhysicalDeviceProperties) {
        icd_term->dispatch.GetPhysicalDeviceProperties(phys_dev_term->phys_dev, pProperties);
    }
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice,
                                                                             uint32_t *pQueueFamilyPropertyCount,
                                                                             VkQueueFamilyProperties *pProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL != icd_term->dispatch.GetPhysicalDeviceQueueFamilyProperties) {
        icd_term->dispatch.GetPhysicalDeviceQueueFamilyProperties(phys_dev_term->phys_dev, pQueueFamilyPropertyCount, pProperties);
    }
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,
                                                                        VkPhysicalDeviceMemoryProperties *pProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL != icd_term->dispatch.GetPhysicalDeviceMemoryProperties) {
        icd_term->dispatch.GetPhysicalDeviceMemoryProperties(phys_dev_term->phys_dev, pProperties);
    }
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
                                                                VkPhysicalDeviceFeatures *pFeatures) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL != icd_term->dispatch.GetPhysicalDeviceFeatures) {
        icd_term->dispatch.GetPhysicalDeviceFeatures(phys_dev_term->phys_dev, pFeatures);
    }
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format,
                                                                        VkFormatProperties *pFormatInfo) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL != icd_term->dispatch.GetPhysicalDeviceFormatProperties) {
        icd_term->dispatch.GetPhysicalDeviceFormatProperties(phys_dev_term->phys_dev, format, pFormatInfo);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format,
                                                                                 VkImageType type, VkImageTiling tiling,
                                                                                 VkImageUsageFlags usage, VkImageCreateFlags flags,
                                                                                 VkImageFormatProperties *pImageFormatProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL == icd_term->dispatch.GetPhysicalDeviceImageFormatProperties) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "Encountered the vkEnumerateDeviceLayerProperties "
                   "terminator.  This means a layer improperly continued.");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return icd_term->dispatch.GetPhysicalDeviceImageFormatProperties(phys_dev_term->phys_dev, format, type, tiling, usage, flags,
                                                                     pImageFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format,
                                                                                   VkImageType type, VkSampleCountFlagBits samples,
                                                                                   VkImageUsageFlags usage, VkImageTiling tiling,
                                                                                   uint32_t *pNumProperties,
                                                                                   VkSparseImageFormatProperties *pProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL != icd_term->dispatch.GetPhysicalDeviceSparseImageFormatProperties) {
        icd_term->dispatch.GetPhysicalDeviceSparseImageFormatProperties(phys_dev_term->phys_dev, format, type, samples, usage,
                                                                        tiling, pNumProperties, pProperties);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                                                             const char *pLayerName, uint32_t *pPropertyCount,
                                                                             VkExtensionProperties *pProperties) {
    struct loader_physical_device_term *phys_dev_term;

    struct loader_layer_list implicit_layer_list = {0};
    struct loader_extension_list all_exts = {0};
    struct loader_extension_list icd_exts = {0};

    assert(pLayerName == NULL || strlen(pLayerName) == 0);

    // Any layer or trampoline wrapping should be removed at this point in time can just cast to the expected
    // type for VkPhysicalDevice.
    phys_dev_term = (struct loader_physical_device_term *)physicalDevice;

    // This case is during the call down the instance chain with pLayerName == NULL
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    uint32_t icd_ext_count = *pPropertyCount;
    VkResult res;

    // Get the available device extensions
    res = icd_term->dispatch.EnumerateDeviceExtensionProperties(phys_dev_term->phys_dev, NULL, &icd_ext_count, pProperties);
    if (res != VK_SUCCESS) {
        goto out;
    }

    if (!loader_init_layer_list(icd_term->this_instance, &implicit_layer_list)) {
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }

    loader_add_implicit_layers(icd_term->this_instance, &implicit_layer_list, NULL, &icd_term->this_instance->instance_layer_list);
    // We need to determine which implicit layers are active, and then add their extensions. This can't be cached as
    // it depends on results of environment variables (which can change).
    if (pProperties != NULL) {
        // Initialize dev_extension list within the physicalDevice object
        res = loader_init_device_extensions(icd_term->this_instance, phys_dev_term, icd_ext_count, pProperties, &icd_exts);
        if (res != VK_SUCCESS) {
            goto out;
        }

        // We need to determine which implicit layers are active, and then add their extensions. This can't be cached as
        // it depends on results of environment variables (which can change).
        res = loader_add_to_ext_list(icd_term->this_instance, &all_exts, icd_exts.count, icd_exts.list);
        if (res != VK_SUCCESS) {
            goto out;
        }

        loader_add_implicit_layers(icd_term->this_instance, &implicit_layer_list, NULL,
                                   &icd_term->this_instance->instance_layer_list);

        for (uint32_t i = 0; i < implicit_layer_list.count; i++) {
            for (uint32_t j = 0; j < implicit_layer_list.list[i].device_extension_list.count; j++) {
                res = loader_add_to_ext_list(icd_term->this_instance, &all_exts, 1,
                                             &implicit_layer_list.list[i].device_extension_list.list[j].props);
                if (res != VK_SUCCESS) {
                    goto out;
                }
            }
        }
        uint32_t capacity = *pPropertyCount;
        VkExtensionProperties *props = pProperties;

        for (uint32_t i = 0; i < all_exts.count && i < capacity; i++) {
            props[i] = all_exts.list[i];
        }

        // Wasn't enough space for the extensions, we did partial copy now return VK_INCOMPLETE
        if (capacity < all_exts.count) {
            res = VK_INCOMPLETE;
        } else {
            *pPropertyCount = all_exts.count;
        }
    } else {
        // Just return the count; need to add in the count of implicit layer extensions
        // don't worry about duplicates being added in the count
        *pPropertyCount = icd_ext_count;

        for (uint32_t i = 0; i < implicit_layer_list.count; i++) {
            *pPropertyCount += implicit_layer_list.list[i].device_extension_list.count;
        }
        res = VK_SUCCESS;
    }

out:

    if (NULL != implicit_layer_list.list) {
        loader_destroy_generic_list(icd_term->this_instance, (struct loader_generic_list *)&implicit_layer_list);
    }
    if (NULL != all_exts.list) {
        loader_destroy_generic_list(icd_term->this_instance, (struct loader_generic_list *)&all_exts);
    }
    if (NULL != icd_exts.list) {
        loader_destroy_generic_list(icd_term->this_instance, (struct loader_generic_list *)&icd_exts);
    }

    return res;
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
                                                                         VkLayerProperties *pProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
               "Encountered the vkEnumerateDeviceLayerProperties "
               "terminator.  This means a layer improperly continued.");
    // Should never get here this call isn't dispatched down the chain
    return VK_ERROR_INITIALIZATION_FAILED;
}

VkStringErrorFlags vk_string_validate(const int max_length, const char *utf8) {
    VkStringErrorFlags result = VK_STRING_ERROR_NONE;
    int num_char_bytes = 0;
    int i, j;

    for (i = 0; i <= max_length; i++) {
        if (utf8[i] == 0) {
            break;
        } else if (i == max_length) {
            result |= VK_STRING_ERROR_LENGTH;
            break;
        } else if ((utf8[i] >= 0x20) && (utf8[i] < 0x7f)) {
            num_char_bytes = 0;
        } else if ((utf8[i] & UTF8_ONE_BYTE_MASK) == UTF8_ONE_BYTE_CODE) {
            num_char_bytes = 1;
        } else if ((utf8[i] & UTF8_TWO_BYTE_MASK) == UTF8_TWO_BYTE_CODE) {
            num_char_bytes = 2;
        } else if ((utf8[i] & UTF8_THREE_BYTE_MASK) == UTF8_THREE_BYTE_CODE) {
            num_char_bytes = 3;
        } else {
            result = VK_STRING_ERROR_BAD_DATA;
        }

        // Validate the following num_char_bytes of data
        for (j = 0; (j < num_char_bytes) && (i < max_length); j++) {
            if (++i == max_length) {
                result |= VK_STRING_ERROR_LENGTH;
                break;
            }
            if ((utf8[i] & UTF8_DATA_BYTE_MASK) != UTF8_DATA_BYTE_CODE) {
                result |= VK_STRING_ERROR_BAD_DATA;
            }
        }
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
terminator_EnumerateInstanceVersion(const VkEnumerateInstanceVersionChain *chain, uint32_t* pApiVersion) {
    // NOTE: The Vulkan WG doesn't want us checking pApiVersion for NULL, but instead
    // prefers us crashing.
    *pApiVersion = VK_MAKE_VERSION(loader_major_version, loader_minor_version, 0);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
terminator_EnumerateInstanceExtensionProperties(const VkEnumerateInstanceExtensionPropertiesChain *chain, const char *pLayerName,
                                                uint32_t *pPropertyCount, VkExtensionProperties *pProperties) {
    struct loader_extension_list *global_ext_list = NULL;
    struct loader_layer_list instance_layers;
    struct loader_extension_list local_ext_list;
    struct loader_icd_tramp_list icd_tramp_list;
    uint32_t copy_size;
    VkResult res = VK_SUCCESS;

    // tls_instance = NULL;
    memset(&local_ext_list, 0, sizeof(local_ext_list));
    memset(&instance_layers, 0, sizeof(instance_layers));

    // Get layer libraries if needed
    if (pLayerName && strlen(pLayerName) != 0) {
        if (vk_string_validate(MaxLoaderStringLength, pLayerName) != VK_STRING_ERROR_NONE) {
            assert(VK_FALSE &&
                   "vkEnumerateInstanceExtensionProperties:  "
                   "pLayerName is too long or is badly formed");
            res = VK_ERROR_EXTENSION_NOT_PRESENT;
            goto out;
        }

        loader_layer_scan(NULL, &instance_layers);
        for (uint32_t i = 0; i < instance_layers.count; i++) {
            struct loader_layer_properties *props = &instance_layers.list[i];
            if (strcmp(props->info.layerName, pLayerName) == 0) {
                global_ext_list = &props->instance_extension_list;
                break;
            }
        }
    } else {
        // Scan/discover all ICD libraries
        memset(&icd_tramp_list, 0, sizeof(icd_tramp_list));
        res = loader_icd_scan(NULL, &icd_tramp_list);
        if (VK_SUCCESS != res) {
            goto out;
        }
        // Get extensions from all ICD's, merge so no duplicates
        res = loader_get_icd_loader_instance_extensions(NULL, &icd_tramp_list, &local_ext_list);
        if (VK_SUCCESS != res) {
            goto out;
        }
        loader_scanned_icd_clear(NULL, &icd_tramp_list);

        // Append enabled implicit layers.
        loader_implicit_layer_scan(NULL, &instance_layers);
        for (uint32_t i = 0; i < instance_layers.count; i++) {
            if (!loader_is_implicit_layer_enabled(NULL, &instance_layers.list[i])) {
                continue;
            }
            struct loader_extension_list *ext_list = &instance_layers.list[i].instance_extension_list;
            loader_add_to_ext_list(NULL, &local_ext_list, ext_list->count, ext_list->list);
        }

        global_ext_list = &local_ext_list;
    }

    if (global_ext_list == NULL) {
        res = VK_ERROR_LAYER_NOT_PRESENT;
        goto out;
    }

    if (pProperties == NULL) {
        *pPropertyCount = global_ext_list->count;
        goto out;
    }

    copy_size = *pPropertyCount < global_ext_list->count ? *pPropertyCount : global_ext_list->count;
    for (uint32_t i = 0; i < copy_size; i++) {
        memcpy(&pProperties[i], &global_ext_list->list[i], sizeof(VkExtensionProperties));
    }
    *pPropertyCount = copy_size;

    if (copy_size < global_ext_list->count) {
        res = VK_INCOMPLETE;
        goto out;
    }

out:

    loader_destroy_generic_list(NULL, (struct loader_generic_list *)&local_ext_list);
    loader_delete_layer_properties(NULL, &instance_layers);
    return res;
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_EnumerateInstanceLayerProperties(const VkEnumerateInstanceLayerPropertiesChain *chain,
                                                                           uint32_t *pPropertyCount,
                                                                           VkLayerProperties *pProperties) {
    VkResult result = VK_SUCCESS;
    struct loader_layer_list instance_layer_list;
    tls_instance = NULL;

    LOADER_PLATFORM_THREAD_ONCE(&once_init, loader_initialize);

    uint32_t copy_size;

    // Get layer libraries
    memset(&instance_layer_list, 0, sizeof(instance_layer_list));
    loader_layer_scan(NULL, &instance_layer_list);

    if (pProperties == NULL) {
        *pPropertyCount = instance_layer_list.count;
        goto out;
    }

    copy_size = (*pPropertyCount < instance_layer_list.count) ? *pPropertyCount : instance_layer_list.count;
    for (uint32_t i = 0; i < copy_size; i++) {
        memcpy(&pProperties[i], &instance_layer_list.list[i].info, sizeof(VkLayerProperties));
    }

    *pPropertyCount = copy_size;

    if (copy_size < instance_layer_list.count) {
        result = VK_INCOMPLETE;
        goto out;
    }

out:

    loader_delete_layer_properties(NULL, &instance_layer_list);
    return result;
}

#if defined(_WIN32) && defined(LOADER_DYNAMIC_LIB)
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            loader_initialize();
            break;
        case DLL_PROCESS_DETACH:
            if (NULL == reserved) {
                loader_release();
            }
            break;
        default:
            // Do nothing
            break;
    }
    return TRUE;
}
#elif !defined(_WIN32)
__attribute__((constructor)) void loader_init_library() { loader_initialize(); }

__attribute__((destructor)) void loader_free_library() { loader_release(); }
#endif

// ---- Vulkan Core 1.1 terminators

VkResult setupLoaderTermPhysDevGroups(struct loader_instance *inst) {
    VkResult res = VK_SUCCESS;
    struct loader_icd_term *icd_term;
    uint32_t total_count = 0;
    uint32_t cur_icd_group_count = 0;
    VkPhysicalDeviceGroupPropertiesKHR **new_phys_dev_groups = NULL;
    VkPhysicalDeviceGroupPropertiesKHR *local_phys_dev_groups = NULL;
    PFN_vkEnumeratePhysicalDeviceGroups fpEnumeratePhysicalDeviceGroups = NULL;

    if (0 == inst->phys_dev_count_term) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
            "setupLoaderTermPhysDevGroups:  Loader failed to setup physical "
            "device terminator info before calling \'EnumeratePhysicalDeviceGroups\'.");
        assert(false);
        res = VK_ERROR_INITIALIZATION_FAILED;
        goto out;
    }

    // For each ICD, query the number of physical device groups, and then get an
    // internal value for those physical devices.
    icd_term = inst->icd_terms;
    for (uint32_t icd_idx = 0; NULL != icd_term; icd_term = icd_term->next, icd_idx++) {
        // Get the function pointer to use to call into the ICD. This could be the core or KHR version
        if (inst->enabled_known_extensions.khr_device_group_creation) {
            fpEnumeratePhysicalDeviceGroups = icd_term->dispatch.EnumeratePhysicalDeviceGroupsKHR;
        } else {
            fpEnumeratePhysicalDeviceGroups = icd_term->dispatch.EnumeratePhysicalDeviceGroups;
        }

        cur_icd_group_count = 0;
        if (NULL == fpEnumeratePhysicalDeviceGroups) {
            // Treat each ICD's GPU as it's own group if the extension isn't supported
            res = icd_term->dispatch.EnumeratePhysicalDevices(icd_term->instance, &cur_icd_group_count, NULL);
            if (res != VK_SUCCESS) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                    "setupLoaderTermPhysDevGroups:  Failed during dispatch call of "
                    "\'EnumeratePhysicalDevices\' to ICD %d to get plain phys dev count.",
                    icd_idx);
                goto out;
            }
        } else {
            // Query the actual group info
            res = fpEnumeratePhysicalDeviceGroups(icd_term->instance, &cur_icd_group_count, NULL);
            if (res != VK_SUCCESS) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                    "setupLoaderTermPhysDevGroups:  Failed during dispatch call of "
                    "\'EnumeratePhysicalDeviceGroups\' to ICD %d to get count.",
                    icd_idx);
                goto out;
            }
        }
        total_count += cur_icd_group_count;
    }

    // Create an array for the new physical device groups, which will be stored
    // in the instance for the Terminator code.
    new_phys_dev_groups = (VkPhysicalDeviceGroupProperties **)loader_instance_heap_alloc(
        inst, total_count * sizeof(VkPhysicalDeviceGroupProperties *), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (NULL == new_phys_dev_groups) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
            "setupLoaderTermPhysDevGroups:  Failed to allocate new physical device"
            " group array of size %d",
            total_count);
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }
    memset(new_phys_dev_groups, 0, total_count * sizeof(VkPhysicalDeviceGroupProperties *));

    // Create a temporary array (on the stack) to keep track of the
    // returned VkPhysicalDevice values.
    local_phys_dev_groups = loader_stack_alloc(sizeof(VkPhysicalDeviceGroupProperties) * total_count);
    if (NULL == local_phys_dev_groups) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
            "setupLoaderTermPhysDevGroups:  Failed to allocate local "
            "physical device group array of size %d",
            total_count);
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }
    // Initialize the memory to something valid
    memset(local_phys_dev_groups, 0, sizeof(VkPhysicalDeviceGroupProperties) * total_count);
    for (uint32_t group = 0; group < total_count; group++) {
        local_phys_dev_groups[group].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES_KHR;
        local_phys_dev_groups[group].pNext = NULL;
        local_phys_dev_groups[group].subsetAllocation = false;
    }

    cur_icd_group_count = 0;
    icd_term = inst->icd_terms;
    for (uint32_t icd_idx = 0; NULL != icd_term; icd_term = icd_term->next, icd_idx++) {
        uint32_t count_this_time = total_count - cur_icd_group_count;

        // Get the function pointer to use to call into the ICD. This could be the core or KHR version
        if (inst->enabled_known_extensions.khr_device_group_creation) {
            fpEnumeratePhysicalDeviceGroups = icd_term->dispatch.EnumeratePhysicalDeviceGroupsKHR;
        } else {
            fpEnumeratePhysicalDeviceGroups = icd_term->dispatch.EnumeratePhysicalDeviceGroups;
        }

        if (NULL == fpEnumeratePhysicalDeviceGroups) {
            VkPhysicalDevice* phys_dev_array = loader_stack_alloc(sizeof(VkPhysicalDevice) * count_this_time);
            if (NULL == phys_dev_array) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                    "setupLoaderTermPhysDevGroups:  Failed to allocate local "
                    "physical device array of size %d",
                    count_this_time);
                res = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto out;
            }

            res = icd_term->dispatch.EnumeratePhysicalDevices(icd_term->instance, &count_this_time, phys_dev_array);
            if (res != VK_SUCCESS) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                    "setupLoaderTermPhysDevGroups:  Failed during dispatch call of "
                    "\'EnumeratePhysicalDevices\' to ICD %d to get plain phys dev count.",
                    icd_idx);
                goto out;
            }

            // Add each GPU as it's own group
            for (uint32_t indiv_gpu = 0; indiv_gpu < count_this_time; indiv_gpu++) {
                local_phys_dev_groups[indiv_gpu + cur_icd_group_count].physicalDeviceCount = 1;
                local_phys_dev_groups[indiv_gpu + cur_icd_group_count].physicalDevices[0] = phys_dev_array[indiv_gpu];
            }

        } else {
            res = fpEnumeratePhysicalDeviceGroups(icd_term->instance, &count_this_time, &local_phys_dev_groups[cur_icd_group_count]);
            if (VK_SUCCESS != res) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                    "setupLoaderTermPhysDevGroups:  Failed during dispatch call of "
                    "\'EnumeratePhysicalDeviceGroups\' to ICD %d to get content.",
                    icd_idx);
                goto out;
            }
        }

        cur_icd_group_count += count_this_time;
    }

    // Replace all the physical device IDs with the proper loader values
    for (uint32_t group = 0; group < total_count; group++) {
        for (uint32_t group_gpu = 0; group_gpu < local_phys_dev_groups[group].physicalDeviceCount; group_gpu++) {
            bool found = false;
            for (uint32_t term_gpu = 0; term_gpu < inst->phys_dev_count_term; term_gpu++) {
                if (local_phys_dev_groups[group].physicalDevices[group_gpu] == inst->phys_devs_term[term_gpu]->phys_dev) {
                    local_phys_dev_groups[group].physicalDevices[group_gpu] = (VkPhysicalDevice)inst->phys_devs_term[term_gpu];
                    found = true;
                    break;
                }
            }
            if (!found) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                    "setupLoaderTermPhysDevGroups:  Failed to find GPU %d in group %d"
                    " returned by \'EnumeratePhysicalDeviceGroups\' in list returned"
                    " by \'EnumeratePhysicalDevices\'", group_gpu, group);
                res = VK_ERROR_INITIALIZATION_FAILED;
                goto out;
            }
        }
    }

    // Copy or create everything to fill the new array of physical device groups
    for (uint32_t new_idx = 0; new_idx < total_count; new_idx++) {
        // Check if this physical device group with the same contents is already in the old buffer
        for (uint32_t old_idx = 0; old_idx < inst->phys_dev_group_count_term; old_idx++) {
            if (local_phys_dev_groups[new_idx].physicalDeviceCount == inst->phys_dev_groups_term[old_idx]->physicalDeviceCount) {
                bool found_all_gpus = true;
                for (uint32_t old_gpu = 0; old_gpu < inst->phys_dev_groups_term[old_idx]->physicalDeviceCount; old_gpu++) {
                    bool found_gpu = false;
                    for (uint32_t new_gpu = 0; new_gpu < local_phys_dev_groups[new_idx].physicalDeviceCount; new_gpu++) {
                        if (local_phys_dev_groups[new_idx].physicalDevices[new_gpu] == inst->phys_dev_groups_term[old_idx]->physicalDevices[old_gpu]) {
                            found_gpu = true;
                            break;
                        }
                    }

                    if (!found_gpu) {
                        found_all_gpus = false;
                        break;
                    }
                }
                if (!found_all_gpus) {
                    continue;
                } else {
                    new_phys_dev_groups[new_idx] = inst->phys_dev_groups_term[old_idx];
                    break;
                }
            }
        }

        // If this physical device group isn't in the old buffer, create it
        if (NULL == new_phys_dev_groups[new_idx]) {
            new_phys_dev_groups[new_idx] = (VkPhysicalDeviceGroupPropertiesKHR *)loader_instance_heap_alloc(
                inst, sizeof(VkPhysicalDeviceGroupPropertiesKHR), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
            if (NULL == new_phys_dev_groups[new_idx]) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                    "setupLoaderTermPhysDevGroups:  Failed to allocate "
                    "physical device group Terminator object %d",
                    new_idx);
                total_count = new_idx;
                res = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto out;
            }
            memcpy(new_phys_dev_groups[new_idx], &local_phys_dev_groups[new_idx],
                sizeof(VkPhysicalDeviceGroupPropertiesKHR));
        }
    }

out:

    if (VK_SUCCESS != res) {
        if (NULL != new_phys_dev_groups) {
            for (uint32_t i = 0; i < total_count; i++) {
                loader_instance_heap_free(inst, new_phys_dev_groups[i]);
            }
            loader_instance_heap_free(inst, new_phys_dev_groups);
        }
        total_count = 0;
    } else {
        // Free everything that didn't carry over to the new array of
        // physical device groups
        if (NULL != inst->phys_dev_groups_term) {
            for (uint32_t i = 0; i < inst->phys_dev_group_count_term; i++) {
                bool found = false;
                for (uint32_t j = 0; j < total_count; j++) {
                    if (inst->phys_dev_groups_term[i] == new_phys_dev_groups[j]) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    loader_instance_heap_free(inst, inst->phys_dev_groups_term[i]);
                }
            }
            loader_instance_heap_free(inst, inst->phys_dev_groups_term);
        }

        // Swap in the new physical device group list
        inst->phys_dev_group_count_term = total_count;
        inst->phys_dev_groups_term = new_phys_dev_groups;
    }

    return res;
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_EnumeratePhysicalDeviceGroups(
    VkInstance instance, uint32_t *pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupProperties *pPhysicalDeviceGroupProperties) {
    struct loader_instance *inst = (struct loader_instance *)instance;
    VkResult res = VK_SUCCESS;

    // Always call the setup loader terminator physical device groups because they may
    // have changed at any point.
    res = setupLoaderTermPhysDevGroups(inst);
    if (VK_SUCCESS != res) {
        goto out;
    }

    uint32_t copy_count = inst->phys_dev_group_count_term;
    if (NULL != pPhysicalDeviceGroupProperties) {
        if (copy_count > *pPhysicalDeviceGroupCount) {
            copy_count = *pPhysicalDeviceGroupCount;
            res = VK_INCOMPLETE;
        }

        for (uint32_t i = 0; i < copy_count; i++) {
            memcpy(&pPhysicalDeviceGroupProperties[i], inst->phys_dev_groups_term[i],
                   sizeof(VkPhysicalDeviceGroupPropertiesKHR));
        }
    }

    *pPhysicalDeviceGroupCount = copy_count;

out:

    return res;
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                                                    VkPhysicalDeviceFeatures2 *pFeatures) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    const struct loader_instance *inst = icd_term->this_instance;

    // Get the function pointer to use to call into the ICD. This could be the core or KHR version
    PFN_vkGetPhysicalDeviceFeatures2 fpGetPhysicalDeviceFeatures2 = NULL;
    if (inst != NULL && inst->enabled_known_extensions.khr_get_physical_device_properties2) {
        fpGetPhysicalDeviceFeatures2 = icd_term->dispatch.GetPhysicalDeviceFeatures2KHR;
    } else {
        fpGetPhysicalDeviceFeatures2 = icd_term->dispatch.GetPhysicalDeviceFeatures2;
    }

    if (fpGetPhysicalDeviceFeatures2 != NULL || !inst->enabled_known_extensions.khr_get_physical_device_properties2) {
        // Pass the call to the driver
        fpGetPhysicalDeviceFeatures2(phys_dev_term->phys_dev, pFeatures);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceFeatures2: Emulating call in ICD \"%s\" using vkGetPhysicalDeviceFeatures",
                   icd_term->scanned_icd->lib_name);

        // Write to the VkPhysicalDeviceFeatures2 struct
        icd_term->dispatch.GetPhysicalDeviceFeatures(phys_dev_term->phys_dev, &pFeatures->features);

        void *pNext = pFeatures->pNext;
        while (pNext != NULL) {
            switch (*(VkStructureType *)pNext) {
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES: {
                    // Skip the check if VK_KHR_multiview is enabled because it's a device extension
                    // Write to the VkPhysicalDeviceMultiviewFeaturesKHR struct
                    VkPhysicalDeviceMultiviewFeaturesKHR *multiview_features = pNext;
                    multiview_features->multiview = VK_FALSE;
                    multiview_features->multiviewGeometryShader = VK_FALSE;
                    multiview_features->multiviewTessellationShader = VK_FALSE;

                    pNext = multiview_features->pNext;
                    break;
                }
                default: {
                    loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                               "vkGetPhysicalDeviceFeatures2: Emulation found unrecognized structure type in pFeatures->pNext - "
                               "this struct will be ignored");

                    struct VkStructureHeader *header = pNext;
                    pNext = (void *)header->pNext;
                    break;
                }
            }
        }
    }
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                                                      VkPhysicalDeviceProperties2 *pProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    const struct loader_instance *inst = icd_term->this_instance;

    // Get the function pointer to use to call into the ICD. This could be the core or KHR version
    PFN_vkGetPhysicalDeviceProperties2 fpGetPhysicalDeviceProperties2 = NULL;
    if (inst != NULL && inst->enabled_known_extensions.khr_get_physical_device_properties2) {
        fpGetPhysicalDeviceProperties2 = icd_term->dispatch.GetPhysicalDeviceProperties2KHR;
    } else {
        fpGetPhysicalDeviceProperties2 = icd_term->dispatch.GetPhysicalDeviceProperties2;
    }

    if (fpGetPhysicalDeviceProperties2 != NULL || !inst->enabled_known_extensions.khr_get_physical_device_properties2) {
        // Pass the call to the driver
        fpGetPhysicalDeviceProperties2(phys_dev_term->phys_dev, pProperties);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceProperties2: Emulating call in ICD \"%s\" using vkGetPhysicalDeviceProperties",
                   icd_term->scanned_icd->lib_name);

        // Write to the VkPhysicalDeviceProperties2 struct
        icd_term->dispatch.GetPhysicalDeviceProperties(phys_dev_term->phys_dev, &pProperties->properties);

        void *pNext = pProperties->pNext;
        while (pNext != NULL) {
            switch (*(VkStructureType *)pNext) {
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: {
                    VkPhysicalDeviceIDPropertiesKHR *id_properties = pNext;

                    // Verify that "VK_KHR_external_memory_capabilities" is enabled
                    if (icd_term->this_instance->enabled_known_extensions.khr_external_memory_capabilities) {
                        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                                   "vkGetPhysicalDeviceProperties2: Emulation cannot generate unique IDs for struct "
                                   "VkPhysicalDeviceIDProperties - setting IDs to zero instead");

                        // Write to the VkPhysicalDeviceIDPropertiesKHR struct
                        memset(id_properties->deviceUUID, 0, VK_UUID_SIZE);
                        memset(id_properties->driverUUID, 0, VK_UUID_SIZE);
                        id_properties->deviceLUIDValid = VK_FALSE;
                    }

                    pNext = id_properties->pNext;
                    break;
                }
                default: {
                    loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                               "vkGetPhysicalDeviceProperties2KHR: Emulation found unrecognized structure type in "
                               "pProperties->pNext - this struct will be ignored");

                    struct VkStructureHeader *header = pNext;
                    pNext = (void *)header->pNext;
                    break;
                }
            }
        }
    }
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format,
                                                                            VkFormatProperties2 *pFormatProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    const struct loader_instance *inst = icd_term->this_instance;

    // Get the function pointer to use to call into the ICD. This could be the core or KHR version
    PFN_vkGetPhysicalDeviceFormatProperties2 fpGetPhysicalDeviceFormatProperties2 = NULL;
    if (inst != NULL && inst->enabled_known_extensions.khr_get_physical_device_properties2) {
        fpGetPhysicalDeviceFormatProperties2 = icd_term->dispatch.GetPhysicalDeviceFormatProperties2KHR;
    } else {
        fpGetPhysicalDeviceFormatProperties2 = icd_term->dispatch.GetPhysicalDeviceFormatProperties2;
    }

    if (fpGetPhysicalDeviceFormatProperties2 != NULL || !inst->enabled_known_extensions.khr_get_physical_device_properties2) {
        // Pass the call to the driver
        fpGetPhysicalDeviceFormatProperties2(phys_dev_term->phys_dev, format, pFormatProperties);
    } else {
        // Emulate the call
        loader_log(
            icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
            "vkGetPhysicalDeviceFormatProperties2: Emulating call in ICD \"%s\" using vkGetPhysicalDeviceFormatProperties",
            icd_term->scanned_icd->lib_name);

        // Write to the VkFormatProperties2 struct
        icd_term->dispatch.GetPhysicalDeviceFormatProperties(phys_dev_term->phys_dev, format, &pFormatProperties->formatProperties);

        if (pFormatProperties->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceFormatProperties2: Emulation found unrecognized structure type in "
                       "pFormatProperties->pNext - this struct will be ignored");
        }
    }
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_GetPhysicalDeviceImageFormatProperties2(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2KHR *pImageFormatInfo,
    VkImageFormatProperties2KHR *pImageFormatProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    const struct loader_instance *inst = icd_term->this_instance;

    // Get the function pointer to use to call into the ICD. This could be the core or KHR version
    PFN_vkGetPhysicalDeviceImageFormatProperties2 fpGetPhysicalDeviceImageFormatProperties2 = NULL;
    if (inst != NULL && inst->enabled_known_extensions.khr_get_physical_device_properties2) {
        fpGetPhysicalDeviceImageFormatProperties2 = icd_term->dispatch.GetPhysicalDeviceImageFormatProperties2KHR;
    } else {
        fpGetPhysicalDeviceImageFormatProperties2 = icd_term->dispatch.GetPhysicalDeviceImageFormatProperties2;
    }

    if (fpGetPhysicalDeviceImageFormatProperties2 != NULL || !inst->enabled_known_extensions.khr_get_physical_device_properties2) {
        // Pass the call to the driver
        return fpGetPhysicalDeviceImageFormatProperties2(phys_dev_term->phys_dev, pImageFormatInfo, pImageFormatProperties);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceImageFormatProperties2: Emulating call in ICD \"%s\" using "
                   "vkGetPhysicalDeviceImageFormatProperties",
                   icd_term->scanned_icd->lib_name);

        // If there is more info in  either pNext, then this is unsupported
        if (pImageFormatInfo->pNext != NULL || pImageFormatProperties->pNext != NULL) {
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        }

        // Write to the VkImageFormatProperties2KHR struct
        return icd_term->dispatch.GetPhysicalDeviceImageFormatProperties(
            phys_dev_term->phys_dev, pImageFormatInfo->format, pImageFormatInfo->type, pImageFormatInfo->tiling,
            pImageFormatInfo->usage, pImageFormatInfo->flags, &pImageFormatProperties->imageFormatProperties);
    }
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties2KHR *pQueueFamilyProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    const struct loader_instance *inst = icd_term->this_instance;

    // Get the function pointer to use to call into the ICD. This could be the core or KHR version
    PFN_vkGetPhysicalDeviceQueueFamilyProperties2 fpGetPhysicalDeviceQueueFamilyProperties2 = NULL;
    if (inst != NULL && inst->enabled_known_extensions.khr_get_physical_device_properties2) {
        fpGetPhysicalDeviceQueueFamilyProperties2 = icd_term->dispatch.GetPhysicalDeviceQueueFamilyProperties2KHR;
    } else {
        fpGetPhysicalDeviceQueueFamilyProperties2 = icd_term->dispatch.GetPhysicalDeviceQueueFamilyProperties2;
    }

    if (fpGetPhysicalDeviceQueueFamilyProperties2 != NULL || !inst->enabled_known_extensions.khr_get_physical_device_properties2) {
        // Pass the call to the driver
        fpGetPhysicalDeviceQueueFamilyProperties2(phys_dev_term->phys_dev, pQueueFamilyPropertyCount, pQueueFamilyProperties);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceQueueFamilyProperties2: Emulating call in ICD \"%s\" using "
                   "vkGetPhysicalDeviceQueueFamilyProperties",
                   icd_term->scanned_icd->lib_name);

        if (pQueueFamilyProperties == NULL || *pQueueFamilyPropertyCount == 0) {
            // Write to pQueueFamilyPropertyCount
            icd_term->dispatch.GetPhysicalDeviceQueueFamilyProperties(phys_dev_term->phys_dev, pQueueFamilyPropertyCount, NULL);
        } else {
            // Allocate a temporary array for the output of the old function
            VkQueueFamilyProperties *properties = loader_stack_alloc(*pQueueFamilyPropertyCount * sizeof(VkQueueFamilyProperties));
            if (properties == NULL) {
                *pQueueFamilyPropertyCount = 0;
                loader_log(
                    icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                    "vkGetPhysicalDeviceQueueFamilyProperties2: Out of memory - Failed to allocate array for loader emulation.");
                return;
            }

            icd_term->dispatch.GetPhysicalDeviceQueueFamilyProperties(phys_dev_term->phys_dev, pQueueFamilyPropertyCount,
                                                                      properties);
            for (uint32_t i = 0; i < *pQueueFamilyPropertyCount; ++i) {
                // Write to the VkQueueFamilyProperties2KHR struct
                memcpy(&pQueueFamilyProperties[i].queueFamilyProperties, &properties[i], sizeof(VkQueueFamilyProperties));

                if (pQueueFamilyProperties[i].pNext != NULL) {
                    loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                               "vkGetPhysicalDeviceQueueFamilyProperties2: Emulation found unrecognized structure type in "
                               "pQueueFamilyProperties[%d].pNext - this struct will be ignored",
                               i);
                }
            }
        }
    }
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2 *pMemoryProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    const struct loader_instance *inst = icd_term->this_instance;

    // Get the function pointer to use to call into the ICD. This could be the core or KHR version
    PFN_vkGetPhysicalDeviceMemoryProperties2 fpGetPhysicalDeviceMemoryProperties2 = NULL;
    if (inst != NULL && inst->enabled_known_extensions.khr_get_physical_device_properties2) {
        fpGetPhysicalDeviceMemoryProperties2 = icd_term->dispatch.GetPhysicalDeviceMemoryProperties2KHR;
    } else {
        fpGetPhysicalDeviceMemoryProperties2 = icd_term->dispatch.GetPhysicalDeviceMemoryProperties2;
    }

    if (fpGetPhysicalDeviceMemoryProperties2 != NULL || !inst->enabled_known_extensions.khr_get_physical_device_properties2) {
        // Pass the call to the driver
        fpGetPhysicalDeviceMemoryProperties2(phys_dev_term->phys_dev, pMemoryProperties);
    } else {
        // Emulate the call
        loader_log(
            icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
            "vkGetPhysicalDeviceMemoryProperties2: Emulating call in ICD \"%s\" using vkGetPhysicalDeviceMemoryProperties",
            icd_term->scanned_icd->lib_name);

        // Write to the VkPhysicalDeviceMemoryProperties2 struct
        icd_term->dispatch.GetPhysicalDeviceMemoryProperties(phys_dev_term->phys_dev, &pMemoryProperties->memoryProperties);

        if (pMemoryProperties->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceMemoryProperties2: Emulation found unrecognized structure type in "
                       "pMemoryProperties->pNext - this struct will be ignored");
        }
    }
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceSparseImageFormatProperties2(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2KHR *pFormatInfo, uint32_t *pPropertyCount,
    VkSparseImageFormatProperties2KHR *pProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    const struct loader_instance *inst = icd_term->this_instance;

    // Get the function pointer to use to call into the ICD. This could be the core or KHR version
    PFN_vkGetPhysicalDeviceSparseImageFormatProperties2 fpGetPhysicalDeviceSparseImageFormatProperties2 = NULL;
    if (inst != NULL && inst->enabled_known_extensions.khr_get_physical_device_properties2) {
        fpGetPhysicalDeviceSparseImageFormatProperties2 = icd_term->dispatch.GetPhysicalDeviceSparseImageFormatProperties2KHR;
    } else {
        fpGetPhysicalDeviceSparseImageFormatProperties2 = icd_term->dispatch.GetPhysicalDeviceSparseImageFormatProperties2;
    }

    if (fpGetPhysicalDeviceSparseImageFormatProperties2 != NULL || !inst->enabled_known_extensions.khr_get_physical_device_properties2) {
        // Pass the call to the driver
        fpGetPhysicalDeviceSparseImageFormatProperties2(phys_dev_term->phys_dev, pFormatInfo, pPropertyCount, pProperties);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceSparseImageFormatProperties2: Emulating call in ICD \"%s\" using "
                   "vkGetPhysicalDeviceSparseImageFormatProperties",
                   icd_term->scanned_icd->lib_name);

        if (pFormatInfo->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceSparseImageFormatProperties2: Emulation found unrecognized structure type in "
                       "pFormatInfo->pNext - this struct will be ignored");
        }

        if (pProperties == NULL || *pPropertyCount == 0) {
            // Write to pPropertyCount
            icd_term->dispatch.GetPhysicalDeviceSparseImageFormatProperties(
                phys_dev_term->phys_dev, pFormatInfo->format, pFormatInfo->type, pFormatInfo->samples, pFormatInfo->usage,
                pFormatInfo->tiling, pPropertyCount, NULL);
        } else {
            // Allocate a temporary array for the output of the old function
            VkSparseImageFormatProperties *properties =
                loader_stack_alloc(*pPropertyCount * sizeof(VkSparseImageMemoryRequirements));
            if (properties == NULL) {
                *pPropertyCount = 0;
                loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "vkGetPhysicalDeviceSparseImageFormatProperties2: Out of memory - Failed to allocate array for "
                           "loader emulation.");
                return;
            }

            icd_term->dispatch.GetPhysicalDeviceSparseImageFormatProperties(
                phys_dev_term->phys_dev, pFormatInfo->format, pFormatInfo->type, pFormatInfo->samples, pFormatInfo->usage,
                pFormatInfo->tiling, pPropertyCount, properties);
            for (uint32_t i = 0; i < *pPropertyCount; ++i) {
                // Write to the VkSparseImageFormatProperties2KHR struct
                memcpy(&pProperties[i].properties, &properties[i], sizeof(VkSparseImageFormatProperties));

                if (pProperties[i].pNext != NULL) {
                    loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                               "vkGetPhysicalDeviceSparseImageFormatProperties2: Emulation found unrecognized structure type in "
                               "pProperties[%d].pNext - this struct will be ignored",
                               i);
                }
            }
        }
    }
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceExternalBufferProperties(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
    VkExternalBufferProperties *pExternalBufferProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    const struct loader_instance *inst = icd_term->this_instance;

    // Get the function pointer to use to call into the ICD. This could be the core or KHR version
    PFN_vkGetPhysicalDeviceExternalBufferProperties fpGetPhysicalDeviceExternalBufferProperties = NULL;
    if (inst != NULL && inst->enabled_known_extensions.khr_external_memory_capabilities) {
        fpGetPhysicalDeviceExternalBufferProperties = icd_term->dispatch.GetPhysicalDeviceExternalBufferPropertiesKHR;
    } else {
        fpGetPhysicalDeviceExternalBufferProperties = icd_term->dispatch.GetPhysicalDeviceExternalBufferProperties;
    }

    if (fpGetPhysicalDeviceExternalBufferProperties || !inst->enabled_known_extensions.khr_external_memory_capabilities) {
        // Pass the call to the driver
        fpGetPhysicalDeviceExternalBufferProperties(phys_dev_term->phys_dev, pExternalBufferInfo, pExternalBufferProperties);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceExternalBufferProperties: Emulating call in ICD \"%s\"", icd_term->scanned_icd->lib_name);

        if (pExternalBufferInfo->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceExternalBufferProperties: Emulation found unrecognized structure type in "
                       "pExternalBufferInfo->pNext - this struct will be ignored");
        }

        // Fill in everything being unsupported
        memset(&pExternalBufferProperties->externalMemoryProperties, 0, sizeof(VkExternalMemoryPropertiesKHR));

        if (pExternalBufferProperties->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceExternalBufferProperties: Emulation found unrecognized structure type in "
                       "pExternalBufferProperties->pNext - this struct will be ignored");
        }
    }
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceExternalSemaphoreProperties(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo,
    VkExternalSemaphoreProperties *pExternalSemaphoreProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    const struct loader_instance *inst = icd_term->this_instance;

    // Get the function pointer to use to call into the ICD. This could be the core or KHR version
    PFN_vkGetPhysicalDeviceExternalSemaphoreProperties fpGetPhysicalDeviceExternalSemaphoreProperties = NULL;
    if (inst != NULL && inst->enabled_known_extensions.khr_external_semaphore_capabilities) {
        fpGetPhysicalDeviceExternalSemaphoreProperties = icd_term->dispatch.GetPhysicalDeviceExternalSemaphorePropertiesKHR;
    } else {
        fpGetPhysicalDeviceExternalSemaphoreProperties = icd_term->dispatch.GetPhysicalDeviceExternalSemaphoreProperties;
    }

    if (fpGetPhysicalDeviceExternalSemaphoreProperties != NULL || !inst->enabled_known_extensions.khr_external_semaphore_capabilities) {
        // Pass the call to the driver
        fpGetPhysicalDeviceExternalSemaphoreProperties(phys_dev_term->phys_dev, pExternalSemaphoreInfo, pExternalSemaphoreProperties);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceExternalSemaphoreProperties: Emulating call in ICD \"%s\"",
                   icd_term->scanned_icd->lib_name);

        if (pExternalSemaphoreInfo->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceExternalSemaphoreProperties: Emulation found unrecognized structure type in "
                       "pExternalSemaphoreInfo->pNext - this struct will be ignored");
        }

        // Fill in everything being unsupported
        pExternalSemaphoreProperties->exportFromImportedHandleTypes = 0;
        pExternalSemaphoreProperties->compatibleHandleTypes = 0;
        pExternalSemaphoreProperties->externalSemaphoreFeatures = 0;

        if (pExternalSemaphoreProperties->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceExternalSemaphoreProperties: Emulation found unrecognized structure type in "
                       "pExternalSemaphoreProperties->pNext - this struct will be ignored");
        }
    }
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceExternalFenceProperties(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo *pExternalFenceInfo,
    VkExternalFenceProperties *pExternalFenceProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    const struct loader_instance *inst = icd_term->this_instance;

    // Get the function pointer to use to call into the ICD. This could be the core or KHR version
    PFN_vkGetPhysicalDeviceExternalFenceProperties fpGetPhysicalDeviceExternalFenceProperties = NULL;
    if (inst != NULL && inst->enabled_known_extensions.khr_external_fence_capabilities) {
        fpGetPhysicalDeviceExternalFenceProperties = icd_term->dispatch.GetPhysicalDeviceExternalFencePropertiesKHR;
    } else {
        fpGetPhysicalDeviceExternalFenceProperties = icd_term->dispatch.GetPhysicalDeviceExternalFenceProperties;
    }

    if (fpGetPhysicalDeviceExternalFenceProperties != NULL || !inst->enabled_known_extensions.khr_external_fence_capabilities) {
        // Pass the call to the driver
        fpGetPhysicalDeviceExternalFenceProperties(phys_dev_term->phys_dev, pExternalFenceInfo, pExternalFenceProperties);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceExternalFenceProperties: Emulating call in ICD \"%s\"", icd_term->scanned_icd->lib_name);

        if (pExternalFenceInfo->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceExternalFenceProperties: Emulation found unrecognized structure type in "
                       "pExternalFenceInfo->pNext - this struct will be ignored");
        }

        // Fill in everything being unsupported
        pExternalFenceProperties->exportFromImportedHandleTypes = 0;
        pExternalFenceProperties->compatibleHandleTypes = 0;
        pExternalFenceProperties->externalFenceFeatures = 0;

        if (pExternalFenceProperties->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceExternalFenceProperties: Emulation found unrecognized structure type in "
                       "pExternalFenceProperties->pNext - this struct will be ignored");
        }
    }
}
