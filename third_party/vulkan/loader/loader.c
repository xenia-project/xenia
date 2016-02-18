/*
 *
 * Copyright (c) 2014-2016 The Khronos Group Inc.
 * Copyright (c) 2014-2016 Valve Corporation
 * Copyright (c) 2014-2016 LunarG, Inc.
 * Copyright (C) 2015 Google Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and/or associated documentation files (the "Materials"), to
 * deal in the Materials without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Materials, and to permit persons to whom the Materials are
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice(s) and this permission notice shall be included in
 * all copies or substantial portions of the Materials.
 *
 * THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE MATERIALS OR THE
 * USE OR OTHER DEALINGS IN THE MATERIALS.
 *
 * Author: Jon Ashburn <jon@lunarg.com>
 * Author: Courtney Goeltzenleuchter <courtney@LunarG.com>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include <sys/types.h>
#if defined(_WIN32)
#include "dirent_on_windows.h"
#else // _WIN32
#include <dirent.h>
#endif // _WIN32
#include "vk_loader_platform.h"
#include "loader.h"
#include "gpa_helper.h"
#include "table_ops.h"
#include "debug_report.h"
#include "wsi.h"
#include "vulkan/vk_icd.h"
#include "cJSON.h"
#include "murmurhash.h"

static loader_platform_dl_handle
loader_add_layer_lib(const struct loader_instance *inst, const char *chain_type,
                     struct loader_layer_properties *layer_prop);

static void loader_remove_layer_lib(struct loader_instance *inst,
                                    struct loader_layer_properties *layer_prop);

struct loader_struct loader = {0};
// TLS for instance for alloc/free callbacks
THREAD_LOCAL_DECL struct loader_instance *tls_instance;

static bool loader_init_generic_list(const struct loader_instance *inst,
                                     struct loader_generic_list *list_info,
                                     size_t element_size);

static size_t loader_platform_combine_path(char *dest, size_t len, ...);

struct loader_phys_dev_per_icd {
    uint32_t count;
    VkPhysicalDevice *phys_devs;
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

const char *std_validation_str = "VK_LAYER_LUNARG_standard_validation";

// This table contains the loader's instance dispatch table, which contains
// default functions if no instance layers are activated.  This contains
// pointers to "terminator functions".
const VkLayerInstanceDispatchTable instance_disp = {
    .GetInstanceProcAddr = vkGetInstanceProcAddr,
    .DestroyInstance = loader_DestroyInstance,
    .EnumeratePhysicalDevices = loader_EnumeratePhysicalDevices,
    .GetPhysicalDeviceFeatures = loader_GetPhysicalDeviceFeatures,
    .GetPhysicalDeviceFormatProperties =
        loader_GetPhysicalDeviceFormatProperties,
    .GetPhysicalDeviceImageFormatProperties =
        loader_GetPhysicalDeviceImageFormatProperties,
    .GetPhysicalDeviceProperties = loader_GetPhysicalDeviceProperties,
    .GetPhysicalDeviceQueueFamilyProperties =
        loader_GetPhysicalDeviceQueueFamilyProperties,
    .GetPhysicalDeviceMemoryProperties =
        loader_GetPhysicalDeviceMemoryProperties,
    .EnumerateDeviceExtensionProperties =
        loader_EnumerateDeviceExtensionProperties,
    .EnumerateDeviceLayerProperties = loader_EnumerateDeviceLayerProperties,
    .GetPhysicalDeviceSparseImageFormatProperties =
        loader_GetPhysicalDeviceSparseImageFormatProperties,
    .DestroySurfaceKHR = loader_DestroySurfaceKHR,
    .GetPhysicalDeviceSurfaceSupportKHR =
        loader_GetPhysicalDeviceSurfaceSupportKHR,
    .GetPhysicalDeviceSurfaceCapabilitiesKHR =
        loader_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .GetPhysicalDeviceSurfaceFormatsKHR =
        loader_GetPhysicalDeviceSurfaceFormatsKHR,
    .GetPhysicalDeviceSurfacePresentModesKHR =
        loader_GetPhysicalDeviceSurfacePresentModesKHR,
    .CreateDebugReportCallbackEXT = loader_CreateDebugReportCallback,
    .DestroyDebugReportCallbackEXT = loader_DestroyDebugReportCallback,
    .DebugReportMessageEXT = loader_DebugReportMessage,
#ifdef VK_USE_PLATFORM_MIR_KHR
    .CreateMirSurfaceKHR = loader_CreateMirSurfaceKHR,
    .GetPhysicalDeviceMirPresentationSupportKHR =
        loader_GetPhysicalDeviceMirPresentationSupportKHR,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .CreateWaylandSurfaceKHR = loader_CreateWaylandSurfaceKHR,
    .GetPhysicalDeviceWaylandPresentationSupportKHR =
        loader_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
    .CreateWin32SurfaceKHR = loader_CreateWin32SurfaceKHR,
    .GetPhysicalDeviceWin32PresentationSupportKHR =
        loader_GetPhysicalDeviceWin32PresentationSupportKHR,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
    .CreateXcbSurfaceKHR = loader_CreateXcbSurfaceKHR,
    .GetPhysicalDeviceXcbPresentationSupportKHR =
        loader_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .CreateXlibSurfaceKHR = loader_CreateXlibSurfaceKHR,
    .GetPhysicalDeviceXlibPresentationSupportKHR =
        loader_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif
#ifdef VK_USE_PLATFORM_ANDROID_KHR
    .CreateAndroidSurfaceKHR = loader_CreateAndroidSurfaceKHR,
#endif
};

LOADER_PLATFORM_THREAD_ONCE_DECLARATION(once_init);

void *loader_heap_alloc(const struct loader_instance *instance, size_t size,
                        VkSystemAllocationScope alloc_scope) {
    if (instance && instance->alloc_callbacks.pfnAllocation) {
        /* TODO: What should default alignment be? 1, 4, 8, other? */
        return instance->alloc_callbacks.pfnAllocation(
            instance->alloc_callbacks.pUserData, size, sizeof(int),
            alloc_scope);
    }
    return malloc(size);
}

void loader_heap_free(const struct loader_instance *instance, void *pMemory) {
    if (pMemory == NULL)
        return;
    if (instance && instance->alloc_callbacks.pfnFree) {
        instance->alloc_callbacks.pfnFree(instance->alloc_callbacks.pUserData,
                                          pMemory);
        return;
    }
    free(pMemory);
}

void *loader_heap_realloc(const struct loader_instance *instance, void *pMemory,
                          size_t orig_size, size_t size,
                          VkSystemAllocationScope alloc_scope) {
    if (pMemory == NULL || orig_size == 0)
        return loader_heap_alloc(instance, size, alloc_scope);
    if (size == 0) {
        loader_heap_free(instance, pMemory);
        return NULL;
    }
    // TODO use the callback realloc function
    if (instance && instance->alloc_callbacks.pfnAllocation) {
        if (size <= orig_size) {
            memset(((uint8_t *)pMemory) + size, 0, orig_size - size);
            return pMemory;
        }
        /* TODO: What should default alignment be? 1, 4, 8, other? */
        void *new_ptr = instance->alloc_callbacks.pfnAllocation(
            instance->alloc_callbacks.pUserData, size, sizeof(int),
            alloc_scope);
        if (!new_ptr)
            return NULL;
        memcpy(new_ptr, pMemory, orig_size);
        instance->alloc_callbacks.pfnFree(instance->alloc_callbacks.pUserData,
                                          pMemory);
        return new_ptr;
    }
    return realloc(pMemory, size);
}

void *loader_tls_heap_alloc(size_t size) {
    return loader_heap_alloc(tls_instance, size,
                             VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
}

void loader_tls_heap_free(void *pMemory) {
    loader_heap_free(tls_instance, pMemory);
}

void loader_log(const struct loader_instance *inst, VkFlags msg_type,
                       int32_t msg_code, const char *format, ...) {
    char msg[512];
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = vsnprintf(msg, sizeof(msg), format, ap);
    if ((ret >= (int)sizeof(msg)) || ret < 0) {
        msg[sizeof(msg) - 1] = '\0';
    }
    va_end(ap);

    if (inst) {
        util_DebugReportMessage(inst, msg_type,
                                VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT,
                                (uint64_t)inst, 0, msg_code, "loader", msg);
    }

    if (!(msg_type & g_loader_log_msgs)) {
        return;
    }

#if defined(WIN32)
    OutputDebugString(msg);
    OutputDebugString("\n");
#endif
    fputs(msg, stderr);
    fputc('\n', stderr);
}

#if defined(WIN32)
static char *loader_get_next_path(char *path);
/**
* Find the list of registry files (names within a key) in key "location".
*
* This function looks in the registry (hive = DEFAULT_VK_REGISTRY_HIVE) key as
*given in "location"
* for a list or name/values which are added to a returned list (function return
*value).
* The DWORD values within the key must be 0 or they are skipped.
* Function return is a string with a ';'  separated list of filenames.
* Function return is NULL if no valid name/value pairs  are found in the key,
* or the key is not found.
*
* \returns
* A string list of filenames as pointer.
* When done using the returned string list, pointer should be freed.
*/
static char *loader_get_registry_files(const struct loader_instance *inst,
                                       char *location) {
    LONG rtn_value;
    HKEY hive, key;
    DWORD access_flags;
    char name[2048];
    char *out = NULL;
    char *loc = location;
    char *next;
    DWORD idx = 0;
    DWORD name_size = sizeof(name);
    DWORD value;
    DWORD total_size = 4096;
    DWORD value_size = sizeof(value);

    while (*loc) {
        next = loader_get_next_path(loc);
        hive = DEFAULT_VK_REGISTRY_HIVE;
        access_flags = KEY_QUERY_VALUE;
        rtn_value = RegOpenKeyEx(hive, loc, 0, access_flags, &key);
        if (rtn_value != ERROR_SUCCESS) {
            // We still couldn't find the key, so give up:
            loc = next;
            continue;
        }

        while ((rtn_value = RegEnumValue(key, idx++, name, &name_size, NULL,
                                         NULL, (LPBYTE)&value, &value_size)) ==
               ERROR_SUCCESS) {
            if (value_size == sizeof(value) && value == 0) {
                if (out == NULL) {
                    out = loader_heap_alloc(
                        inst, total_size, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
                    out[0] = '\0';
                } else if (strlen(out) + name_size + 1 > total_size) {
                    out = loader_heap_realloc(
                        inst, out, total_size, total_size * 2,
                        VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
                    total_size *= 2;
                }
                if (out == NULL) {
                    loader_log(
                        inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                        "Out of memory, failed loader_get_registry_files");
                    return NULL;
                }
                if (strlen(out) == 0)
                    snprintf(out, name_size + 1, "%s", name);
                else
                    snprintf(out + strlen(out), name_size + 2, "%c%s",
                             PATH_SEPERATOR, name);
            }
            name_size = 2048;
        }
        loc = next;
    }

    return out;
}

#endif // WIN32

/**
 * Combine path elements, separating each element with the platform-specific
 * directory separator, and save the combined string to a destination buffer,
 * not exceeding the given length. Path elements are given as variadic args,
 * with a NULL element terminating the list.
 *
 * \returns the total length of the combined string, not including an ASCII
 * NUL termination character. This length may exceed the available storage:
 * in this case, the written string will be truncated to avoid a buffer
 * overrun, and the return value will greater than or equal to the storage
 * size. A NULL argument may be provided as the destination buffer in order
 * to determine the required string length without actually writing a string.
 */

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
                snprintf(dest + required_len, len - required_len, "%c",
                         DIRECTORY_SYMBOL);
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

/**
 * Given string of three part form "maj.min.pat" convert to a vulkan version
 * number.
 */
static uint32_t loader_make_version(const char *vers_str) {
    uint32_t vers = 0, major = 0, minor = 0, patch = 0;
    char *minor_str = NULL;
    char *patch_str = NULL;
    char *cstr;
    char *str;

    if (!vers_str)
        return vers;
    cstr = loader_stack_alloc(strlen(vers_str) + 1);
    strcpy(cstr, vers_str);
    while ((str = strchr(cstr, '.')) != NULL) {
        if (minor_str == NULL) {
            minor_str = str + 1;
            *str = '\0';
            major = atoi(cstr);
        } else if (patch_str == NULL) {
            patch_str = str + 1;
            *str = '\0';
            minor = atoi(minor_str);
        } else {
            return vers;
        }
        cstr = str + 1;
    }
    patch = atoi(patch_str);

    return VK_MAKE_VERSION(major, minor, patch);
}

bool compare_vk_extension_properties(const VkExtensionProperties *op1,
                                     const VkExtensionProperties *op2) {
    return strcmp(op1->extensionName, op2->extensionName) == 0 ? true : false;
}

/**
 * Search the given ext_array for an extension
 * matching the given vk_ext_prop
 */
bool has_vk_extension_property_array(const VkExtensionProperties *vk_ext_prop,
                                     const uint32_t count,
                                     const VkExtensionProperties *ext_array) {
    for (uint32_t i = 0; i < count; i++) {
        if (compare_vk_extension_properties(vk_ext_prop, &ext_array[i]))
            return true;
    }
    return false;
}

/**
 * Search the given ext_list for an extension
 * matching the given vk_ext_prop
 */
bool has_vk_extension_property(const VkExtensionProperties *vk_ext_prop,
                               const struct loader_extension_list *ext_list) {
    for (uint32_t i = 0; i < ext_list->count; i++) {
        if (compare_vk_extension_properties(&ext_list->list[i], vk_ext_prop))
            return true;
    }
    return false;
}

static inline bool loader_is_layer_type_device(const enum layer_type type) {
    if ((type & VK_LAYER_TYPE_DEVICE_EXPLICIT) ||
        (type & VK_LAYER_TYPE_DEVICE_IMPLICIT))
        return true;
    return false;
}

/*
 * Search the given layer list for a layer matching the given layer name
 */
static struct loader_layer_properties *
loader_get_layer_property(const char *name,
                          const struct loader_layer_list *layer_list) {
    for (uint32_t i = 0; i < layer_list->count; i++) {
        const VkLayerProperties *item = &layer_list->list[i].info;
        if (strcmp(name, item->layerName) == 0)
            return &layer_list->list[i];
    }
    return NULL;
}

/**
 * Get the next unused layer property in the list. Init the property to zero.
 */
static struct loader_layer_properties *
loader_get_next_layer_property(const struct loader_instance *inst,
                               struct loader_layer_list *layer_list) {
    if (layer_list->capacity == 0) {
        layer_list->list =
            loader_heap_alloc(inst, sizeof(struct loader_layer_properties) * 64,
                              VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (layer_list->list == NULL) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "Out of memory can't add any layer properties to list");
            return NULL;
        }
        memset(layer_list->list, 0,
               sizeof(struct loader_layer_properties) * 64);
        layer_list->capacity = sizeof(struct loader_layer_properties) * 64;
    }

    // ensure enough room to add an entry
    if ((layer_list->count + 1) * sizeof(struct loader_layer_properties) >
        layer_list->capacity) {
        layer_list->list = loader_heap_realloc(
            inst, layer_list->list, layer_list->capacity,
            layer_list->capacity * 2, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (layer_list->list == NULL) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "realloc failed for layer list");
        }
        layer_list->capacity *= 2;
    }

    layer_list->count++;
    return &(layer_list->list[layer_list->count - 1]);
}

/**
 * Remove all layer properties entrys from the list
 */
void loader_delete_layer_properties(const struct loader_instance *inst,
                                    struct loader_layer_list *layer_list) {
    uint32_t i, j;
    struct loader_device_extension_list *dev_ext_list;
    if (!layer_list)
        return;

    for (i = 0; i < layer_list->count; i++) {
        loader_destroy_generic_list(
            inst, (struct loader_generic_list *)&layer_list->list[i]
                      .instance_extension_list);
        dev_ext_list = &layer_list->list[i].device_extension_list;
        if (dev_ext_list->capacity > 0 &&
            dev_ext_list->list->entrypoint_count > 0) {
            for (j = 0; j < dev_ext_list->list->entrypoint_count; j++) {
                loader_heap_free(inst, dev_ext_list->list->entrypoints[j]);
            }
            loader_heap_free(inst, dev_ext_list->list->entrypoints);
        }
        loader_destroy_generic_list(inst,
                                    (struct loader_generic_list *)dev_ext_list);
    }
    layer_list->count = 0;

    if (layer_list->capacity > 0) {
        layer_list->capacity = 0;
        loader_heap_free(inst, layer_list->list);
    }
}

static void loader_add_instance_extensions(
    const struct loader_instance *inst,
    const PFN_vkEnumerateInstanceExtensionProperties fp_get_props,
    const char *lib_name, struct loader_extension_list *ext_list) {
    uint32_t i, count = 0;
    VkExtensionProperties *ext_props;
    VkResult res;

    if (!fp_get_props) {
        /* No EnumerateInstanceExtensionProperties defined */
        return;
    }

    res = fp_get_props(NULL, &count, NULL);
    if (res != VK_SUCCESS) {
        loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                   "Error getting Instance extension count from %s", lib_name);
        return;
    }

    if (count == 0) {
        /* No ExtensionProperties to report */
        return;
    }

    ext_props = loader_stack_alloc(count * sizeof(VkExtensionProperties));

    res = fp_get_props(NULL, &count, ext_props);
    if (res != VK_SUCCESS) {
        loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                   "Error getting Instance extensions from %s", lib_name);
        return;
    }

    for (i = 0; i < count; i++) {
        char spec_version[64];

        snprintf(spec_version, sizeof(spec_version), "%d.%d.%d",
                 VK_MAJOR(ext_props[i].specVersion),
                 VK_MINOR(ext_props[i].specVersion),
                 VK_PATCH(ext_props[i].specVersion));
        loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0,
                   "Instance Extension: %s (%s) version %s",
                   ext_props[i].extensionName, lib_name, spec_version);
        loader_add_to_ext_list(inst, ext_list, 1, &ext_props[i]);
    }

    return;
}

/*
 * Initialize ext_list with the physical device extensions.
 * The extension properties are passed as inputs in count and ext_props.
 */
