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
 * Author: Chia-I Wu <olvaffe@gmail.com>
 * Author: Chia-I Wu <olv@lunarg.com>
 * Author: Mark Lobodzinski <mark@LunarG.com>
 *
 */

#ifndef LOADER_H
#define LOADER_H

#include <vulkan/vulkan.h>
#include <vk_loader_platform.h>


#include <vulkan/vk_layer.h>
#include <vulkan/vk_icd.h>
#include <assert.h>

#if defined(__GNUC__) && __GNUC__ >= 4
#define LOADER_EXPORT __attribute__((visibility("default")))
#elif defined(__SUNPRO_C) && (__SUNPRO_C >= 0x590)
#define LOADER_EXPORT __attribute__((visibility("default")))
#else
#define LOADER_EXPORT
#endif

#define MAX_STRING_SIZE 1024
#define VK_MAJOR(version) (version >> 22)
#define VK_MINOR(version) ((version >> 12) & 0x3ff)
#define VK_PATCH(version) (version & 0xfff)

enum layer_type {
    VK_LAYER_TYPE_DEVICE_EXPLICIT = 0x1,
    VK_LAYER_TYPE_INSTANCE_EXPLICIT = 0x2,
    VK_LAYER_TYPE_GLOBAL_EXPLICIT = 0x3, // instance and device layer, bitwise
    VK_LAYER_TYPE_DEVICE_IMPLICIT = 0x4,
    VK_LAYER_TYPE_INSTANCE_IMPLICIT = 0x8,
    VK_LAYER_TYPE_GLOBAL_IMPLICIT = 0xc, // instance and device layer, bitwise
    VK_LAYER_TYPE_META_EXPLICT = 0x10,
};

typedef enum VkStringErrorFlagBits {
    VK_STRING_ERROR_NONE = 0x00000000,
    VK_STRING_ERROR_LENGTH = 0x00000001,
    VK_STRING_ERROR_BAD_DATA = 0x00000002,
} VkStringErrorFlagBits;
typedef VkFlags VkStringErrorFlags;

static const int MaxLoaderStringLength = 256;
static const char UTF8_ONE_BYTE_CODE = 0xC0;
static const char UTF8_ONE_BYTE_MASK = 0xE0;
static const char UTF8_TWO_BYTE_CODE = 0xE0;
static const char UTF8_TWO_BYTE_MASK = 0xF0;
static const char UTF8_THREE_BYTE_CODE = 0xF0;
static const char UTF8_THREE_BYTE_MASK = 0xF8;
static const char UTF8_DATA_BYTE_CODE = 0x80;
static const char UTF8_DATA_BYTE_MASK = 0xC0;

static const char std_validation_names[9][VK_MAX_EXTENSION_NAME_SIZE] = {
    "VK_LAYER_LUNARG_threading",     "VK_LAYER_LUNARG_param_checker",
    "VK_LAYER_LUNARG_device_limits", "VK_LAYER_LUNARG_object_tracker",
    "VK_LAYER_LUNARG_image",         "VK_LAYER_LUNARG_mem_tracker",
    "VK_LAYER_LUNARG_draw_state",    "VK_LAYER_LUNARG_swapchain",
    "VK_LAYER_GOOGLE_unique_objects"};

// form of all dynamic lists/arrays
// only the list element should be changed
struct loader_generic_list {
    size_t capacity;
    uint32_t count;
    void *list;
};

struct loader_extension_list {
    size_t capacity;
    uint32_t count;
    VkExtensionProperties *list;
};

struct loader_dev_ext_props {
    VkExtensionProperties props;
    uint32_t entrypoint_count;
    char **entrypoints;
};

struct loader_device_extension_list {
    size_t capacity;
    uint32_t count;
    struct loader_dev_ext_props *list;
};

struct loader_name_value {
    char name[MAX_STRING_SIZE];
    char value[MAX_STRING_SIZE];
};

struct loader_lib_info {
    char lib_name[MAX_STRING_SIZE];
    uint32_t ref_count;
    loader_platform_dl_handle lib_handle;
};

struct loader_layer_functions {
    char str_gipa[MAX_STRING_SIZE];
    char str_gdpa[MAX_STRING_SIZE];
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
    PFN_vkGetDeviceProcAddr get_device_proc_addr;
};

struct loader_layer_properties {
    VkLayerProperties info;
    enum layer_type type;
    char lib_name[MAX_STRING_SIZE];
    struct loader_layer_functions functions;
    struct loader_extension_list instance_extension_list;
    struct loader_device_extension_list device_extension_list;
    struct loader_name_value disable_env_var;
    struct loader_name_value enable_env_var;
};

struct loader_layer_list {
    size_t capacity;
    uint32_t count;
    struct loader_layer_properties *list;
};

