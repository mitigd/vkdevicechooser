#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <vk_layer_dispatch_table.h>

#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#undef VK_LAYER_EXPORT
#if defined(_WIN32)
#define VK_LAYER_EXPORT extern "C" __declspec(dllexport)
#else
#define VK_LAYER_EXPORT extern "C"
#endif

static std::map<void*, VkLayerInstanceDispatchTable> gInstanceDispatch;
static std::map<void*, VkLayerDispatchTable> gDeviceDispatch;

static constexpr char kEnvVariable[] = "VULKAN_DEVICE_INDEX";

template <typename DispatchableType>
inline void* GetKey(DispatchableType object)
{
    return *(void**)object;
}

template <typename DispatchableType>
inline VkLayerInstanceDispatchTable& GetInstanceDispatch(DispatchableType object)
{
    return gInstanceDispatch[GetKey(object)];
}

template <typename DispatchableType>
inline VkLayerDispatchTable& GetDeviceDispatch(DispatchableType object)
{
    return gDeviceDispatch[GetKey(object)];
}

// Helper function to safely get environment variable on Windows
static const char* GetEnv(const char* varName) {
#if defined(_WIN32)
    char* pValue;
    size_t len;
    errno_t err = _dupenv_s(&pValue, &len, varName);
    if (err || pValue == nullptr) {
        return nullptr;
    }
    // Note: This leaks memory (pValue) because the original code's getenv doesn't require freeing.
    // For this specific use case where the app runs and closes, it's a minor issue.
    // A more robust solution would manage this memory.
    return pValue;
#else
    return getenv(varName);
#endif
}