static VkResult
loader_init_device_extensions(const struct loader_instance *inst,
                              struct loader_physical_device *phys_dev,
                              uint32_t count, VkExtensionProperties *ext_props,
                              struct loader_extension_list *ext_list) {
    VkResult res;
    uint32_t i;

    if (!loader_init_generic_list(inst, (struct loader_generic_list *)ext_list,
                                  sizeof(VkExtensionProperties))) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    for (i = 0; i < count; i++) {
        char spec_version[64];

        snprintf(spec_version, sizeof(spec_version), "%d.%d.%d",
                 VK_MAJOR(ext_props[i].specVersion),
                 VK_MINOR(ext_props[i].specVersion),
                 VK_PATCH(ext_props[i].specVersion));
        loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0,
                   "Device Extension: %s (%s) version %s",
                   ext_props[i].extensionName,
                   phys_dev->this_icd->this_icd_lib->lib_name, spec_version);
        res = loader_add_to_ext_list(inst, ext_list, 1, &ext_props[i]);
        if (res != VK_SUCCESS)
            return res;
    }

    return VK_SUCCESS;
}

static VkResult loader_add_device_extensions(
    const struct loader_instance *inst, struct loader_icd *icd,
    VkPhysicalDevice physical_device, const char *lib_name,
    struct loader_extension_list *ext_list) {
    uint32_t i, count;
    VkResult res;
    VkExtensionProperties *ext_props;

    res = icd->EnumerateDeviceExtensionProperties(physical_device, NULL, &count,
                                                  NULL);
    if (res == VK_SUCCESS && count > 0) {
        ext_props = loader_stack_alloc(count * sizeof(VkExtensionProperties));
        if (!ext_props)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        res = icd->EnumerateDeviceExtensionProperties(physical_device, NULL,
                                                      &count, ext_props);
        if (res != VK_SUCCESS)
            return res;
        for (i = 0; i < count; i++) {
            char spec_version[64];

            snprintf(spec_version, sizeof(spec_version), "%d.%d.%d",
                     VK_MAJOR(ext_props[i].specVersion),
                     VK_MINOR(ext_props[i].specVersion),
                     VK_PATCH(ext_props[i].specVersion));
            loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0,
                       "Device Extension: %s (%s) version %s",
                       ext_props[i].extensionName, lib_name, spec_version);
            res = loader_add_to_ext_list(inst, ext_list, 1, &ext_props[i]);
            if (res != VK_SUCCESS)
                return res;
        }
    } else {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "Error getting physical device extension info count from "
                   "library %s",
                   lib_name);
        return res;
    }

    return VK_SUCCESS;
}

static bool loader_init_generic_list(const struct loader_instance *inst,
                                     struct loader_generic_list *list_info,
                                     size_t element_size) {
    list_info->capacity = 32 * element_size;
    list_info->list = loader_heap_alloc(inst, list_info->capacity,
                                        VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (list_info->list == NULL) {
        return false;
    }
    memset(list_info->list, 0, list_info->capacity);
    list_info->count = 0;
    return true;
}

void loader_destroy_generic_list(const struct loader_instance *inst,
                                 struct loader_generic_list *list) {
    loader_heap_free(inst, list->list);
    list->count = 0;
    list->capacity = 0;
}

/*
 * Append non-duplicate extension properties defined in props
 * to the given ext_list.
 * Return
 *  Vk_SUCCESS on success
 */
VkResult loader_add_to_ext_list(const struct loader_instance *inst,
                                struct loader_extension_list *ext_list,
                                uint32_t prop_list_count,
                                const VkExtensionProperties *props) {
    uint32_t i;
    const VkExtensionProperties *cur_ext;

    if (ext_list->list == NULL || ext_list->capacity == 0) {
        loader_init_generic_list(inst, (struct loader_generic_list *)ext_list,
                                 sizeof(VkExtensionProperties));
    }

    if (ext_list->list == NULL)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    for (i = 0; i < prop_list_count; i++) {
        cur_ext = &props[i];

        // look for duplicates
        if (has_vk_extension_property(cur_ext, ext_list)) {
            continue;
        }

        // add to list at end
        // check for enough capacity
        if (ext_list->count * sizeof(VkExtensionProperties) >=
            ext_list->capacity) {

            ext_list->list = loader_heap_realloc(
                inst, ext_list->list, ext_list->capacity,
                ext_list->capacity * 2, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

            if (ext_list->list == NULL)
                return VK_ERROR_OUT_OF_HOST_MEMORY;

            // double capacity
            ext_list->capacity *= 2;
        }

        memcpy(&ext_list->list[ext_list->count], cur_ext,
               sizeof(VkExtensionProperties));
        ext_list->count++;
    }
    return VK_SUCCESS;
}

/*
 * Append one extension property defined in props with entrypoints
 * defined in entrys to the given ext_list.
 * Return
 *  Vk_SUCCESS on success
 */
VkResult
loader_add_to_dev_ext_list(const struct loader_instance *inst,
                           struct loader_device_extension_list *ext_list,
                           const VkExtensionProperties *props,
                           uint32_t entry_count, char **entrys) {
    uint32_t idx;
    if (ext_list->list == NULL || ext_list->capacity == 0) {
        loader_init_generic_list(inst, (struct loader_generic_list *)ext_list,
                                 sizeof(struct loader_dev_ext_props));
    }

    if (ext_list->list == NULL)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    idx = ext_list->count;
    // add to list at end
    // check for enough capacity
    if (idx * sizeof(struct loader_dev_ext_props) >= ext_list->capacity) {

        ext_list->list = loader_heap_realloc(
            inst, ext_list->list, ext_list->capacity, ext_list->capacity * 2,
            VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

        if (ext_list->list == NULL)
            return VK_ERROR_OUT_OF_HOST_MEMORY;

        // double capacity
        ext_list->capacity *= 2;
    }

    memcpy(&ext_list->list[idx].props, props,
           sizeof(struct loader_dev_ext_props));
    ext_list->list[idx].entrypoint_count = entry_count;
    ext_list->list[idx].entrypoints =
        loader_heap_alloc(inst, sizeof(char *) * entry_count,
                          VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (ext_list->list[idx].entrypoints == NULL)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    for (uint32_t i = 0; i < entry_count; i++) {
        ext_list->list[idx].entrypoints[i] = loader_heap_alloc(
            inst, strlen(entrys[i]) + 1, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (ext_list->list[idx].entrypoints[i] == NULL)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        strcpy(ext_list->list[idx].entrypoints[i], entrys[i]);
    }
    ext_list->count++;

    return VK_SUCCESS;
}

/**
 * Search the given search_list for any layers in the props list.
 * Add these to the output layer_list.  Don't add duplicates to the output
 * layer_list.
 */
static VkResult
loader_add_layer_names_to_list(const struct loader_instance *inst,
                               struct loader_layer_list *output_list,
                               uint32_t name_count, const char *const *names,
                               const struct loader_layer_list *search_list) {
    struct loader_layer_properties *layer_prop;
    VkResult err = VK_SUCCESS;

    for (uint32_t i = 0; i < name_count; i++) {
        const char *search_target = names[i];
        layer_prop = loader_get_layer_property(search_target, search_list);
        if (!layer_prop) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "Unable to find layer %s", search_target);
            err = VK_ERROR_LAYER_NOT_PRESENT;
            continue;
        }

        loader_add_to_layer_list(inst, output_list, 1, layer_prop);
    }

    return err;
}

/*
 * Manage lists of VkLayerProperties
 */
static bool loader_init_layer_list(const struct loader_instance *inst,
                                   struct loader_layer_list *list) {
    list->capacity = 32 * sizeof(struct loader_layer_properties);
    list->list = loader_heap_alloc(inst, list->capacity,
                                   VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (list->list == NULL) {
        return false;
    }
    memset(list->list, 0, list->capacity);
    list->count = 0;
    return true;
}

void loader_destroy_layer_list(const struct loader_instance *inst,
                               struct loader_layer_list *layer_list) {
    loader_heap_free(inst, layer_list->list);
    layer_list->count = 0;
    layer_list->capacity = 0;
}

/*
 * Manage list of layer libraries (loader_lib_info)
 */
static bool
loader_init_layer_library_list(const struct loader_instance *inst,
                               struct loader_layer_library_list *list) {
    list->capacity = 32 * sizeof(struct loader_lib_info);
    list->list = loader_heap_alloc(inst, list->capacity,
                                   VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (list->list == NULL) {
        return false;
    }
    memset(list->list, 0, list->capacity);
    list->count = 0;
    return true;
}

void loader_destroy_layer_library_list(const struct loader_instance *inst,
                                       struct loader_layer_library_list *list) {
    for (uint32_t i = 0; i < list->count; i++) {
        loader_heap_free(inst, list->list[i].lib_name);
    }
    loader_heap_free(inst, list->list);
    list->count = 0;
    list->capacity = 0;
}

void loader_add_to_layer_library_list(const struct loader_instance *inst,
                                      struct loader_layer_library_list *list,
                                      uint32_t item_count,
                                      const struct loader_lib_info *new_items) {
    uint32_t i;
    struct loader_lib_info *item;

    if (list->list == NULL || list->capacity == 0) {
        loader_init_layer_library_list(inst, list);
    }

    if (list->list == NULL)
        return;

    for (i = 0; i < item_count; i++) {
        item = (struct loader_lib_info *)&new_items[i];

        // look for duplicates
        for (uint32_t j = 0; j < list->count; j++) {
            if (strcmp(list->list[i].lib_name, new_items->lib_name) == 0) {
                continue;
            }
        }

        // add to list at end
        // check for enough capacity
        if (list->count * sizeof(struct loader_lib_info) >= list->capacity) {

            list->list = loader_heap_realloc(
                inst, list->list, list->capacity, list->capacity * 2,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
            // double capacity
            list->capacity *= 2;
        }

        memcpy(&list->list[list->count], item, sizeof(struct loader_lib_info));
        list->count++;
    }
}

/*
 * Search the given layer list for a list
 * matching the given VkLayerProperties
 */
bool has_vk_layer_property(const VkLayerProperties *vk_layer_prop,
                           const struct loader_layer_list *list) {
    for (uint32_t i = 0; i < list->count; i++) {
        if (strcmp(vk_layer_prop->layerName, list->list[i].info.layerName) == 0)
            return true;
    }
    return false;
}

/*
 * Search the given layer list for a layer
 * matching the given name
 */
bool has_layer_name(const char *name, const struct loader_layer_list *list) {
    for (uint32_t i = 0; i < list->count; i++) {
        if (strcmp(name, list->list[i].info.layerName) == 0)
            return true;
    }
    return false;
}

/*
 * Append non-duplicate layer properties defined in prop_list
 * to the given layer_info list
 */
void loader_add_to_layer_list(const struct loader_instance *inst,
                              struct loader_layer_list *list,
                              uint32_t prop_list_count,
                              const struct loader_layer_properties *props) {
    uint32_t i;
    struct loader_layer_properties *layer;

    if (list->list == NULL || list->capacity == 0) {
        loader_init_layer_list(inst, list);
    }

    if (list->list == NULL)
        return;

    for (i = 0; i < prop_list_count; i++) {
        layer = (struct loader_layer_properties *)&props[i];

        // look for duplicates
        if (has_vk_layer_property(&layer->info, list)) {
            continue;
        }

        // add to list at end
        // check for enough capacity
        if (list->count * sizeof(struct loader_layer_properties) >=
            list->capacity) {

            list->list = loader_heap_realloc(
                inst, list->list, list->capacity, list->capacity * 2,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
            // double capacity
            list->capacity *= 2;
        }

        memcpy(&list->list[list->count], layer,
               sizeof(struct loader_layer_properties));
        list->count++;
    }
}

/**
 * Search the search_list for any layer with a name
 * that matches the given name and a type that matches the given type
 * Add all matching layers to the found_list
 * Do not add if found loader_layer_properties is already
 * on the found_list.
 */
static void
loader_find_layer_name_add_list(const struct loader_instance *inst,
                                const char *name, const enum layer_type type,
                                const struct loader_layer_list *search_list,
                                struct loader_layer_list *found_list) {
    bool found = false;
    for (uint32_t i = 0; i < search_list->count; i++) {
        struct loader_layer_properties *layer_prop = &search_list->list[i];
        if (0 == strcmp(layer_prop->info.layerName, name) &&
            (layer_prop->type & type)) {
            /* Found a layer with the same name, add to found_list */
            loader_add_to_layer_list(inst, found_list, 1, layer_prop);
            found = true;
        }
    }
    if (!found) {
        loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                   "Warning, couldn't find layer name %s to activate", name);
    }
}

static VkExtensionProperties *
get_extension_property(const char *name,
                       const struct loader_extension_list *list) {
    for (uint32_t i = 0; i < list->count; i++) {
        if (strcmp(name, list->list[i].extensionName) == 0)
            return &list->list[i];
    }
    return NULL;
}

static VkExtensionProperties *
get_dev_extension_property(const char *name,
                           const struct loader_device_extension_list *list) {
    for (uint32_t i = 0; i < list->count; i++) {
        if (strcmp(name, list->list[i].props.extensionName) == 0)
            return &list->list[i].props;
    }
    return NULL;
}

/*
 * This function will return the pNext pointer of any
 * CreateInfo extensions that are not loader extensions.
 * This is used to skip past the loader extensions prepended
 * to the list during CreateInstance and CreateDevice.
 */
void *loader_strip_create_extensions(const void *pNext) {
    VkLayerInstanceCreateInfo *create_info = (VkLayerInstanceCreateInfo *)pNext;

    while (
        create_info &&
        (create_info->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
         create_info->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO)) {
        create_info = (VkLayerInstanceCreateInfo *)create_info->pNext;
    }

    return create_info;
}

/*
 * For Instance extensions implemented within the loader (i.e. DEBUG_REPORT
 * the extension must provide two entry points for the loader to use:
 * - "trampoline" entry point - this is the address returned by GetProcAddr
 * and will always do what's necessary to support a global call.
 * - "terminator" function - this function will be put at the end of the
 * instance chain and will contain the necessary logic to call / process
 * the extension for the appropriate ICDs that are available.
 * There is no generic mechanism for including these functions, the references
 * must be placed into the appropriate loader entry points.
 * GetInstanceProcAddr: call extension GetInstanceProcAddr to check for
 * GetProcAddr requests
 * loader_coalesce_extensions(void) - add extension records to the list of
 * global
 * extension available to the app.
 * instance_disp - add function pointer for terminator function to this array.
 * The extension itself should be in a separate file that will be
 * linked directly with the loader.
 */

void loader_get_icd_loader_instance_extensions(
    const struct loader_instance *inst, struct loader_icd_libs *icd_libs,
    struct loader_extension_list *inst_exts) {
    struct loader_extension_list icd_exts;
    loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0,
               "Build ICD instance extension list");
    // traverse scanned icd list adding non-duplicate extensions to the list
    for (uint32_t i = 0; i < icd_libs->count; i++) {
        loader_init_generic_list(inst, (struct loader_generic_list *)&icd_exts,
                                 sizeof(VkExtensionProperties));
        loader_add_instance_extensions(
            inst, icd_libs->list[i].EnumerateInstanceExtensionProperties,
            icd_libs->list[i].lib_name, &icd_exts);
        loader_add_to_ext_list(inst, inst_exts, icd_exts.count, icd_exts.list);
        loader_destroy_generic_list(inst,
                                    (struct loader_generic_list *)&icd_exts);
    };

    // Traverse loader's extensions, adding non-duplicate extensions to the list
    wsi_add_instance_extensions(inst, inst_exts);
    debug_report_add_instance_extensions(inst, inst_exts);
}

struct loader_physical_device *
loader_get_physical_device(const VkPhysicalDevice physdev) {
    uint32_t i;
    for (struct loader_instance *inst = loader.instances; inst;
         inst = inst->next) {
        for (i = 0; i < inst->total_gpu_count; i++) {
            // TODO this aliases physDevices within instances, need for this
            // function to go away
            if (inst->phys_devs[i].disp ==
                loader_get_instance_dispatch(physdev)) {
                return &inst->phys_devs[i];
            }
        }
    }
    return NULL;
}

struct loader_icd *loader_get_icd_and_device(const VkDevice device,
                                             struct loader_device **found_dev) {
    *found_dev = NULL;
    for (struct loader_instance *inst = loader.instances; inst;
         inst = inst->next) {
        for (struct loader_icd *icd = inst->icds; icd; icd = icd->next) {
            for (struct loader_device *dev = icd->logical_device_list; dev;
                 dev = dev->next)
                /* Value comparison of device prevents object wrapping by layers
                 */
                if (loader_get_dispatch(dev->device) ==
                    loader_get_dispatch(device)) {
                    *found_dev = dev;
                    return icd;
                }
        }
    }
    return NULL;
}

static void loader_destroy_logical_device(const struct loader_instance *inst,
                                          struct loader_device *dev) {
    loader_heap_free(inst, dev->app_extension_props);
    loader_destroy_layer_list(inst, &dev->activated_layer_list);
    loader_heap_free(inst, dev);
}

static struct loader_device *
loader_add_logical_device(const struct loader_instance *inst,
                          struct loader_device **device_list) {
    struct loader_device *new_dev;

    new_dev = loader_heap_alloc(inst, sizeof(struct loader_device),
                                VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
    if (!new_dev) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "Failed to alloc struct loader-device");
        return NULL;
    }

    memset(new_dev, 0, sizeof(struct loader_device));

    new_dev->next = *device_list;
    *device_list = new_dev;
    return new_dev;
}

void loader_remove_logical_device(const struct loader_instance *inst,
                                  struct loader_icd *icd,
                                  struct loader_device *found_dev) {
    struct loader_device *dev, *prev_dev;

    if (!icd || !found_dev)
        return;

    prev_dev = NULL;
    dev = icd->logical_device_list;
    while (dev && dev != found_dev) {
        prev_dev = dev;
        dev = dev->next;
    }

    if (prev_dev)
        prev_dev->next = found_dev->next;
    else
        icd->logical_device_list = found_dev->next;
    loader_destroy_logical_device(inst, found_dev);
}

static void loader_icd_destroy(struct loader_instance *ptr_inst,
                               struct loader_icd *icd) {
    ptr_inst->total_icd_count--;
    for (struct loader_device *dev = icd->logical_device_list; dev;) {
        struct loader_device *next_dev = dev->next;
        loader_destroy_logical_device(ptr_inst, dev);
        dev = next_dev;
    }

    loader_heap_free(ptr_inst, icd);
}

static struct loader_icd *
loader_icd_create(const struct loader_instance *inst) {
    struct loader_icd *icd;

    icd = loader_heap_alloc(inst, sizeof(*icd),
                            VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (!icd)
        return NULL;

    memset(icd, 0, sizeof(*icd));

    return icd;
}

static struct loader_icd *
loader_icd_add(struct loader_instance *ptr_inst,
               const struct loader_scanned_icds *icd_lib) {
    struct loader_icd *icd;

    icd = loader_icd_create(ptr_inst);
    if (!icd)
        return NULL;

    icd->this_icd_lib = icd_lib;
    icd->this_instance = ptr_inst;

    /* prepend to the list */
    icd->next = ptr_inst->icds;
    ptr_inst->icds = icd;
    ptr_inst->total_icd_count++;

    return icd;
}

void loader_scanned_icd_clear(const struct loader_instance *inst,
                              struct loader_icd_libs *icd_libs) {
    if (icd_libs->capacity == 0)
        return;
    for (uint32_t i = 0; i < icd_libs->count; i++) {
        loader_platform_close_library(icd_libs->list[i].handle);
        loader_heap_free(inst, icd_libs->list[i].lib_name);
    }
    loader_heap_free(inst, icd_libs->list);
    icd_libs->capacity = 0;
    icd_libs->count = 0;
    icd_libs->list = NULL;
}

static void loader_scanned_icd_init(const struct loader_instance *inst,
                                    struct loader_icd_libs *icd_libs) {
    loader_scanned_icd_clear(inst, icd_libs);
    icd_libs->capacity = 8 * sizeof(struct loader_scanned_icds);
    icd_libs->list = loader_heap_alloc(inst, icd_libs->capacity,
                                       VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
}

static void loader_scanned_icd_add(const struct loader_instance *inst,
                                   struct loader_icd_libs *icd_libs,
                                   const char *filename, uint32_t api_version) {
    loader_platform_dl_handle handle;
    PFN_vkCreateInstance fp_create_inst;
    PFN_vkEnumerateInstanceExtensionProperties fp_get_inst_ext_props;
    PFN_vkGetInstanceProcAddr fp_get_proc_addr;
    struct loader_scanned_icds *new_node;

    /* TODO implement ref counting of libraries, for now this function leaves
       libraries open and the scanned_icd_clear closes them */
    // Used to call: dlopen(filename, RTLD_LAZY);
    handle = loader_platform_open_library(filename);
    if (!handle) {
        loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                   loader_platform_open_library_error(filename));
        return;
    }

    fp_get_proc_addr =
        loader_platform_get_proc_address(handle, "vk_icdGetInstanceProcAddr");
    if (!fp_get_proc_addr) {
        // Use deprecated interface
        fp_get_proc_addr =
            loader_platform_get_proc_address(handle, "vkGetInstanceProcAddr");
        if (!fp_get_proc_addr) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       loader_platform_get_proc_address_error(
                           "vk_icdGetInstanceProcAddr"));
            return;
        } else {
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "Using deprecated ICD interface of "
                       "vkGetInstanceProcAddr instead of "
                       "vk_icdGetInstanceProcAddr");
        }
        fp_create_inst =
            loader_platform_get_proc_address(handle, "vkCreateInstance");
        if (!fp_create_inst) {
            loader_log(
                inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                "Couldn't get vkCreateInstance via dlsym/loadlibrary from ICD");
            return;
        }
        fp_get_inst_ext_props = loader_platform_get_proc_address(
            handle, "vkEnumerateInstanceExtensionProperties");
        if (!fp_get_inst_ext_props) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "Couldn't get vkEnumerateInstanceExtensionProperties "
                       "via dlsym/loadlibrary from ICD");
            return;
        }
    } else {
        // Use newer interface
        fp_create_inst =
            (PFN_vkCreateInstance)fp_get_proc_addr(NULL, "vkCreateInstance");
        if (!fp_create_inst) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "Couldn't get vkCreateInstance via "
                       "vk_icdGetInstanceProcAddr from ICD");
            return;
        }
        fp_get_inst_ext_props =
            (PFN_vkEnumerateInstanceExtensionProperties)fp_get_proc_addr(
                NULL, "vkEnumerateInstanceExtensionProperties");
        if (!fp_get_inst_ext_props) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "Couldn't get vkEnumerateInstanceExtensionProperties "
                       "via vk_icdGetInstanceProcAddr from ICD");
            return;
        }
    }

    // check for enough capacity
    if ((icd_libs->count * sizeof(struct loader_scanned_icds)) >=
        icd_libs->capacity) {

        icd_libs->list = loader_heap_realloc(
            inst, icd_libs->list, icd_libs->capacity, icd_libs->capacity * 2,
            VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        // double capacity
        icd_libs->capacity *= 2;
    }
    new_node = &(icd_libs->list[icd_libs->count]);

    new_node->handle = handle;
    new_node->api_version = api_version;
    new_node->GetInstanceProcAddr = fp_get_proc_addr;
    new_node->EnumerateInstanceExtensionProperties = fp_get_inst_ext_props;
    new_node->CreateInstance = fp_create_inst;

    new_node->lib_name = (char *)loader_heap_alloc(
        inst, strlen(filename) + 1, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (!new_node->lib_name) {
        loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                   "Out of memory can't add icd");
        return;
    }
    strcpy(new_node->lib_name, filename);
    icd_libs->count++;
}