struct loader_layer_library_list {
    size_t capacity;
    uint32_t count;
    struct loader_lib_info *list;
};

struct loader_dispatch_hash_list {
    size_t capacity;
    uint32_t count;
    uint32_t *index; // index into the dev_ext dispatch table
};

#define MAX_NUM_DEV_EXTS 250
// loader_dispatch_hash_entry and loader_dev_ext_dispatch_table.DevExt have one
// to one
// correspondence; one loader_dispatch_hash_entry for one DevExt dispatch entry.
// Also have a one to one correspondence with functions in dev_ext_trampoline.c
struct loader_dispatch_hash_entry {
    char *func_name;
    struct loader_dispatch_hash_list list; // to handle hashing collisions
};

typedef void(VKAPI_PTR *PFN_vkDevExt)(VkDevice device);
struct loader_dev_ext_dispatch_table {
    PFN_vkDevExt DevExt[MAX_NUM_DEV_EXTS];
};

struct loader_dev_dispatch_table {
    VkLayerDispatchTable core_dispatch;
    struct loader_dev_ext_dispatch_table ext_dispatch;
};

/* per CreateDevice structure */
struct loader_device {
    struct loader_dev_dispatch_table loader_dispatch;
    VkDevice device; // device object from the icd

    uint32_t app_extension_count;
    VkExtensionProperties *app_extension_props;

    struct loader_layer_list activated_layer_list;

    struct loader_device *next;
};

/* per ICD structure */
struct loader_icd {
    // pointers to find other structs
    const struct loader_scanned_icds *this_icd_lib;
    const struct loader_instance *this_instance;

    struct loader_device *logical_device_list;
    VkInstance instance; // instance object from the icd
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
    PFN_vkDestroyInstance DestroyInstance;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceFeatures GetPhysicalDeviceFeatures;
    PFN_vkGetPhysicalDeviceFormatProperties GetPhysicalDeviceFormatProperties;
    PFN_vkGetPhysicalDeviceImageFormatProperties
        GetPhysicalDeviceImageFormatProperties;
    PFN_vkCreateDevice CreateDevice;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties
        GetPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties;
    PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties;
    PFN_vkGetPhysicalDeviceSparseImageFormatProperties
        GetPhysicalDeviceSparseImageFormatProperties;
    PFN_vkCreateDebugReportCallbackEXT CreateDebugReportCallbackEXT;
    PFN_vkDestroyDebugReportCallbackEXT DestroyDebugReportCallbackEXT;
    PFN_vkDebugReportMessageEXT DebugReportMessageEXT;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR
        GetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR
        GetPhysicalDeviceSurfacePresentModesKHR;
#ifdef VK_USE_PLATFORM_WIN32_KHR
    PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR
        GetPhysicalDeviceWin32PresentationSupportKHR;
#endif
#ifdef VK_USE_PLATFORM_MIR_KHR
    PFN_vkGetPhysicalDeviceMirPresentationSupportKHR
        GetPhysicalDeviceMirPresentvationSupportKHR;
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR
        GetPhysicalDeviceWaylandPresentationSupportKHR;
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
    PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR
        GetPhysicalDeviceXcbPresentationSupportKHR;
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
    PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR
        GetPhysicalDeviceXlibPresentationSupportKHR;
#endif

    struct loader_icd *next;
};

/* per ICD library structure */
struct loader_icd_libs {
    size_t capacity;
    uint32_t count;
    struct loader_scanned_icds *list;
};

/* per instance structure */
struct loader_instance {
    VkLayerInstanceDispatchTable *disp; // must be first entry in structure

    uint32_t total_gpu_count;
    struct loader_physical_device *phys_devs;
    uint32_t total_icd_count;
    struct loader_icd *icds;
    struct loader_instance *next;
    struct loader_extension_list ext_list; // icds and loaders extensions
    struct loader_icd_libs icd_libs;
    struct loader_layer_list instance_layer_list;
    struct loader_layer_list device_layer_list;
    struct loader_dispatch_hash_entry disp_hash[MAX_NUM_DEV_EXTS];

    struct loader_msg_callback_map_entry *icd_msg_callback_map;

    struct loader_layer_list activated_layer_list;

    VkInstance instance;

    bool debug_report_enabled;
    VkLayerDbgFunctionNode *DbgFunctionHead;

    VkAllocationCallbacks alloc_callbacks;

    bool wsi_surface_enabled;
#ifdef VK_USE_PLATFORM_WIN32_KHR
    bool wsi_win32_surface_enabled;
#endif
#ifdef VK_USE_PLATFORM_MIR_KHR
    bool wsi_mir_surface_enabled;
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    bool wsi_wayland_surface_enabled;
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
    bool wsi_xcb_surface_enabled;
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
    bool wsi_xlib_surface_enabled;
#endif
#ifdef VK_USE_PLATFORM_ANDROID_KHR
    bool wsi_android_surface_enabled;
#endif
};

