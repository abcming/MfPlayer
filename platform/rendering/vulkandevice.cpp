#include "platform/rendering/vulkandevice.h"
#if QT_CONFIG(vulkan)

#include <QVulkanFunctions>
#include <QCoreApplication>
#include <QVarLengthArray>
#include <QVersionNumber>
#include <QDebug>
#include <cstring>
#include <vector>

namespace {

QVulkanInstance *s_inst = nullptr;
VkPhysicalDevice s_physDev = VK_NULL_HANDLE;
VkDevice s_dev = VK_NULL_HANDLE;
uint32_t s_queueFamily = 0;

// Feature chain the device is created with. Persists for the app lifetime:
// MpvController::ensureRenderCtx() passes it to libmpv long after initialize().
VkPhysicalDeviceVulkan11Features s_feat11{};
VkPhysicalDeviceVulkan12Features s_feat12{};
VkPhysicalDeviceVulkan13Features s_feat13{};
#ifdef VK_API_VERSION_1_4
VkPhysicalDeviceVulkan14Features s_feat14{};
#endif
VkPhysicalDeviceFeatures2 s_feat2{};

std::vector<const char *> s_extensions; // string literals from the Vulkan headers

void cleanupDevice()
{
    if (s_dev && s_inst) {
        QVulkanDeviceFunctions *df = s_inst->deviceFunctions(s_dev);
        df->vkDeviceWaitIdle(s_dev);
        df->vkDestroyDevice(s_dev, nullptr);
        s_dev = VK_NULL_HANDLE;
    }
    delete s_inst;
    s_inst = nullptr;
}

} // anonymous namespace

namespace VulkanDevice {

bool initialize()
{
    auto *inst = new QVulkanInstance;
    const QVersionNumber supported = inst->supportedApiVersion();
    if (supported < QVersionNumber(1, 3)) {
        qWarning() << "VulkanDevice: loader only supports" << supported
                   << "- need >= 1.3, leaving device creation to Qt";
        delete inst;
        return false;
    }
    inst->setApiVersion(qMin(supported, QVersionNumber(1, 4)));
    if (!inst->create()) {
        qWarning() << "VulkanDevice: instance creation failed:" << inst->errorCode();
        delete inst;
        return false;
    }
    QVulkanFunctions *f = inst->functions();

    // Physical device: first discrete GPU, else first.
    uint32_t devCount = 0;
    f->vkEnumeratePhysicalDevices(inst->vkInstance(), &devCount, nullptr);
    QVarLengthArray<VkPhysicalDevice, 4> devs(devCount);
    if (devCount)
        f->vkEnumeratePhysicalDevices(inst->vkInstance(), &devCount, devs.data());
    VkPhysicalDevice physDev = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props{};
    for (uint32_t i = 0; i < devCount; ++i) {
        VkPhysicalDeviceProperties p;
        f->vkGetPhysicalDeviceProperties(devs[i], &p);
        if (!physDev || p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            physDev = devs[i];
            props = p;
        }
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            break;
    }
    if (!physDev || props.apiVersion < VK_API_VERSION_1_3) {
        qWarning() << "VulkanDevice: no >= 1.3 physical device, leaving device creation to Qt";
        inst->destroy();
        delete inst;
        return false;
    }

    // First queue family with graphics; Qt and mpv share this single queue.
    uint32_t qfCount = 0;
    f->vkGetPhysicalDeviceQueueFamilyProperties(physDev, &qfCount, nullptr);
    QVarLengthArray<VkQueueFamilyProperties, 8> qfs(qfCount);
    f->vkGetPhysicalDeviceQueueFamilyProperties(physDev, &qfCount, qfs.data());
    int qfi = -1;
    for (uint32_t i = 0; i < qfCount; ++i) {
        if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            qfi = int(i);
            break;
        }
    }
    if (qfi < 0) {
        qWarning() << "VulkanDevice: no graphics queue family";
        inst->destroy();
        delete inst;
        return false;
    }