static bool loader_icd_init_entrys(struct loader_icd *icd, VkInstance inst,
                                   const PFN_vkGetInstanceProcAddr fp_gipa) {
/* initialize entrypoint function pointers */

#define LOOKUP_GIPA(func, required)                                            \
    do {                                                                       \
        icd->func = (PFN_vk##func)fp_gipa(inst, "vk" #func);                   \
        if (!icd->func && required) {                                          \
            loader_log((struct loader_instance *)inst,                         \
                       VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,                        \
                       loader_platform_get_proc_address_error("vk" #func));    \
            return false;                                                      \
        }                                                                      \
    } while (0)

    LOOKUP_GIPA(GetDeviceProcAddr, true);
    LOOKUP_GIPA(DestroyInstance, true);
    LOOKUP_GIPA(EnumeratePhysicalDevices, true);
    LOOKUP_GIPA(GetPhysicalDeviceFeatures, true);
    LOOKUP_GIPA(GetPhysicalDeviceFormatProperties, true);
    LOOKUP_GIPA(GetPhysicalDeviceImageFormatProperties, true);
    LOOKUP_GIPA(CreateDevice, true);
    LOOKUP_GIPA(GetPhysicalDeviceProperties, true);
    LOOKUP_GIPA(GetPhysicalDeviceMemoryProperties, true);
    LOOKUP_GIPA(GetPhysicalDeviceQueueFamilyProperties, true);
    LOOKUP_GIPA(EnumerateDeviceExtensionProperties, true);
    LOOKUP_GIPA(GetPhysicalDeviceSparseImageFormatProperties, true);
    LOOKUP_GIPA(CreateDebugReportCallbackEXT, false);
    LOOKUP_GIPA(DestroyDebugReportCallbackEXT, false);
    LOOKUP_GIPA(GetPhysicalDeviceSurfaceSupportKHR, false);
    LOOKUP_GIPA(GetPhysicalDeviceSurfaceCapabilitiesKHR, false);
    LOOKUP_GIPA(GetPhysicalDeviceSurfaceFormatsKHR, false);
    LOOKUP_GIPA(GetPhysicalDeviceSurfacePresentModesKHR, false);
#ifdef VK_USE_PLATFORM_WIN32_KHR
    LOOKUP_GIPA(GetPhysicalDeviceWin32PresentationSupportKHR, false);
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
    LOOKUP_GIPA(GetPhysicalDeviceXcbPresentationSupportKHR, false);
#endif

#undef LOOKUP_GIPA

    return true;
}

static void loader_debug_init(void) {
    const char *env, *orig;

    if (g_loader_debug > 0)
        return;

    g_loader_debug = 0;

    /* parse comma-separated debug options */
    orig = env = loader_getenv("VK_LOADER_DEBUG");
    while (env) {
        const char *p = strchr(env, ',');
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

        if (!p)
            break;

        env = p + 1;
    }

    loader_free_getenv(orig);
}

void loader_initialize(void) {
    // initialize mutexs
    loader_platform_thread_create_mutex(&loader_lock);
    loader_platform_thread_create_mutex(&loader_json_lock);

    // initialize logging
    loader_debug_init();

    // initial cJSON to use alloc callbacks
    cJSON_Hooks alloc_fns = {
        .malloc_fn = loader_tls_heap_alloc, .free_fn = loader_tls_heap_free,
    };
    cJSON_InitHooks(&alloc_fns);
}

struct loader_manifest_files {
    uint32_t count;
    char **filename_list;
};

/**
 * Get next file or dirname given a string list or registry key path
 *
 * \returns
 * A pointer to first char in the next path.
 * The next path (or NULL) in the list is returned in next_path.
 * Note: input string is modified in some cases. PASS IN A COPY!
 */
static char *loader_get_next_path(char *path) {
    uint32_t len;
    char *next;

    if (path == NULL)
        return NULL;
    next = strchr(path, PATH_SEPERATOR);
    if (next == NULL) {
        len = (uint32_t)strlen(path);
        next = path + len;
    } else {
        *next = '\0';
        next++;
    }

    return next;
}

/**
 * Given a path which is absolute or relative, expand the path if relative or
 * leave the path unmodified if absolute. The base path to prepend to relative
 * paths is given in rel_base.
 *
 * \returns
 * A string in out_fullpath of the full absolute path
 */
static void loader_expand_path(const char *path, const char *rel_base,
                               size_t out_size, char *out_fullpath) {
    if (loader_platform_is_path_absolute(path)) {
        // do not prepend a base to an absolute path
        rel_base = "";
    }

    loader_platform_combine_path(out_fullpath, out_size, rel_base, path, NULL);
}

/**
 * Given a filename (file)  and a list of paths (dir), try to find an existing
 * file in the paths.  If filename already is a path then no
 * searching in the given paths.
 *
 * \returns
 * A string in out_fullpath of either the full path or file.
 */
static void loader_get_fullpath(const char *file, const char *dirs,
                                size_t out_size, char *out_fullpath) {
    if (!loader_platform_is_path(file) && *dirs) {
        char *dirs_copy, *dir, *next_dir;

        dirs_copy = loader_stack_alloc(strlen(dirs) + 1);
        strcpy(dirs_copy, dirs);

        // find if file exists after prepending paths in given list
        for (dir = dirs_copy; *dir && (next_dir = loader_get_next_path(dir));
             dir = next_dir) {
            loader_platform_combine_path(out_fullpath, out_size, dir, file,
                                         NULL);
            if (loader_platform_file_exists(out_fullpath)) {
                return;
            }
        }
    }

    snprintf(out_fullpath, out_size, "%s", file);
}

/**
 * Read a JSON file into a buffer.
 *
 * \returns
 * A pointer to a cJSON object representing the JSON parse tree.
 * This returned buffer should be freed by caller.
 */
static cJSON *loader_get_json(const struct loader_instance *inst,
                              const char *filename) {
    FILE *file;
    char *json_buf;
    cJSON *json;
    size_t len;
    file = fopen(filename, "rb");
    if (!file) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "Couldn't open JSON file %s", filename);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    len = ftell(file);
    fseek(file, 0, SEEK_SET);
    json_buf = (char *)loader_stack_alloc(len + 1);
    if (json_buf == NULL) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "Out of memory can't get JSON file");
        fclose(file);
        return NULL;
    }
    if (fread(json_buf, sizeof(char), len, file) != len) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "fread failed can't get JSON file");
        fclose(file);
        return NULL;
    }
    fclose(file);
    json_buf[len] = '\0';

    // parse text from file
    json = cJSON_Parse(json_buf);
    if (json == NULL)
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "Can't parse JSON file %s", filename);
    return json;
}

/**
 * Do a deep copy of the loader_layer_properties structure.
 */