/* per enumerated PhysicalDevice structure */
struct loader_physical_device {
    VkLayerInstanceDispatchTable *disp; // must be first entry in structure
    struct loader_instance *this_instance;
    struct loader_icd *this_icd;
    VkPhysicalDevice phys_dev; // object from ICD
                               /*
                                * Fill in the cache of available device extensions from
                                * this physical device. This cache can be used during CreateDevice
                                */
    struct loader_extension_list device_extension_cache;
};

struct loader_struct {
    struct loader_instance *instances;

    unsigned int loaded_layer_lib_count;
    size_t loaded_layer_lib_capacity;
    struct loader_lib_info *loaded_layer_lib_list;
    // TODO add ref counting of ICD libraries
    // TODO use this struct loader_layer_library_list scanned_layer_libraries;
    // TODO add list of icd libraries for ref counting them for closure
};

struct loader_scanned_icds {
    char *lib_name;
    loader_platform_dl_handle handle;
    uint32_t api_version;
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
    PFN_vkCreateInstance CreateInstance;
    PFN_vkEnumerateInstanceExtensionProperties
        EnumerateInstanceExtensionProperties;
};

static inline struct loader_instance *loader_instance(VkInstance instance) {
    return (struct loader_instance *)instance;
}

static inline void loader_set_dispatch(void *obj, const void *data) {
    *((const void **)obj) = data;
}

static inline VkLayerDispatchTable *loader_get_dispatch(const void *obj) {
    return *((VkLayerDispatchTable **)obj);
}

static inline struct loader_dev_dispatch_table *
loader_get_dev_dispatch(const void *obj) {
    return *((struct loader_dev_dispatch_table **)obj);
}

static inline VkLayerInstanceDispatchTable *
loader_get_instance_dispatch(const void *obj) {
    return *((VkLayerInstanceDispatchTable **)obj);
}

static inline void loader_init_dispatch(void *obj, const void *data) {
#ifdef DEBUG
    assert(valid_loader_magic_value(obj) &&
           "Incompatible ICD, first dword must be initialized to "
           "ICD_LOADER_MAGIC. See loader/README.md for details.");
#endif

    loader_set_dispatch(obj, data);
}

/* global variables used across files */
extern struct loader_struct loader;
extern THREAD_LOCAL_DECL struct loader_instance *tls_instance;
extern LOADER_PLATFORM_THREAD_ONCE_DEFINITION(once_init);
extern loader_platform_thread_mutex loader_lock;
extern loader_platform_thread_mutex loader_json_lock;
extern const VkLayerInstanceDispatchTable instance_disp;
extern const char *std_validation_str;

struct loader_msg_callback_map_entry {
    VkDebugReportCallbackEXT icd_obj;
    VkDebugReportCallbackEXT loader_obj;
};

void loader_log(const struct loader_instance *inst, VkFlags msg_type,
                       int32_t msg_code, const char *format, ...);

bool compare_vk_extension_properties(const VkExtensionProperties *op1,
                                     const VkExtensionProperties *op2);

VkResult loader_validate_layers(const struct loader_instance *inst,
                                const uint32_t layer_count,
                                const char *const *ppEnabledLayerNames,
                                const struct loader_layer_list *list);

VkResult loader_validate_instance_extensions(
    const struct loader_instance *inst,
    const struct loader_extension_list *icd_exts,
    const struct loader_layer_list *instance_layer,
    const VkInstanceCreateInfo *pCreateInfo);

/* instance layer chain termination entrypoint definitions */
VKAPI_ATTR VkResult VKAPI_CALL
loader_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkInstance *pInstance);

VKAPI_ATTR void VKAPI_CALL
loader_DestroyInstance(VkInstance instance,
                       const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL
loader_EnumeratePhysicalDevices(VkInstance instance,
                                uint32_t *pPhysicalDeviceCount,
                                VkPhysicalDevice *pPhysicalDevices);

VKAPI_ATTR void VKAPI_CALL
loader_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
                                 VkPhysicalDeviceFeatures *pFeatures);

VKAPI_ATTR void VKAPI_CALL
loader_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice,
                                         VkFormat format,
                                         VkFormatProperties *pFormatInfo);

VKAPI_ATTR VkResult VKAPI_CALL loader_GetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
    VkImageFormatProperties *pImageFormatProperties);

VKAPI_ATTR void VKAPI_CALL loader_GetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkSampleCountFlagBits samples, VkImageUsageFlags usage,
    VkImageTiling tiling, uint32_t *pNumProperties,
    VkSparseImageFormatProperties *pProperties);