    // Features: query the full supported set and enable it verbatim — this is
    // what covers libplacebo's required (hostQueryReset, timelineSemaphore)
    // and recommended (synchronization2, pushDescriptor, ...) sets without
    // duplicating its lists here. Only the robustness features are forced off
    // (well-known performance traps, same policy as ffmpeg's hwcontext).
    s_feat2 = {};
    s_feat11 = {};
    s_feat12 = {};
    s_feat13 = {};
    s_feat2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    s_feat11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    s_feat12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    s_feat13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    s_feat2.pNext = &s_feat11;
    s_feat11.pNext = &s_feat12;
    s_feat12.pNext = &s_feat13;
#ifdef VK_API_VERSION_1_4
    if (props.apiVersion >= VK_API_VERSION_1_4) {
        s_feat14 = {};
        s_feat14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
        s_feat13.pNext = &s_feat14;
    }
#endif
    f->vkGetPhysicalDeviceFeatures2(physDev, &s_feat2);
    s_feat2.features.robustBufferAccess = VK_FALSE;
    s_feat13.robustImageAccess = VK_FALSE;

    // Extensions: swapchain is mandatory for Qt; the rest opportunistic.
    static const char *const kWanted[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_HDR_METADATA_EXTENSION_NAME,
#ifdef VK_EXT_full_screen_exclusive
        VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME,
#endif
    };
    uint32_t extCount = 0;
    f->vkEnumerateDeviceExtensionProperties(physDev, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    if (extCount)
        f->vkEnumerateDeviceExtensionProperties(physDev, nullptr, &extCount, exts.data());
    s_extensions.clear();
    for (const char *want : kWanted) {
        for (const VkExtensionProperties &e : exts) {
            if (std::strcmp(e.extensionName, want) == 0) {
                s_extensions.push_back(want);
                break;
            }
        }
    }
    if (s_extensions.empty() || std::strcmp(s_extensions[0], VK_KHR_SWAPCHAIN_EXTENSION_NAME) != 0) {
        qWarning() << "VulkanDevice: VK_KHR_swapchain not supported";
        inst->destroy();
        delete inst;
        return false;
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = uint32_t(qfi);
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &prio;

    VkDeviceCreateInfo devInfo{};
    devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devInfo.pNext = &s_feat2;
    devInfo.queueCreateInfoCount = 1;
    devInfo.pQueueCreateInfos = &queueInfo;
    devInfo.enabledExtensionCount = uint32_t(s_extensions.size());
    devInfo.ppEnabledExtensionNames = s_extensions.data();

    VkDevice dev = VK_NULL_HANDLE;
    VkResult res = f->vkCreateDevice(physDev, &devInfo, nullptr, &dev);
    if (res != VK_SUCCESS) {
        qWarning() << "VulkanDevice: vkCreateDevice failed:" << res;
        inst->destroy();
        delete inst;
        return false;
    }

    s_inst = inst;
    s_physDev = physDev;
    s_dev = dev;
    s_queueFamily = uint32_t(qfi);
    // Runs in ~QCoreApplication — after the QML engine (and with it every
    // window/scenegraph using this device) has been destroyed.
    qAddPostRoutine(cleanupDevice);

    qDebug() << "VulkanDevice: created device on" << props.deviceName
             << "api" << VK_API_VERSION_MAJOR(props.apiVersion) << "."
             << VK_API_VERSION_MINOR(props.apiVersion)
             << "queue family" << qfi
             << "extensions" << uint32_t(s_extensions.size());
    return true;
}

bool isActive()
{
    return s_dev != VK_NULL_HANDLE;
}

QVulkanInstance *vulkanInstance()
{
    return s_inst;
}

QQuickGraphicsDevice graphicsDevice()
{
    return QQuickGraphicsDevice::fromDeviceObjects(s_physDev, s_dev, int(s_queueFamily), 0);
}

const void *enabledFeatures2()
{
    return s_dev ? &s_feat2 : nullptr;
}

const char *const *enabledExtensions()
{
    return s_dev && !s_extensions.empty() ? s_extensions.data() : nullptr;
}

quint32 enabledExtensionCount()
{
    return s_dev ? quint32(s_extensions.size()) : 0;
}

} // namespace VulkanDevice
#endif // QT_CONFIG(vulkan)