static void loader_copy_layer_properties(const struct loader_instance *inst,
                                         struct loader_layer_properties *dst,
                                         struct loader_layer_properties *src) {
    uint32_t cnt, i;
    memcpy(dst, src, sizeof(*src));
    dst->instance_extension_list.list =
        loader_heap_alloc(inst, sizeof(VkExtensionProperties) *
                                    src->instance_extension_list.count,
                          VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    dst->instance_extension_list.capacity =
        sizeof(VkExtensionProperties) * src->instance_extension_list.count;
    memcpy(dst->instance_extension_list.list, src->instance_extension_list.list,
           dst->instance_extension_list.capacity);
    dst->device_extension_list.list =
        loader_heap_alloc(inst, sizeof(struct loader_dev_ext_props) *
                                    src->device_extension_list.count,
                          VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

    dst->device_extension_list.capacity =
        sizeof(struct loader_dev_ext_props) * src->device_extension_list.count;
    memcpy(dst->device_extension_list.list, src->device_extension_list.list,
           dst->device_extension_list.capacity);
    if (src->device_extension_list.count > 0 &&
        src->device_extension_list.list->entrypoint_count > 0) {
        cnt = src->device_extension_list.list->entrypoint_count;
        dst->device_extension_list.list->entrypoints = loader_heap_alloc(
            inst, sizeof(char *) * cnt, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        for (i = 0; i < cnt; i++) {
            dst->device_extension_list.list->entrypoints[i] = loader_heap_alloc(
                inst,
                strlen(src->device_extension_list.list->entrypoints[i]) + 1,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
            strcpy(dst->device_extension_list.list->entrypoints[i],
                   src->device_extension_list.list->entrypoints[i]);
        }
    }
}

static bool
loader_find_layer_name_list(const char *name,
                            const struct loader_layer_list *layer_list) {
    if (!layer_list)
        return false;
    for (uint32_t j = 0; j < layer_list->count; j++)
        if (!strcmp(name, layer_list->list[j].info.layerName))
            return true;
    return false;
}

static bool loader_find_layer_name(const char *name, uint32_t layer_count,
                                   const char **layer_list) {
    if (!layer_list)
        return false;
    for (uint32_t j = 0; j < layer_count; j++)
        if (!strcmp(name, layer_list[j]))
            return true;
    return false;
}

static bool loader_find_layer_name_array(
    const char *name, uint32_t layer_count,
    const char layer_list[][VK_MAX_EXTENSION_NAME_SIZE]) {
    if (!layer_list)
        return false;
    for (uint32_t j = 0; j < layer_count; j++)
        if (!strcmp(name, layer_list[j]))
            return true;
    return false;
}

/**
 * Searches through an array of layer names (ppp_layer_names) looking for a
 * layer key_name.
 * If not found then simply returns updating nothing.
 * Otherwise, it uses expand_count, expand_names adding them to layer names.
 * Any duplicate (pre-existing) exapand_names in layer names are removed.
 * Expand names are added to the back/end of the list of layer names.
 * @param inst
 * @param layer_count
 * @param ppp_layer_names
 */
void loader_expand_layer_names(
    const struct loader_instance *inst, const char *key_name,
    uint32_t expand_count,
    const char expand_names[][VK_MAX_EXTENSION_NAME_SIZE],
    uint32_t *layer_count, char ***ppp_layer_names) {
    char **pp_layer_names, **pp_src_layers = *ppp_layer_names;

    if (!loader_find_layer_name(key_name, *layer_count,
                                (const char **)pp_src_layers))
        return; // didn't find the key_name in the list

    // since the total number of layers may expand, allocate new memory for the
    // array of pointers
    pp_layer_names =
        loader_heap_alloc(inst, (expand_count + *layer_count) * sizeof(char *),
                          VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);

    loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
               "Found meta layer %s, replacing with actual layer group",
               key_name);
    // In place removal of any expand_names found in layer_name (remove
    // duplicates)
    // Also remove the key_name
    uint32_t src_idx, dst_idx, cnt = *layer_count;
    for (src_idx = 0; src_idx < *layer_count; src_idx++) {
        if (loader_find_layer_name_array(pp_src_layers[src_idx], expand_count,
                                         expand_names)) {
            pp_src_layers[src_idx] = NULL;
            cnt--;
        } else if (!strcmp(pp_src_layers[src_idx], key_name)) {
            pp_src_layers[src_idx] = NULL;
            cnt--;
        }
        pp_layer_names[src_idx] = pp_src_layers[src_idx];
    }
    for (dst_idx = 0; dst_idx < cnt; dst_idx++) {
        if (pp_layer_names[dst_idx] == NULL) {
            src_idx = dst_idx + 1;
            while (src_idx < *layer_count && pp_src_layers[src_idx] == NULL)
                src_idx++;
            if (src_idx < *layer_count && pp_src_layers[src_idx] != NULL)
                pp_layer_names[dst_idx] = pp_src_layers[src_idx];
        }
    }

    // Add the expand_names to layer_names
    src_idx = 0;
    for (dst_idx = cnt; dst_idx < cnt + expand_count; dst_idx++) {
        pp_layer_names[dst_idx] = (char *)&expand_names[src_idx++][0];
    }
    *layer_count = expand_count + cnt;
    *ppp_layer_names = pp_layer_names;
    return;
}

/**
 * Restores the layer name list and count into the pCreatInfo structure.
 * If is_device == tru then pCreateInfo is a device structure else an instance
 * structure.
 * @param layer_count
 * @param layer_names
 * @param pCreateInfo
 */
void loader_unexpand_dev_layer_names(const struct loader_instance *inst,
                                     uint32_t layer_count, char **layer_names,
                                     char **layer_ptr,
                                     const VkDeviceCreateInfo *pCreateInfo) {
    uint32_t *p_cnt = (uint32_t *)&pCreateInfo->enabledLayerCount;
    *p_cnt = layer_count;

    char ***p_ptr = (char ***)&pCreateInfo->ppEnabledLayerNames;
    if ((char **)pCreateInfo->ppEnabledLayerNames != layer_ptr)
        loader_heap_free(inst, (void *)pCreateInfo->ppEnabledLayerNames);
    *p_ptr = layer_ptr;
    for (uint32_t i = 0; i < layer_count; i++) {
        char **pp_str = (char **)&pCreateInfo->ppEnabledLayerNames[i];
        *pp_str = layer_names[i];
    }
}

void loader_unexpand_inst_layer_names(const struct loader_instance *inst,
                                      uint32_t layer_count, char **layer_names,
                                      char **layer_ptr,
                                      const VkInstanceCreateInfo *pCreateInfo) {
    uint32_t *p_cnt = (uint32_t *)&pCreateInfo->enabledLayerCount;
    *p_cnt = layer_count;

    char ***p_ptr = (char ***)&pCreateInfo->ppEnabledLayerNames;
    if ((char **)pCreateInfo->ppEnabledLayerNames != layer_ptr)
        loader_heap_free(inst, (void *)pCreateInfo->ppEnabledLayerNames);
    *p_ptr = layer_ptr;
    for (uint32_t i = 0; i < layer_count; i++) {
        char **pp_str = (char **)&pCreateInfo->ppEnabledLayerNames[i];
        *pp_str = layer_names[i];
    }
}

/**
 * Searches through the existing instance and device layer lists looking for
 * the set of required layer names. If found then it adds a meta property to the
 * layer list.
 * Assumes the required layers are the same for both instance and device lists.
 * @param inst
 * @param layer_count  number of layers in layer_names
 * @param layer_names  array of required layer names
 * @param layer_instance_list
 * @param layer_device_list
 */
static void loader_add_layer_property_meta(
    const struct loader_instance *inst, uint32_t layer_count,
    const char layer_names[][VK_MAX_EXTENSION_NAME_SIZE],
    struct loader_layer_list *layer_instance_list,
    struct loader_layer_list *layer_device_list) {
    uint32_t i, j;
    bool found;
    struct loader_layer_list *layer_list;

    if (0 == layer_count ||
        NULL == layer_instance_list ||
        NULL == layer_device_list ||
        (layer_count > layer_instance_list->count &&
                         layer_count > layer_device_list->count))
        return;

    for (j = 0; j < 2; j++) {
        if (j == 0)
            layer_list = layer_instance_list;
        else
            layer_list = layer_device_list;
        found = true;
        for (i = 0; i < layer_count; i++) {
            if (loader_find_layer_name_list(layer_names[i], layer_list))
                continue;
            found = false;
            break;
        }

        struct loader_layer_properties *props;
        if (found) {
            props = loader_get_next_layer_property(inst, layer_list);
            props->type = VK_LAYER_TYPE_META_EXPLICT;
            strncpy(props->info.description, "LunarG Standard Validation Layer",
                    sizeof(props->info.description));
            props->info.implementationVersion = 1;
            strncpy(props->info.layerName, std_validation_str,
                    sizeof(props->info.layerName));
            // TODO what about specVersion? for now insert loader's built
            // version
            props->info.specVersion = VK_API_VERSION;
        }
    }
}

/**
 * Given a cJSON struct (json) of the top level JSON object from layer manifest
 * file, add entry to the layer_list.
 * Fill out the layer_properties in this list entry from the input cJSON object.
 *
 * \returns
 * void
 * layer_list has a new entry and initialized accordingly.
 * If the json input object does not have all the required fields no entry
 * is added to the list.
 */
static void
loader_add_layer_properties(const struct loader_instance *inst,
                            struct loader_layer_list *layer_instance_list,
                            struct loader_layer_list *layer_device_list,
                            cJSON *json, bool is_implicit, char *filename) {
    /* Fields in layer manifest file that are required:
     * (required) “file_format_version”
     * following are required in the "layer" object:
     * (required) "name"
     * (required) "type"
     * (required) “library_path”
     * (required) “api_version”
     * (required) “implementation_version”
     * (required) “description”
     * (required for implicit layers) “disable_environment”
     *
     * First get all required items and if any missing abort
     */

    cJSON *item, *layer_node, *ext_item;
    char *temp;
    char *name, *type, *library_path, *api_version;
    char *implementation_version, *description;
    cJSON *disable_environment;
    int i, j;
    VkExtensionProperties ext_prop;
    item = cJSON_GetObjectItem(json, "file_format_version");
    if (item == NULL) {
        return;
    }
    char *file_vers = cJSON_PrintUnformatted(item);
    loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
               "Found manifest file %s, version %s", filename, file_vers);
    if (strcmp(file_vers, "\"1.0.0\"") != 0)
        loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                   "Unexpected manifest file version (expected 1.0.0), may "
                   "cause errors");
    loader_tls_heap_free(file_vers);

    layer_node = cJSON_GetObjectItem(json, "layer");
    if (layer_node == NULL) {
        loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                   "Can't find \"layer\" object in manifest JSON file, "
                   "skipping this file");
        return;
    }

    // loop through all "layer" objects in the file
    do {
#define GET_JSON_OBJECT(node, var)                                             \
    {                                                                          \
        var = cJSON_GetObjectItem(node, #var);                                 \
        if (var == NULL) {                                                     \
            layer_node = layer_node->next;                                     \
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,                  \
                       "Didn't find required layer object %s in manifest "     \
                       "JSON file, skipping this layer",                       \
                       #var);                                                  \
            continue;                                                          \
        }                                                                      \
    }
#define GET_JSON_ITEM(node, var)                                               \
    {                                                                          \
        item = cJSON_GetObjectItem(node, #var);                                \
        if (item == NULL) {                                                    \
            layer_node = layer_node->next;                                     \
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,                  \
                       "Didn't find required layer value %s in manifest JSON " \
                       "file, skipping this layer",                            \
                       #var);                                                  \
            continue;                                                          \
        }                                                                      \
        temp = cJSON_Print(item);                                              \
        temp[strlen(temp) - 1] = '\0';                                         \
        var = loader_stack_alloc(strlen(temp) + 1);                            \
        strcpy(var, &temp[1]);                                                 \
        loader_tls_heap_free(temp);                                            \
    }
        GET_JSON_ITEM(layer_node, name)
        GET_JSON_ITEM(layer_node, type)
        GET_JSON_ITEM(layer_node, library_path)
        GET_JSON_ITEM(layer_node, api_version)
        GET_JSON_ITEM(layer_node, implementation_version)
        GET_JSON_ITEM(layer_node, description)
        if (is_implicit) {
            GET_JSON_OBJECT(layer_node, disable_environment)
        }
#undef GET_JSON_ITEM
#undef GET_JSON_OBJECT

        // add list entry
        struct loader_layer_properties *props = NULL;
        if (!strcmp(type, "DEVICE")) {
            if (layer_device_list == NULL) {
                layer_node = layer_node->next;
                continue;
            }
            props = loader_get_next_layer_property(inst, layer_device_list);
            props->type = (is_implicit) ? VK_LAYER_TYPE_DEVICE_IMPLICIT
                                        : VK_LAYER_TYPE_DEVICE_EXPLICIT;
        }
        if (!strcmp(type, "INSTANCE")) {
            if (layer_instance_list == NULL) {
                layer_node = layer_node->next;
                continue;
            }
            props = loader_get_next_layer_property(inst, layer_instance_list);
            props->type = (is_implicit) ? VK_LAYER_TYPE_INSTANCE_IMPLICIT
                                        : VK_LAYER_TYPE_INSTANCE_EXPLICIT;
        }
        if (!strcmp(type, "GLOBAL")) {
            if (layer_instance_list != NULL)
                props =
                    loader_get_next_layer_property(inst, layer_instance_list);
            else if (layer_device_list != NULL)
                props = loader_get_next_layer_property(inst, layer_device_list);
            else {
                layer_node = layer_node->next;
                continue;
            }
            props->type = (is_implicit) ? VK_LAYER_TYPE_GLOBAL_IMPLICIT
                                        : VK_LAYER_TYPE_GLOBAL_EXPLICIT;
        }

        if (props == NULL) {
            layer_node = layer_node->next;
            continue;
        }

        strncpy(props->info.layerName, name, sizeof(props->info.layerName));
        props->info.layerName[sizeof(props->info.layerName) - 1] = '\0';

        char *fullpath = props->lib_name;
        char *rel_base;
        if (loader_platform_is_path(library_path)) {
            // a relative or absolute path
            char *name_copy = loader_stack_alloc(strlen(filename) + 1);
            strcpy(name_copy, filename);
            rel_base = loader_platform_dirname(name_copy);
            loader_expand_path(library_path, rel_base, MAX_STRING_SIZE,
                               fullpath);
        } else {
            // a filename which is assumed in a system directory
            loader_get_fullpath(library_path, DEFAULT_VK_LAYERS_PATH,
                                MAX_STRING_SIZE, fullpath);
        }
        props->info.specVersion = loader_make_version(api_version);
        props->info.implementationVersion = atoi(implementation_version);
        strncpy((char *)props->info.description, description,
                sizeof(props->info.description));
        props->info.description[sizeof(props->info.description) - 1] = '\0';
        if (is_implicit) {
            strncpy(props->disable_env_var.name,
                    disable_environment->child->string,
                    sizeof(props->disable_env_var.name));
            props->disable_env_var
                .name[sizeof(props->disable_env_var.name) - 1] = '\0';
            strncpy(props->disable_env_var.value,
                    disable_environment->child->valuestring,
                    sizeof(props->disable_env_var.value));
            props->disable_env_var
                .value[sizeof(props->disable_env_var.value) - 1] = '\0';
        }

/**
 * Now get all optional items and objects and put in list:
 * functions
 * instance_extensions
 * device_extensions
 * enable_environment (implicit layers only)
 */
#define GET_JSON_OBJECT(node, var)                                             \
    { var = cJSON_GetObjectItem(node, #var); }
#define GET_JSON_ITEM(node, var)                                               \
    {                                                                          \
        item = cJSON_GetObjectItem(node, #var);                                \
        if (item != NULL) {                                                    \
            temp = cJSON_Print(item);                                          \
            temp[strlen(temp) - 1] = '\0';                                     \
            var = loader_stack_alloc(strlen(temp) + 1);                        \
            strcpy(var, &temp[1]);                                             \
            loader_tls_heap_free(temp);                                        \
        }                                                                      \
    }

        cJSON *instance_extensions, *device_extensions, *functions,
            *enable_environment;
        cJSON *entrypoints;
        char *vkGetInstanceProcAddr, *vkGetDeviceProcAddr, *spec_version;
        char **entry_array;
        vkGetInstanceProcAddr = NULL;
        vkGetDeviceProcAddr = NULL;
        spec_version = NULL;
        entrypoints = NULL;
        entry_array = NULL;
        /**
         * functions
         *     vkGetInstanceProcAddr
         *     vkGetDeviceProcAddr
         */
        GET_JSON_OBJECT(layer_node, functions)
        if (functions != NULL) {
            GET_JSON_ITEM(functions, vkGetInstanceProcAddr)
            GET_JSON_ITEM(functions, vkGetDeviceProcAddr)
            if (vkGetInstanceProcAddr != NULL)
                strncpy(props->functions.str_gipa, vkGetInstanceProcAddr,
                        sizeof(props->functions.str_gipa));
            props->functions.str_gipa[sizeof(props->functions.str_gipa) - 1] =
                '\0';
            if (vkGetDeviceProcAddr != NULL)
                strncpy(props->functions.str_gdpa, vkGetDeviceProcAddr,
                        sizeof(props->functions.str_gdpa));
            props->functions.str_gdpa[sizeof(props->functions.str_gdpa) - 1] =
                '\0';
        }
        /**
         * instance_extensions
         * array of
         *     name
         *     spec_version
         */
        GET_JSON_OBJECT(layer_node, instance_extensions)
        if (instance_extensions != NULL) {
            int count = cJSON_GetArraySize(instance_extensions);
            for (i = 0; i < count; i++) {
                ext_item = cJSON_GetArrayItem(instance_extensions, i);
                GET_JSON_ITEM(ext_item, name)
                GET_JSON_ITEM(ext_item, spec_version)
                if (name != NULL) {
                    strncpy(ext_prop.extensionName, name,
                            sizeof(ext_prop.extensionName));
                    ext_prop.extensionName[sizeof(ext_prop.extensionName) - 1] =
                        '\0';
                }
                ext_prop.specVersion = atoi(spec_version);
                loader_add_to_ext_list(inst, &props->instance_extension_list, 1,
                                       &ext_prop);
            }
        }
        /**
         * device_extensions
         * array of
         *     name
         *     spec_version
         *     entrypoints
         */
        GET_JSON_OBJECT(layer_node, device_extensions)
        if (device_extensions != NULL) {
            int count = cJSON_GetArraySize(device_extensions);
            for (i = 0; i < count; i++) {
                ext_item = cJSON_GetArrayItem(device_extensions, i);
                GET_JSON_ITEM(ext_item, name)
                GET_JSON_ITEM(ext_item, spec_version)
                if (name != NULL) {
                    strncpy(ext_prop.extensionName, name,
                            sizeof(ext_prop.extensionName));
                    ext_prop.extensionName[sizeof(ext_prop.extensionName) - 1] =
                        '\0';
                }
                ext_prop.specVersion = atoi(spec_version);
                // entrypoints = cJSON_GetObjectItem(ext_item, "entrypoints");
                GET_JSON_OBJECT(ext_item, entrypoints)
                int entry_count;
                if (entrypoints == NULL) {
                    loader_add_to_dev_ext_list(inst,
                                               &props->device_extension_list,
                                               &ext_prop, 0, NULL);
                    continue;
                }
                entry_count = cJSON_GetArraySize(entrypoints);
                if (entry_count)
                    entry_array = (char **)loader_stack_alloc(sizeof(char *) *
                                                              entry_count);
                for (j = 0; j < entry_count; j++) {
                    ext_item = cJSON_GetArrayItem(entrypoints, j);
                    if (ext_item != NULL) {
                        temp = cJSON_Print(ext_item);
                        temp[strlen(temp) - 1] = '\0';
                        entry_array[j] = loader_stack_alloc(strlen(temp) + 1);
                        strcpy(entry_array[j], &temp[1]);
                        loader_tls_heap_free(temp);
                    }
                }
                loader_add_to_dev_ext_list(inst, &props->device_extension_list,
                                           &ext_prop, entry_count, entry_array);
            }
        }
        if (is_implicit) {
            GET_JSON_OBJECT(layer_node, enable_environment)

            // enable_environment is optional
            if (enable_environment) {
                strncpy(props->enable_env_var.name,
                        enable_environment->child->string,
                        sizeof(props->enable_env_var.name));
                props->enable_env_var
                    .name[sizeof(props->enable_env_var.name) - 1] = '\0';
                strncpy(props->enable_env_var.value,
                        enable_environment->child->valuestring,
                        sizeof(props->enable_env_var.value));
                props->enable_env_var
                    .value[sizeof(props->enable_env_var.value) - 1] = '\0';
            }
        }
#undef GET_JSON_ITEM
#undef GET_JSON_OBJECT
        // for global layers need to add them to both device and instance list
        if (!strcmp(type, "GLOBAL")) {
            struct loader_layer_properties *dev_props;
            if (layer_instance_list == NULL || layer_device_list == NULL) {
                layer_node = layer_node->next;
                continue;
            }
            dev_props = loader_get_next_layer_property(inst, layer_device_list);
            // copy into device layer list
            loader_copy_layer_properties(inst, dev_props, props);
        }
        layer_node = layer_node->next;
    } while (layer_node != NULL);
    return;
}

/**
 * Find the Vulkan library manifest files.
 *
 * This function scans the location or env_override directories/files
 * for a list of JSON manifest files.  If env_override is non-NULL
 * and has a valid value. Then the location is ignored.  Otherwise
 * location is used to look for manifest files. The location
 * is interpreted as  Registry path on Windows and a directory path(s)
 * on Linux.
 *
 * \returns
 * A string list of manifest files to be opened in out_files param.
 * List has a pointer to string for each manifest filename.
 * When done using the list in out_files, pointers should be freed.
 * Location or override  string lists can be either files or directories as
 *follows:
 *            | location | override
 * --------------------------------
 * Win ICD    | files    | files
 * Win Layer  | files    | dirs
 * Linux ICD  | dirs     | files
 * Linux Layer| dirs     | dirs
 */
static void loader_get_manifest_files(const struct loader_instance *inst,
                                      const char *env_override, bool is_layer,
                                      const char *location,
                                      struct loader_manifest_files *out_files) {
    char *override = NULL;
    char *loc;
    char *file, *next_file, *name;
    size_t alloced_count = 64;
    char full_path[2048];
    DIR *sysdir = NULL;
    bool list_is_dirs = false;
    struct dirent *dent;

    out_files->count = 0;
    out_files->filename_list = NULL;

    if (env_override != NULL && (override = loader_getenv(env_override))) {
#if !defined(_WIN32)
        if (geteuid() != getuid()) {
            /* Don't allow setuid apps to use the env var: */
            loader_free_getenv(override);
            override = NULL;
        }
#endif
    }

    if (location == NULL) {
        loader_log(
            inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
            "Can't get manifest files with NULL location, env_override=%s",
            env_override);
        return;
    }

#if defined(_WIN32)
    list_is_dirs = (is_layer && override != NULL) ? true : false;
#else
    list_is_dirs = (override == NULL || is_layer) ? true : false;
#endif
    // Make a copy of the input we are using so it is not modified
    // Also handle getting the location(s) from registry on Windows
    if (override == NULL) {
        loc = loader_stack_alloc(strlen(location) + 1);
        if (loc == NULL) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "Out of memory can't get manifest files");
            return;
        }
        strcpy(loc, location);
#if defined(_WIN32)
        loc = loader_get_registry_files(inst, loc);
        if (loc == NULL) {
            if (!is_layer) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "Registry lookup failed can't get ICD manifest "
                           "files, do you have a Vulkan driver installed");
            } else {
                // warning only for layers
                loader_log(
                    inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                    "Registry lookup failed can't get layer manifest files");
            }
            return;
        }
#endif
    } else {
        loc = loader_stack_alloc(strlen(override) + 1);
        if (loc == NULL) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "Out of memory can't get manifest files");
            return;
        }
        strcpy(loc, override);
        loader_free_getenv(override);
    }

    // Print out the paths being searched if debugging is enabled
    loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0,
               "Searching the following paths for manifest files: %s\n", loc);

    file = loc;
    while (*file) {
        next_file = loader_get_next_path(file);
        if (list_is_dirs) {
            sysdir = opendir(file);
            name = NULL;
            if (sysdir) {
                dent = readdir(sysdir);
                if (dent == NULL)
                    break;
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
                           "Out of memory can't get manifest files");
                return;
            }
            strcpy(dir, loc);

            loader_get_fullpath(file, dir, sizeof(full_path), full_path);

            name = full_path;
#endif
        }
        while (name) {
            /* Look for files ending with ".json" suffix */
            uint32_t nlen = (uint32_t)strlen(name);
            const char *suf = name + nlen - 5;
            if ((nlen > 5) && !strncmp(suf, ".json", 5)) {
                if (out_files->count == 0) {
                    out_files->filename_list =
                        loader_heap_alloc(inst, alloced_count * sizeof(char *),
                                          VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
                } else if (out_files->count == alloced_count) {
                    out_files->filename_list =
                        loader_heap_realloc(inst, out_files->filename_list,
                                            alloced_count * sizeof(char *),
                                            alloced_count * sizeof(char *) * 2,
                                            VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
                    alloced_count *= 2;
                }
                if (out_files->filename_list == NULL) {
                    loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                               "Out of memory can't alloc manifest file list");
                    return;
                }
                out_files->filename_list[out_files->count] = loader_heap_alloc(
                    inst, strlen(name) + 1, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
                if (out_files->filename_list[out_files->count] == NULL) {
                    loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                               "Out of memory can't get manifest files");
                    return;
                }
                strcpy(out_files->filename_list[out_files->count], name);
                out_files->count++;
            } else if (!list_is_dirs) {
                loader_log(
                    inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                    "Skipping manifest file %s, file name must end in .json",
                    name);
            }
            if (list_is_dirs) {
                dent = readdir(sysdir);
                if (dent == NULL)
                    break;
                name = &(dent->d_name[0]);
                loader_get_fullpath(name, file, sizeof(full_path), full_path);
                name = full_path;
            } else {
                break;
            }
        }
        if (sysdir)
            closedir(sysdir);
        file = next_file;
    }
    return;
}