VKAPI_ATTR void VKAPI_CALL
loader_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                   VkPhysicalDeviceProperties *pProperties);

VKAPI_ATTR VkResult VKAPI_CALL
loader_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                          const char *pLayerName,
                                          uint32_t *pCount,
                                          VkExtensionProperties *pProperties);

VKAPI_ATTR VkResult VKAPI_CALL
loader_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice,
                                      uint32_t *pCount,
                                      VkLayerProperties *pProperties);

VKAPI_ATTR void VKAPI_CALL loader_GetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice, uint32_t *pCount,
    VkQueueFamilyProperties *pProperties);

VKAPI_ATTR void VKAPI_CALL loader_GetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties *pProperties);

VKAPI_ATTR VkResult VKAPI_CALL
loader_create_device_terminator(VkPhysicalDevice physicalDevice,
                                const VkDeviceCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkDevice *pDevice);

VKAPI_ATTR VkResult VKAPI_CALL
loader_CreateDevice(VkPhysicalDevice gpu, const VkDeviceCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator, VkDevice *pDevice);

/* helper function definitions */
void loader_initialize(void);
bool has_vk_extension_property_array(const VkExtensionProperties *vk_ext_prop,
                                     const uint32_t count,
                                     const VkExtensionProperties *ext_array);
bool has_vk_extension_property(const VkExtensionProperties *vk_ext_prop,
                               const struct loader_extension_list *ext_list);

VkResult loader_add_to_ext_list(const struct loader_instance *inst,
                                struct loader_extension_list *ext_list,
                                uint32_t prop_list_count,
                                const VkExtensionProperties *props);
void loader_destroy_generic_list(const struct loader_instance *inst,
                                 struct loader_generic_list *list);
void loader_delete_layer_properties(const struct loader_instance *inst,
                                    struct loader_layer_list *layer_list);
void loader_expand_layer_names(
    const struct loader_instance *inst, const char *key_name,
    uint32_t expand_count,
    const char expand_names[][VK_MAX_EXTENSION_NAME_SIZE],
    uint32_t *layer_count, char ***ppp_layer_names);
void loader_unexpand_dev_layer_names(const struct loader_instance *inst,
                                     uint32_t layer_count, char **layer_names,
                                     char **layer_ptr,
                                     const VkDeviceCreateInfo *pCreateInfo);
void loader_unexpand_inst_layer_names(const struct loader_instance *inst,
                                      uint32_t layer_count, char **layer_names,
                                      char **layer_ptr,
                                      const VkInstanceCreateInfo *pCreateInfo);
void loader_add_to_layer_list(const struct loader_instance *inst,
                              struct loader_layer_list *list,
                              uint32_t prop_list_count,
                              const struct loader_layer_properties *props);
void loader_scanned_icd_clear(const struct loader_instance *inst,
                              struct loader_icd_libs *icd_libs);
void loader_icd_scan(const struct loader_instance *inst,
                     struct loader_icd_libs *icds);
void loader_layer_scan(const struct loader_instance *inst,
                       struct loader_layer_list *instance_layers,
                       struct loader_layer_list *device_layers);
void loader_get_icd_loader_instance_extensions(
    const struct loader_instance *inst, struct loader_icd_libs *icd_libs,
    struct loader_extension_list *inst_exts);
struct loader_icd *loader_get_icd_and_device(const VkDevice device,
                                             struct loader_device **found_dev);
void *loader_dev_ext_gpa(struct loader_instance *inst, const char *funcName);
void *loader_get_dev_ext_trampoline(uint32_t index);
struct loader_instance *loader_get_instance(const VkInstance instance);
void loader_remove_logical_device(const struct loader_instance *inst,
                                  struct loader_icd *icd,
                                  struct loader_device *found_dev);
VkResult
loader_enable_instance_layers(struct loader_instance *inst,
                              const VkInstanceCreateInfo *pCreateInfo,
                              const struct loader_layer_list *instance_layers);
void loader_deactivate_instance_layers(struct loader_instance *instance);

VkResult loader_create_instance_chain(const VkInstanceCreateInfo *pCreateInfo,
                                      const VkAllocationCallbacks *pAllocator,
                                      struct loader_instance *inst,
                                      VkInstance *created_instance);

void loader_activate_instance_layer_extensions(struct loader_instance *inst,
                                               VkInstance created_inst);

void *loader_heap_alloc(const struct loader_instance *instance, size_t size,
                        VkSystemAllocationScope allocationScope);

void loader_heap_free(const struct loader_instance *instance, void *pMemory);

void *loader_tls_heap_alloc(size_t size);

void loader_tls_heap_free(void *pMemory);

VkStringErrorFlags vk_string_validate(const int max_length,
                                      const char *char_array);

#endif /* LOADER_H */