static VkResult ChooseDevice(VkInstance                          instance,
    const VkLayerInstanceDispatchTable& dispatch,
    const char* const                   env,
    VkPhysicalDevice& outDevice)
{
    std::vector<VkPhysicalDevice> devices;
    uint32_t count = 0;

    VkResult result = dispatch.EnumeratePhysicalDevices(instance, &count, nullptr);

    if (result != VK_SUCCESS)
    {
        return result;
    }
    else if (count == 0)
    {
        outDevice = VK_NULL_HANDLE;
        return VK_SUCCESS;
    }

    devices.resize(count);

    result = dispatch.EnumeratePhysicalDevices(instance, &count, &devices[0]);

    if (result != VK_SUCCESS)
    {
        return result;
    }

    int deviceIndex = atoi(env);

    if (deviceIndex < 0 || static_cast<uint32_t>(deviceIndex) >= count)
    {
        fprintf(stderr, "[DeviceChooserLayer] Warning: Device index '%d' is out of bounds (found %u devices). Falling back to device 0.\n", deviceIndex, count);
        deviceIndex = 0;
    }
    else
    {
        printf("[DeviceChooserLayer] Using Vulkan device index %d\n", deviceIndex);
    }

    outDevice = devices[deviceIndex];
    return VK_SUCCESS;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
DeviceChooserLayer_EnumeratePhysicalDevices(VkInstance        instance,
    uint32_t* pPhysicalDeviceCount,
    VkPhysicalDevice* pPhysicalDevices)
{
    const VkLayerInstanceDispatchTable& dispatch = GetInstanceDispatch(instance);

    const char* const env = GetEnv(kEnvVariable);
    if (!env)
    {
        return dispatch.EnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
    }

    VkPhysicalDevice device;
    VkResult result = ChooseDevice(instance, dispatch, env, device);

    if (result != VK_SUCCESS)
    {
        return result;
    }
    else if (device == VK_NULL_HANDLE)
    {
        *pPhysicalDeviceCount = 0;
    }
    else if (pPhysicalDevices == nullptr)
    {
        *pPhysicalDeviceCount = 1;
    }
    else if (*pPhysicalDeviceCount > 0)
    {
        *pPhysicalDevices = device;
        *pPhysicalDeviceCount = 1;
    }

    return VK_SUCCESS;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
DeviceChooserLayer_EnumeratePhysicalDeviceGroups(VkInstance                       instance,
    uint32_t* pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroups)
{
    const VkLayerInstanceDispatchTable& dispatch = GetInstanceDispatch(instance);

    const char* const env = GetEnv(kEnvVariable);
    if (!env)
    {
        return dispatch.EnumeratePhysicalDeviceGroups(instance, pPhysicalDeviceGroupCount, pPhysicalDeviceGroups);
    }

    /* Just return a single device group containing the requested device. */
    VkPhysicalDevice device;
    VkResult result = ChooseDevice(instance, dispatch, env, device);

    if (result != VK_SUCCESS)
    {
        return result;
    }
    else if (device == VK_NULL_HANDLE)
    {
        *pPhysicalDeviceGroupCount = 0;
    }
    else if (pPhysicalDeviceGroups == nullptr)
    {
        *pPhysicalDeviceGroupCount = 1;
    }
    else if (*pPhysicalDeviceGroupCount > 0)
    {
        *pPhysicalDeviceGroupCount = 1;

        pPhysicalDeviceGroups[0].physicalDeviceCount = 1;
        pPhysicalDeviceGroups[0].physicalDevices[0] = device;
        pPhysicalDeviceGroups[0].subsetAllocation = VK_FALSE;
    }

    return VK_SUCCESS;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
DeviceChooserLayer_EnumeratePhysicalDeviceGroupsKHR(VkInstance                          instance,
    uint32_t* pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupPropertiesKHR* pPhysicalDeviceGroups)
{
    // This function has the same signature as the non-KHR version
    return DeviceChooserLayer_EnumeratePhysicalDeviceGroups(instance, pPhysicalDeviceGroupCount, pPhysicalDeviceGroups);
}


VK_LAYER_EXPORT VkResult VKAPI_CALL
DeviceChooserLayer_CreateInstance(const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance)
{
    VkLayerInstanceCreateInfo* layerCreateInfo = (VkLayerInstanceCreateInfo*)pCreateInfo->pNext;

    while (layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
        layerCreateInfo->function != VK_LAYER_LINK_INFO))
    {
        layerCreateInfo = (VkLayerInstanceCreateInfo*)layerCreateInfo->pNext;
    }

    if (layerCreateInfo == nullptr)
    {
        // No layer info found, fail
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr gpa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    // Move chain on for next layer
    layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

    PFN_vkCreateInstance createFunc = (PFN_vkCreateInstance)gpa(VK_NULL_HANDLE, "vkCreateInstance");

    VkResult ret = createFunc(pCreateInfo, pAllocator, pInstance);
    if (ret != VK_SUCCESS)
    {
        return ret;
    }

    // Initialize the dispatch table for this instance
    VkLayerInstanceDispatchTable dispatchTable;
    dispatchTable.GetInstanceProcAddr = gpa;
    dispatchTable.DestroyInstance = (PFN_vkDestroyInstance)gpa(*pInstance, "vkDestroyInstance");
    dispatchTable.EnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties)gpa(*pInstance, "vkEnumerateDeviceExtensionProperties");
    dispatchTable.EnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)gpa(*pInstance, "vkEnumeratePhysicalDevices");
    dispatchTable.EnumeratePhysicalDeviceGroups = (PFN_vkEnumeratePhysicalDeviceGroups)gpa(*pInstance, "vkEnumeratePhysicalDeviceGroups");
    dispatchTable.EnumeratePhysicalDeviceGroupsKHR = (PFN_vkEnumeratePhysicalDeviceGroupsKHR)gpa(*pInstance, "vkEnumeratePhysicalDeviceGroupsKHR");

    gInstanceDispatch[GetKey(*pInstance)] = dispatchTable;

    return VK_SUCCESS;
}

VK_LAYER_EXPORT void VKAPI_CALL
DeviceChooserLayer_DestroyInstance(VkInstance                   instance,
    const VkAllocationCallbacks* pAllocator)
{
    void* const key = GetKey(instance);
    const VkLayerInstanceDispatchTable& dispatch = gInstanceDispatch[key];
    dispatch.DestroyInstance(instance, pAllocator);
    gInstanceDispatch.erase(key);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
DeviceChooserLayer_CreateDevice(VkPhysicalDevice             physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice)
{
    VkLayerDeviceCreateInfo* layerCreateInfo = (VkLayerDeviceCreateInfo*)pCreateInfo->pNext;

    while (layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
        layerCreateInfo->function != VK_LAYER_LINK_INFO))
    {
        layerCreateInfo = (VkLayerDeviceCreateInfo*)layerCreateInfo->pNext;
    }

    if (layerCreateInfo == nullptr)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr gipa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdpa = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    // Move chain on for next layer
    layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

    PFN_vkCreateDevice createFunc = (PFN_vkCreateDevice)gipa(VK_NULL_HANDLE, "vkCreateDevice");

    VkResult ret = createFunc(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (ret != VK_SUCCESS)
    {
        return ret;
    }

    // Initialize the dispatch table for this device
    VkLayerDispatchTable dispatchTable;
    dispatchTable.GetDeviceProcAddr = gdpa;
    dispatchTable.DestroyDevice = (PFN_vkDestroyDevice)gdpa(*pDevice, "vkDestroyDevice");

    gDeviceDispatch[GetKey(*pDevice)] = dispatchTable;

    return ret;
}

VK_LAYER_EXPORT void VKAPI_CALL
DeviceChooserLayer_DestroyDevice(VkDevice                     device,
    const VkAllocationCallbacks* pAllocator)
{
    void* const key = GetKey(device);
    const VkLayerDispatchTable& dispatch = gDeviceDispatch[key];
    dispatch.DestroyDevice(device, pAllocator);
    gDeviceDispatch.erase(key);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
DeviceChooserLayer_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount,
    VkLayerProperties* pProperties)
{
    if (pPropertyCount)
    {
        *pPropertyCount = 1;
    }

    if (pProperties)
    {
        strcpy_s(pProperties->layerName, "VK_LAYER_AEJS_DeviceChooserLayer");
        strcpy_s(pProperties->description, "Device chooser layer");
        pProperties->implementationVersion = 1;
        pProperties->specVersion = VK_API_VERSION_1_0;
    }

    return VK_SUCCESS;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
DeviceChooserLayer_EnumerateDeviceLayerProperties(VkPhysicalDevice   physicalDevice,
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties)
{
    return DeviceChooserLayer_EnumerateInstanceLayerProperties(pPropertyCount, pProperties);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
DeviceChooserLayer_EnumerateInstanceExtensionProperties(const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties)
{
    if (pLayerName == nullptr || strcmp(pLayerName, "VK_LAYER_AEJS_DeviceChooserLayer") != 0)
    {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    if (pPropertyCount)
    {
        *pPropertyCount = 0;
    }

    return VK_SUCCESS;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
DeviceChooserLayer_EnumerateDeviceExtensionProperties(VkPhysicalDevice       physicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties)
{
    if (pLayerName == nullptr || strcmp(pLayerName, "VK_LAYER_AEJS_DeviceChooserLayer") != 0)
    {
        if (physicalDevice == VK_NULL_HANDLE)
        {
            return VK_SUCCESS;
        }

        return GetInstanceDispatch(physicalDevice).EnumerateDeviceExtensionProperties(physicalDevice,
            pLayerName,
            pPropertyCount,
            pProperties);
    }

    if (pPropertyCount)
    {
        *pPropertyCount = 0;
    }

    return VK_SUCCESS;
}

// Macro to simplify function lookups
#define GETPROCADDR(func) if (strcmp(pName, "vk" #func) == 0) return (PFN_vkVoidFunction)&DeviceChooserLayer_##func;

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL
DeviceChooserLayer_GetDeviceProcAddr(VkDevice    device,
    const char* pName)
{
    GETPROCADDR(GetDeviceProcAddr);
    GETPROCADDR(CreateDevice);
    GETPROCADDR(DestroyDevice);
    GETPROCADDR(EnumerateDeviceLayerProperties);
    GETPROCADDR(EnumerateDeviceExtensionProperties);

    // Hand off to the next layer in the chain
    return GetDeviceDispatch(device).GetDeviceProcAddr(device, pName);
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL
DeviceChooserLayer_GetInstanceProcAddr(VkInstance  instance,
    const char* pName)
{
    GETPROCADDR(GetInstanceProcAddr);
    GETPROCADDR(CreateInstance);
    GETPROCADDR(DestroyInstance);
    GETPROCADDR(EnumeratePhysicalDevices);
    GETPROCADDR(EnumeratePhysicalDeviceGroups);
    GETPROCADDR(EnumeratePhysicalDeviceGroupsKHR);
    GETPROCADDR(EnumerateInstanceLayerProperties);
    GETPROCADDR(EnumerateInstanceExtensionProperties);

    // For device functions, we need to pass them through as well
    GETPROCADDR(GetDeviceProcAddr);
    GETPROCADDR(CreateDevice);
    GETPROCADDR(DestroyDevice);
    GETPROCADDR(EnumerateDeviceLayerProperties);
    GETPROCADDR(EnumerateDeviceExtensionProperties);

    // Hand off to the next layer in the chain
    return GetInstanceDispatch(instance).GetInstanceProcAddr(instance, pName);
}