void loader_init_icd_lib_list() {}

void loader_destroy_icd_lib_list() {}
/**
 * Try to find the Vulkan ICD driver(s).
 *
 * This function scans the default system loader path(s) or path
 * specified by the \c VK_ICD_FILENAMES environment variable in
 * order to find loadable VK ICDs manifest files. From these
 * manifest files it finds the ICD libraries.
 *
 * \returns
 * a list of icds that were discovered
 */
void loader_icd_scan(const struct loader_instance *inst,
                     struct loader_icd_libs *icds) {
    char *file_str;
    struct loader_manifest_files manifest_files;

    loader_scanned_icd_init(inst, icds);
    // Get a list of manifest files for ICDs
    loader_get_manifest_files(inst, "VK_ICD_FILENAMES", false,
                              DEFAULT_VK_DRIVERS_INFO, &manifest_files);
    if (manifest_files.count == 0)
        return;
    loader_platform_thread_lock_mutex(&loader_json_lock);
    for (uint32_t i = 0; i < manifest_files.count; i++) {
        file_str = manifest_files.filename_list[i];
        if (file_str == NULL)
            continue;

        cJSON *json;
        json = loader_get_json(inst, file_str);
        if (!json)
            continue;
        cJSON *item, *itemICD;
        item = cJSON_GetObjectItem(json, "file_format_version");
        if (item == NULL) {
            loader_platform_thread_unlock_mutex(&loader_json_lock);
            return;
        }
        char *file_vers = cJSON_Print(item);
        loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "Found manifest file %s, version %s", file_str, file_vers);
        if (strcmp(file_vers, "\"1.0.0\"") != 0)
            loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "Unexpected manifest file version (expected 1.0.0), may "
                       "cause errors");
        loader_tls_heap_free(file_vers);
        itemICD = cJSON_GetObjectItem(json, "ICD");
        if (itemICD != NULL) {
            item = cJSON_GetObjectItem(itemICD, "library_path");
            if (item != NULL) {
                char *temp = cJSON_Print(item);
                if (!temp || strlen(temp) == 0) {
                    loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                               "Can't find \"library_path\" in ICD JSON file "
                               "%s, skipping",
                               file_str);
                    loader_tls_heap_free(temp);
                    loader_heap_free(inst, file_str);
                    cJSON_Delete(json);
                    continue;
                }
                // strip out extra quotes
                temp[strlen(temp) - 1] = '\0';
                char *library_path = loader_stack_alloc(strlen(temp) + 1);
                strcpy(library_path, &temp[1]);
                loader_tls_heap_free(temp);
                if (!library_path || strlen(library_path) == 0) {
                    loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                               "Can't find \"library_path\" in ICD JSON file "
                               "%s, skipping",
                               file_str);
                    loader_heap_free(inst, file_str);
                    cJSON_Delete(json);
                    continue;
                }
                char fullpath[MAX_STRING_SIZE];
                // Print out the paths being searched if debugging is enabled
                loader_log(
                    inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0,
                    "Searching for ICD drivers named %s default dir %s\n",
                    library_path, DEFAULT_VK_DRIVERS_PATH);
                if (loader_platform_is_path(library_path)) {
                    // a relative or absolute path
                    char *name_copy = loader_stack_alloc(strlen(file_str) + 1);
                    char *rel_base;
                    strcpy(name_copy, file_str);
                    rel_base = loader_platform_dirname(name_copy);
                    loader_expand_path(library_path, rel_base, sizeof(fullpath),
                                       fullpath);
                } else {
                    // a filename which is assumed in a system directory
                    loader_get_fullpath(library_path, DEFAULT_VK_DRIVERS_PATH,
                                        sizeof(fullpath), fullpath);
                }

                uint32_t vers = 0;
                item = cJSON_GetObjectItem(itemICD, "api_version");
                if (item != NULL) {
                    temp = cJSON_Print(item);
                    vers = loader_make_version(temp);
                    loader_tls_heap_free(temp);
                }
                loader_scanned_icd_add(inst, icds, fullpath, vers);
            } else
                loader_log(inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                           "Can't find \"library_path\" object in ICD JSON "
                           "file %s, skipping",
                           file_str);
        } else
            loader_log(
                inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                "Can't find \"ICD\" object in ICD JSON file %s, skipping",
                file_str);

        loader_heap_free(inst, file_str);
        cJSON_Delete(json);
    }
    loader_heap_free(inst, manifest_files.filename_list);
    loader_platform_thread_unlock_mutex(&loader_json_lock);
}

void loader_layer_scan(const struct loader_instance *inst,
                       struct loader_layer_list *instance_layers,
                       struct loader_layer_list *device_layers) {
    char *file_str;
    struct loader_manifest_files
        manifest_files[2]; // [0] = explicit, [1] = implicit
    cJSON *json;
    uint32_t i;
    uint32_t implicit;

    // Get a list of manifest files for  explicit layers
    loader_get_manifest_files(inst, LAYERS_PATH_ENV, true,
                              DEFAULT_VK_ELAYERS_INFO, &manifest_files[0]);
    // Pass NULL for environment variable override - implicit layers are not
    // overridden by LAYERS_PATH_ENV
    loader_get_manifest_files(inst, NULL, true, DEFAULT_VK_ILAYERS_INFO,
                              &manifest_files[1]);
    if (manifest_files[0].count == 0 && manifest_files[1].count == 0)
        return;

#if 0 // TODO
    /**
     * We need a list of the layer libraries, not just a list of
     * the layer properties (a layer library could expose more than
     * one layer property). This list of scanned layers would be
     * used to check for global and physicaldevice layer properties.
     */
    if (!loader_init_layer_library_list(&loader.scanned_layer_libraries)) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "Alloc for layer list failed: %s line: %d", __FILE__, __LINE__);
        return;
    }
#endif

    /* cleanup any previously scanned libraries */
    loader_delete_layer_properties(inst, instance_layers);
    loader_delete_layer_properties(inst, device_layers);

    loader_platform_thread_lock_mutex(&loader_json_lock);
    for (implicit = 0; implicit < 2; implicit++) {
        for (i = 0; i < manifest_files[implicit].count; i++) {
            file_str = manifest_files[implicit].filename_list[i];
            if (file_str == NULL)
                continue;

            // parse file into JSON struct
            json = loader_get_json(inst, file_str);
            if (!json) {
                continue;
            }

            // TODO error if device layers expose instance_extensions
            // TODO error if instance layers expose device extensions
            loader_add_layer_properties(inst, instance_layers, device_layers,
                                        json, (implicit == 1), file_str);

            loader_heap_free(inst, file_str);
            cJSON_Delete(json);
        }
    }
    if (manifest_files[0].count != 0)
        loader_heap_free(inst, manifest_files[0].filename_list);

    if (manifest_files[1].count != 0)
        loader_heap_free(inst, manifest_files[1].filename_list);

    // add a meta layer for validation if the validation layers are all present
    loader_add_layer_property_meta(
        inst, sizeof(std_validation_names) / sizeof(std_validation_names[0]),
        std_validation_names, instance_layers, device_layers);

    loader_platform_thread_unlock_mutex(&loader_json_lock);
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
loader_gpa_instance_internal(VkInstance inst, const char *pName) {
    if (!strcmp(pName, "vkGetInstanceProcAddr"))
        return (void *)loader_gpa_instance_internal;
    if (!strcmp(pName, "vkCreateInstance"))
        return (void *)loader_CreateInstance;
    if (!strcmp(pName, "vkCreateDevice"))
        return (void *)loader_create_device_terminator;

    // inst is not wrapped
    if (inst == VK_NULL_HANDLE) {
        return NULL;
    }
    VkLayerInstanceDispatchTable *disp_table =
        *(VkLayerInstanceDispatchTable **)inst;
    void *addr;

    if (disp_table == NULL)
        return NULL;

    addr = loader_lookup_instance_dispatch_table(disp_table, pName);
    if (addr) {
        return addr;
    }

    if (disp_table->GetInstanceProcAddr == NULL) {
        return NULL;
    }
    return disp_table->GetInstanceProcAddr(inst, pName);
}

/**
 * Initialize device_ext dispatch table entry as follows:
 * If dev == NULL find all logical devices created within this instance and
 *  init the entry (given by idx) in the ext dispatch table.
 * If dev != NULL only initialize the entry in the given dev's dispatch table.
 * The initialization value is gotten by calling down the device chain with
 * GDPA.
 * If GDPA returns NULL then don't initialize the dispatch table entry.
 */
static void loader_init_dispatch_dev_ext_entry(struct loader_instance *inst,
                                               struct loader_device *dev,
                                               uint32_t idx,
                                               const char *funcName)

{
    void *gdpa_value;
    if (dev != NULL) {
        gdpa_value = dev->loader_dispatch.core_dispatch.GetDeviceProcAddr(
            dev->device, funcName);
        if (gdpa_value != NULL)
            dev->loader_dispatch.ext_dispatch.DevExt[idx] =
                (PFN_vkDevExt)gdpa_value;
    } else {
        for (uint32_t i = 0; i < inst->total_icd_count; i++) {
            struct loader_icd *icd = &inst->icds[i];
            struct loader_device *dev = icd->logical_device_list;
            while (dev) {
                gdpa_value =
                    dev->loader_dispatch.core_dispatch.GetDeviceProcAddr(
                        dev->device, funcName);
                if (gdpa_value != NULL)
                    dev->loader_dispatch.ext_dispatch.DevExt[idx] =
                        (PFN_vkDevExt)gdpa_value;
                dev = dev->next;
            }
        }
    }
}

/**
 * Find all dev extension in the hash table  and initialize the dispatch table
 * for dev  for each of those extension entrypoints found in hash table.

 */
static void loader_init_dispatch_dev_ext(struct loader_instance *inst,
                                         struct loader_device *dev) {
    for (uint32_t i = 0; i < MAX_NUM_DEV_EXTS; i++) {
        if (inst->disp_hash[i].func_name != NULL)
            loader_init_dispatch_dev_ext_entry(inst, dev, i,
                                               inst->disp_hash[i].func_name);
    }
}

static bool loader_check_icds_for_address(struct loader_instance *inst,
                                          const char *funcName) {
    struct loader_icd *icd;
    icd = inst->icds;
    while (icd) {
        if (icd->this_icd_lib->GetInstanceProcAddr(icd->instance, funcName))
            // this icd supports funcName
            return true;
        icd = icd->next;
    }

    return false;
}

static void loader_free_dev_ext_table(struct loader_instance *inst) {
    for (uint32_t i = 0; i < MAX_NUM_DEV_EXTS; i++) {
        loader_heap_free(inst, inst->disp_hash[i].func_name);
        loader_heap_free(inst, inst->disp_hash[i].list.index);
    }
    memset(inst->disp_hash, 0, sizeof(inst->disp_hash));
}

static bool loader_add_dev_ext_table(struct loader_instance *inst,
                                     uint32_t *ptr_idx, const char *funcName) {
    uint32_t i;
    uint32_t idx = *ptr_idx;
    struct loader_dispatch_hash_list *list = &inst->disp_hash[idx].list;

    if (!inst->disp_hash[idx].func_name) {
        // no entry here at this idx, so use it
        assert(list->capacity == 0);
        inst->disp_hash[idx].func_name = (char *)loader_heap_alloc(
            inst, strlen(funcName) + 1, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (inst->disp_hash[idx].func_name == NULL) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_add_dev_ext_table() can't allocate memory for "
                       "func_name");
            return false;
        }
        strncpy(inst->disp_hash[idx].func_name, funcName, strlen(funcName) + 1);
        return true;
    }

    // check for enough capacity
    if (list->capacity == 0) {
        list->index = loader_heap_alloc(inst, 8 * sizeof(*(list->index)),
                                        VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (list->index == NULL) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader_add_dev_ext_table() can't allocate list memory");
            return false;
        }
        list->capacity = 8 * sizeof(*(list->index));
    } else if (list->capacity < (list->count + 1) * sizeof(*(list->index))) {
        list->index = loader_heap_realloc(inst, list->index, list->capacity,
                                          list->capacity * 2,
                                          VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (list->index == NULL) {
            loader_log(
                inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                "loader_add_dev_ext_table() can't reallocate list memory");
            return false;
        }
        list->capacity *= 2;
    }

    // find an unused index in the hash table and use it
    i = (idx + 1) % MAX_NUM_DEV_EXTS;
    do {
        if (!inst->disp_hash[i].func_name) {
            assert(inst->disp_hash[i].list.capacity == 0);
            inst->disp_hash[i].func_name =
                (char *)loader_heap_alloc(inst, strlen(funcName) + 1,
                                          VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
            if (inst->disp_hash[i].func_name == NULL) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "loader_add_dev_ext_table() can't rallocate "
                           "func_name memory");
                return false;
            }
            strncpy(inst->disp_hash[i].func_name, funcName,
                    strlen(funcName) + 1);
            list->index[list->count] = i;
            list->count++;
            *ptr_idx = i;
            return true;
        }
        i = (i + 1) % MAX_NUM_DEV_EXTS;
    } while (i != idx);

    loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
               "loader_add_dev_ext_table() couldn't insert into hash table; is "
               "it full?");
    return false;
}

static bool loader_name_in_dev_ext_table(struct loader_instance *inst,
                                         uint32_t *idx, const char *funcName) {
    uint32_t alt_idx;
    if (inst->disp_hash[*idx].func_name &&
        !strcmp(inst->disp_hash[*idx].func_name, funcName))
        return true;

    // funcName wasn't at the primary spot in the hash table
    // search the list of secondary locations (shallow search, not deep search)
    for (uint32_t i = 0; i < inst->disp_hash[*idx].list.count; i++) {
        alt_idx = inst->disp_hash[*idx].list.index[i];
        if (!strcmp(inst->disp_hash[*idx].func_name, funcName)) {
            *idx = alt_idx;
            return true;
        }
    }

    return false;
}

/**
 * This function returns generic trampoline code address for unknown entry
 * points.
 * Presumably, these unknown entry points (as given by funcName) are device
 * extension entrypoints.  A hash table is used to keep a list of unknown entry
 * points and their mapping to the device extension dispatch table
 * (struct loader_dev_ext_dispatch_table).
 * \returns
 * For a given entry point string (funcName), if an existing mapping is found
 * the
 * trampoline address for that mapping is returned. Otherwise, this unknown
 * entry point
 * has not been seen yet. Next check if a layer or ICD supports it.  If so then
 * a
 * new entry in the hash table is initialized and that trampoline address for
 * the new entry is returned. Null is returned if the hash table is full or
 * if no discovered layer or ICD returns a non-NULL GetProcAddr for it.
 */
void *loader_dev_ext_gpa(struct loader_instance *inst, const char *funcName) {
    uint32_t idx;
    uint32_t seed = 0;

    idx = murmurhash(funcName, strlen(funcName), seed) % MAX_NUM_DEV_EXTS;

    if (loader_name_in_dev_ext_table(inst, &idx, funcName))
        // found funcName already in hash
        return loader_get_dev_ext_trampoline(idx);

    // Check if funcName is supported in either ICDs or a layer library
    if (!loader_check_icds_for_address(inst, funcName)) {
        // TODO Add check in layer libraries for support of address
        // if support found in layers continue on
        return NULL;
    }

    if (loader_add_dev_ext_table(inst, &idx, funcName)) {
        // successfully added new table entry
        // init any dev dispatch table entrys as needed
        loader_init_dispatch_dev_ext_entry(inst, NULL, idx, funcName);
        return loader_get_dev_ext_trampoline(idx);
    }

    return NULL;
}

struct loader_instance *loader_get_instance(const VkInstance instance) {
    /* look up the loader_instance in our list by comparing dispatch tables, as
     * there is no guarantee the instance is still a loader_instance* after any
     * layers which wrap the instance object.
     */
    const VkLayerInstanceDispatchTable *disp;
    struct loader_instance *ptr_instance = NULL;
    disp = loader_get_instance_dispatch(instance);
    for (struct loader_instance *inst = loader.instances; inst;
         inst = inst->next) {
        if (inst->disp == disp) {
            ptr_instance = inst;
            break;
        }
    }
    return ptr_instance;
}

