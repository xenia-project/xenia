# Architecture of the Vulkan Loader Interfaces

## Table of Contents
  * [Overview](#overview)
    * [Who Should Read This Document](#who-should-read-this-document)
    * [The Loader](#the-loader)
    * [Layers](#layers)
    * [Installable Client Drivers](#installable-client-drivers)
    * [Instance Versus Device](#instance-versus-device)
    * [Dispatch Tables and Call Chains](#dispatch-tables-and-call-chains)

  * [Application Interface to the Loader](#application-interface-to-the-loader)
    * [Interfacing with Vulkan Functions](#interfacing-with-vulkan-functions)
    * [Application Layer Usage](#application-layer-usage)
    * [Application Usage of Extensions](#application-usage-of-extensions)

  * [Loader and Layer Interface](#loader-and-layer-interface)
    * [Layer Discovery](#layer-discovery)
    * [Layer Version Negotiation](#layer-version-negotiation)
    * [Layer Call Chains and Distributed Dispatch](#layer-call-chains-and-distributed-dispatch)
    * [Layer Unknown Physical Device Extensions](#layer-unknown-physical-device-extensions)
    * [Layer Intercept Requirements](#layer-intercept-requirements)
    * [Distributed Dispatching Requirements](#distributed-dispatching-requirements)
    * [Layer Conventions and Rules](#layer-conventions-and-rules)
    * [Layer Dispatch Initialization](#layer-dispatch-initialization)
    * [Example Code for CreateInstance](#example-code-for-createinstance)
    * [Example Code for CreateDevice](#example-code-for-createdevice)
    * [Meta-layers](#meta-layers)
    * [Pre-Instance Functions](#pre-instance-functions)
    * [Special Considerations](#special-considerations)
    * [Layer Manifest File Format](#layer-manifest-file-format)
    * [Layer Library Versions](#layer-library-versions)

  * [Vulkan Installable Client Driver interface with the loader](#vulkan-installable-client-driver-interface-with-the-loader)
    * [ICD Discovery](#icd-discovery)
    * [ICD Manifest File Format](#icd-manifest-file-format)
    * [ICD Vulkan Entry-Point Discovery](#icd-vulkan-entry-point-discovery)
    * [ICD API Version](#icd-api-version)
    * [ICD Unknown Physical Device Extensions](#icd-unknown-physical-device-extensions)
    * [ICD Dispatchable Object Creation](#icd-dispatchable-object-creation)
    * [Handling KHR Surface Objects in WSI Extensions](#handling-khr-surface-objects-in-wsi-extensions)
    * [Loader and ICD Interface Negotiation](#loader-and-icd-interface-negotiation)

  * [Table of Debug Environment Variables](#table-of-debug-environment-variables)
  * [Glossary of Terms](#glossary-of-terms)
 
## Overview

Vulkan is a layered architecture, made up of the following elements:
  * The Vulkan Application
  * [The Vulkan Loader](#the-loader)
  * [Vulkan Layers](#layers)
  * [Installable Client Drivers (ICDs)](#installable-client-drivers)

![High Level View of Loader](./images/high_level_loader.png)

The general concepts in this document are applicable to the loaders available
for Windows, Linux and Android based systems.


#### Who Should Read This Document

While this document is primarily targeted at developers of Vulkan applications,
drivers and layers, the information contained in it could be useful to anyone
wanting a better understanding of the Vulkan runtime.


#### The Loader

The application sits on one end of, and interfaces directly with, the
loader.  On the other end of the loader from the application are the ICDs, which
control the Vulkan-capable hardware.  An important point to remember is that
Vulkan-capable hardware can be graphics-based, compute-based, or both. Between
the application and the ICDs the loader can inject a number of optional
[layers](#layers) that provide special functionality.

The loader is responsible for working with the various layers as well as
supporting multiple GPUs and their drivers.  Any Vulkan function may
wind up calling into a diverse set of modules: loader, layers, and ICDs.
The loader is critical to managing the proper dispatching of Vulkan
functions to the appropriate set of layers and ICDs. The Vulkan object
model allows the loader to insert layers into a call chain so that the layers
can process Vulkan functions prior to the ICD being called.

This document is intended to provide an overview of the necessary interfaces
between each of these.


##### Goals of the Loader

The loader was designed with the following goals in mind.
 1. Support one or more Vulkan-capable ICD on a user's computer system without
them interfering with one another.
 2. Support Vulkan Layers which are optional modules that can be enabled by an
application, developer, or standard system settings.
 3. Impact the overall performance of a Vulkan application in the lowest
possible fashion.


#### Layers

Layers are optional components that augment the Vulkan system.  They can
intercept, evaluate, and modify existing Vulkan functions on their way from the
application down to the hardware.  Layers are implemented as libraries that can
be enabled in different ways (including by application request) and are loaded
during CreateInstance.  Each layer can choose to hook (intercept) any Vulkan
functions which in turn can be ignored or augmented.  A layer does not need to
intercept all Vulkan functions.  It may choose to intercept all known functions,
or, it may choose to intercept only one function.

Some examples of features that layers may expose include:
 * Validating API usage
 * Adding the ability to perform Vulkan API tracing and debugging
 * Overlay additional content on the applications surfaces

Because layers are optionally, you may choose to enable layers for debugging
your application, but then disable any layer usage when you release your
product.


#### Installable Client Drivers

Vulkan allows multiple Installable Client Drivers (ICDs) each supporting one
or more devices (represented by a Vulkan `VkPhysicalDevice` object) to be used
collectively. The loader is responsible for discovering available Vulkan ICDs on
the system. Given a list of available ICDs, the loader can enumerate all the
physical devices available  for an application and return this information to
the application.


#### Instance Versus Device

There is an important concept which you will see brought up repeatedly
throughout this document.  Many functions, extensions, and other things in
Vulkan are separated into two main groups:
 * Instance-related Objects
 * Device-related Objects


##### Instance-related Objects

A Vulkan Instance is a high-level construct used to provide Vulkan system-level
information, or functionality.  Vulkan objects associated directly with an
instance are:
 * `VkInstance`
 * `VkPhysicalDevice`

An Instance function is any Vulkan function which takes as its first parameter
either an object from the Instance list, or nothing at all.  Some Vulkan
Instance functions are:
 * `vkEnumerateInstanceExtensionProperties`
 * `vkEnumeratePhysicalDevices`
 * `vkCreateInstance`
 * `vkDestroyInstance`

You query Vulkan Instance functions using `vkGetInstanceProcAddr`.
`vkGetInstanceProcAddr` can be used to query either device or instance entry-
points in addition to all core entry-points.  The returned function pointer is
valid for this Instance and any object created under this Instance (including
all `VkDevice` objects).  

Similarly, an Instance extension is a set of Vulkan Instance functions extending
the Vulkan language.  These will be discussed in more detail later.


##### Device-related Objects

A Vulkan Device, on the other-hand, is a logical identifier used to associate
functions with a particular physical device on a user's system.  Vulkan
constructs associated directly with a device include:
 * `VkDevice`
 * `VkQueue`
 * `VkCommandBuffer`
 * Any dispatchable object that is a child of a one of the above.

A Device function is any Vulkan function which takes any Device Object as its
first parameter.  Some Vulkan Device functions are:
 * `vkQueueSubmit`
 * `vkBeginCommandBuffer`
 * `vkCreateEvent`

You can query Vulkan Device functions using either `vkGetInstanceProcAddr` or 
`vkGetDeviceProcAddr`.  If you choose to use `vkGetInstanceProcAddr`, it will
have an additional level built into the call chain, which will reduce
performance slightly.  However, the function pointer returned can be used for
any device created later, as long as it is associated with the same Vulkan
Instance. If, instead you use `vkGetDeviceProcAddr`, the call chain will be more
optimized to the specific device, but it will **only** work for the device used
to query the function function pointer.  Also, unlike `vkGetInstanceProcAddr`,
`vkGetDeviceProcAddr` can only be used on core Vulkan Device functions, or
Device extension functions.

The best solution is to query Instance extension functions using
`vkGetInstanceProcAddr`, and to query Device extension functions using
`vkGetDeviceProcAddr`.  See
[Best Application Performance Setup](#best-application-performance-setup) for
more information on this.

As with Instance extensions, a Device extension is a set of Vulkan Device
functions extending the Vulkan language. You can read more about these later in
the document.


#### Dispatch Tables and Call Chains

Vulkan uses an object model to control the scope of a particular action /
operation.  The object to be acted on is always the first parameter of a Vulkan
call and is a dispatchable object (see Vulkan specification section 2.3 Object
Model).  Under the covers, the dispatchable object handle is a pointer to a
structure, which in turn, contains a pointer to a dispatch table maintained by
the loader.  This dispatch table contains pointers to the Vulkan functions
appropriate to that object.

There are two types of dispatch tables the loader maintains:
 - Instance Dispatch Table
  - Created in the loader during the call to `vkCreateInstance`
 - Device Dispatch Table
  - Created in the loader during the call to `vkCreateDevice`

At that time the application and/or system can specify optional layers to be
included.  The loader will initialize the specified layers to create a call
chain for each Vulkan function and each entry of the dispatch table will point
to the first element of that chain. Thus, the loader builds an instance call
chain for each `VkInstance` that is created and a device call chain for each
`VkDevice` that is created.

When an application calls a Vulkan function, this typically will first hit a
*trampoline* function in the loader.  These *trampoline* functions are small,
simple functions that jump to the appropriate dispatch table entry for the
object they are given.  Additionally, for functions in the instance call chain,
the loader has an additional function, called a *terminator*, which is called
after all enabled layers to marshall the appropriate information to all
available ICDs.


##### Instance Call Chain Example

For example, the diagram below represents what happens in the call chain for
`vkCreateInstance`. After initializing the chain, the loader will call into the
first layer's `vkCreateInstance` which will call the next finally terminating in
the loader again where this function calls every ICD's `vkCreateInstance` and
saves the results. This allows every enabled layer for this chain to set up
what it needs based on the `VkInstanceCreateInfo` structure from the
application.

![Instance Call Chain](./images/loader_instance_chain.png)

This also highlights some of the complexity the loader must manage when using
instance call chains. As shown here, the loader's *terminator* must aggregate
information to and from multiple ICDs when they are present. This implies that
the loader has to be aware of any instance-level extensions which work on a
`VkInstance` to aggregate them correctly.


##### Device Call Chain Example

Device call chains are created at `vkCreateDevice` and are generally simpler
because they deal with only a single device and the ICD can always be the
*terminator* of the chain. 

![Loader Device Call Chain](./images/loader_device_chain_loader.png)


<br/>
<br/>

## Application Interface to the Loader

In this section we'll discuss how an application interacts with the loader,
including:
  * [Interfacing with Vulkan Functions](#interfacing-with-vulkan-functions)
    * [Vulkan Direct Exports](#vulkan-direct-exports)
    * [Directly Linking to the Loader](#directly-linking-to-the-loader)
      * [Dynamic Linking](#dynamic-linking)
      * [Static Linking](#static-linking)
    * [Indirectly Linking to the Loader](#indirectly-linking-to-the-loader)
    * [Best Application Performance Setup](#best-application-performance-setup)
    * [ABI Versioning](#abi-versioning)
  * [Application Layer Usage](#application-layer-usage)
    * [Implicit vs Explicit Layers](#implicit-vs-explicit-layers)
    * [Forcing Layer Source Folders](#forcing-layer-source-folders)
    * [Forcing Layers to be Enabled](#forcing-layers-to-be-enabled)
    * [Overall Layer Ordering](#overall-layer-ordering)
  * [Application Usage of Extensions](#application-usage-of-extensions)
    * [Instance and Device Extensions](#instance-and-device-extensions)
    * [WSI Extensions](#wsi-extensions)
    * [Unknown Extensions](#unknown-extensions)

  
#### Interfacing with Vulkan Functions
There are several ways you can interface with Vulkan functions through the
loader.


##### Vulkan Direct Exports
The loader library on Windows, Linux and Android will export all core Vulkan
and all appropriate Window System Interface (WSI) extensions. This is done to
make it simpler to get started with Vulkan development. When an application
links directly to the loader library in this way, the Vulkan calls are simple
*trampoline* functions that jump to the appropriate dispatch table entry for the
object they are given.


##### Directly Linking to the Loader

###### Dynamic Linking
The loader is ordinarily distributed as a dynamic library (.dll on Windows or
.so on Linux) which gets installed to the system path for dynamic libraries.
Linking to the dynamic library is generally the preferred method of linking to
the loader, as doing so allows the loader to be updated for bug fixes and
improvements. Furthermore, the dynamic library is generally installed to Windows
systems as part of driver installation and is generally provided on Linux
through the system package manager. This means that applications can usually
expect a copy of the loader to be present on a system. If applications want to
be completely sure that a loader is present, they can include a loader or
runtime installer with their application.

###### Static Linking
The loader can also be used as a static library (this is shipped in the
Windows SDK as `VKstatic.1.lib`). Linking to the static loader means that the
user does not need to have a Vulkan runtime installed, and it also guarantees
that your application will use a specific version of the loader. However, there
are several downsides to this approach:

  - The static library can never be updated without re-linking the application
  - This opens up the possibility that two included libraries could contain
  different versions of the loader
    - This could potentially cause conflicts between the different loader versions

As a result, it is recommended that users prefer linking to the .dll and
.so versions of the loader.


##### Indirectly Linking to the Loader
Applications are not required to link directly to the loader library, instead
they can use the appropriate platform specific dynamic symbol lookup on the
loader library to initialize the application's own dispatch table. This allows
an application to fail gracefully if the loader cannot be found.  It also
provides the fastest mechanism for the application to call Vulkan functions. An
application will only need to query (via system calls such as dlsym()) the
address of `vkGetInstanceProcAddr` from the loader library. Using
`vkGetInstanceProcAddr` the application can then discover the address of all
functions and extensions available, such as `vkCreateInstance`,
`vkEnumerateInstanceExtensionProperties` and
`vkEnumerateInstanceLayerProperties` in a platform-independent way.


##### Best Application Performance Setup

If you desire the best performance possible, you should setup your own
dispatch table so that all your Instance functions are queried using
`vkGetInstanceProcAddr` and all your Device functions are queried using
`vkGetDeviceProcAddr`.

*Why should you do this?*

The answer comes in how the call chain of Instance functions are implemented
versus the call chain of a Device functions.  Remember, a [Vulkan Instance is a
high-level construct used to provide Vulkan system-level information](#instance-
related-objects). Because of this, Instance functions need to be broadcasted to
every available ICD on the system.  The following diagram shows an approximate
view of an Instance call chain with 3 enabled layers:

![Instance Call Chain](./images/loader_instance_chain.png)

This is also how a Vulkan Device function call chain looks if you query it
using `vkGetInstanceProcAddr`.  On the otherhand, a Device
function doesn't need to worry about the broadcast becuase it knows specifically
which associated ICD and which associated Physical Device the call should
terminate at.  Because of this, the loader doesn't need to get involved between
any enabled layers and the ICD.  Thus, if you used a loader-exported Vulkan
Device function, the call chain in the same scenario as above would look like:

![Loader Device Call Chain](./images/loader_device_chain_loader.png)

An even better solution would be for an application to perform a
`vkGetDeviceProcAddr` call on all Device functions.  This further optimizes the
call chain by removing the loader all-together under most scenarios:

![Application Device Call Chain](./images/loader_device_chain_app.png)

Also, notice if no layers are enabled, your application function pointer would
point **directly to the ICD**.  If called enough, those fewer calls can add up
to performance savings.

**NOTE:** There are some Device functions which still require the loader to
intercept them with a *trampoline* and *terminator*. There are very few of
these, but they are typically functions which the loader wraps with its own
data.  In those cases, even the Device call chain will continue to look like the
Instance call chain.  One example of a Device function requiring a *terminator*
is `vkCreateSwapchainKHR`.  For that function, the loader needs to potentially
convert the KHR_surface object into an ICD-specific KHR_surface object prior to
passing down the rest of the function's information to the ICD.

Remember:
 * `vkGetInstanceProcAddr` can be used to query
either device or instance entry-points in addition to all core entry-points.
 * `vkGetDeviceProcAddr` can only be used to query for device
extension or core device entry-points.


##### ABI Versioning

The Vulkan loader library will be distributed in various ways including Vulkan
SDKs, OS package distributions and Independent Hardware Vendor (IHV) driver
packages. These details are beyond the scope of this document. However, the name
and versioning of the Vulkan loader library is specified so an app can link to
the correct Vulkan ABI library version. Vulkan versioning is such that ABI
backwards compatibility is guaranteed for all versions with the same major
number (e.g. 1.0 and 1.1). On Windows, the loader library encodes the ABI
version in its name such that multiple ABI incompatible versions of the loader
can peacefully coexist on a given system. The Vulkan loader library file name is
`vulkan-<ABI version>.dll`. For example, for Vulkan version 1.X on Windows the
library filename is vulkan-1.dll. And this library file can typically be found
in the windows/system32 directory (on 64-bit Windows installs, the 32-bit
version of the loader with the same name can be found in the windows/sysWOW64
directory).

For Linux, shared libraries are versioned based on a suffix. Thus, the ABI
number is not encoded in the base of the library filename as on Windows. On
Linux an application wanting to link to the latest Vulkan ABI version would
just link to the name vulkan (libvulkan.so).  A specific Vulkan ABI version can
also be linked to by applications (e.g. libvulkan.so.1).


#### Application Layer Usage

Applications desiring Vulkan functionality beyond what the core API offers may
use various layers or extensions. A layer cannot introduce new Vulkan core API
entry-points to an application that are not exposed in Vulkan.h.  However,
layers may offer extensions that introduce new Vulkan commands that can be
queried through the extension interface.

A common use of layers is for API validation which can be enabled by
loading the layer during application development, but not loading the layer
for application release. This eliminates the overhead of validating the
application's usage of the API, something that wasn't available on some previous
graphics APIs.

To find out what layers are available to your application, use
`vkEnumerateInstanceLayerProperties`.  This will report all layers
that have been discovered by the loader.  The loader looks in various locations
to find layers on the system.  For more information see the
[Layer discovery](#layer-discovery) section below.

To enable a layer, or layers, simply pass the name of the layers you wish to
enable in the `ppEnabledLayerNames` field of the `VkInstanceCreateInfo` during
a call to `vkCreateInstance`.  Once done, the layers you have enabled will be
active for all Vulkan functions using the created `VkInstance`, and any of
its child objects.

**NOTE:** Layer ordering is important in several cases since some layers
interact with each other.  Be careful when enabling layers as this may be
the case.  See the [Overall Layer Ordering](#overall-layer-ordering) section
for more information.

The following code section shows how you would go about enabling the
VK_LAYER_LUNARG_standard_validation layer.

```
   char *instance_validation_layers[] = {
        "VK_LAYER_LUNARG_standard_validation"
    };
    const VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "TEST_APP",
        .applicationVersion = 0,
        .pEngineName = "TEST_ENGINE",
        .engineVersion = 0,
        .apiVersion = VK_API_VERSION_1_0,
    };
    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = NULL,
        .pApplicationInfo = &app,
        .enabledLayerCount = 1,
        .ppEnabledLayerNames = (const char *const *)instance_validation_layers,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = NULL,
    };
    err = vkCreateInstance(&inst_info, NULL, &demo->inst);
```

At `vkCreateInstance` and `vkCreateDevice`, the loader constructs call chains
that include the application specified (enabled) layers.  Order is important in
the `ppEnabledLayerNames` array; array element 0 is the topmost (closest to the
application) layer inserted in the chain and the last array element is closest
to the driver.  See the [Overall Layer Ordering](#overall-layer-ordering)
section for more information on layer ordering.

**NOTE:** *Device Layers Are Now Deprecated*
> `vkCreateDevice` originally was able to select layers in a similar manner to
`vkCreateInstance`.  This lead to the concept of "instance
> layers" and "device layers".  It was decided by Khronos to deprecate the
> "device layer" functionality and only consider "instance layers".
> Therefore, `vkCreateDevice` will use the layers specified at
`vkCreateInstance`.
> Because of this, the following items have been deprecated:
> * `VkDeviceCreateInfo` fields:
>  * `ppEnabledLayerNames`
>  * `enabledLayerCount`
> * The `vkEnumerateDeviceLayerProperties` function


##### Implicit vs Explicit Layers

Explicit layers are layers which are enabled by an application (e.g. with the
vkCreateInstance function), or by an environment variable (as mentioned
previously).

Implicit layers are those which are enabled by their existence. For example,
certain application environments (e.g. Steam or an automotive infotainment
system) may have layers which they always want enabled for all applications
that they start. Other implicit layers may be for all applications started on a
given system (e.g. layers that overlay frames-per-second). Implicit layers are
enabled automatically, whereas explicit layers must be enabled explicitly.

Implicit layers have an additional requirement over explicit layers in that they
require being able to be disabled by an environmental variable.  This is due
to the fact that they are not visible to the application and could cause issues.
A good principle to keep in mind would be to define both an enable and disable
environment variable so the users can deterministicly enable the functionality.
On Desktop platforms (Windows and Linux), these enable/disable settings are
defined in the layer's JSON file.

Discovery of system-installed implicit and explicit layers is described later in
the [Layer Discovery Section](#layer-discovery).  For now, simply know that what
distinguishes a layer as implicit or explicit is dependent on the Operating
system, as shown in the table below.

| Operating System | Implicit Layer Identification |
|----------------|--------------------|
| Windows  | Implicit Layers are located in a different Windows registry location than Explicit Layers. |
| Linux | Implicit Layers are located in a different directory location than Explicit Layers. |
| Android | There is **No Support For Implicit Layers** on Android. |


##### Forcing Layer Source Folders

Developers may need to use special, pre-production layers, without modifying the
system-installed layers. You can direct the loader to look for layers in a
specific folder by defining the "VK\_LAYER\_PATH" environment variable.  This
will override the mechanism used for finding system-installed layers. Because
layers of interest may exist in several disinct folders on a system, this
environment variable can containis several paths seperated by the operating
specific path separator.  On Windows, each separate folder should be separated
in the list using a semi-colon.  On Linux, each folder name should be separated
using a colon.

If "VK\_LAYER\_PATH" exists, **only** the folders listed in it will be scanned
for layers.  Each directory listed should be the full pathname of a folder
containing layer manifest files.


##### Forcing Layers to be Enabled on Windows and Linux

Developers may want to enable layers that are not enabled by the given
application they are using. On Linux and Windows, the environment variable
"VK\_INSTANCE\_LAYERS" can be used to enable additional layers which are
not specified (enabled) by the application at `vkCreateInstance`.
"VK\_INSTANCE\_LAYERS" is a colon (Linux)/semi-colon (Windows) separated
list of layer names to enable. Order is relevant with the first layer in the
list being the top-most layer (closest to the application) and the last
layer in the list being the bottom-most layer (closest to the driver).
See the [Overall Layer Ordering](#overall-layer-ordering) section
for more information.

Application specified layers and user specified layers (via environment
variables) are aggregated and duplicates removed by the loader when enabling
layers. Layers specified via environment variable are top-most (closest to the
application) while layers specified by the application are bottommost.

An example of using these environment variables to activate the validation
layer `VK_LAYER_LUNARG_parameter_validation` on Windows or Linux is as follows:

```
> $ export VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_parameter_validation
```


##### Overall Layer Ordering

The overall ordering of all layers by the loader based on the above looks
as follows:

![Loader Layer Ordering](./images/loader_layer_order.png)

Ordering may also be important internal to the list of Explicit Layers.
Some layers may be dependent on other behavior being implemented before
or after the loader calls it.  For example: the VK_LAYER_LUNARG_core_validation
layer expects the VK_LAYER_LUNARG_parameter_validation to be called first.
This is because the VK_LAYER_LUNARG_parameter_validation will filter out any
invalid `NULL` pointer calls prior to the rest of the validation checking
done by VK_LAYER_LUNARG_core_validation.  If not done properly, you may see
crashes in the VK_LAYER_LUNARG_core_validation layer that would otherwise be
avoided.


#### Application Usage of Extensions

Extensions are optional functionality provided by a layer, the loader or an
ICD. Extensions can modify the behavior of the Vulkan API and need to be
specified and registered with Khronos.  These extensions can be created
by an Independent Hardware Vendor (IHV) to expose new hardware functionality,
or by a layer writer to expose some internal feature, or by the loader to
improve functional behavior.  Information about various extensions can be
found in the Vulkan Spec, and vulkan.h header file.


##### Instance and Device Extensions

As hinted at in the [Instance Versus Device](#instance-versus-device) section,
there are really two types of extensions:
 * Instance Extensions
 * Device Extensions

An Instance extension is an extension which modifies existing behavior or
implements new behavior on instance-level objects, like a `VkInstance` or
a `VkPhysicalDevice`.  A Device extension is an extension which does the same,
but for any `VkDevice` object, or any dispatchable object that is a child of a
`VkDevice` (`VkQueue` and `VkCommandBuffer` are examples of these).

It is **very** important to know what type of extension you are desiring to
enable as you will enable Instance extensions during `vkCreateInstance` and
Device extensions during `vkCreateDevice`.

The loader discovers and aggregates all
extensions from layers (both explicit and implicit), ICDs and the loader before
reporting them to the application in `vkEnumerateXXXExtensionProperties`
(where XXX is either "Instance" or "Device").
 - Instance extensions are discovered via
`vkEnumerateInstanceExtensionProperties`.
 - Device extensions are be discovered via
`vkEnumerateDeviceExtensionProperties`.

Looking at `vulkan.h`, you'll notice that they are both similar.  For example,
`vkEnumerateInstanceExtensionProperties` prototype looks as follows:

```
   VkResult
   vkEnumerateInstanceExtensionProperties(const char *pLayerName,
                                          uint32_t *pPropertyCount,
                                          VkExtensionProperties *pProperties);
```

The "pLayerName" parameter in these functions is used to select either a single
layer or the Vulkan platform implementation. If "pLayerName" is NULL, extensions
from Vulkan implementation components (including loader, implicit layers, and
ICDs) are enumerated. If "pLayerName" is equal to a discovered layer module name
then only extensions from that layer (which may be implicit or explicit) are
enumerated. Duplicate extensions (e.g. an implicit layer and ICD might report
support for the same extension) are eliminated by the loader. For duplicates,
the ICD version is reported and the layer version is culled.

Also, Extensions *must be enabled* (in `vkCreateInstance` or `vkCreateDevice`)
before the functions associated with the extensions can be used.  If you get an
Extension function using either `vkGetInstanceProcAddr` or
`vkGetDeviceProcAddr`, but fail to enable it, you could experience undefined
behavior.  This should actually be flagged if you run with Validation layers
enabled.


##### WSI Extensions

Khronos approved WSI extensions are available and provide Windows System
Integration support for various execution environments. It is important to
understand that some WSI extensions are valid for all targets, but others are
particular to a given execution environment (and loader). This desktop loader
(currently targeting Windows and Linux) only enables and directly exports those
WSI extensions that are appropriate to the current environment. For the most
part, the selection is done in the loader using compile-time preprocessor flags.
All versions of the desktop loader currently expose at least the following WSI
extension support:
- VK_KHR_surface
- VK_KHR_swapchain
- VK_KHR_display

In addition, each of the following OS targets for the loader support target-
specific extensions:

| Windowing System | Extensions available |
|----------------|--------------------|
| Windows  | VK_KHR_win32_surface |
| Linux (Default) |  VK_KHR_xcb_surface and VK_KHR_xlib_surface |
| Linux (Wayland) | VK_KHR_wayland_surface |
| Linux (Mir)  | VK_KHR_mir_surface |

**NOTE:** Wayland and Mir targets are not fully supported at this time.  Wayland
support is present, but should be considered Beta quality.  Mir support is not
completely implemented at this time.

It is important to understand that while the loader may support the various
entry-points for these extensions, there is a hand-shake required to actually
use them:
* At least one physical device must support the extension(s)
* The application must select such a physical device
* The application must request the extension(s) be enabled while creating the
instance or logical device (This depends on whether or not the given extension
works with an instance or a device).
* The instance and/or logical device creation must succeed.

Only then can you expect to properly use a WSI extension in your Vulkan program.


##### Unknown Extensions

With the ability to expand Vulkan so easily, extensions will be created that the
loader knows nothing about.  If the extension is a device extension, the loader
will pass the unknown entry-point down the device call chain ending with the
appropriate ICD entry-points.  The same thing will happen, if the extension is
an instance extension which takes a physical device paramater as it's first
component.  However, for all other instance extensions the loader will fail to
load it.

*But why doesn't the loader support unknown instance extensions?*
<br/>
Let's look again at the Instance call chain:

![Instance call chain](./images/loader_instance_chain.png)

Notice that for a normal instance function call, the loader has to handle
passing along the function call to the available ICDs.  If the loader has no
idea of the parameters or return value of the instance call, it can't properly
pass information along to the ICDs.  There may be ways to do this, which will be
explored in the future.  However, for now, this loader does not support
instance extensions which don't take a physical device as their first parameter.

Because the device call-chain does not normally pass through the loader
*terminator*, this is not a problem for device extensions.  Additionally,
since a physical device is associated with one ICD, we can use a generic
*terminator* pointing to one ICD.  This is because both of these extensions
terminate directly in the ICD they are associated with.

*Is this a big problem?*
<br/>
No!  Most extension functionality only affects either a physical or logical
device and not an instance.  Thus, the overwhelming majority of extensions
should be supported with direct loader support.

##### Filtering Out Unknown Instance Extension Names
In some cases, an ICD may support instance extensions that the loader does not.
For the above reasons, the loader will filter out the names of these unknown instance
extensions when an application calls `vkEnumerateInstanceExtensionProperties`.
Additionally, this behavior will cause the loader to throw an error during
`vkCreateInstance` if you still attempt to use one of these extensions.  The intent is
to protect applications so that they don't inadvertantly use functionality
which could lead to a crash.  

On the other-hand, if you know you can safely use the extension, you may disable
the filtering by defining the environment variable `VK_LOADER_DISABLE_INST_EXT_FILTER`
and setting the value to a non-zero number.  This will effectively disable the
loader's filtering out of instance extension names.

<br/>
<br/>

## Loader and Layer Interface

In this section we'll discuss how the loader interacts with layers, including:
  * [Layer Discovery](#layer-discovery)
    * [Layer Manifest File Usage](#layer-manifest-file-usage)
    * [Android Layer Discovery](#android-layer-discovery)
    * [Windows Layer Discovery](#windows-layer-discovery)
    * [Linux Layer Discovery](#linux-layer-discovery)
  * [Layer Version Negotiation](#layer-version-negotiation)
  * [Layer Call Chains and Distributed Dispatch](#layer-call-chains-and-distributed-dispatch)
  * [Layer Unknown Physical Device Extensions](#layer-unknown-physical-device-extensions)
  * [Layer Intercept Requirements](#layer-intercept-requirements)
  * [Distributed Dispatching Requirements](#distributed-dispatching-requirements)
  * [Layer Conventions and Rules](#layer-conventions-and-rules)
  * [Layer Dispatch Initialization](#layer-dispatch-initialization)
  * [Example Code for CreateInstance](#example-code-for-createinstance)
  * [Example Code for CreateDevice](#example-code-for-createdevice)
  * [Meta-layers](#meta-layers)
  * [Pre-Instance Functions](#pre-instance-functions)
  * [Special Considerations](#special-considerations)
    * [Associating Private Data with Vulkan Objects Within a Layer](#associating-private-data-with-vulkan-objects-within-a-layer)
      * [Wrapping](#wrapping)
      * [Hash Maps](#hash-maps)
    * [Creating New Dispatchable Objects](#creating-new-dispatchable-objects)
  * [Layer Manifest File Format](#layer-manifest-file-format)
    * [Layer Manifest File Version History](#layer-manifest-file-version-history)
  * [Layer Library Versions](#layer-library-versions)
    * [Layer Library API Version 2](#layer-library-api-version-2)
    * [Layer Library API Version 1](#layer-library-api-version-1)
    * [Layer Library API Version 0](#layer-library-api-version-0)
  

 
#### Layer Discovery

As mentioned in the
[Application Interface section](#implicit-vs-explicit-layers),
layers can be categorized into two categories:
 * Implicit Layers
 * Explicit Layers

The main difference between the two is that Implicit Layers are automatically
enabled, unless overriden, and Explicit Layers must be enabled.  Remember,
Implicit Layers are not present on all Operating Systems (like Android).

On any system, the loader looks in specific areas for information on the
layers that it can load at a user's request.  The process of finding the
available layers on a system is known as Layer Discovery.  During discovery,
the loader determines what layers are available, the layer name, the layer
version, and any extensions supported by the layer.  This information is
provided back to an application through `vkEnumerateInstanceLayerProperties`.

The group of layers available to the loader is known as a layer library.  This
section defines an extensible interface to discover what layers are contained in
the layer library.

This section also specifies the minimal conventions and rules a layer must
follow, especially with regards to interacting with the loader and other layers.

##### Layer Manifest File Usage

On Windows and Linux systems, JSON formatted manifest files are used to store
layer information.  In order to find system-installed layers, the Vulkan loader
will read the JSON files to identify the names and attributes of layers and
their extensions. The use of manifest files allows the loader to avoid loading
any shared library files when the application does not query nor request any
extensions.  The format of [Layer Manifest File](#layer-manifest-file-format)
is detailed below.

The Android loader does not use manifest files.  Instead, the loader queries the
layer properties using special functions known as "introspection" functions.
The intent of these functions is to determine the same required information
gathered from reading the manifest files.  These introspection functions are
not used by the desktop loader but should be present in layers to maintain
consistency.  The specific "introspection" functions are called out in
the [Layer Manifest File Format](#layer-manifest-file-format) table.


##### Android Layer Discovery

On Android, the loader looks for layers to enumerate in the
/data/local/debug/vulkan folder.  An application enabled for debug has the
ability to enumerate and enable any layers in that location.


##### Windows Layer Discovery

In order to find system-installed layers, the Vulkan loader will scan the
values in the following Windows registry keys:

```
   HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\Vulkan\ExplicitLayers
   HKEY_CURRENT_USER\SOFTWARE\Khronos\Vulkan\ExplicitLayers
   HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\Vulkan\ImplicitLayers
   HKEY_CURRENT_USER\SOFTWARE\Khronos\Vulkan\ImplicitLayers
```

For each value in these keys which has DWORD data set to 0, the loader opens
the JSON manifest file specified by the name of the value. Each name must be a
full pathname to the manifest file.

Additionally, the loader will scan through registry keys specific to Display
Adapters and all Software Components associated with these adapters for the
locations of JSON manifest files. These keys are located in device keys
created during driver installation and contain configuration information
for base settings, including Vulkan, OpenGL, and Direct3D ICD location.

The Device Adapter and Software Component key paths should be obtained through the PnP
Configuration Manager API. The `000X` key will be a numbered key, where each
device is assigned a different number.

```
   HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Class\{Adapter GUID}\000X\VulkanExplicitLayers
   HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Class\{Adapter GUID}\000X\VulkanImplicitLayers
   HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Class\{Software Component GUID}\000X\VulkanExplicitLayers
   HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Class\{Software Component GUID}\000X\VulkanImplicitLayers
```

In addition, on 64-bit systems there may be another set of registry values, listed
below. These values record the locations of 32-bit layers on 64-bit operating systems,
in the same way as the Windows-on-Windows functionality.

```
   HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Class\{Adapter GUID}\000X\VulkanExplicitLayersWow
   HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Class\{Adapter GUID}\000X\VulkanImplicitLayersWow
   HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Class\{Software Component GUID}\000X\VulkanExplicitLayersWow
   HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Class\{Software Component GUID}\000X\VulkanImplicitLayersWow
```

If any of the above values exist and is of type `REG_SZ`, the loader will open the JSON
manifest file specified by the key value. Each value must be a full absolute
path to a JSON manifest file. A key value may also be of type `REG_MULTI_SZ`, in
which case the value will be interpreted as a list of paths to JSON manifest files.

In general, applications should install layers into the `SOFTWARE\Khrosos\Vulkan`
paths. The PnP registry locations are intended specifically for layers that are
distrubuted as part of a driver installation. An application installer should not
modify the device-specific registries, while a device driver should not modify
the system wide registries.

The Vulkan loader will open each manifest file that is given
to obtain information about the layer, including the name or pathname of a
shared library (".dll") file.  However, if VK\_LAYER\_PATH is defined, then the
loader will instead look at the paths defined by that variable instead of using
the information provided by these registry keys.  See
[Forcing Layer Source Folders](#forcing-layer-source-folders) for more
information on this.


##### Linux Layer Discovery

On Linux, the Vulkan loader will scan the files in the following Linux
directories:

    /usr/local/etc/vulkan/explicit_layer.d
    /usr/local/etc/vulkan/implicit_layer.d
    /usr/local/share/vulkan/explicit_layer.d
    /usr/local/share/vulkan/implicit_layer.d
    /etc/vulkan/explicit_layer.d
    /etc/vulkan/implicit_layer.d
    /usr/share/vulkan/explicit_layer.d
    /usr/share/vulkan/implicit_layer.d
    $HOME/.local/share/vulkan/explicit_layer.d
    $HOME/.local/share/vulkan/implicit_layer.d

Of course, ther are some things you have to know about the above folders:
 1. The "/usr/local/*" directories can be configured to be other directories at
build time.
 2. $HOME is the current home directory of the application's user id; this path
will be ignored for suid programs.
 3. The "/usr/local/etc/vulkan/\*\_layer.d" and
"/usr/local/share/vulkan/\*\_layer.d" directories are for layers that are
installed from locally-built sources.
 4. The "/usr/share/vulkan/\*\_layer.d" directories are for layers that are
installed from Linux-distribution-provided packages.

As on Windows, if VK\_LAYER\_PATH is defined, then the
loader will instead look at the paths defined by that variable instead of using
the information provided by these default paths.  However, these
environment variables are only used for non-suid programs.  See
[Forcing Layer Source Folders](#forcing-layer-source-folders) for more
information on this.


#### Layer Version Negotiation

Now that a layer has been discovered, an application can choose to load it (or
it is loaded by default if it is an Implicit layer).  When the loader attempts
to load the layer, the first thing it does is attempt to negotiate the version
of the loader to layer interface.  In order to negotiate the loader/layer
interface version, the layer must implement the
`vkNegotiateLoaderLayerInterfaceVersion` function.  The following information is
provided for this interface in include/vulkan/vk_layer.h:

```cpp
  typedef enum VkNegotiateLayerStructType {
      LAYER_NEGOTIATE_INTERFACE_STRUCT = 1,
  } VkNegotiateLayerStructType;

  typedef struct VkNegotiateLayerInterface {
      VkNegotiateLayerStructType sType;
      void *pNext;
      uint32_t loaderLayerInterfaceVersion;
      PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr;
      PFN_vkGetDeviceProcAddr pfnGetDeviceProcAddr;
      PFN_GetPhysicalDeviceProcAddr pfnGetPhysicalDeviceProcAddr;
  } VkNegotiateLayerInterface;

  VkResult vkNegotiateLoaderLayerInterfaceVersion(
                   VkNegotiateLayerInterface *pVersionStruct);
```

You'll notice the `VkNegotiateLayerInterface` structure is similar to other
Vulkan structures.  The "sType" field, in this case takes a new enum defined
just for internal loader/layer interfacing use.  The valid values for "sType"
could grow in the future, but right only havs the one value
"LAYER_NEGOTIATE_INTERFACE_STRUCT".

This function (`vkNegotiateLoaderLayerInterfaceVersion`) should be exported by
the layer so that using "GetProcAddress" on Windows or "dlsym" on Linux, should
return a valid function pointer to it.  Once the loader has grabbed a valid
address to the layers function, the loader will create a variable of type
`VkNegotiateLayerInterface` and initialize it in the following ways:
 1. Set the structure "sType" to "LAYER_NEGOTIATE_INTERFACE_STRUCT"
 2. Set pNext to NULL.
     - This is for future growth
 3. Set "loaderLayerInterfaceVersion" to the current version the loader desires
to set the interface to.
      - The minimum value sent by the loader will be 2 since it is the first
version supporting this function.

The loader will then individually call each layer’s
`vkNegotiateLoaderLayerInterfaceVersion` function with the filled out
“VkNegotiateLayerInterface”. The layer will either accept the loader's version
set in "loaderLayerInterfaceVersion", or modify it to the closest value version
of the interface that the layer can support.  The value should not be higher
than the version requested by the loader.  If the layer can't support at a
minimum the version requested, then the layer should return an error like
"VK_ERROR_INITIALIZATION_FAILED".  If a layer can support some version, then
the layer should do the following:
 1. Adjust the version to the layer's desired version.
 2. The layer should fill in the function pointer values to its internal
functions:
    - "pfnGetInstanceProcAddr" should be set to the layer’s internal
`GetInstanceProcAddr` function.
    - "pfnGetDeviceProcAddr" should be set to the layer’s internal
`GetDeviceProcAddr` function.
    - "pfnGetPhysicalDeviceProcAddr" should be set to the layer’s internal
`GetPhysicalDeviceProcAddr` function.
      - If the layer supports no physical device extensions, it may set the
value to NULL.
      - More on this function later
 3. The layer should return "VK_SUCCESS"

This function **SHOULD NOT CALL DOWN** the layer chain to the next layer.
The loader will work with each layer individually.

If the layer supports the new interface and reports version 2 or greater, then
the loader will use the “fpGetInstanceProcAddr” and “fpGetDeviceProcAddr”
functions from the “VkNegotiateLayerInterface” structure.  Prior to these
changes, the loader would query each of those functions using "GetProcAddress"
on Windows or "dlsym" on Linux.


#### Layer Call Chains and Distributed Dispatch

There are two key architectural features that drive the loader to layer library
interface:
 1. Separate and distinct instance and device call chains
 2. Distributed dispatch.

You can read an overview of dispatch tables and call chains above in the
[Dispatch Tables and Call Chains](#dispatch-tables-and-call-chains) section.

What's important to note here is that a layer can intercept Vulkan
instance functions, device functions or both. For a layer to intercept instance
functions, it must participate in the instance call chain.  For a layer to
intercept device functions, it must participate in the device call chain.

Remember, a layer does not need to intercept all instance or device functions,
instead, it can choose to intercept only a subset of those functions.

Normally, when a layer intercepts a given Vulkan function, it will call down the
instance or device call chain as needed. The loader and all layer libraries that
participate in a call chain cooperate to ensure the correct sequencing of calls
from one entity to the next. This group effort for call chain sequencing is
hereinafter referred to as **distributed dispatch**.

In distributed dispatch each layer is responsible for properly calling the next
entity in the call chain. This means that a dispatch mechanism is required for
all Vulkan functions that a layer intercepts. If a Vulkan function is not
intercepted by a layer, or if a layer chooses to terminate the function by not
calling down the chain, then no dispatch is needed for that particular function.

For example, if the enabled layers intercepted only certain instance functions,
the call chain would look as follows:
![Instance Function Chain](./images/function_instance_chain.png)

Likewise, if the enabled layers intercepted only a few of the device functions,
the call chain could look this way:
![Device Function Chain](./images/function_device_chain.png)

The loader is responsible for dispatching all core and instance extension Vulkan
functions to the first entity in the call chain.


#### Layer Unknown Physical Device Extensions

Originally, if the loader was called with `vkGetInstanceProcAddr`, it would
result in the following behavior:
 1. The loader would check if core function:
    - If it was, it would return the function pointer
 2. The loader would check if known extension function:
    - If it was, it would return the function pointer
 3. If the loader knew nothing about it, it would call down using
`GetInstanceProcAddr`
    - If it returned non-NULL, treat it as an unknown logical device command.
    - This meant setting up a generic trampoline function that takes in a
VkDevice as the first parameter and adjusting the dispatch table to call the
ICD/Layers function after getting the dispatch table from the VkDevice.
 4. If all the above failed, the loader would return NULL to the application.

This caused problems when a layer attempted to expose new physical device
extensions the loader knew nothing about, but an application did.  Because the
loader knew nothing about it, the loader would get to step 3 in the above
process and would treat the function as an unknown logical device command.  The
problem is, this would create a generic VkDevice trampoline function which, on
the first call, would attempt to dereference the VkPhysicalDevice as a VkDevice.
This would lead to a crash or corruption.

In order to identify the extension entry-points specific to physical device
extensions, the following function can be added to a layer:

```cpp
PFN_vkVoidFunction vk_layerGetPhysicalDeviceProcAddr(VkInstance instance,
                                                     const char* pName);
```

This function behaves similar to `vkGetInstanceProcAddr` and
`vkGetDeviceProcAddr` except it should only return values for physical device
extension entry-points.  In this way, it compares "pName" to every physical
device function supported in the layer.

The following rules apply:
  * If it is the name of a physical device function supported by the layer, the
pointer to the layer's corresponding function should be returned.
  * If it is the name of a valid function which is **not** a physical device
function (i.e. an Instance, Device, or other function implemented by the layer),
then the value of NULL should be returned.
    * We don’t call down since we know the command is not a physical device
extension).
  * If the layer has no idea what this function is, it should call down the layer
chain to the next `vk_layerGetPhysicalDeviceProcAddr` call.
    * This can be retrieved in one of two ways:
      * During `vkCreateInstance`, it is passed to a layer in the
chain information passed to a layer in the `VkLayerInstanceCreateInfo`
structure.
        * Use `get_chain_info()` to get the pointer to the
`VkLayerInstanceCreateInfo` structure.  Let's call it chain_info.
        * The address is then under
chain_info->u.pLayerInfo->pfnNextGetPhysicalDeviceProcAddr
        * See
[Example Code for CreateInstance](#example-code-for-createinstance)
      * Using the next layer’s `GetInstanceProcAddr` function to query for
`vk_layerGetPhysicalDeviceProcAddr`.

This support is optional and should not be considered a requirement.  This is
only required if a layer intends to support some functionality not directly
supported by loaders released in the public.  If a layer does implement this
support, it should return the address of its `vk_layerGetPhysicalDeviceProcAddr`
function in the "pfnGetPhysicalDeviceProcAddr" member of the
`VkNegotiateLayerInterface` structure during
[Layer Version Negotiation](#layer-version-negotiation).  Additionally, the
layer should also make sure `vkGetInstanceProcAddr` returns a valid function
pointer to a query of `vk_layerGetPhysicalDeviceProcAddr`.

The new behavior of the loader's `vkGetInstanceProcAddr` with support for the
`vk_layerGetPhysicalDeviceProcAddr` function is as follows:
 1. Check if core function:
    - If it is, return the function pointer
 2. Check if known instance or device extension function:
    - If it is, return the function pointer
 3. Call the layer/ICD `GetPhysicalDeviceProcAddr`
    - If it returns non-NULL, return a trampoline to a generic physical device
function, and setup a generic terminator which will pass it to the proper ICD.
 4. Call down using `GetInstanceProcAddr`
    - If it returns non-NULL, treat it as an unknown logical device command.
This means setting up a generic trampoline function that takes in a VkDevice as
the first parameter and adjusting the dispatch table to call the ICD/Layers
function after getting the dispatch table from the VkDevice. Then, return the
pointer to corresponding trampoline function.
 5. Return NULL

You can see now, that, if the command gets promoted to core later, it will no
longer be setup using `vk_layerGetPhysicalDeviceProcAddr`.  Additionally, if the
loader adds direct support for the extension, it will no longer get to step 3,
because step 2 will return a valid function pointer.  However, the layer should
continue to support the command query via `vk_layerGetPhysicalDeviceProcAddr`,
until at least a Vulkan version bump, because an older loader may still be
attempting to use the commands.


#### Layer Intercept Requirements

  * Layers intercept a Vulkan function by defining a C/C++ function with
signature **identical** to the Vulkan API for that function.
  * A layer **must intercept at least** `vkGetInstanceProcAddr` and
`vkCreateInstance` to participate in the instance call chain.
  * A layer **may also intercept** `vkGetDeviceProcAddr` and `vkCreateDevice`
to participate in the device call chain.
  * For any Vulkan function a layer intercepts which has a non-void return value,
**an appropriate value must be returned** by the layer intercept function.
  * Most functions a layer intercepts **should call down the chain** to the
corresponding Vulkan function in the next entity.
    * The common behavior for a layer is to intercept a call, perform some
behavior, then pass it down to the next entity.
      * If you don't pass the information down, undefined behavior may occur.
      * This is because the function will not be received by layers further down
the chain, or any ICDs.
    * One function that **must never call down the chain** is:
      * `vkNegotiateLoaderLayerInterfaceVersion`
    * Three common functions that **may not call down the chain** are:
      * `vkGetInstanceProcAddr`
      * `vkGetDeviceProcAddr`
      * `vk_layerGetPhysicalDeviceProcAddr`
      * These functions only call down the chain for Vulkan functions that they
do not intercept.
  * Layer intercept functions **may insert extra calls** to Vulkan functions in
addition to the intercept.
    * For example, a layer intercepting `vkQueueSubmit` may want to add a call to
`vkQueueWaitIdle` after calling down the chain for `vkQueueSubmit`.
    * This would result in two calls down the chain: First a call down the
`vkQueueSubmit` chain, followed by a call down the `vkQueueWaitIdle` chain.
    * Any additional calls inserted by a layer must be on the same chain
      * If the function is a device function, only other device functions should
be added.
      * Likewise, if the function is an instance function, only other instance
functions should be added.


#### Distributed Dispatching Requirements

- For each entry-point a layer intercepts, it must keep track of the entry
point residing in the next entity in the chain it will call down into.
  * In other words, the layer must have a list of pointers to functions of the
appropriate type to call into the next entity.
  * This can be implemented in various ways but
for clarity, will be referred to as a dispatch table.
- A layer can use the `VkLayerDispatchTable` structure as a device dispatch
table (see include/vulkan/vk_layer.h).
- A layer can use the `VkLayerInstanceDispatchTable` structure as a instance
dispatch table (see include/vulkan/vk_layer.h).
- A Layer's `vkGetInstanceProcAddr` function uses the next entity's
`vkGetInstanceProcAddr` to call down the chain for unknown (i.e.
non-intercepted) functions.
- A Layer's `vkGetDeviceProcAddr` function uses the next entity's
`vkGetDeviceProcAddr` to call down the chain for unknown (i.e. non-intercepted)
functions.
- A Layer's `vk_layerGetPhysicalDeviceProcAddr` function uses the next entity's
`vk_layerGetPhysicalDeviceProcAddr` to call down the chain for unknown (i.e.
non-intercepted) functions.


#### Layer Conventions and Rules

A layer, when inserted into an otherwise compliant Vulkan implementation, must
still result in a compliant Vulkan implementation.  The intention is for layers
to have a well-defined baseline behavior.  Therefore, it must follow some
conventions and rules defined below.

A layer is always chained with other layers.  It must not make invalid calls
to, or rely on undefined behaviors of, its lower layers.  When it changes the
behavior of a function, it must make sure its upper layers do not make invalid
calls to or rely on undefined behaviors of its lower layers because of the
changed behavior.  For example, when a layer intercepts an object creation
function to wrap the objects created by its lower layers, it must make sure its
lower layers never see the wrapping objects, directly from itself or
indirectly from its upper layers.

When a layer requires host memory, it may ignore the provided allocators.  It
should use memory allocators if the layer is intended to run in a production
environment.  For example, this usually applies to implicit layers that are
always enabled.  That will allow applications to include the layer's memory
usage.

Additional rules include:
  - `vkEnumerateInstanceLayerProperties` **must** enumerate and **only**
enumerate the layer itself.
  - `vkEnumerateInstanceExtensionProperties` **must** handle the case where
`pLayerName` is itself.
    - It **must** return `VK_ERROR_LAYER_NOT_PRESENT` otherwise, including when
`pLayerName` is `NULL`.
  - `vkEnumerateDeviceLayerProperties` **is deprecated and may be omitted**.
    - Using this will result in undefined behavior.
  - `vkEnumerateDeviceExtensionProperties` **must** handle the case where
`pLayerName` is itself.
    - In other cases, it should normally chain to other layers.
  - `vkCreateInstance` **must not** generate an error for unrecognized layer
names and extension names.
    - It may assume the layer names and extension names have been validated.
  - `vkGetInstanceProcAddr` intercepts a Vulkan function by returning a local
entry-point
    - Otherwise it returns the value obtained by calling down the instance call
chain.
  - `vkGetDeviceProcAddr` intercepts a Vulkan function by returning a local
entry-point
    - Otherwise it returns the value obtained by calling down the device call
chain.
    - These additional functions must be intercepted if the layer implements
device-level call chaining:
      - `vkGetDeviceProcAddr`
      - `vkCreateDevice`(only required for any device-level chaining)
         - **NOTE:** older layer libraries may expect that `vkGetInstanceProcAddr`
ignore `instance` when `pName` is `vkCreateDevice`.
  - The specification **requires** `NULL` to be returned from
`vkGetInstanceProcAddr` and `vkGetDeviceProcAddr` for disabled functions.
    - A layer may return `NULL` itself or rely on the following layers to do so.


#### Layer Dispatch Initialization

- A layer initializes its instance dispatch table within its `vkCreateInstance`
function.
- A layer initializes its device dispatch table within its `vkCreateDevice`
function.
- The loader passes a linked list of initialization structures to layers via
the "pNext" field in the `VkInstanceCreateInfo` and `VkDeviceCreateInfo`
structures for `vkCreateInstance` and `VkCreateDevice` respectively.
- The head node in this linked list is of type `VkLayerInstanceCreateInfo` for
instance and VkLayerDeviceCreateInfo for device. See file
`include/vulkan/vk_layer.h` for details.
- A VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO is used by the loader for the
"sType" field in `VkLayerInstanceCreateInfo`.
- A VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO is used by the loader for the
"sType" field in `VkLayerDeviceCreateInfo`.
- The "function" field indicates how the union field "u" should be interpreted
within `VkLayer*CreateInfo`. The loader will set the "function" field to
VK_LAYER_LINK_INFO. This indicates "u" field should be `VkLayerInstanceLink` or
`VkLayerDeviceLink`.
- The `VkLayerInstanceLink` and `VkLayerDeviceLink` structures are the list
nodes.
- The `VkLayerInstanceLink` contains the next entity's `vkGetInstanceProcAddr`
used by a layer.
- The `VkLayerDeviceLink` contains the next entity's `vkGetInstanceProcAddr` and
`vkGetDeviceProcAddr` used by a layer.
- Given the above structures set up by the loader, layer must initialize their
dispatch table as follows:
  - Find the `VkLayerInstanceCreateInfo`/`VkLayerDeviceCreateInfo` structure in
the `VkInstanceCreateInfo`/`VkDeviceCreateInfo` structure.
  - Get the next entity's vkGet*ProcAddr from the "pLayerInfo" field.
  - For CreateInstance get the next entity's `vkCreateInstance` by calling the
"pfnNextGetInstanceProcAddr":
     pfnNextGetInstanceProcAddr(NULL, "vkCreateInstance").
  - For CreateDevice get the next entity's `vkCreateDevice` by calling the
"pfnNextGetInstanceProcAddr":
     pfnNextGetInstanceProcAddr(NULL, "vkCreateDevice").
  - Advanced the linked list to the next node: pLayerInfo = pLayerInfo->pNext.
  - Call down the chain either `vkCreateDevice` or `vkCreateInstance`
  - Initialize your layer dispatch table by calling the next entity's
Get*ProcAddr function once for each Vulkan function needed in your dispatch
table

#### Example Code for CreateInstance

```cpp
VkResult vkCreateInstance(
        const VkInstanceCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator,
        VkInstance *pInstance)
{
   VkLayerInstanceCreateInfo *chain_info =
        get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);

    assert(chain_info->u.pLayerInfo);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr =
        chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkCreateInstance fpCreateInstance =
        (PFN_vkCreateInstance)fpGetInstanceProcAddr(NULL, "vkCreateInstance");
    if (fpCreateInstance == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Advance the link info for the next element of the chain
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    // Continue call down the chain
    VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS)
        return result;

    // Init layer's dispatch table using GetInstanceProcAddr of
    // next layer in the chain.
    instance_dispatch_table = new VkLayerInstanceDispatchTable;
    layer_init_instance_dispatch_table(
        *pInstance, my_data->instance_dispatch_table, fpGetInstanceProcAddr);

    // Other layer initialization
    ...

    return VK_SUCCESS;
}
```

#### Example Code for CreateDevice

```cpp
VkResult 
vkCreateDevice(
        VkPhysicalDevice gpu,
        const VkDeviceCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator,
        VkDevice *pDevice)
{
    VkLayerDeviceCreateInfo *chain_info =
        get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);

    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr =
        chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr =
        chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    PFN_vkCreateDevice fpCreateDevice =
        (PFN_vkCreateDevice)fpGetInstanceProcAddr(NULL, "vkCreateDevice");
    if (fpCreateDevice == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Advance the link info for the next element on the chain
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    VkResult result = fpCreateDevice(gpu, pCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS) {
        return result;
    }

    // initialize layer's dispatch table
    device_dispatch_table = new VkLayerDispatchTable;
    layer_init_device_dispatch_table(
        *pDevice, device_dispatch_table, fpGetDeviceProcAddr);

    // Other layer initialization
    ...

    return VK_SUCCESS;
}
```


#### Meta-layers

Meta-layers are a special kind of layer which is only available through the
desktop loader.  While normal layers are associated with one particular library,
a meta-layer is actually a collection layer which contains an ordered list of
other layers (called component layers).

The most common example of a meta-layer is the
`VK_LAYER_LUNARG_standard_validation` layer which groups all the most common
individual validation layers into a single layer for ease-of-use.

The benefits of a meta-layer are:
 1. You can activate more than one layer using a single layer name by simply
grouping multiple layers in a meta-layer.
 2. You can define the order the loader will activate individual layers within
the meta-layer.
 3. You can easily share your special layer configuration with others.
 4. The loader will automatically collate all instance and device extensions in
a meta-layer's component layers, and report them as the meta-layer's properties
to the application when queried.
 
Restrictions to defining and using a meta-layer are:
 1. A Meta-layer Manifest file **must** be a properly formated that contains one
or more component layers.
 3. All component layers **must be** present on a system for the meta-layer to
be used.
 4. All component layers **must be** at the same Vulkan API major and minor
version for the meta-layer to be used.
 
The ordering of a meta-layer's component layers in the instance or device
call-chain is simple:
  * The first layer listed will be the layer closest to the application.
  * The last layer listed will be the layer closest to the drivers.

Inside the meta-layer Manifest file, each component layer is listed by its
layer name.  This is the "name" tag's value associated with each component layer's
Manifest file under the "layer" or "layers" tag.  This is also the name that
would normally be used when activating a layer during `vkCreateInstance`.

Any duplicate layer names in either the component layer list, or globally among
all enabled layers, will simply be ignored.  Only the first instance of any
layer name will be used.

For example, if you have a layer enabled using the environment variable
`VK_INSTANCE_LAYERS` and have that same layer listed in a meta-layer, then the
environment variable enabled layer will be used and the component layer will
be dropped.  Likewise, if a person were to enable a meta-layer and then
separately enable one of the component layers afterwards, the second
instantiation of the layer name would be ignored.

The
Manifest file formatting necessary to define a meta-layer can be found in the
[Layer Manifest File Format](#layer-manifest-file-format) section.

#### Pre-Instance Functions

Vulkan includes a small number of functions which are called without any dispatchable object.
Most layers do not intercept these functions, as layers are enabled when an instance is created.
However, under certain conditions it is possible for a layer to intercept these functions.

In order to intercept the pre-instance functions, several conditions must be met:
* The layer must be implicit
* The layer manifest version must be 1.1.2 or later
* The layer must export the entry point symbols for each intercepted function
* The layer manifest must specify the name of each intercepted function in a `pre_instance_functions` JSON object

The functions that may be intercepted in this way are:
* `vkEnumerateInstanceExtensionProperties`
* `vkEnumerateInstanceLayerProperties`

Pre-instance functions work differently from all other layer intercept functions.
Other intercept functions have a function prototype identical to that of the function they are intercepting.
They then rely on data that was passed to the layer at instance or device creation so that layers can call down the chain.
Because there is no need to create an instance before calling the pre-instance functions, these functions must use a separate mechanism for constructing the call chain.
This mechanism consists of an extra parameter that will be passed to the layer intercept function when it is called.
This parameter will be a pointer to a struct, defined as follows:

```
typedef struct Vk...Chain
{
    struct {
        VkChainType type;
        uint32_t version;
        uint32_t size;
    } header;
    PFN_vkVoidFunction pfnNextLayer;
    const struct Vk...Chain* pNextLink;
} Vk...Chain;
```

These structs are defined in the `vk_layer.h` file so that it is not necessary to redefine the chain structs in any external code.
The name of each struct is be similar to the name of the function it corresponds to, but the leading "V" is capitalized, and the word "Chain" is added to the end.
For example, the struct for `vkEnumerateInstanceExtensionProperties` is called `VkEnumerateInstanceExtensionPropertiesChain`.
Furthermore, the `pfnNextLayer` struct member is not actually a void function pointer &mdash; its type will be the actual type of each function in the call chain.

Each layer intercept function must have a prototype that is the same as the prototype of the function being intercepted, except that the first parameter must be that function's chain struct (passed as a const pointer).
For example, a function that wishes to intercept `vkEnumerateInstanceExtensionProperties` would have the prototype:

```
VkResult InterceptFunctionName(const VkEnumerateInstanceExtensionProperties* pChain,
    const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties);
```

The name of the function is arbitrary; it can be anything provided that it is given in the layer manifest file (see [Layer Manifest File Format](#layer-manifest-file-format)).
The implementation of each intercept functions is responsible for calling the next item in the call chain, using the chain parameter.
This is done by calling the `pfnNextLayer` member of the chain struct, passing `pNextLink` as the first argument, and passing the remaining function arguments after that.
For example, a simple implementation for `vkEnumerateInstanceExtensionProperties` that does nothing but call down the chain would look like:

```
VkResult InterceptFunctionName(const VkEnumerateInstanceExtensionProperties* pChain,
    const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
{
    return pChain->pfnNextLayer(pChain->pNextLink, pLayerName, pPropertyCount, pProperties);
}
```

When using a C++ compiler, each chain type also defines a function named `CallDown` which can be used to automatically handle the first argument.
Implementing the above function using this method would look like:

```
VkResult InterceptFunctionName(const VkEnumerateInstanceExtensionProperties* pChain,
    const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
{
    return pChain->CallDown(pLayerName, pPropertyCount, pProperties);
}
```

Unlike with other functions in layers, the layer may not save any global data between these function calls.
Because Vulkan does not store any state until an instance has been created, all layer libraries are released at the end of each pre-instance call.
This means that implicit layers can use pre-instance intercepts to modify data that is returned by the functions, but they cannot be used to record that data.

#### Special Considerations


##### Associating Private Data with Vulkan Objects Within a Layer

A layer may want to associate it's own private data with one or more Vulkan
objects.  Two common methods to do this are hash maps and object wrapping. 


###### Wrapping

The loader supports layers wrapping any Vulkan object, including dispatchable
objects.  For functions that return object handles, each layer does not touch
the value passed down the call chain.  This is because lower items may need to
use the original value.  However, when the value is returned from a
lower-level layer (possibly the ICD), the layer saves the handle  and returns
its own handle to the layer above it (possibly the application).  When a layer
receives a Vulkan function using something that it previously returned a handle
for, the layer is required to unwrap the handle and pass along the saved handle
to the layer below it.  This means that the layer **must intercept every Vulkan
function which uses the object in question**, and wrap or unwrap the object, as
appropriate.  This includes adding support for all extensions with functions
using any object the layer wraps.

Layers above the object wrapping layer will see the wrapped object. Layers
which wrap dispatchable objects must ensure that the first field in the wrapping
structure is a pointer to a dispatch table as defined in `vk_layer.h`.
Specifically, an instance wrapped dispatchable object could be as follows:
```
struct my_wrapped_instance_obj_ {
    VkLayerInstanceDispatchTable *disp;
    // whatever data layer wants to add to this object
};
```
A device wrapped dispatchable object could be as follows:
```
struct my_wrapped_instance_obj_ {
    VkLayerDispatchTable *disp;
    // whatever data layer wants to add to this object
};
```

Layers that wrap dispatchable objects must follow the guidelines for creating
new dispatchable objects (below).

<u>Cautions About Wrapping</u>

Layers are generally discouraged from wrapping objects, because of the
potential for incompatibilities with new extensions.  For example, let's say
that a layer wraps `VkImage` objects, and properly wraps and unwraps `VkImage`
object handles for all core functions.  If a new extension is created which has
functions that take `VkImage` objects as parameters, and if the layer does not
support those new functions, an application that uses both the layer and the new
extension will have undefined behavior when those new functions are called (e.g.
the application may crash).  This is because the lower-level layers and ICD
won't receive the handle that they generated.  Instead, they will receive a
handle that is only known by the layer that is wrapping the object.

Because of the potential for incompatibilities with unsupported extensions,
layers that wrap objects must check which extensions are being used by the
application, and take appropriate action if the layer is used with unsupported
extensions (e.g. disable layer functionality, stop wrapping objects, issue a
message to the user).

The reason that the validation layers wrap objects, is to track the proper use
and destruction of each object.  They issue a validation error if used with
unsupported extensions, alerting the user to the potential for undefined
behavior.


###### Hash Maps

Alternatively, a layer may want to use a hash map to associate data with a
given object. The key to the map could be the object. Alternatively, for
dispatchable objects at a given level (eg device or instance) the layer may
want data associated with the `VkDevice` or `VkInstance` objects. Since
there are multiple dispatchable objects for a given `VkInstance` or `VkDevice`,
the `VkDevice` or `VkInstance` object is not a great map key. Instead the layer
should use the dispatch table pointer within the `VkDevice` or `VkInstance`
since that will be unique for a given `VkInstance` or `VkDevice`.


##### Creating New Dispatchable Objects

Layers which create dispatchable objects must take special care. Remember that
loader *trampoline* code normally fills in the dispatch table pointer in the
newly created object. Thus, the layer must fill in the dispatch table pointer if
the loader *trampoline* will not do so.  Common cases where a layer (or ICD) may
create a dispatchable object without loader *trampoline* code is as follows:
- layers that wrap dispatchable objects
- layers which add extensions that create dispatchable objects
- layers which insert extra Vulkan functions in the stream of functions they
intercept from the application
- ICDs which add extensions that create dispatchable objects

The desktop loader provides a callback that can be used for initializing
a dispatchable object.  The callback is passed as an extension structure via the
pNext field in the create info structure when creating an instance
(`VkInstanceCreateInfo`) or device (`VkDeviceCreateInfo`).  The callback
prototype is defined as follows for instance and device callbacks respectively
(see `vk_layer.h`):

```cpp
VKAPI_ATTR VkResult VKAPI_CALL vkSetInstanceLoaderData(VkInstance instance,
                                                       void *object);
VKAPI_ATTR VkResult VKAPI_CALL vkSetDeviceLoaderData(VkDevice device,
                                                     void *object);
```

To obtain these callbacks the layer must search through the list of structures
pointed to by the "pNext" field in the `VkInstanceCreateInfo` and
`VkDeviceCreateInfo` parameters to find any callback structures inserted by the
loader. The salient details are as follows:
- For `VkInstanceCreateInfo` the callback structure pointed to by "pNext" is
`VkLayerInstanceCreateInfo` as defined in `include/vulkan/vk_layer.h`.
- A "sType" field in of VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO within
`VkInstanceCreateInfo` parameter indicates a loader structure.
- Within `VkLayerInstanceCreateInfo`, the "function" field indicates how the
union field "u" should be interpreted.
- A "function" equal to VK_LOADER_DATA_CALLBACK indicates the "u" field will
contain the callback in "pfnSetInstanceLoaderData".
- For `VkDeviceCreateInfo` the callback structure pointed to by "pNext" is
`VkLayerDeviceCreateInfo` as defined in `include/vulkan/vk_layer.h`.
- A "sType" field in of VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO within
`VkDeviceCreateInfo` parameter indicates a loader structure.
- Within `VkLayerDeviceCreateInfo`, the "function" field indicates how the union
field "u" should be interpreted.
- A "function" equal to VK_LOADER_DATA_CALLBACK indicates the "u" field will
contain the callback in "pfnSetDeviceLoaderData".

Alternatively, if an older loader is being used that doesn't provide these
callbacks, the layer may manually initialize the newly created dispatchable
object.  To fill in the dispatch table pointer in newly created dispatchable
object, the layer should copy the dispatch pointer, which is always the first
entry in the structure, from an existing parent object of the same level
(instance versus device).

For example, if there is a newly created `VkCommandBuffer` object, then the
dispatch pointer from the `VkDevice` object, which is the parent of the
`VkCommandBuffer` object, should be copied into the newly created object.


#### Layer Manifest File Format

On Windows and Linux (desktop), the loader uses manifest files to discover
layer libraries and layers.  The desktop loader doesn't directly query the
layer library except during chaining.  This is to reduce the likelihood of
loading a malicious layer into memory.  Instead, details are read from the
Manifest file, which are then provided for applications to determine what
layers should actually be loaded.

The following section discusses the details of the Layer Manifest JSON file
format.  The JSON file itself does not have any requirements for naming.  The
only requirement is that the extension suffix of the file ends with ".json".

Here is an example layer JSON Manifest file with a single layer:

```
{
   "file_format_version" : "1.0.0",
   "layer": {
       "name": "VK_LAYER_LUNARG_overlay",
       "type": "INSTANCE",
       "library_path": "vkOverlayLayer.dll"
       "api_version" : "1.0.5",
       "implementation_version" : "2",
       "description" : "LunarG HUD layer",
       "functions": {
           "vkNegotiateLoaderLayerInterfaceVersion":
               "OverlayLayer_NegotiateLoaderLayerInterfaceVersion"
       },
       "instance_extensions": [
           {
               "name": "VK_EXT_debug_report",
               "spec_version": "1"
           },
           {
               "name": "VK_VENDOR_ext_x",
               "spec_version": "3"
            }
       ],
       "device_extensions": [
           {
               "name": "VK_EXT_debug_marker",
               "spec_version": "1",
               "entrypoints": ["vkCmdDbgMarkerBegin", "vkCmdDbgMarkerEnd"]
           }
       ],
       "enable_environment": {
           "ENABLE_LAYER_OVERLAY_1": "1"
       },
       "disable_environment": {
           "DISABLE_LAYER_OVERLAY_1": ""
       }
   }
}
```

Here's a snippet with the changes required to support multiple layers per
manifest file:
```
{
   "file_format_version" : "1.0.1",
   "layers": [
      {
           "name": "VK_LAYER_layer_name1",
           "type": "INSTANCE",
           ...
      },
      {
           "name": "VK_LAYER_layer_name2",
           "type": "INSTANCE",
           ...
      }
   ]
}
```

Here's an example of a meta-layer manifest file:
```
{
   "file_format_version" : "1.1.1",
   "layer": {
       "name": "VK_LAYER_LUNARG_standard_validation",
       "type": "GLOBAL",
       "api_version" : "1.0.40",
       "implementation_version" : "1",
       "description" : "LunarG Standard Validation Meta-layer",
       "component_layers": [
           "VK_LAYER_GOOGLE_threading",
           "VK_LAYER_LUNARG_parameter_validation",
           "VK_LAYER_LUNARG_object_tracker",
           "VK_LAYER_LUNARG_core_validation",
           "VK_LAYER_GOOGLE_unique_objects"
       ]
   }
}
```
| JSON Node | Description and Notes | Introspection Query |
|:----------------:|--------------------|:----------------:
| "file\_format\_version" | Manifest format major.minor.patch version number. | N/A |
| | Supported versions are: 1.0.0, 1.0.1, 1.1.0, 1.1.1, and 1.1.2. | |
| "layer" | The identifier used to group a single layer's information together. | vkEnumerateInstanceLayerProperties |
| "layers" | The identifier used to group multiple layers' information together.  This requires a minimum Manifest file format version of 1.0.1.| vkEnumerateInstanceLayerProperties |
| "name" | The string used to uniquely identify this layer to applications. | vkEnumerateInstanceLayerProperties |
| "type" | This field indicates the type of layer.  The values can be: GLOBAL, or INSTANCE | vkEnumerate*LayerProperties |
|  | **NOTES:** Prior to deprecation, the "type" node was used to indicate which layer chain(s) to activate the layer upon: instance, device, or both. Distinct instance and device layers are deprecated; there are now just layers. Allowable values for type (both before and after deprecation) are "INSTANCE", "GLOBAL" and, "DEVICE." "DEVICE" layers are skipped over by the loader as if they were not found. |  |
| "library\_path" | The "library\_path" specifies either a filename, a relative pathname, or a full pathname to a layer shared library file.  If "library\_path" specifies a relative pathname, it is relative to the path of the JSON manifest file (e.g. for cases when an application provides a layer that is in the same folder hierarchy as the rest of the application files).  If "library\_path" specifies a filename, the library must live in the system's shared object search path. There are no rules about the name of the layer shared library files other than it should end with the appropriate suffix (".DLL" on Windows, and ".so" on Linux).  **This field must not be present if "component_layers" is defined**  | N/A |
| "api\_version" | The major.minor.patch version number of the Vulkan API that the shared library file for the library was built against. For example: 1.0.33. | vkEnumerateInstanceLayerProperties |
| "implementation_version" | The version of the layer implemented.  If the layer itself has any major changes, this number should change so the loader and/or application can identify it properly. | vkEnumerateInstanceLayerProperties |
| "description" | A high-level description of the layer and it's intended use. | vkEnumerateInstanceLayerProperties |
| "functions" | **OPTIONAL:** This section can be used to identify a different function name for the loader to use in place of standard layer interface functions. The "functions" node is required if the layer is using an alternative name for `vkNegotiateLoaderLayerInterfaceVersion`. | vkGet*ProcAddr |
| "instance\_extensions" | **OPTIONAL:** Contains the list of instance extension names supported by this layer. One "instance\_extensions" node with an array of one or more elements is required if any instance extensions are supported by a layer, otherwise the node is optional. Each element of the array must have the nodes "name" and "spec_version" which correspond to `VkExtensionProperties` "extensionName" and "specVersion" respectively. | vkEnumerateInstanceExtensionProperties |
| "device\_extensions" | **OPTIONAL:** Contains the list of device extension names supported by this layer. One "device_\extensions" node with an array of one or more elements is required if any device extensions are supported by a layer, otherwise the node is optional. Each element of the array must have the nodes "name" and "spec_version" which correspond to `VkExtensionProperties` "extensionName" and "specVersion" respectively. Additionally, each element of the array of device extensions must have the node "entrypoints" if the device extension adds Vulkan API functions, otherwise this node is not required. The "entrypoint" node is an array of the names of all entrypoints added by the supported extension. | vkEnumerateDeviceExtensionProperties |
| "enable\_environment" | **Implicit Layers Only** - **OPTIONAL:** Indicates an environment variable used to enable the Implicit Layer (w/ value of 1).  This environment variable (which should vary with each "version" of the layer) must be set to the given value or else the implicit layer is not loaded. This is for application environments (e.g. Steam) which want to enable a layer(s) only for applications that they launch, and allows for applications run outside of an application environment to not get that implicit layer(s).| N/A |
| "disable\_environment" | **Implicit Layers Only** - **REQUIRED:**Indicates an environment variable used to disable the Implicit Layer (w/ value of 1). In rare cases of an application not working with an implicit layer, the application can set this environment variable (before calling Vulkan functions) in order to "blacklist" the layer. This environment variable (which should vary with each "version" of the layer) must be set (not particularly to any value). If both the "enable_environment" and "disable_environment" variables are set, the implicit layer is disabled. | N/A |
| "component_layers" | **Meta-layers Only** - Indicates the component layer names that are part of a meta-layer.  The names listed must be the "name" identified in each of the component layer's Mainfest file "name" tag (this is the same as the name of the layer that is passed to the `vkCreateInstance` command).  All component layers must be present on the system and found by the loader in order for this meta-layer to be available and activated. **This field must not be present if "library\_path" is defined** | N/A |
| "pre_instance_functions" | **Implicit Layers Only** - **OPTIONAL:** Indicates which functions the layer wishes to intercept, that do not require that an instance has been created. This should be an object where each function to be intercepted is defined as a string entry where the key is the Vulkan function name and the value is the name of the intercept function in the layer's dynamic library. Available in layer manifest versions 1.1.2 and up. See [Pre-Instance Functions](#pre-instance-functions) for more information. | vkEnumerateInstance*Properties |

##### Layer Manifest File Version History

The current highest supported Layer Manifest file format supported is 1.1.2.
Information about each version is detailed in the following sub-sections:

###### Layer Manifest File Version 1.1.2

Version 1.1.2 introduced the ability of layers to intercept function calls that do not have an instance.

###### Layer Manifest File Version 1.1.1

The ability to define custom metalayers was added.
To support metalayers, the "component_layers" section was added, and the requirement for a "library_path" section to be present was removed when the "component_layers" section is present.

###### Layer Manifest File Version 1.1.0

Layer Manifest File Version 1.1.0 is tied to changes exposed by the Loader/Layer
interface version 2.
  1. Renaming "vkGetInstanceProcAddr" in the "functions" section is
     deprecated since the loader no longer needs to query the layer about
     "vkGetInstanceProcAddr" directly.  It is now returned during the layer
     negotiation, so this field will be ignored.
  2. Renaming "vkGetDeviceProcAddr" in the "functions" section is
     deprecated since the loader no longer needs to query the layer about
     "vkGetDeviceProcAddr" directly.  It too is now returned during the layer
     negotiation, so this field will be ignored.
  3. Renaming the "vkNegotiateLoaderLayerInterfaceVersion" function is
     being added to the "functions" section, since this is now the only
     function the loader needs to query using OS-specific calls.
      - NOTE: This is an optional field and, as the two previous fields, only
needed if the layer requires changing the name of the function for some reason.

You do not need to update your layer manifest file if you don't change the
names of any of the listed functions.

###### Layer Manifest File Version 1.0.1

The ability to define multiple layers using the "layers" array was added.  This
JSON array field can be used when defining a single layer or multiple layers.
The "layer" field is still present and valid for a single layer definition.

###### Layer Manifest File Version 1.0.0

The initial version of the layer manifest file specified the basic format and
fields of a layer JSON file.  The fields of the 1.0.0 file format include:
 * "file\_format\_version"
 * "layer"
 * "name"
 * "type"
 * "library\_path"
 * "api\_version"
 * "implementation\_version"
 * "description"
 * "functions"
 * "instance\_extensions"
 * "device\_extensions"
 * "enable\_environment"
 * "disable\_environment"

It was also during this time that the value of "DEVICE" was deprecated from
the "type" field.


#### Layer Library Versions

The current Layer Library interface is at version 2.  The following sections
detail the differences between the various versions.

##### Layer Library API Version 2

Introduced the concept of
[loader and layer interface](#layer-version-negotiation) using the new
`vkNegotiateLoaderLayerInterfaceVersion` function. Additionally, it introduced
the concept of
[Layer Unknown Physical Device Extensions](#layer-unknown-physical-device-
extensions)
and the associated `vk_layerGetPhysicalDeviceProcAddr` function.  Finally, it
changed the manifest file defition to 1.1.0.

##### Layer Library API Version 1

A layer library supporting interface version 1 had the following behavior:
 1. `GetInstanceProcAddr` and `GetDeviceProcAddr` were directly exported
 2. The layer manifest file was able to override the names of the
`GetInstanceProcAddr` and `GetDeviceProcAddr`functions.

##### Layer Library API Version 0

A layer library supporting interface version 0 must define and export these
introspection functions, unrelated to any Vulkan function despite the names,
signatures, and other similarities:

- `vkEnumerateInstanceLayerProperties` enumerates all layers in a layer
library.
  - This function never fails.
  - When a layer library contains only one layer, this function may be an alias
   to the layer's `vkEnumerateInstanceLayerProperties`.
- `vkEnumerateInstanceExtensionProperties` enumerates instance extensions of
   layers in a layer library.
  - "pLayerName" is always a valid layer name.
  - This function never fails.
  - When a layer library contains only one layer, this function may be an alias
   to the layer's `vkEnumerateInstanceExtensionProperties`.
- `vkEnumerateDeviceLayerProperties` enumerates a subset (can be full,
   proper, or empty subset) of layers in a layer library.
  - "physicalDevice" is always `VK_NULL_HANDLE`.
  - This function never fails.
  - If a layer is not enumerated by this function, it will not participate in
   device function interception.
- `vkEnumerateDeviceExtensionProperties` enumerates device extensions of
   layers in a layer library.
  - "physicalDevice" is always `VK_NULL_HANDLE`.
  - "pLayerName" is always a valid layer name.
  - This function never fails.

It must also define and export these functions once for each layer in the
library:

- `<layerName>GetInstanceProcAddr(instance, pName)` behaves identically to a
layer's vkGetInstanceProcAddr except it is exported.

   When a layer library contains only one layer, this function may
   alternatively be named `vkGetInstanceProcAddr`.

- `<layerName>GetDeviceProcAddr`  behaves identically to a layer's
vkGetDeviceProcAddr except it is exported.

   When a layer library contains only one layer, this function may
   alternatively be named `vkGetDeviceProcAddr`.

All layers contained within a library must support `vk_layer.h`.  They do not
need to implement functions that they do not intercept.  They are recommended
not to export any functions.


<br/>
<br/>

## Vulkan Installable Client Driver Interface With the Loader

This section discusses the various requirements for the loader and a Vulkan
ICD to properly hand-shake.

  * [ICD Discovery](#icd-discovery)
    * [Overriding the Default ICD Usage](#overriding-the-default-icd-usage)
    * [ICD Manifest File Usage](#icd-manifest-file-usage)
    * [ICD Discovery on Windows](#icd-discovery-on-windows)
    * [ICD Discovery on Linux](#icd-discovery-on-linux)
    * [Using Pre-Production ICDs on Windows and Linux](#using-pre-production-icds-on-windows-and-linux)
    * [ICD Discovery on Android](#icd-discovery-on-android)
  * [ICD Manifest File Format](#icd-manifest-file-format)
    * [ICD Manifest File Versions](#icd-manifest-file-versions)
      * [ICD Manifest File Version 1.0.0](#icd-manifest-file-version-1.0.0)
  * [ICD Vulkan Entry-Point Discovery](#icd-vulkan-entry-point-discovery)
  * [ICD API Version](#icd-api-version)
  * [ICD Unknown Physical Device Extensions](#icd-unknown-physical-device-extensions)
  * [ICD Dispatchable Object Creation](#icd-dispatchable-object-creation)
  * [Handling KHR Surface Objects in WSI Extensions](#handling-khr-surface-objects-in-wsi-extensions)
  * [Loader and ICD Interface Negotiation](#loader-and-icd-interface-negotiation)
    * [Windows and Linux ICD Negotiation](#windows-and-linux-icd-negotiation)
      * [Version Negotiation Between Loader and ICDs](#version-negotiation-between-loader-and-icds)
        * [Interfacing With Legacy ICDs or Loader](#interfacing-with-legacy-icds-or-loader)
      * [Loader Version 5 Interface Requirements](#loader-version-5-interface-requirements)
      * [Loader Version 4 Interface Requirements](#loader-version-4-interface-requirements)
      * [Loader Version 3 Interface Requirements](#loader-version-3-interface-requirements)
      * [Loader Version 2 Interface Requirements](#loader-version-2-interface-requirements)
      * [Loader Versions 0 and 1 Interface Requirements](#loader-versions-0-and-1-interface-requirements)
    * [Android ICD Negotiation](#android-icd-negotiation)


### ICD Discovery

Vulkan allows multiple drivers each with one or more devices (represented by a
Vulkan `VkPhysicalDevice` object) to be used collectively. The loader is
responsible for discovering available Vulkan ICDs on the system. Given a list
of available ICDs, the loader can enumerate all the physical devices available
for an application and return this information to the application. The process
in which the loader discovers the available Installable Client Drivers (ICDs)
on a system is platform dependent. Windows, Linux and Android ICD discovery
details are listed below.

#### Overriding the Default ICD Usage

There may be times that a developer wishes to force the loader to use a specific ICD.
This could be for many reasons including : using a beta driver, or forcing the loader
to skip a problematic ICD.  In order to support this, the loader can be forced to
look at specific ICDs with the `VK_ICD_FILENAMES` environment variable.  In order
to use the setting, simply set it to a properly delimited list of ICD Manifest
files that you wish to use.  In this case, please provide the global path to these
files to reduce issues.

For example:

##### On Windows

```
set VK_ICD_FILENAMES=/windows/system32/nv-vk64.json
```

This is an example which is using the `VK_ICD_FILENAMES` override on Windows to point
to the Nvidia Vulkan driver's ICD Manifest file.

##### On Linux

```
export VK_ICD_FILENAMES=/home/user/dev/mesa/share/vulkan/icd.d/intel_icd.x86_64.json
```

This is an example which is using the `VK_ICD_FILENAMES` override on Linux to point
to the Intel Mesa driver's ICD Manifest file.


#### ICD Manifest File Usage

As with layers, on Windows and Linux systems, JSON formatted manifest files are
used to store ICD information.  In order to find system-installed drivers, the
Vulkan loader will read the JSON files to identify the names and attributes of
each driver.  One thing you will notice is that ICD Manifest files are much
simpler than the corresponding layer Manifest files.

See the [Current ICD Manifest File Format](#icd-manifest-file-format) section
for more details.


#### ICD Discovery on Windows

In order to find installed ICDs, the loader scans through registry keys specific to Display
Adapters and all Software Components associated with these adapters for the
locations of JSON manifest files. These keys are located in device keys
created during driver installation and contain configuration information
for base settings, including OpenGL and Direct3D ICD location.

The Device Adapter and Software Component key paths should be obtained through the PnP
Configuration Manager API. The `000X` key will be a numbered key, where each
device is assigned a different number.

```
   HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Class\{Adapter GUID}\000X\VulkanDriverName
   HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Class\{SoftwareComponent GUID}\000X\VulkanDriverName
```

In addition, on 64-bit systems there may be another set of registry values, listed
below. These values record the locations of 32-bit layers on 64-bit operating systems,
in the same way as the Windows-on-Windows functionality.

```
   HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Class\{Adapter GUID}\000X\VulkanDriverNameWow
   HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Class\{SoftwareComponent GUID}\000X\VulkanDriverNameWow
```

If any of the above values exist and is of type `REG_SZ`, the loader will open the JSON
manifest file specified by the key value. Each value must be a full absolute
path to a JSON manifest file. The values may also be of type `REG_MULTI_SZ`, in
which case the value will be interpreted as a list of paths to JSON manifest files.

Additionally, the Vulkan loader will scan the values in the following Windows registry key:

```
   HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\Vulkan\Drivers
```

For 32-bit applications on 64-bit Windows, the loader scan's the 32-bit
registry location:

```
   HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers
```

Every ICD in these locations should be given as a DWORD, with value 0, where
the name of the value is the full path to a JSON manifest file. The Vulkan loader
will attempt to open each manifest file to obtain the information about an ICD's
shared library (".dll") file.

For example, let us assume the registry contains the following data:

```
[HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\Vulkan\Drivers\]

"C:\vendor a\vk_vendora.json"=dword:00000000
"C:\windows\system32\vendorb_vk.json"=dword:00000001
"C:\windows\system32\vendorc_icd.json"=dword:00000000
```

In this case, the loader will step through each entry, and check the value.  If
the value is 0, then the loader will attempt to load the file.  In this case,
the loader will open the first and last listings, but not the middle.  This
is because the value of 1 for vendorb_vk.json disables the driver.

The Vulkan loader will open each enabled manifest file found to obtain the name
or pathname of an ICD shared library (".DLL") file.

ICDs should use the registry locations from the PnP Configuration Manager wherever
practical. That location clearly ties the ICD to a given device. The
`SOFTWARE\Khronos\Vulkan\Drivers` location is the older method for locating ICDs,
and is retained for backwards compatibility.

See the [ICD Manifest File Format](#icd-manifest-file-format) section for more
details.


#### ICD Discovery on Linux

In order to find installed ICDs, the Vulkan loader will scan the files
in the following Linux directories:

```
    /usr/local/etc/vulkan/icd.d
    /usr/local/share/vulkan/icd.d
    /etc/vulkan/icd.d
    /usr/share/vulkan/icd.d
    $HOME/.local/share/vulkan/icd.d
```

The "/usr/local/*" directories can be configured to be other directories at
build time.

The typical usage of the directories is indicated in the table below.

| Location  |  Details |
|-------------------|------------------------|
| $HOME/.local/share/vulkan/icd.d | $HOME is the current home directory of the application's user id; this path will be ignored for suid programs |
| "/usr/local/etc/vulkan/icd.d" | Directory for locally built ICDs |
| "/usr/local/share/vulkan/icd.d" | Directory for locally built ICDs |
| "/etc/vulkan/icd.d" | Location of ICDs installed from non-Linux-distribution-provided packages |
| "/usr/share/vulkan/icd.d" | Location of ICDs installed from Linux-distribution-provided packages |

The Vulkan loader will open each manifest file found to obtain the name or
pathname of an ICD shared library (".so") file.

See the [ICD Manifest File Format](#icd-manifest-file-format) section for more
details.

##### Additional Settings For ICD Debugging

If you are seeing issues which may be related to the ICD.  A possible option to debug is to enable the
`LD_BIND_NOW` environment variable.  This forces every dynamic library's symbols to be fully resolved on load.  If
there is a problem with an ICD missing symbols on your system, this will expose it and cause the Vulkan loader
to fail on loading the ICD.  It is recommended that you enable `LD_BIND_NOW` along with `VK_LOADER_DEBUG=warn`
to expose any issues.

#### Using Pre-Production ICDs on Windows and Linux

Independent Hardware Vendor (IHV) pre-production ICDs. In some cases, a
pre-production ICD may be in an installable package. In other cases, a
pre-production ICD may simply be a shared library in the developer's build tree.
In this latter case, we want to allow developers to point to such an ICD without
modifying the system-installed ICD(s) on their system.

This need is met with the use of the "VK\_ICD\_FILENAMES" environment variable,
which will override the mechanism used for finding system-installed ICDs. In
other words, only the ICDs listed in "VK\_ICD\_FILENAMES" will be used.

The "VK\_ICD\_FILENAMES" environment variable is a list of ICD
manifest files, containing the full path to the ICD JSON Manifest file.  This
list is colon-separated on Linux, and semi-colon separated on Windows.

Typically, "VK\_ICD\_FILENAMES" will only contain a full pathname to one info
file for a developer-built ICD. A separator (colon or semi-colon) is only used
if more than one ICD is listed.

**NOTE:** On Linux, this environment variable will be ignored for suid programs.


#### ICD Discovery on Android

The Android loader lives in the system library folder. The location cannot be
changed. The loader will load the driver/ICD via hw\_get\_module with the ID
of "vulkan". **Due to security policies in Android, none of this can be modified
under normal use.**


### ICD Manifest File Format

The following section discusses the details of the ICD Manifest JSON file
format.  The JSON file itself does not have any requirements for naming.  The
only requirement is that the extension suffix of the file ends with ".json".

Here is an example ICD JSON Manifest file:

```
{
   "file_format_version": "1.0.0",
   "ICD": {
      "library_path": "path to ICD library",
      "api_version": "1.0.5"
   }
}
```

| Field Name | Field Value |
|----------------|--------------------|
| "file\_format\_version" | The JSON format major.minor.patch version number of this file.  Currently supported version is 1.0.0. |
| "ICD" | The identifier used to group all ICD information together. |
| "library_path" | The "library\_path" specifies either a filename, a relative pathname, or a full pathname to a layer shared library file.  If "library\_path" specifies a relative pathname, it is relative to the path of the JSON manifest file.  If "library\_path" specifies a filename, the library must live in the system's shared object search path. There are no rules about the name of the ICD shared library files other than it should end with the appropriate suffix (".DLL" on Windows, and ".so" on Linux). | N/A |
| "api_version" | The major.minor.patch version number of the Vulkan API that the shared library files for the ICD was built against. For example: 1.0.33. |

**NOTE:** If the same ICD shared library supports multiple, incompatible
versions of text manifest file format versions, it must have separate
JSON files for each (all of which may point to the same shared library).

##### ICD Manifest File Versions

There has only been one version of the ICD manifest files supported.  This is
version 1.0.0.

###### ICD Manifest File Version 1.0.0

The initial version of the ICD Manifest file specified the basic format and
fields of a layer JSON file.  The fields of the 1.0.0 file format include:
 * "file\_format\_version"
 * "ICD"
 * "library\_path"
 * "api\_version"

 
###  ICD Vulkan Entry-Point Discovery

The Vulkan symbols exported by an ICD must not clash with the loader's exported
Vulkan symbols.  This could be for several reasons.  Because of this, all ICDs
must export the following function that is used for discovery of ICD Vulkan
entry-points.  This entry-point is not a part of the Vulkan API itself, only a
private interface between the loader and ICDs for version 1 and higher
interfaces.

```cpp
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
                                               VkInstance instance,
                                               const char* pName);
```

This function has very similar semantics to `vkGetInstanceProcAddr`.
`vk_icdGetInstanceProcAddr` returns valid function pointers for all the global-
level and instance-level Vulkan functions, and also for `vkGetDeviceProcAddr`.
Global-level functions are those which contain no dispatchable object as the
first parameter, such as `vkCreateInstance` and
`vkEnumerateInstanceExtensionProperties`. The ICD must support querying global-
level entry-points by calling `vk_icdGetInstanceProcAddr` with a NULL
`VkInstance` parameter. Instance-level functions are those that have either
`VkInstance`, or `VkPhysicalDevice` as the first parameter dispatchable object.
Both core entry-points and any instance extension entry-points the ICD supports
should be available via `vk_icdGetInstanceProcAddr`. Future Vulkan instance
extensions may define and use new instance-level dispatchable objects other
than `VkInstance` and `VkPhysicalDevice`, in which case extension entry-points
using these newly defined dispatchable objects must be queryable via
`vk_icdGetInstanceProcAddr`.

All other Vulkan entry-points must either:
 * NOT be exported directly from the ICD library
 * or NOT use the official Vulkan function names if they are exported
 
This requirement is for ICD libraries that include other
functionality (such as OpenGL) and thus could be loaded by the
application prior to when the Vulkan loader library is loaded by the
application.

Beware of interposing by dynamic OS library loaders if the official Vulkan
names are used. On Linux, if official names are used, the ICD library must be
linked with -Bsymbolic.


### ICD API Version
When an application calls `vkCreateInstance`, it can optionally include a
`VkApplicationInfo` struct, which includes an `apiVersion` field. A Vulkan 1.0
ICD was required to return `VK_ERROR_INCOMPATIBLE_DRIVER` if it did not
support the API version that the user passed. Beginning with Vulkan 1.1, ICDs
are not allowed to return this error for any value of `apiVersion`. This
creates a problem when working with multiple ICDs, where one is a 1.0 ICD and
another is newer.

A loader that is newer than 1.0 will always give the version it supports when
the application calls `vkEnumerateInstanceVersion`, regardless of the API
version supported by the ICDs on the system. This means that when the
application calls `vkCreateInstance`, the loader will be forced to pass a copy
of the `VkApplicationInfo` struct where `apiVersion` is 1.0 to any 1.0 drivers
in order to prevent an error. To determine if this must be done, the loader
will perform the following steps:

1. Load the ICD's dynamic library
2. Call the ICD's `vkGetInstanceProcAddr` command to get a pointer to
`vkEnumerateInstanceVersion`
3. If the pointer to `vkEnumerateInstanceVersion` is not `NULL`, it will be
called to get the ICD's supported API version

The ICD will be treated as a 1.0 ICD if any of the following conditions are met:

- The function pointer to `vkEnumerateInstanceVersion` is `NULL`
- The version returned by `vkEnumerateInstanceVersion` is less than 1.1
- `vkEnumerateInstanceVersion` returns anything other than `VK_SUCCESS`

If the ICD only supports Vulkan 1.0, the loader will ensure that any
`VkApplicationInfo` struct that is passed to the ICD will have an `apiVersion`
field set to Vulkan 1.0. Otherwise, the loader will pass the struct to the ICD
without any changes.


### ICD Unknown Physical Device Extensions

Originally, if the loader was called with `vkGetInstanceProcAddr`, it would
result in the following behavior:
 1. The loader would check if core function:
    - If it was, it would return the function pointer
 2. The loader would check if known extension function:
    - If it was, it would return the function pointer
 3. If the loader knew nothing about it, it would call down using
`GetInstanceProcAddr`
    - If it returned non-NULL, treat it as an unknown logical device command.
    - This meant setting up a generic trampoline function that takes in a
VkDevice as the first parameter and adjusting the dispatch table to call the
ICD/Layers function after getting the dispatch table from the VkDevice.
 4. If all the above failed, the loader would return NULL to the application.

This caused problems when an ICD attempted to expose new physical device
extensions the loader knew nothing about, but an application did.  Because the
loader knew nothing about it, the loader would get to step 3 in the above
process and would treat the function as an unknown logical device command.  The
problem is, this would create a generic VkDevice trampoline function which, on
the first call, would attempt to dereference the VkPhysicalDevice as a VkDevice.
This would lead to a crash or corruption.

In order to identify the extension entry-points specific to physical device
extensions, the following function can be added to an ICD:

```cpp
PFN_vkVoidFunction vk_icdGetPhysicalDeviceProcAddr(VkInstance instance,
                                                   const char* pName);
```

This function behaves similar to `vkGetInstanceProcAddr` and
`vkGetDeviceProcAddr` except it should only return values for physical device
extension entry-points.  In this way, it compares "pName" to every physical
device function supported in the ICD.

The following rules apply:
* If it is the name of a physical device function supported by the ICD, the
pointer to the ICD's corresponding function should be returned.
* If it is the name of a valid function which is **not** a physical device
function (i.e. an Instance, Device, or other function implemented by the ICD),
then the value of NULL should be returned.
* If the ICD has no idea what this function is, it should return NULL.

This support is optional and should not be considered a requirement.  This is
only required if an ICD intends to support some functionality not directly
supported by a significant population of loaders in the public.  If an ICD
does implement this support, it should return the address of its
`vk_icdGetPhysicalDeviceProcAddr` function through the `vkGetInstanceProcAddr`
function.

The new behavior of the loader's vkGetInstanceProcAddr with support for the
`vk_icdGetPhysicalDeviceProcAddr` function is as follows:
 1. Check if core function:
    - If it is, return the function pointer
 2. Check if known instance or device extension function:
    - If it is, return the function pointer
 3. Call the layer/ICD `GetPhysicalDeviceProcAddr`
    - If it returns non-NULL, return a trampoline to a generic physical device
function, and setup a generic terminator which will pass it to the proper ICD.
 4. Call down using `GetInstanceProcAddr`
    - If it returns non-NULL, treat it as an unknown logical device command.
This means setting up a generic trampoline function that takes in a VkDevice as
the first parameter and adjusting the dispatch table to call the ICD/Layers
function after getting the dispatch table from the VkDevice. Then, return the
pointer to corresponding trampoline function.
 5. Return NULL

You can see now, that, if the command gets promoted to core later, it will no
longer be setup using `vk_icdGetPhysicalDeviceProcAddr`.  Additionally, if the
loader adds direct support for the extension, it will no longer get to step 3,
because step 2 will return a valid function pointer.  However, the ICD should
continue to support the command query via `vk_icdGetPhysicalDeviceProcAddr`,
until at least a Vulkan version bump, because an older loader may still be
attempting to use the commands.


### ICD Dispatchable Object Creation

As previously covered, the loader requires dispatch tables to be accessible
within Vulkan dispatchable objects, such as: `VkInstance`, `VkPhysicalDevice`,
`VkDevice`, `VkQueue`, and `VkCommandBuffer`. The specific requirements on all
dispatchable objects created by ICDs are as follows:

- All dispatchable objects created by an ICD can be cast to void \*\*
- The loader will replace the first entry with a pointer to the dispatch table
  which is owned by the loader. This implies three things for ICD drivers
  1. The ICD must return a pointer for the opaque dispatchable object handle
  2. This pointer points to a regular C structure with the first entry being a
   pointer.
   * **NOTE:** For any C\++ ICD's that implement VK objects directly as C\++
classes.
     * The C\++ compiler may put a vtable at offset zero if your class is non-
POD due to the use of a virtual function.
     * In this case use a regular C structure (see below).
  3. The loader checks for a magic value (ICD\_LOADER\_MAGIC) in all the created
   dispatchable objects, as follows (see `include/vulkan/vk_icd.h`):

```cpp
#include "vk_icd.h"

union _VK_LOADER_DATA {
    uintptr loadermagic;
    void *loaderData;
} VK_LOADER_DATA;

vkObj alloc_icd_obj()
{
    vkObj *newObj = alloc_obj();
    ...
    // Initialize pointer to loader's dispatch table with ICD_LOADER_MAGIC

    set_loader_magic_value(newObj);
    ...
    return newObj;
}
```
 

### Handling KHR Surface Objects in WSI Extensions

Normally, ICDs handle object creation and destruction for various Vulkan
objects. The WSI surface extensions for Linux and Windows
("VK\_KHR\_win32\_surface", "VK\_KHR\_xcb\_surface", "VK\_KHR\_xlib\_surface",
"VK\_KHR\_mir\_surface", "VK\_KHR\_wayland\_surface", and "VK\_KHR\_surface")
are handled differently.  For these extensions, the `VkSurfaceKHR` object
creation and destruction may be handled by either the loader, or an ICD.

If the loader handles the management of the `VkSurfaceKHR` objects:
 1. The loader will handle the calls to `vkCreateXXXSurfaceKHR` and
`vkDestroySurfaceKHR`
    functions without involving the ICDs.
    * Where XXX stands for the Windowing System name:
      * Mir
      * Wayland
      * Xcb
      * Xlib
      * Windows
      * Android
 2. The loader creates a `VkIcdSurfaceXXX` object for the corresponding
`vkCreateXXXSurfaceKHR` call.
    * The `VkIcdSurfaceXXX` structures are defined in `include/vulkan/vk_icd.h`.
 3. ICDs can cast any `VkSurfaceKHR` object to a pointer to the appropriate
    `VkIcdSurfaceXXX` structure.
 4. The first field of all the `VkIcdSurfaceXXX` structures is a
`VkIcdSurfaceBase` enumerant that indicates whether the
    surface object is Win32, Xcb, Xlib, Mir, or Wayland.

The ICD may choose to handle `VkSurfaceKHR` object creation instead.  If an ICD
desires to handle creating and destroying it must do the following:
 1. Support version 3 or newer of the loader/ICD interface.
 2. Export and handle all functions that take in a `VkSurfaceKHR` object,
including:
     * `vkCreateXXXSurfaceKHR`
     * `vkGetPhysicalDeviceSurfaceSupportKHR`
     * `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`
     * `vkGetPhysicalDeviceSurfaceFormatsKHR`
     * `vkGetPhysicalDeviceSurfacePresentModesKHR`
     * `vkCreateSwapchainKHR`
     * `vkDestroySurfaceKHR`

Because the `VkSurfaceKHR` object is an instance-level object, one object can be
associated with multiple ICDs.  Therefore, when the loader receives the
`vkCreateXXXSurfaceKHR` call, it still creates an internal `VkSurfaceIcdXXX`
object.  This object acts as a container for each ICD's version of the
`VkSurfaceKHR` object.  If an ICD does not support the creation of its own
`VkSurfaceKHR` object, the loader's container stores a NULL for that ICD.  On
the otherhand, if the ICD does support `VkSurfaceKHR` creation, the loader will
make the appropriate `vkCreateXXXSurfaceKHR` call to the ICD, and store the
returned pointer in it's container object.  The loader then returns the
`VkSurfaceIcdXXX` as a `VkSurfaceKHR` object back up the call chain.  Finally,
when the loader receives the `vkDestroySurfaceKHR` call, it subsequently calls
`vkDestroySurfaceKHR` for each ICD who's internal `VkSurfaceKHR` object is not
NULL.  Then the loader destroys the container object before returning.


### Loader and ICD Interface Negotiation

Generally, for functions issued by an application, the loader can be
viewed as a pass through. That is, the loader generally doesn't modify the
functions or their parameters, but simply calls the ICDs entry-point for that
function. There are specific additional interface requirements an ICD needs to
comply with that are not part of any requirements from the Vulkan specification.
These addtional requirements are versioned to allow flexibility in the future.


#### Windows and Linux ICD Negotiation


##### Version Negotiation Between Loader and ICDs

All ICDs (supporting interface version 2 or higher) must export the following
function that is used for determination of the interface version that will be
used.  This entry-point is not a part of the Vulkan API itself, only a private
interface between the loader and ICDs.

```cpp
   VKAPI_ATTR VkResult VKAPI_CALL
       vk_icdNegotiateLoaderICDInterfaceVersion(
           uint32_t* pSupportedVersion);
```

This function allows the loader and ICD to agree on an interface version to use.
The "pSupportedVersion" parameter is both an input and output parameter.
"pSupportedVersion" is filled in by the loader with the desired latest interface
version supported by the loader (typically the latest). The ICD receives this
and returns back the version it desires in the same field.  Because it is
setting up the interface version between the loader and ICD, this should be
the first call made by a loader to the ICD (even prior to any calls to
`vk_icdGetInstanceProcAddr`).

If the ICD receiving the call no longer supports the interface version provided
by the loader (due to deprecation), then it should report
VK_ERROR_INCOMPATIBLE_DRIVER error.  Otherwise it sets the value pointed by
"pSupportedVersion" to the latest interface version supported by both the ICD
and the loader and returns VK_SUCCESS.

The ICD should report VK_SUCCESS in case the loader provided interface version
is newer than that supported by the ICD, as it's the loader's responsibility to
determine whether it can support the older interface version supported by the
ICD.  The ICD should also report VK_SUCCESS in the case its interface version
is greater than the loader's, but return the loader's version. Thus, upon
return of VK_SUCCESS the "pSupportedVersion" will contain the desired interface
version to be used by the ICD.

If the loader receives an interface version from the ICD that the loader no
longer supports (due to deprecation), or it receives a
VK_ERROR_INCOMPATIBLE_DRIVER error instead of VK_SUCCESS, then the loader will
treat the ICD as incompatible and will not load it for use.  In this case, the
application will not see the ICDs `vkPhysicalDevice` during enumeration.

###### Interfacing With Legacy ICDs or Loader

If a loader sees that an ICD does not export the
`vk_icdNegotiateLoaderICDInterfaceVersion` function, then the loader assumes the
corresponding ICD only supports either interface version 0 or 1.

From the other side of the interface, if an ICD sees a call to
`vk_icdGetInstanceProcAddr` before a call to
`vk_icdNegotiateLoaderICDInterfaceVersion`, then it knows that loader making the calls
is a legacy loader supporting version 0 or 1.  If the loader calls
`vk_icdGetInstanceProcAddr` first, it supports at least version 1.  Otherwise,
the loader only supports version 0.


##### Loader Version 5 Interface Requirements

Version 5 of the loader/ICD interface has no changes to the actual interface.
If the loader requests interface version 5 or greater, it is simply
an indication to ICDs that the loader is now evaluating if the API Version info
passed into vkCreateInstance is a valid version for the loader.  If it is not,
the loader will catch this during vkCreateInstance and fail with a
VK_ERROR_INCOMPATIBLE_DRIVER error.

On the other hand, if version 5 or newer is not requested by the loader, then it
indicates to the ICD that the loader is ignorant of the API version being
requested.  Because of this, it falls on the ICD to validate that the API
Version is not greater than major = 1 and minor = 0.  If it is, then the ICD
should automatically fail with a VK_ERROR_INCOMPATIBLE_DRIVER error since the
loader is a 1.0 loader, and is unaware of the version.

Here is a table of the expected behaviors:

| Loader Supports I/f Version  |  ICD Supports I/f Version  |    Result        |
| :---: |:---:|------------------------|
|           <= 4               |           <= 4             | ICD must fail with `VK_ERROR_INCOMPATIBLE_DRIVER` for all vkCreateInstance calls with apiVersion set to > Vulkan 1.0 because both the loader and ICD support interface version <= 4. Otherwise, the ICD should behave as normal. |
|           <= 4               |           >= 5             | ICD must fail with `VK_ERROR_INCOMPATIBLE_DRIVER` for all vkCreateInstance calls with apiVersion set to > Vulkan 1.0 because the loader is still at interface version <= 4. Otherwise, the ICD should behave as normal.  |
|           >= 5               |           <= 4             | Loader will fail with `VK_ERROR_INCOMPATIBLE_DRIVER` if it can't handle the apiVersion.  ICD may pass for all apiVersions, but since it's interface is <= 4, it is best if it assumes it needs to do the work of rejecting anything > Vulkan 1.0 and fail with `VK_ERROR_INCOMPATIBLE_DRIVER`. Otherwise, the ICD should behave as normal.  |
|           >= 5               |           >= 5             | Loader will fail with `VK_ERROR_INCOMPATIBLE_DRIVER` if it can't handle the apiVersion, and ICDs should fail with `VK_ERROR_INCOMPATIBLE_DRIVER` **only if** they can not support the specified apiVersion. Otherwise, the ICD should behave as normal.  |

##### Loader Version 4 Interface Requirements

The major change to version 4 of the loader/ICD interface is the support of
[Unknown Physical Device Extensions](#icd-unknown-physical-device-
extensions] using the `vk_icdGetPhysicalDeviceProcAddr` function.  This
function is purely optional.  However, if an ICD supports a Physical Device
extension, it must provide a `vk_icdGetPhysicalDeviceProcAddr` function.
Otherwise, the loader will continue to treat any unknown functions as VkDevice
functions and cause invalid behavior.


##### Loader Version 3 Interface Requirements

The primary change that occurred in version 3 of the loader/ICD interface was to
allow an ICD to handle creation/destruction of their own KHR_surfaces.  Up until
this point, the loader created a surface object that was used by all ICDs.
However, some ICDs may want to provide their own surface handles.  If an ICD
chooses to enable this support, it must export support for version 3 of the
loader/ICD interface, as well as any Vulkan function that uses a KHR_surface
handle, such as:
- `vkCreateXXXSurfaceKHR` (where XXX is the platform specific identifier [i.e.
`vkCreateWin32SurfaceKHR` for Windows])
- `vkDestroySurfaceKHR`
- `vkCreateSwapchainKHR`
- `vkGetPhysicalDeviceSurfaceSupportKHR`
- `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`
- `vkGetPhysicalDeviceSurfaceFormatsKHR`
- `vkGetPhysicalDeviceSurfacePresentModesKHR`

An ICD can still choose to not take advantage of this functionality by simply
not exposing the above the `vkCreateXXXSurfaceKHR` and `vkDestroySurfaceKHR`
functions.


##### Loader Version 2 Interface Requirements

Version 2 interface has requirements in three areas:
 1. ICD Vulkan entry-point discovery,
 2. `KHR_surface` related requirements in the WSI extensions,
 3. Vulkan dispatchable object creation requirements.

##### Loader Versions 0 and 1 Interface Requirements

Version 0 and 1 interfaces do not support version negotiation via
`vk_icdNegotiateLoaderICDInterfaceVersion`.  ICDs can distinguish version 0 and
version 1 interfaces as follows: if the loader calls `vk_icdGetInstanceProcAddr`
first it supports version 1; otherwise the loader only supports version 0.

Version 0 interface does not support `vk_icdGetInstanceProcAddr`.  Version 0
interface requirements for obtaining ICD Vulkan entry-points are as follows:

- The function `vkGetInstanceProcAddr` **must be exported** in the ICD library
and returns valid function pointers for all the Vulkan API entry-points.
- `vkCreateInstance` **must be exported** by the ICD library.
- `vkEnumerateInstanceExtensionProperties` **must be exported** by the ICD
library.

Additional Notes:

- The loader will filter out extensions requested in `vkCreateInstance` and
`vkCreateDevice` before calling into the ICD; Filtering will be of extensions
advertised by entities (e.g. layers) different from the ICD in question.
- The loader will not call the ICD for `vkEnumerate\*LayerProperties`() as layer
properties are obtained from the layer libraries and layer JSON files.
- If an ICD library author wants to implement a layer, it can do so by having
the appropriate layer JSON manifest file refer to the ICD library file.
- The loader will not call the ICD for
  `vkEnumerate\*ExtensionProperties` if "pLayerName" is not equal to `NULL`.
- ICDs creating new dispatchable objects via device extensions need to
initialize the created dispatchable object.  The loader has generic *trampoline*
code for unknown device extensions.  This generic *trampoline* code doesn't
initialize the dispatch table within the newly created object.  See the
[Creating New Dispatchable Objects](#creating-new-dispatchable-objects) section
for more information on how to initialize created dispatchable objects for
extensions non known by the loader.


#### Android ICD Negotiation

The Android loader uses the same protocol for initializing the dispatch
table as described above. The only difference is that the Android
loader queries layer and extension information directly from the
respective libraries and does not use the json manifest files used
by the Windows and Linux loaders.

## Table of Debug Environment Variables

The following are all the Debug Environment Variables available for use with the
Loader.  These are referenced throughout the text, but collected here for ease
of discovery.

| Environment Variable              | Behavior |  Example Format  |
|:---:|---------------------|----------------------|
| VK_ICD_FILENAMES                  | Force the loader to use the specific ICD JSON files.  The value should contain a list of delimited full path listings to ICD JSON Manifest files.  **NOTE:** If you fail to use the global path to a JSON file, you may encounter issues.  |  `export VK_ICD_FILENAMES=<folder_a>\intel.json:<folder_b>\amd.json`<br/><br/>`set VK_ICD_FILENAMES=<folder_a>\nvidia.json;<folder_b>\mesa.json` |
| VK_INSTANCE_LAYERS                | Force the loader to add the given layers to the list of Enabled layers normally passed into `vkCreateInstance`.  These layers are added first, and the loader will remove any duplicate layers that appear in both this list as well as that passed into `ppEnabledLayerNames`. | `export VK_INSTANCE_LAYERS=<layer_a>:<layer_b>`<br/><br/>`set VK_INSTANCE_LAYERS=<layer_a>;<layer_b>` |
| VK_LAYER_PATH                     | Override the loader's standard Layer library search folders and use the provided delimited folders to search for layer Manifest files. | `export VK_LAYER_PATH=<path_a>:<path_b>`<br/><br/>`set VK_LAYER_PATH=<path_a>;<pathb>` |
| VK_LOADER_DISABLE_INST_EXT_FILTER | Disable the filtering out of instance extensions that the loader doesn't know about.  This will allow applications to enable instance extensions exposed by ICDs but that the loader has no support for.  **NOTE:** This may cause the loader or applciation to crash. |  `export VK_LOADER_DISABLE_INST_EXT_FILTER=1`<br/><br/>`set VK_LOADER_DISABLE_INST_EXT_FILTER=1` |
| VK_LOADER_DEBUG                   | Enable loader debug messages.  Options are:<br/>- error (only errors)<br/>- warn (warnings and errors)<br/>- info (info, warning, and errors)<br/> - debug (debug + all before) <br/> -all (report out all messages) | `export VK_LOADER_DEBUG=all`<br/><br/>`set VK_LOADER_DEBUG=warn` |
 
## Glossary of Terms

| Field Name | Field Value |
|:---:|--------------------|
| Android Loader | The loader designed to work primarily for the Android OS.  This is generated from a different code-base than the desktop loader.  But, in all important aspects, should be functionally equivalent. |
| Desktop Loader | The loader designed to work on both Windows and Linux.  This is generated from a different [code-base](#https://github.com/KhronosGroup/Vulkan-LoaderAndValidationLayers) than the Android loader.  But in all important aspects, should be functionally equivalent. |
| Core Function | A function that is already part of the Vulkan core specification and not an extension.  For example, vkCreateDevice(). |
| Device Call Chain | The call chain of functions followed for device functions.  This call chain for a device function is usually as follows: first the application calls into a loader trampoline, then the loader trampoline calls enabled layers, the final layer calls into the ICD specific to the device.  See the [Dispatch Tables and Call Chains](#dispatch-tables-and-call-chains) section for more information |
| Device Function | A Device function is any Vulkan function which takes a `VkDevice`, `VkQueue`, `VkCommandBuffer`, or any child of these, as its first parameter.  Some Vulkan Device functions are: `vkQueueSubmit`, `vkBeginCommandBuffer`, `vkCreateEvent`.  See the [Instance Versus Device](#instance-versus-device) section for more information. |
| Discovery | The process of the loader searching for ICD and Layer files to setup the internal list of Vulkan objects available.  On Windows/Linux, the discovery process typically focuses on searching for Manifest files.  While on Android, the process focuses on searching for library files. |
| Dispatch Table | An array of function pointers (including core and possibly extension functions) used to step to the next entity in a call chain.  The entity could be the loader, a layer or an ICD.  See [Dispatch Tables and Call Chains](#dispatch-tables-and-call-chains) for more information.  |
| Extension | A concept of Vulkan used to expand the core Vulkan functionality.  Extensions may be IHV-specific, platform-specific, or more broadly available.  You should always query if an extension exists, and enable it during `vkCreateInstance` (if it is an instance extension) or during `vkCreateDevice` (if it is a device extension). |
| ICD | Acronym for Installable Client Driver.  These are drivers that are provided by IHVs to interact with the hardware they provide.  See [Installable Client Drivers](#installable-client-drivers) section for more information.
| IHV | Acronym for an Independent Hardware Vendor.  Typically the company that built the underlying hardware technology you are trying to use.  A typical examples for a Graphics IHV are: AMD, ARM, Imagination, Intel, Nvidia, Qualcomm, etc. |
| Instance Call Chain | The call chain of functions followed for instance functions.  This call chain for an instance function is usually as follows: first the application calls into a loader trampoline, then the loader trampoline calls enabled layers, the final layer calls a loader terminator, and the loader terminator calls all available ICDs.  See the [Dispatch Tables and Call Chains](#dispatch-tables-and-call-chains) section for more information |
| Instance Function | An Instance function is any Vulkan function which takes as its first parameter either a `VkInstance` or a `VkPhysicalDevice` or nothing at all.  Some Vulkan Instance functions are: `vkEnumerateInstanceExtensionProperties`, `vkEnumeratePhysicalDevices`, `vkCreateInstance`, `vkDestroyInstance`.  See the [Instance Versus Device](#instance-versus-device) section for more information. |
| Layer | Layers are optional components that augment the Vulkan system.  They can intercept, evaluate, and modify existing Vulkan functions on their way from the application down to the hardware.  See the [Layers](#layers) section for more information. |
| Loader | The middle-ware program which acts as the mediator between Vulkan applications, Vulkan layers and Vulkan drivers.  See [The Loader](#the loader) section for more information. |
| Manifest Files | Data files in JSON format used by the desktop loader.  These files contain specific information for either a [Layer](#layer-manifest-file-format) or an [ICD](#icd-manifest-file-format).
| Terminator Function | The last function in the instance call chain above the ICDs and owned by the loader.  This function is required in the instance call chain because all instance functionality must be communicated to all ICDs capable of receiving the call.  See [Dispatch Tables and Call Chains](#dispatch-tables-and-call-chains) for more information. |
| Trampoline Function | The first function in an instance or device call chain owned by the loader which handles the setup and proper call chain walk using the appropriate dispatch table.  On device functions (in the device call chain) this function can actually be skipped.  See [Dispatch Tables and Call Chains](#dispatch-tables-and-call-chains) for more information. |
| WSI Extension | Acronym for Windowing System Integration.  A Vulkan extension targeting a particular Windowing and designed to interface between the Windowing system and Vulkan. See [WSI Extensions](#wsi-extensions) for more information. |
