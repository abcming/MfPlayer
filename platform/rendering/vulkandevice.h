#pragma once
#include <QtGui/qtguiglobal.h>
#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#include <QQuickGraphicsDevice>

// Owns the VkDevice shared by Qt Quick and libmpv when the Vulkan backend is
// selected. Qt's own device creation enables only a minimal feature set, but
// libplacebo uses any feature promoted to core at the device's apiVersion
// (synchronization2, pushDescriptor, ...) — promoted does not mean enabled,
// and using them un-enabled is undefined behavior (the source of the
// device-lost crashes). So we create the device ourselves with everything the
// GPU supports and hand it to Qt via QQuickWindow::setGraphicsDevice().
namespace VulkanDevice {

// Creates the instance + device. Returns false (logging why) when the loader
// or GPU can't support it — callers then leave Qt to its default device path
// (libmpv degrades itself to a Vulkan 1.2 feature level in that case).
bool initialize();
bool isActive();

QVulkanInstance *vulkanInstance();
QQuickGraphicsDevice graphicsDevice();

// The exact device creation state, passed through to mpv_vulkan_init_params
// so libplacebo knows precisely what it may use.
const void *enabledFeatures2();            // VkPhysicalDeviceFeatures2 chain
const char *const *enabledExtensions();
quint32 enabledExtensionCount();

} // namespace VulkanDevice
#endif // QT_CONFIG(vulkan)