static loader_platform_dl_handle
loader_add_layer_lib(const struct loader_instance *inst, const char *chain_type,
                     struct loader_layer_properties *layer_prop) {
    struct loader_lib_info *new_layer_lib_list, *my_lib;
    size_t new_alloc_size;
    /*
     * TODO: We can now track this information in the
     * scanned_layer_libraries list.
     */
    for (uint32_t i = 0; i < loader.loaded_layer_lib_count; i++) {
        if (strcmp(loader.loaded_layer_lib_list[i].lib_name,
                   layer_prop->lib_name) == 0) {
            /* Have already loaded this library, just increment ref count */
            loader.loaded_layer_lib_list[i].ref_count++;
            loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0,
                       "%s Chain: Increment layer reference count for layer "
                       "library %s",
                       chain_type, layer_prop->lib_name);
            return loader.loaded_layer_lib_list[i].lib_handle;
        }
    }

    /* Haven't seen this library so load it */
    new_alloc_size = 0;
    if (loader.loaded_layer_lib_capacity == 0)
        new_alloc_size = 8 * sizeof(struct loader_lib_info);
    else if (loader.loaded_layer_lib_capacity <=
             loader.loaded_layer_lib_count * sizeof(struct loader_lib_info))
        new_alloc_size = loader.loaded_layer_lib_capacity * 2;

    if (new_alloc_size) {
        new_layer_lib_list = loader_heap_realloc(
            inst, loader.loaded_layer_lib_list,
            loader.loaded_layer_lib_capacity, new_alloc_size,
            VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
        if (!new_layer_lib_list) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "loader: realloc failed in loader_add_layer_lib");
            return NULL;
        }
        loader.loaded_layer_lib_capacity = new_alloc_size;
        loader.loaded_layer_lib_list = new_layer_lib_list;
    } else
        new_layer_lib_list = loader.loaded_layer_lib_list;
    my_lib = &new_layer_lib_list[loader.loaded_layer_lib_count];

    strncpy(my_lib->lib_name, layer_prop->lib_name, sizeof(my_lib->lib_name));
    my_lib->lib_name[sizeof(my_lib->lib_name) - 1] = '\0';
    my_lib->ref_count = 0;
    my_lib->lib_handle = NULL;

    if ((my_lib->lib_handle = loader_platform_open_library(my_lib->lib_name)) ==
        NULL) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   loader_platform_open_library_error(my_lib->lib_name));
        return NULL;
    } else {
        loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0,
                   "Chain: %s: Loading layer library %s", chain_type,
                   layer_prop->lib_name);
    }
    loader.loaded_layer_lib_count++;
    my_lib->ref_count++;

    return my_lib->lib_handle;
}

static void
loader_remove_layer_lib(struct loader_instance *inst,
                        struct loader_layer_properties *layer_prop) {
    uint32_t idx = loader.loaded_layer_lib_count;
    struct loader_lib_info *new_layer_lib_list, *my_lib = NULL;

    for (uint32_t i = 0; i < loader.loaded_layer_lib_count; i++) {
        if (strcmp(loader.loaded_layer_lib_list[i].lib_name,
                   layer_prop->lib_name) == 0) {
            /* found matching library */
            idx = i;
            my_lib = &loader.loaded_layer_lib_list[i];
            break;
        }
    }

    if (idx == loader.loaded_layer_lib_count) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "Unable to unref library %s", layer_prop->lib_name);
        return;
    }

    if (my_lib) {
        my_lib->ref_count--;
        if (my_lib->ref_count > 0) {
            loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0,
                       "Decrement reference count for layer library %s",
                       layer_prop->lib_name);
            return;
        }
    }
    loader_platform_close_library(my_lib->lib_handle);
    loader_log(inst, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0,
               "Unloading layer library %s", layer_prop->lib_name);

    /* Need to remove unused library from list */
    new_layer_lib_list =
        loader_heap_alloc(inst, loader.loaded_layer_lib_capacity,
                          VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (!new_layer_lib_list) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "loader: heap alloc failed loader_remove_layer_library");
        return;
    }

    if (idx > 0) {
        /* Copy records before idx */
        memcpy(new_layer_lib_list, &loader.loaded_layer_lib_list[0],
               sizeof(struct loader_lib_info) * idx);
    }
    if (idx < (loader.loaded_layer_lib_count - 1)) {
        /* Copy records after idx */
        memcpy(&new_layer_lib_list[idx], &loader.loaded_layer_lib_list[idx + 1],
               sizeof(struct loader_lib_info) *
                   (loader.loaded_layer_lib_count - idx - 1));
    }

    loader_heap_free(inst, loader.loaded_layer_lib_list);
    loader.loaded_layer_lib_count--;
    loader.loaded_layer_lib_list = new_layer_lib_list;
}

/**
 * Go through the search_list and find any layers which match type. If layer
 * type match is found in then add it to ext_list.
 */
static void
loader_add_layer_implicit(const struct loader_instance *inst,
                          const enum layer_type type,
                          struct loader_layer_list *list,
                          const struct loader_layer_list *search_list) {
    bool enable;
    char *env_value;
    uint32_t i;
    for (i = 0; i < search_list->count; i++) {
        const struct loader_layer_properties *prop = &search_list->list[i];
        if (prop->type & type) {
            /* Found an implicit layer, see if it should be enabled */
            enable = false;

            // if no enable_environment variable is specified, this implicit
            // layer
            // should always be enabled. Otherwise check if the variable is set
            if (prop->enable_env_var.name[0] == 0) {
                enable = true;
            } else {
                env_value = loader_getenv(prop->enable_env_var.name);
                if (env_value && !strcmp(prop->enable_env_var.value, env_value))
                    enable = true;
                loader_free_getenv(env_value);
            }

            // disable_environment has priority, i.e. if both enable and disable
            // environment variables are set, the layer is disabled. Implicit
            // layers
            // are required to have a disable_environment variables
            env_value = loader_getenv(prop->disable_env_var.name);
            if (env_value)
                enable = false;
            loader_free_getenv(env_value);

            if (enable)
                loader_add_to_layer_list(inst, list, 1, prop);
        }
    }
}

/**
 * Get the layer name(s) from the env_name environment variable. If layer
 * is found in search_list then add it to layer_list.  But only add it to
 * layer_list if type matches.
 */
static void loader_add_layer_env(const struct loader_instance *inst,
                                 const enum layer_type type,
                                 const char *env_name,
                                 struct loader_layer_list *layer_list,
                                 const struct loader_layer_list *search_list) {
    char *layerEnv;
    char *next, *name;

    layerEnv = loader_getenv(env_name);
    if (layerEnv == NULL) {
        return;
    }
    name = loader_stack_alloc(strlen(layerEnv) + 1);
    if (name == NULL) {
        return;
    }
    strcpy(name, layerEnv);

    loader_free_getenv(layerEnv);

    while (name && *name) {
        next = loader_get_next_path(name);
        if (!strcmp(std_validation_str, name)) {
            /* add meta list of layers
               don't attempt to remove duplicate layers already added by app or
               env var
             */
            loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                       "Expanding meta layer %s found in environment variable",
                       std_validation_str);
            for (uint32_t i = 0; i < sizeof(std_validation_names) /
                                         sizeof(std_validation_names[0]);
                 i++) {
                loader_find_layer_name_add_list(inst, std_validation_names[i],
                                                type, search_list, layer_list);
            }
        } else {
            loader_find_layer_name_add_list(inst, name, type, search_list,
                                            layer_list);
        }
        name = next;
    }

    return;
}

void loader_deactivate_instance_layers(struct loader_instance *instance) {
    /* Create instance chain of enabled layers */
    for (uint32_t i = 0; i < instance->activated_layer_list.count; i++) {
        struct loader_layer_properties *layer_prop =
            &instance->activated_layer_list.list[i];

        loader_remove_layer_lib(instance, layer_prop);
    }
    loader_destroy_layer_list(instance, &instance->activated_layer_list);
}

VkResult
loader_enable_instance_layers(struct loader_instance *inst,
                              const VkInstanceCreateInfo *pCreateInfo,
                              const struct loader_layer_list *instance_layers) {
    VkResult err;

    assert(inst && "Cannot have null instance");

    if (!loader_init_layer_list(inst, &inst->activated_layer_list)) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "Failed to alloc Instance activated layer list");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    /* Add any implicit layers first */
    loader_add_layer_implicit(inst, VK_LAYER_TYPE_INSTANCE_IMPLICIT,
                              &inst->activated_layer_list, instance_layers);

    /* Add any layers specified via environment variable next */
    loader_add_layer_env(inst, VK_LAYER_TYPE_INSTANCE_EXPLICIT,
                         "VK_INSTANCE_LAYERS", &inst->activated_layer_list,
                         instance_layers);

    /* Add layers specified by the application */
    err = loader_add_layer_names_to_list(
        inst, &inst->activated_layer_list, pCreateInfo->enabledLayerCount,
        pCreateInfo->ppEnabledLayerNames, instance_layers);

    return err;
}

/*
 * Given the list of layers to activate in the loader_instance
 * structure. This function will add a VkLayerInstanceCreateInfo
 * structure to the VkInstanceCreateInfo.pNext pointer.
 * Each activated layer will have it's own VkLayerInstanceLink
 * structure that tells the layer what Get*ProcAddr to call to
 * get function pointers to the next layer down.
 * Once the chain info has been created this function will
 * execute the CreateInstance call chain. Each layer will
 * then have an opportunity in it's CreateInstance function
 * to setup it's dispatch table when the lower layer returns
 * successfully.
 * Each layer can wrap or not-wrap the returned VkInstance object
 * as it sees fit.
 * The instance chain is terminated by a loader function
 * that will call CreateInstance on all available ICD's and
 * cache those VkInstance objects for future use.
 */
VkResult loader_create_instance_chain(const VkInstanceCreateInfo *pCreateInfo,
                                      const VkAllocationCallbacks *pAllocator,
                                      struct loader_instance *inst,
                                      VkInstance *created_instance) {
    uint32_t activated_layers = 0;
    VkLayerInstanceCreateInfo chain_info;
    VkLayerInstanceLink *layer_instance_link_info = NULL;
    VkInstanceCreateInfo loader_create_info;
    VkResult res;

    PFN_vkGetInstanceProcAddr nextGIPA = loader_gpa_instance_internal;
    PFN_vkGetInstanceProcAddr fpGIPA = loader_gpa_instance_internal;

    memcpy(&loader_create_info, pCreateInfo, sizeof(VkInstanceCreateInfo));

    if (inst->activated_layer_list.count > 0) {

        chain_info.u.pLayerInfo = NULL;
        chain_info.pNext = pCreateInfo->pNext;
        chain_info.sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO;
        chain_info.function = VK_LAYER_LINK_INFO;
        loader_create_info.pNext = &chain_info;

        layer_instance_link_info = loader_stack_alloc(
            sizeof(VkLayerInstanceLink) * inst->activated_layer_list.count);
        if (!layer_instance_link_info) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "Failed to alloc Instance objects for layer");
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }

        /* Create instance chain of enabled layers */
        for (int32_t i = inst->activated_layer_list.count - 1; i >= 0; i--) {
            struct loader_layer_properties *layer_prop =
                &inst->activated_layer_list.list[i];
            loader_platform_dl_handle lib_handle;

            lib_handle = loader_add_layer_lib(inst, "instance", layer_prop);
            if (!lib_handle)
                continue;
            if ((fpGIPA = layer_prop->functions.get_instance_proc_addr) ==
                NULL) {
                if (layer_prop->functions.str_gipa == NULL ||
                    strlen(layer_prop->functions.str_gipa) == 0) {
                    fpGIPA = (PFN_vkGetInstanceProcAddr)
                        loader_platform_get_proc_address(
                            lib_handle, "vkGetInstanceProcAddr");
                    layer_prop->functions.get_instance_proc_addr = fpGIPA;
                } else
                    fpGIPA = (PFN_vkGetInstanceProcAddr)
                        loader_platform_get_proc_address(
                            lib_handle, layer_prop->functions.str_gipa);
                if (!fpGIPA) {
                    loader_log(
                        inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                        "Failed to find vkGetInstanceProcAddr in layer %s",
                        layer_prop->lib_name);
                    continue;
                }
            }

            layer_instance_link_info[activated_layers].pNext =
                chain_info.u.pLayerInfo;
            layer_instance_link_info[activated_layers]
                .pfnNextGetInstanceProcAddr = nextGIPA;
            chain_info.u.pLayerInfo =
                &layer_instance_link_info[activated_layers];
            nextGIPA = fpGIPA;

            loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                       "Insert instance layer %s (%s)",
                       layer_prop->info.layerName, layer_prop->lib_name);

            activated_layers++;
        }
    }

    PFN_vkCreateInstance fpCreateInstance =
        (PFN_vkCreateInstance)nextGIPA(*created_instance, "vkCreateInstance");
    if (fpCreateInstance) {
        VkLayerInstanceCreateInfo instance_create_info;

        instance_create_info.sType =
            VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO;
        instance_create_info.function = VK_LAYER_INSTANCE_INFO;

        instance_create_info.u.instanceInfo.instance_info = inst;
        instance_create_info.u.instanceInfo.pfnNextGetInstanceProcAddr =
            nextGIPA;

        instance_create_info.pNext = loader_create_info.pNext;
        loader_create_info.pNext = &instance_create_info;

        res =
            fpCreateInstance(&loader_create_info, pAllocator, created_instance);
    } else {
        // Couldn't find CreateInstance function!
        res = VK_ERROR_INITIALIZATION_FAILED;
    }

    if (res != VK_SUCCESS) {
        // TODO: Need to clean up here
    } else {
        loader_init_instance_core_dispatch_table(inst->disp, nextGIPA,
                                                 *created_instance);
    }

    return res;
}

void loader_activate_instance_layer_extensions(struct loader_instance *inst,
                                               VkInstance created_inst) {

    loader_init_instance_extension_dispatch_table(
        inst->disp, inst->disp->GetInstanceProcAddr, created_inst);
}

static VkResult
loader_enable_device_layers(const struct loader_instance *inst,
                            struct loader_icd *icd,
                            struct loader_layer_list *activated_layer_list,
                            const VkDeviceCreateInfo *pCreateInfo,
                            const struct loader_layer_list *device_layers)

{
    VkResult err;

    assert(activated_layer_list && "Cannot have null output layer list");

    if (activated_layer_list->list == NULL ||
        activated_layer_list->capacity == 0) {
        loader_init_layer_list(inst, activated_layer_list);
    }

    if (activated_layer_list->list == NULL) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "Failed to alloc device activated layer list");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    /* Add any implicit layers first */
    loader_add_layer_implicit(inst, VK_LAYER_TYPE_DEVICE_IMPLICIT,
                              activated_layer_list, device_layers);

    /* Add any layers specified via environment variable next */
    loader_add_layer_env(inst, VK_LAYER_TYPE_DEVICE_EXPLICIT,
                         "VK_DEVICE_LAYERS", activated_layer_list,
                         device_layers);

    /* Add layers specified by the application */
    err = loader_add_layer_names_to_list(
        inst, activated_layer_list, pCreateInfo->enabledLayerCount,
        pCreateInfo->ppEnabledLayerNames, device_layers);

    return err;
}

VKAPI_ATTR VkResult VKAPI_CALL
loader_create_device_terminator(VkPhysicalDevice physicalDevice,
                                const VkDeviceCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkDevice *pDevice) {
    struct loader_physical_device *phys_dev;
    phys_dev = loader_get_physical_device(physicalDevice);

    VkLayerDeviceCreateInfo *chain_info =
        (VkLayerDeviceCreateInfo *)pCreateInfo->pNext;
    while (chain_info &&
           !(chain_info->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
             chain_info->function == VK_LAYER_DEVICE_INFO)) {
        chain_info = (VkLayerDeviceCreateInfo *)chain_info->pNext;
    }
    assert(chain_info != NULL);

    struct loader_device *dev =
        (struct loader_device *)chain_info->u.deviceInfo.device_info;
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr =
        chain_info->u.deviceInfo.pfnNextGetInstanceProcAddr;
    PFN_vkCreateDevice fpCreateDevice =
        (PFN_vkCreateDevice)fpGetInstanceProcAddr(phys_dev->this_icd->instance,
                                                  "vkCreateDevice");
    if (fpCreateDevice == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDeviceCreateInfo localCreateInfo;
    memcpy(&localCreateInfo, pCreateInfo, sizeof(localCreateInfo));
    localCreateInfo.pNext = loader_strip_create_extensions(pCreateInfo->pNext);

    /*
     * NOTE: Need to filter the extensions to only those
     * supported by the ICD.
     * No ICD will advertise support for layers. An ICD
     * library could support a layer, but it would be
     * independent of the actual ICD, just in the same library.
     */
    char **filtered_extension_names = NULL;
    filtered_extension_names =
        loader_stack_alloc(pCreateInfo->enabledExtensionCount * sizeof(char *));
    if (!filtered_extension_names) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    localCreateInfo.enabledLayerCount = 0;
    localCreateInfo.ppEnabledLayerNames = NULL;

    localCreateInfo.enabledExtensionCount = 0;
    localCreateInfo.ppEnabledExtensionNames =
        (const char *const *)filtered_extension_names;

    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        const char *extension_name = pCreateInfo->ppEnabledExtensionNames[i];
        VkExtensionProperties *prop = get_extension_property(
            extension_name, &phys_dev->device_extension_cache);
        if (prop) {
            filtered_extension_names[localCreateInfo.enabledExtensionCount] =
                (char *)extension_name;
            localCreateInfo.enabledExtensionCount++;
        }
    }

    VkDevice localDevice;
    // TODO: Why does fpCreateDevice behave differently than
    // this_icd->CreateDevice?
    //    VkResult res = fpCreateDevice(phys_dev->phys_dev, &localCreateInfo,
    //    pAllocator, &localDevice);
    VkResult res = phys_dev->this_icd->CreateDevice(
        phys_dev->phys_dev, &localCreateInfo, pAllocator, &localDevice);

    if (res != VK_SUCCESS) {
        return res;
    }

    *pDevice = localDevice;

    /* Init dispatch pointer in new device object */
    loader_init_dispatch(*pDevice, &dev->loader_dispatch);

    return res;
}

VkResult loader_create_device_chain(VkPhysicalDevice physicalDevice,
                                    const VkDeviceCreateInfo *pCreateInfo,
                                    const VkAllocationCallbacks *pAllocator,
                                    struct loader_instance *inst,
                                    struct loader_icd *icd,
                                    struct loader_device *dev) {
    uint32_t activated_layers = 0;
    VkLayerDeviceLink *layer_device_link_info;
    VkLayerDeviceCreateInfo chain_info;
    VkLayerDeviceCreateInfo device_info;
    VkDeviceCreateInfo loader_create_info;
    VkResult res;

    PFN_vkGetDeviceProcAddr fpGDPA, nextGDPA = icd->GetDeviceProcAddr;
    PFN_vkGetInstanceProcAddr fpGIPA, nextGIPA = loader_gpa_instance_internal;

    memcpy(&loader_create_info, pCreateInfo, sizeof(VkDeviceCreateInfo));

    chain_info.sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;
    chain_info.function = VK_LAYER_LINK_INFO;
    chain_info.u.pLayerInfo = NULL;
    chain_info.pNext = pCreateInfo->pNext;

    layer_device_link_info = loader_stack_alloc(
        sizeof(VkLayerDeviceLink) * dev->activated_layer_list.count);
    if (!layer_device_link_info) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "Failed to alloc Device objects for layer");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    /*
     * This structure is used by loader_create_device_terminator
     * so that it can intialize the device dispatch table pointer
     * in the device object returned by the ICD. Without this
     * structure the code wouldn't know where the loader's device_info
     * structure is located.
     */
    device_info.sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;
    device_info.function = VK_LAYER_DEVICE_INFO;
    device_info.pNext = &chain_info;
    device_info.u.deviceInfo.device_info = dev;
    device_info.u.deviceInfo.pfnNextGetInstanceProcAddr =
        icd->this_icd_lib->GetInstanceProcAddr;

    loader_create_info.pNext = &device_info;

    if (dev->activated_layer_list.count > 0) {
        /* Create instance chain of enabled layers */
        for (int32_t i = dev->activated_layer_list.count - 1; i >= 0; i--) {
            struct loader_layer_properties *layer_prop =
                &dev->activated_layer_list.list[i];
            loader_platform_dl_handle lib_handle;

            lib_handle = loader_add_layer_lib(inst, "device", layer_prop);
            if (!lib_handle)
                continue;
            if ((fpGIPA = layer_prop->functions.get_instance_proc_addr) ==
                NULL) {
                if (layer_prop->functions.str_gipa == NULL ||
                    strlen(layer_prop->functions.str_gipa) == 0) {
                    fpGIPA = (PFN_vkGetInstanceProcAddr)
                        loader_platform_get_proc_address(
                            lib_handle, "vkGetInstanceProcAddr");
                    layer_prop->functions.get_instance_proc_addr = fpGIPA;
                } else
                    fpGIPA = (PFN_vkGetInstanceProcAddr)
                        loader_platform_get_proc_address(
                            lib_handle, layer_prop->functions.str_gipa);
                if (!fpGIPA) {
                    loader_log(
                        inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                        "Failed to find vkGetInstanceProcAddr in layer %s",
                        layer_prop->lib_name);
                    continue;
                }
            }
            if ((fpGDPA = layer_prop->functions.get_device_proc_addr) == NULL) {
                if (layer_prop->functions.str_gdpa == NULL ||
                    strlen(layer_prop->functions.str_gdpa) == 0) {
                    fpGDPA = (PFN_vkGetDeviceProcAddr)
                        loader_platform_get_proc_address(lib_handle,
                                                         "vkGetDeviceProcAddr");
                    layer_prop->functions.get_device_proc_addr = fpGDPA;
                } else
                    fpGDPA = (PFN_vkGetDeviceProcAddr)
                        loader_platform_get_proc_address(
                            lib_handle, layer_prop->functions.str_gdpa);
                if (!fpGDPA) {
                    loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                               "Failed to find vkGetDeviceProcAddr in layer %s",
                               layer_prop->lib_name);
                    continue;
                }
            }

            layer_device_link_info[activated_layers].pNext =
                chain_info.u.pLayerInfo;
            layer_device_link_info[activated_layers]
                .pfnNextGetInstanceProcAddr = nextGIPA;
            layer_device_link_info[activated_layers].pfnNextGetDeviceProcAddr =
                nextGDPA;
            chain_info.u.pLayerInfo = &layer_device_link_info[activated_layers];
            nextGIPA = fpGIPA;
            nextGDPA = fpGDPA;

            loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                       "Insert device layer %s (%s)",
                       layer_prop->info.layerName, layer_prop->lib_name);

            activated_layers++;
        }
    }

    PFN_vkCreateDevice fpCreateDevice =
        (PFN_vkCreateDevice)nextGIPA((VkInstance)inst, "vkCreateDevice");
    if (fpCreateDevice) {
        res = fpCreateDevice(physicalDevice, &loader_create_info, pAllocator,
                             &dev->device);
    } else {
        // Couldn't find CreateDevice function!
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* Initialize device dispatch table */
    loader_init_device_dispatch_table(&dev->loader_dispatch, nextGDPA,
                                      dev->device);

    return res;
}

VkResult loader_validate_layers(const struct loader_instance *inst,
                                const uint32_t layer_count,
                                const char *const *ppEnabledLayerNames,
                                const struct loader_layer_list *list) {
    struct loader_layer_properties *prop;

    for (uint32_t i = 0; i < layer_count; i++) {
        VkStringErrorFlags result =
            vk_string_validate(MaxLoaderStringLength, ppEnabledLayerNames[i]);
        if (result != VK_STRING_ERROR_NONE) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "Loader: Device ppEnabledLayerNames contains string "
                       "that is too long or is badly formed");
            return VK_ERROR_LAYER_NOT_PRESENT;
        }

        prop = loader_get_layer_property(ppEnabledLayerNames[i], list);
        if (!prop) {
            return VK_ERROR_LAYER_NOT_PRESENT;
        }
    }
    return VK_SUCCESS;
}

VkResult loader_validate_instance_extensions(
    const struct loader_instance *inst,
    const struct loader_extension_list *icd_exts,
    const struct loader_layer_list *instance_layer,
    const VkInstanceCreateInfo *pCreateInfo) {

    VkExtensionProperties *extension_prop;
    struct loader_layer_properties *layer_prop;

    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        VkStringErrorFlags result = vk_string_validate(
            MaxLoaderStringLength, pCreateInfo->ppEnabledExtensionNames[i]);
        if (result != VK_STRING_ERROR_NONE) {
            loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                       "Loader: Instance ppEnabledExtensionNames contains "
                       "string that is too long or is badly formed");
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }

        extension_prop = get_extension_property(
            pCreateInfo->ppEnabledExtensionNames[i], icd_exts);

        if (extension_prop) {
            continue;
        }

        extension_prop = NULL;

        /* Not in global list, search layer extension lists */
        for (uint32_t j = 0; j < pCreateInfo->enabledLayerCount; j++) {
            layer_prop = loader_get_layer_property(
                pCreateInfo->ppEnabledLayerNames[i], instance_layer);
            if (!layer_prop) {
                /* Should NOT get here, loader_validate_layers
                 * should have already filtered this case out.
                 */
                continue;
            }

            extension_prop =
                get_extension_property(pCreateInfo->ppEnabledExtensionNames[i],
                                       &layer_prop->instance_extension_list);
            if (extension_prop) {
                /* Found the extension in one of the layers enabled by the app.
                 */
                break;
            }
        }

        if (!extension_prop) {
            /* Didn't find extension name in any of the global layers, error out
             */
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }
    return VK_SUCCESS;
}

VkResult loader_validate_device_extensions(
    struct loader_physical_device *phys_dev,
    const struct loader_layer_list *activated_device_layers,
    const VkDeviceCreateInfo *pCreateInfo) {
    VkExtensionProperties *extension_prop;
    struct loader_layer_properties *layer_prop;

    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {

        VkStringErrorFlags result = vk_string_validate(
            MaxLoaderStringLength, pCreateInfo->ppEnabledExtensionNames[i]);
        if (result != VK_STRING_ERROR_NONE) {
            loader_log(phys_dev->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT,
                       0, "Loader: Device ppEnabledExtensionNames contains "
                          "string that is too long or is badly formed");
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }

        const char *extension_name = pCreateInfo->ppEnabledExtensionNames[i];
        extension_prop = get_extension_property(
            extension_name, &phys_dev->device_extension_cache);

        if (extension_prop) {
            continue;
        }

        /* Not in global list, search activated layer extension lists */
        for (uint32_t j = 0; j < activated_device_layers->count; j++) {
            layer_prop = &activated_device_layers->list[j];

            extension_prop = get_dev_extension_property(
                extension_name, &layer_prop->device_extension_list);
            if (extension_prop) {
                /* Found the extension in one of the layers enabled by the app.
                 */
                break;
            }
        }

        if (!extension_prop) {
            /* Didn't find extension name in any of the device layers, error out
             */
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
loader_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkInstance *pInstance) {
    struct loader_icd *icd;
    VkExtensionProperties *prop;
    char **filtered_extension_names = NULL;
    VkInstanceCreateInfo icd_create_info;
    VkResult res = VK_SUCCESS;
    bool success = false;

    VkLayerInstanceCreateInfo *chain_info =
        (VkLayerInstanceCreateInfo *)pCreateInfo->pNext;
    while (
        chain_info &&
        !(chain_info->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
          chain_info->function == VK_LAYER_INSTANCE_INFO)) {
        chain_info = (VkLayerInstanceCreateInfo *)chain_info->pNext;
    }
    assert(chain_info != NULL);

    struct loader_instance *ptr_instance =
        (struct loader_instance *)chain_info->u.instanceInfo.instance_info;
    memcpy(&icd_create_info, pCreateInfo, sizeof(icd_create_info));

    icd_create_info.enabledLayerCount = 0;
    icd_create_info.ppEnabledLayerNames = NULL;

    // strip off the VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO entries
    icd_create_info.pNext = loader_strip_create_extensions(pCreateInfo->pNext);

    /*
     * NOTE: Need to filter the extensions to only those
     * supported by the ICD.
     * No ICD will advertise support for layers. An ICD
     * library could support a layer, but it would be
     * independent of the actual ICD, just in the same library.
     */
    filtered_extension_names =
        loader_stack_alloc(pCreateInfo->enabledExtensionCount * sizeof(char *));
    if (!filtered_extension_names) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    icd_create_info.ppEnabledExtensionNames =
        (const char *const *)filtered_extension_names;

    for (uint32_t i = 0; i < ptr_instance->icd_libs.count; i++) {
        icd = loader_icd_add(ptr_instance, &ptr_instance->icd_libs.list[i]);
        if (icd) {
            icd_create_info.enabledExtensionCount = 0;
            struct loader_extension_list icd_exts;

            loader_log(ptr_instance, VK_DEBUG_REPORT_DEBUG_BIT_EXT, 0,
                       "Build ICD instance extension list");
            // traverse scanned icd list adding non-duplicate extensions to the
            // list
            loader_init_generic_list(ptr_instance,
                                     (struct loader_generic_list *)&icd_exts,
                                     sizeof(VkExtensionProperties));
            loader_add_instance_extensions(
                ptr_instance,
                icd->this_icd_lib->EnumerateInstanceExtensionProperties,
                icd->this_icd_lib->lib_name, &icd_exts);

            for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
                prop = get_extension_property(
                    pCreateInfo->ppEnabledExtensionNames[i], &icd_exts);
                if (prop) {
                    filtered_extension_names[icd_create_info
                                                 .enabledExtensionCount] =
                        (char *)pCreateInfo->ppEnabledExtensionNames[i];
                    icd_create_info.enabledExtensionCount++;
                }
            }

            loader_destroy_generic_list(
                ptr_instance, (struct loader_generic_list *)&icd_exts);

            res = ptr_instance->icd_libs.list[i].CreateInstance(
                &icd_create_info, pAllocator, &(icd->instance));
            if (res == VK_SUCCESS)
                success = loader_icd_init_entrys(
                    icd, icd->instance,
                    ptr_instance->icd_libs.list[i].GetInstanceProcAddr);

            if (res != VK_SUCCESS || !success) {
                ptr_instance->icds = ptr_instance->icds->next;
                loader_icd_destroy(ptr_instance, icd);
                loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "ICD ignored: failed to CreateInstance and find "
                           "entrypoints with ICD");
            }
        }
    }

    /*
     * If no ICDs were added to instance list and res is unchanged
     * from it's initial value, the loader was unable to find
     * a suitable ICD.
     */
    if (ptr_instance->icds == NULL) {
        if (res == VK_SUCCESS) {
            return VK_ERROR_INCOMPATIBLE_DRIVER;
        } else {
            return res;
        }
    }

    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
loader_DestroyInstance(VkInstance instance,
                       const VkAllocationCallbacks *pAllocator) {
    struct loader_instance *ptr_instance = loader_instance(instance);
    struct loader_icd *icds = ptr_instance->icds;
    struct loader_icd *next_icd;

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

    while (icds) {
        if (icds->instance) {
            icds->DestroyInstance(icds->instance, pAllocator);
        }
        next_icd = icds->next;
        icds->instance = VK_NULL_HANDLE;
        loader_icd_destroy(ptr_instance, icds);

        icds = next_icd;
    }
    loader_delete_layer_properties(ptr_instance,
                                   &ptr_instance->device_layer_list);
    loader_delete_layer_properties(ptr_instance,
                                   &ptr_instance->instance_layer_list);
    loader_scanned_icd_clear(ptr_instance, &ptr_instance->icd_libs);
    loader_destroy_generic_list(
        ptr_instance, (struct loader_generic_list *)&ptr_instance->ext_list);
    for (uint32_t i = 0; i < ptr_instance->total_gpu_count; i++)
        loader_destroy_generic_list(
            ptr_instance,
            (struct loader_generic_list *)&ptr_instance->phys_devs[i]
                .device_extension_cache);
    loader_heap_free(ptr_instance, ptr_instance->phys_devs);
    loader_free_dev_ext_table(ptr_instance);
}

VkResult
loader_init_physical_device_info(struct loader_instance *ptr_instance) {
    struct loader_icd *icd;
    uint32_t i, j, idx, count = 0;
    VkResult res;
    struct loader_phys_dev_per_icd *phys_devs;

    ptr_instance->total_gpu_count = 0;
    phys_devs = (struct loader_phys_dev_per_icd *)loader_stack_alloc(
        sizeof(struct loader_phys_dev_per_icd) * ptr_instance->total_icd_count);
    if (!phys_devs)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    icd = ptr_instance->icds;
    for (i = 0; i < ptr_instance->total_icd_count; i++) {
        assert(icd);
        res = icd->EnumeratePhysicalDevices(icd->instance, &phys_devs[i].count,
                                            NULL);
        if (res != VK_SUCCESS)
            return res;
        count += phys_devs[i].count;
        icd = icd->next;
    }

    ptr_instance->phys_devs =
        (struct loader_physical_device *)loader_heap_alloc(
            ptr_instance, count * sizeof(struct loader_physical_device),
            VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (!ptr_instance->phys_devs)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    icd = ptr_instance->icds;

    struct loader_physical_device *inst_phys_devs = ptr_instance->phys_devs;
    idx = 0;
    for (i = 0; i < ptr_instance->total_icd_count; i++) {
        assert(icd);

        phys_devs[i].phys_devs = (VkPhysicalDevice *)loader_stack_alloc(
            phys_devs[i].count * sizeof(VkPhysicalDevice));
        if (!phys_devs[i].phys_devs) {
            loader_heap_free(ptr_instance, ptr_instance->phys_devs);
            ptr_instance->phys_devs = NULL;
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        res = icd->EnumeratePhysicalDevices(
            icd->instance, &(phys_devs[i].count), phys_devs[i].phys_devs);
        if ((res == VK_SUCCESS)) {
            ptr_instance->total_gpu_count += phys_devs[i].count;
            for (j = 0; j < phys_devs[i].count; j++) {

                // initialize the loader's physicalDevice object
                loader_set_dispatch((void *)&inst_phys_devs[idx],
                                    ptr_instance->disp);
                inst_phys_devs[idx].this_instance = ptr_instance;
                inst_phys_devs[idx].this_icd = icd;
                inst_phys_devs[idx].phys_dev = phys_devs[i].phys_devs[j];
                memset(&inst_phys_devs[idx].device_extension_cache, 0,
                       sizeof(struct loader_extension_list));

                idx++;
            }
        } else {
            loader_heap_free(ptr_instance, ptr_instance->phys_devs);
            ptr_instance->phys_devs = NULL;
            return res;
        }

        icd = icd->next;
    }

    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
loader_EnumeratePhysicalDevices(VkInstance instance,
                                uint32_t *pPhysicalDeviceCount,
                                VkPhysicalDevice *pPhysicalDevices) {
    uint32_t i;
    uint32_t copy_count = 0;
    struct loader_instance *ptr_instance = (struct loader_instance *)instance;
    VkResult res = VK_SUCCESS;

    if (ptr_instance->total_gpu_count == 0) {
        res = loader_init_physical_device_info(ptr_instance);
    }

    *pPhysicalDeviceCount = ptr_instance->total_gpu_count;
    if (!pPhysicalDevices) {
        return res;
    }

    copy_count = (ptr_instance->total_gpu_count < *pPhysicalDeviceCount)
                     ? ptr_instance->total_gpu_count
                     : *pPhysicalDeviceCount;
    for (i = 0; i < copy_count; i++) {
        pPhysicalDevices[i] = (VkPhysicalDevice)&ptr_instance->phys_devs[i];
    }
    *pPhysicalDeviceCount = copy_count;

    if (copy_count < ptr_instance->total_gpu_count) {
        return VK_INCOMPLETE;
    }

    return res;
}

VKAPI_ATTR void VKAPI_CALL
loader_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                   VkPhysicalDeviceProperties *pProperties) {
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_icd *icd = phys_dev->this_icd;

    if (icd->GetPhysicalDeviceProperties)
        icd->GetPhysicalDeviceProperties(phys_dev->phys_dev, pProperties);
}

VKAPI_ATTR void VKAPI_CALL loader_GetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties *pProperties) {
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_icd *icd = phys_dev->this_icd;

    if (icd->GetPhysicalDeviceQueueFamilyProperties)
        icd->GetPhysicalDeviceQueueFamilyProperties(
            phys_dev->phys_dev, pQueueFamilyPropertyCount, pProperties);
}

VKAPI_ATTR void VKAPI_CALL loader_GetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties *pProperties) {
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_icd *icd = phys_dev->this_icd;

    if (icd->GetPhysicalDeviceMemoryProperties)
        icd->GetPhysicalDeviceMemoryProperties(phys_dev->phys_dev, pProperties);
}

VKAPI_ATTR void VKAPI_CALL
loader_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
                                 VkPhysicalDeviceFeatures *pFeatures) {
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_icd *icd = phys_dev->this_icd;

    if (icd->GetPhysicalDeviceFeatures)
        icd->GetPhysicalDeviceFeatures(phys_dev->phys_dev, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL
loader_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice,
                                         VkFormat format,
                                         VkFormatProperties *pFormatInfo) {
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_icd *icd = phys_dev->this_icd;

    if (icd->GetPhysicalDeviceFormatProperties)
        icd->GetPhysicalDeviceFormatProperties(phys_dev->phys_dev, format,
                                               pFormatInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL loader_GetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
    VkImageFormatProperties *pImageFormatProperties) {
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_icd *icd = phys_dev->this_icd;

    if (!icd->GetPhysicalDeviceImageFormatProperties)
        return VK_ERROR_INITIALIZATION_FAILED;

    return icd->GetPhysicalDeviceImageFormatProperties(
        phys_dev->phys_dev, format, type, tiling, usage, flags,
        pImageFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL loader_GetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkSampleCountFlagBits samples, VkImageUsageFlags usage,
    VkImageTiling tiling, uint32_t *pNumProperties,
    VkSparseImageFormatProperties *pProperties) {
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_icd *icd = phys_dev->this_icd;

    if (icd->GetPhysicalDeviceSparseImageFormatProperties)
        icd->GetPhysicalDeviceSparseImageFormatProperties(
            phys_dev->phys_dev, format, type, samples, usage, tiling,
            pNumProperties, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL
loader_CreateDevice(VkPhysicalDevice physicalDevice,
                    const VkDeviceCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkDevice *pDevice) {
    struct loader_physical_device *phys_dev;
    struct loader_icd *icd;
    struct loader_device *dev;
    struct loader_instance *inst;
    struct loader_layer_list activated_layer_list = {0};
    VkResult res;

    assert(pCreateInfo->queueCreateInfoCount >= 1);

    // TODO this only works for one physical device per instance
    // once CreateDevice layer bootstrapping is done via DeviceCreateInfo
    // hopefully don't need this anymore in trampoline code
    phys_dev = loader_get_physical_device(physicalDevice);
    icd = phys_dev->this_icd;
    if (!icd)
        return VK_ERROR_INITIALIZATION_FAILED;

    inst = phys_dev->this_instance;

    if (!icd->CreateDevice) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* validate any app enabled layers are available */
    if (pCreateInfo->enabledLayerCount > 0) {
        res = loader_validate_layers(inst, pCreateInfo->enabledLayerCount,
                                     pCreateInfo->ppEnabledLayerNames,
                                     &inst->device_layer_list);
        if (res != VK_SUCCESS) {
            return res;
        }
    }

    /* Get the physical device extensions if they haven't been retrieved yet */
    if (phys_dev->device_extension_cache.capacity == 0) {
        if (!loader_init_generic_list(
                inst,
                (struct loader_generic_list *)&phys_dev->device_extension_cache,
                sizeof(VkExtensionProperties))) {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }

        res = loader_add_device_extensions(
            inst, icd, phys_dev->phys_dev,
            phys_dev->this_icd->this_icd_lib->lib_name,
            &phys_dev->device_extension_cache);
        if (res != VK_SUCCESS) {
            return res;
        }
    }

    /* convert any meta layers to the actual layers makes a copy of layer name*/
    uint32_t saved_layer_count = pCreateInfo->enabledLayerCount;
    char **saved_layer_names;
    char **saved_layer_ptr;
    saved_layer_names =
        loader_stack_alloc(sizeof(char *) * pCreateInfo->enabledLayerCount);
    for (uint32_t i = 0; i < saved_layer_count; i++) {
        saved_layer_names[i] = (char *)pCreateInfo->ppEnabledLayerNames[i];
    }
    saved_layer_ptr = (char **)pCreateInfo->ppEnabledLayerNames;

    loader_expand_layer_names(
        inst, std_validation_str,
        sizeof(std_validation_names) / sizeof(std_validation_names[0]),
        std_validation_names, (uint32_t *)&pCreateInfo->enabledLayerCount,
        (char ***)&pCreateInfo->ppEnabledLayerNames);

    /* fetch a list of all layers activated, explicit and implicit */
    res = loader_enable_device_layers(inst, icd, &activated_layer_list,
                                      pCreateInfo, &inst->device_layer_list);
    if (res != VK_SUCCESS) {
        loader_unexpand_dev_layer_names(inst, saved_layer_count,
                                        saved_layer_names, saved_layer_ptr,
                                        pCreateInfo);
        return res;
    }

    /* make sure requested extensions to be enabled are supported */
    res = loader_validate_device_extensions(phys_dev, &activated_layer_list,
                                            pCreateInfo);
    if (res != VK_SUCCESS) {
        loader_unexpand_dev_layer_names(inst, saved_layer_count,
                                        saved_layer_names, saved_layer_ptr,
                                        pCreateInfo);
        loader_destroy_generic_list(
            inst, (struct loader_generic_list *)&activated_layer_list);
        return res;
    }

    dev = loader_add_logical_device(inst, &icd->logical_device_list);
    if (dev == NULL) {
        loader_unexpand_dev_layer_names(inst, saved_layer_count,
                                        saved_layer_names, saved_layer_ptr,
                                        pCreateInfo);
        loader_destroy_generic_list(
            inst, (struct loader_generic_list *)&activated_layer_list);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    /* move the locally filled layer list into the device, and pass ownership of
     * the memory */
    dev->activated_layer_list.capacity = activated_layer_list.capacity;
    dev->activated_layer_list.count = activated_layer_list.count;
    dev->activated_layer_list.list = activated_layer_list.list;
    memset(&activated_layer_list, 0, sizeof(activated_layer_list));

    /* activate any layers on device chain which terminates with device*/
    res = loader_enable_device_layers(inst, icd, &dev->activated_layer_list,
                                      pCreateInfo, &inst->device_layer_list);
    if (res != VK_SUCCESS) {
        loader_unexpand_dev_layer_names(inst, saved_layer_count,
                                        saved_layer_names, saved_layer_ptr,
                                        pCreateInfo);
        loader_remove_logical_device(inst, icd, dev);
        return res;
    }

    res = loader_create_device_chain(physicalDevice, pCreateInfo, pAllocator,
                                     inst, icd, dev);
    if (res != VK_SUCCESS) {
        loader_unexpand_dev_layer_names(inst, saved_layer_count,
                                        saved_layer_names, saved_layer_ptr,
                                        pCreateInfo);
        loader_remove_logical_device(inst, icd, dev);
        return res;
    }

    *pDevice = dev->device;

    /* initialize any device extension dispatch entry's from the instance list*/
    loader_init_dispatch_dev_ext(inst, dev);

    /* initialize WSI device extensions as part of core dispatch since loader
     * has
     * dedicated trampoline code for these*/
    loader_init_device_extension_dispatch_table(
        &dev->loader_dispatch,
        dev->loader_dispatch.core_dispatch.GetDeviceProcAddr, *pDevice);

    loader_unexpand_dev_layer_names(inst, saved_layer_count, saved_layer_names,
                                    saved_layer_ptr, pCreateInfo);
    return res;
}

/**
 * Get an instance level or global level entry point address.
 * @param instance
 * @param pName
 * @return
 *    If instance == NULL returns a global level functions only
 *    If instance is valid returns a trampoline entry point for all dispatchable
 * Vulkan
 *    functions both core and extensions.
 */
LOADER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *pName) {

    void *addr;

    addr = globalGetProcAddr(pName);
    if (instance == VK_NULL_HANDLE) {
        // get entrypoint addresses that are global (no dispatchable object)

        return addr;
    } else {
        // if a global entrypoint return NULL
        if (addr)
            return NULL;
    }

    struct loader_instance *ptr_instance = loader_get_instance(instance);
    if (ptr_instance == NULL)
        return NULL;
    // Return trampoline code for non-global entrypoints including any
    // extensions.
    // Device extensions are returned if a layer or ICD supports the extension.
    // Instance extensions are returned if the extension is enabled and the
    // loader
    // or someone else supports the extension
    return trampolineGetProcAddr(ptr_instance, pName);
}

/**
 * Get a device level or global level entry point address.
 * @param device
 * @param pName
 * @return
 *    If device is valid, returns a device relative entry point for device level
 *    entry points both core and extensions.
 *    Device relative means call down the device chain.
 */
LOADER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char *pName) {
    void *addr;

    /* for entrypoints that loader must handle (ie non-dispatchable or create
       object)
       make sure the loader entrypoint is returned */
    addr = loader_non_passthrough_gdpa(pName);
    if (addr) {
        return addr;
    }

    /* Although CreateDevice is on device chain it's dispatchable object isn't
     * a VkDevice or child of VkDevice so return NULL.
     */
    if (!strcmp(pName, "CreateDevice"))
        return NULL;

    /* return the dispatch table entrypoint for the fastest case */
    const VkLayerDispatchTable *disp_table = *(VkLayerDispatchTable **)device;
    if (disp_table == NULL)
        return NULL;

    addr = loader_lookup_device_dispatch_table(disp_table, pName);
    if (addr)
        return addr;

    if (disp_table->GetDeviceProcAddr == NULL)
        return NULL;
    return disp_table->GetDeviceProcAddr(device, pName);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceExtensionProperties(const char *pLayerName,
                                       uint32_t *pPropertyCount,
                                       VkExtensionProperties *pProperties) {
    struct loader_extension_list *global_ext_list = NULL;
    struct loader_layer_list instance_layers;
    struct loader_extension_list icd_extensions;
    struct loader_icd_libs icd_libs;
    uint32_t copy_size;

    tls_instance = NULL;
    memset(&icd_extensions, 0, sizeof(icd_extensions));
    memset(&instance_layers, 0, sizeof(instance_layers));
    loader_platform_thread_once(&once_init, loader_initialize);

    /* get layer libraries if needed */
    if (pLayerName && strlen(pLayerName) != 0) {
        if (vk_string_validate(MaxLoaderStringLength, pLayerName) ==
            VK_STRING_ERROR_NONE) {
            loader_layer_scan(NULL, &instance_layers, NULL);
            for (uint32_t i = 0; i < instance_layers.count; i++) {
                struct loader_layer_properties *props =
                    &instance_layers.list[i];
                if (strcmp(props->info.layerName, pLayerName) == 0) {
                    global_ext_list = &props->instance_extension_list;
                }
            }
        } else {
            assert(VK_FALSE && "vkEnumerateInstanceExtensionProperties:  "
                               "pLayerName is too long or is badly formed");
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    } else {
        /* Scan/discover all ICD libraries */
        memset(&icd_libs, 0, sizeof(struct loader_icd_libs));
        loader_icd_scan(NULL, &icd_libs);
        /* get extensions from all ICD's, merge so no duplicates */
        loader_get_icd_loader_instance_extensions(NULL, &icd_libs,
                                                  &icd_extensions);
        loader_scanned_icd_clear(NULL, &icd_libs);
        global_ext_list = &icd_extensions;
    }

    if (global_ext_list == NULL) {
        loader_destroy_layer_list(NULL, &instance_layers);
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    if (pProperties == NULL) {
        *pPropertyCount = global_ext_list->count;
        loader_destroy_layer_list(NULL, &instance_layers);
        loader_destroy_generic_list(
            NULL, (struct loader_generic_list *)&icd_extensions);
        return VK_SUCCESS;
    }

    copy_size = *pPropertyCount < global_ext_list->count
                    ? *pPropertyCount
                    : global_ext_list->count;
    for (uint32_t i = 0; i < copy_size; i++) {
        memcpy(&pProperties[i], &global_ext_list->list[i],
               sizeof(VkExtensionProperties));
    }
    *pPropertyCount = copy_size;
    loader_destroy_generic_list(NULL,
                                (struct loader_generic_list *)&icd_extensions);

    if (copy_size < global_ext_list->count) {
        loader_destroy_layer_list(NULL, &instance_layers);
        return VK_INCOMPLETE;
    }

    loader_destroy_layer_list(NULL, &instance_layers);
    return VK_SUCCESS;
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                   VkLayerProperties *pProperties) {

    struct loader_layer_list instance_layer_list;
    tls_instance = NULL;

    loader_platform_thread_once(&once_init, loader_initialize);

    uint32_t copy_size;

    /* get layer libraries */
    memset(&instance_layer_list, 0, sizeof(instance_layer_list));
    loader_layer_scan(NULL, &instance_layer_list, NULL);

    if (pProperties == NULL) {
        *pPropertyCount = instance_layer_list.count;
        loader_destroy_layer_list(NULL, &instance_layer_list);
        return VK_SUCCESS;
    }

    copy_size = (*pPropertyCount < instance_layer_list.count)
                    ? *pPropertyCount
                    : instance_layer_list.count;
    for (uint32_t i = 0; i < copy_size; i++) {
        memcpy(&pProperties[i], &instance_layer_list.list[i].info,
               sizeof(VkLayerProperties));
    }

    *pPropertyCount = copy_size;
    loader_destroy_layer_list(NULL, &instance_layer_list);

    if (copy_size < instance_layer_list.count) {
        return VK_INCOMPLETE;
    }

    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
loader_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                          const char *pLayerName,
                                          uint32_t *pPropertyCount,
                                          VkExtensionProperties *pProperties) {
    struct loader_physical_device *phys_dev;
    uint32_t copy_size;

    uint32_t count;
    struct loader_device_extension_list *dev_ext_list = NULL;
    struct loader_layer_list implicit_layer_list;

    // TODO fix this aliases physical devices
    phys_dev = loader_get_physical_device(physicalDevice);

    /* get layer libraries if needed */
    if (pLayerName && strlen(pLayerName) != 0) {
        if (vk_string_validate(MaxLoaderStringLength, pLayerName) ==
            VK_STRING_ERROR_NONE) {
            for (uint32_t i = 0;
                 i < phys_dev->this_instance->device_layer_list.count; i++) {
                struct loader_layer_properties *props =
                    &phys_dev->this_instance->device_layer_list.list[i];
                if (strcmp(props->info.layerName, pLayerName) == 0) {
                    dev_ext_list = &props->device_extension_list;
                }
            }
            count = (dev_ext_list == NULL) ? 0 : dev_ext_list->count;
            if (pProperties == NULL) {
                *pPropertyCount = count;
                return VK_SUCCESS;
            }

            copy_size = *pPropertyCount < count ? *pPropertyCount : count;
            for (uint32_t i = 0; i < copy_size; i++) {
                memcpy(&pProperties[i], &dev_ext_list->list[i].props,
                       sizeof(VkExtensionProperties));
            }
            *pPropertyCount = copy_size;

            if (copy_size < count) {
                return VK_INCOMPLETE;
            }
        } else {
            loader_log(phys_dev->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT,
                       0, "vkEnumerateDeviceExtensionProperties:  pLayerName "
                          "is too long or is badly formed");
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
        return VK_SUCCESS;
    } else {
        /* this case is during the call down the instance chain with pLayerName
         * == NULL*/
        struct loader_icd *icd = phys_dev->this_icd;
        uint32_t icd_ext_count = *pPropertyCount;
        VkResult res;

        /* get device extensions */
        res = icd->EnumerateDeviceExtensionProperties(
            phys_dev->phys_dev, NULL, &icd_ext_count, pProperties);
        if (res != VK_SUCCESS)
            return res;

        loader_init_layer_list(phys_dev->this_instance, &implicit_layer_list);

        loader_add_layer_implicit(
            phys_dev->this_instance, VK_LAYER_TYPE_INSTANCE_IMPLICIT,
            &implicit_layer_list,
            &phys_dev->this_instance->instance_layer_list);
        /* we need to determine which implicit layers are active,
         * and then add their extensions. This can't be cached as
         * it depends on results of environment variables (which can change).
         */
        if (pProperties != NULL) {
            /* initialize dev_extension list within the physicalDevice object */
            res = loader_init_device_extensions(
                phys_dev->this_instance, phys_dev, icd_ext_count, pProperties,
                &phys_dev->device_extension_cache);
            if (res != VK_SUCCESS)
                return res;

            /* we need to determine which implicit layers are active,
             * and then add their extensions. This can't be cached as
             * it depends on results of environment variables (which can
             * change).
             */
            struct loader_extension_list all_exts = {0};
            loader_add_to_ext_list(phys_dev->this_instance, &all_exts,
                                   phys_dev->device_extension_cache.count,
                                   phys_dev->device_extension_cache.list);

            loader_init_layer_list(phys_dev->this_instance,
                                   &implicit_layer_list);

            loader_add_layer_implicit(
                phys_dev->this_instance, VK_LAYER_TYPE_INSTANCE_IMPLICIT,
                &implicit_layer_list,
                &phys_dev->this_instance->instance_layer_list);

            for (uint32_t i = 0; i < implicit_layer_list.count; i++) {
                for (
                    uint32_t j = 0;
                    j < implicit_layer_list.list[i].device_extension_list.count;
                    j++) {
                    loader_add_to_ext_list(phys_dev->this_instance, &all_exts,
                                           1,
                                           &implicit_layer_list.list[i]
                                                .device_extension_list.list[j]
                                                .props);
                }
            }
            uint32_t capacity = *pPropertyCount;
            VkExtensionProperties *props = pProperties;

            for (uint32_t i = 0; i < all_exts.count && i < capacity; i++) {
                props[i] = all_exts.list[i];
            }
            /* wasn't enough space for the extensions, we did partial copy now
             * return VK_INCOMPLETE */
            if (capacity < all_exts.count) {
                res = VK_INCOMPLETE;
            } else {
                *pPropertyCount = all_exts.count;
            }
            loader_destroy_generic_list(
                phys_dev->this_instance,
                (struct loader_generic_list *)&all_exts);
        } else {
            /* just return the count; need to add in the count of implicit layer
             * extensions
             * don't worry about duplicates being added in the count */
            *pPropertyCount = icd_ext_count;

            for (uint32_t i = 0; i < implicit_layer_list.count; i++) {
                *pPropertyCount +=
                    implicit_layer_list.list[i].device_extension_list.count;
            }
            res = VK_SUCCESS;
        }

        loader_destroy_generic_list(
            phys_dev->this_instance,
            (struct loader_generic_list *)&implicit_layer_list);
        return res;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL
loader_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice,
                                      uint32_t *pPropertyCount,
                                      VkLayerProperties *pProperties) {
    uint32_t copy_size;
    struct loader_physical_device *phys_dev;
    // TODO fix this, aliases physical devices
    phys_dev = loader_get_physical_device(physicalDevice);
    uint32_t count = phys_dev->this_instance->device_layer_list.count;

    if (pProperties == NULL) {
        *pPropertyCount = count;
        return VK_SUCCESS;
    }

    copy_size = (*pPropertyCount < count) ? *pPropertyCount : count;
    for (uint32_t i = 0; i < copy_size; i++) {
        memcpy(&pProperties[i],
               &(phys_dev->this_instance->device_layer_list.list[i].info),
               sizeof(VkLayerProperties));
    }
    *pPropertyCount = copy_size;

    if (copy_size < count) {
        return VK_INCOMPLETE;
    }

    return VK_SUCCESS;
}

VkStringErrorFlags vk_string_validate(const int max_length, const char *utf8) {
    VkStringErrorFlags result = VK_STRING_ERROR_NONE;
    int num_char_bytes;
    int i, j;

    for (i = 0; i < max_length; i++) {
        if (utf8[i] == 0) {
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
