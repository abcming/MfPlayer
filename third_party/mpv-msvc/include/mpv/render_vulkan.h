/* Copyright (C) 2025 the mpv developers
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef MPV_CLIENT_API_RENDER_VULKAN_H_
#define MPV_CLIENT_API_RENDER_VULKAN_H_

#include "render.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Vulkan backend
 * --------------
 *
 * This header contains definitions for using Vulkan with the render.h API.
 *
 * API use
 * -------
 *
 * The mpv_render_* API is used. That API supports multiple backends, and this
 * section documents specifics for the Vulkan backend.
 *
 * Use mpv_render_context_create() with MPV_RENDER_PARAM_API_TYPE set to
 * MPV_RENDER_API_TYPE_VULKAN, and MPV_RENDER_PARAM_VULKAN_INIT_PARAMS provided.
 *
 * Call mpv_render_context_render() with MPV_RENDER_PARAM_VULKAN_FBO to render
 * the video frame to a VkImage. The API user is responsible for creating and
 * presenting the image (for example, via a swap chain).
 *
 * Synchronization
 * ---------------
 *
 * The backend treats the image contents as undefined on entry and
 * transitions the image as needed internally; the caller does not need to
 * pre-transition it. When mpv_render_context_render() returns, the layout
 * the image was left in is reported in mpv_vulkan_fbo.out_layout. The
 * backend performs no layout transition of its own after rendering — the
 * caller is expected to transition from out_layout itself, in a command
 * buffer of its own submitted after the render call on the shared queue,
 * before consuming the image.
 *
 * Threading
 * ---------
 *
 * All mpv_render_* functions must be called from the same thread, and that
 * thread must be the one that owns the VkDevice's graphics queue.  Unlike
 * OpenGL, Vulkan does not use per-thread implicit state, so any thread may
 * call the mpv_render_* functions as long as the caller serializes access
 * to the VkDevice and queue.
 */

/**
 * For initializing the mpv Vulkan state via MPV_RENDER_PARAM_VULKAN_INIT_PARAMS.
 */
typedef struct mpv_vulkan_init_params {
    /**
     * The VkInstance to use.  Must be the same instance that created the
     * VkPhysicalDevice and VkDevice below.  libmpv does not take ownership.
     *
     * Type: VkInstance
     */
    void *instance;

    /**
     * A function pointer to vkGetInstanceProcAddr.  If NULL, libplacebo will
     * use the directly linked Vulkan loader.  On platforms where Vulkan is
     * dynamically loaded (e.g. via QVulkanInstance), the caller must provide
     * this so libplacebo can resolve device-level functions.
     *
     * Type: PFN_vkGetInstanceProcAddr
     */
    void *get_proc_addr;

    /**
     * The VkPhysicalDevice selected by the host application.
     *
     * Type: VkPhysicalDevice
     */
    void *phys_device;

    /**
     * The VkDevice created by the host application.  Must have been created
     * with at least the features listed in pl_vulkan_required_features.
     *
     * Type: VkDevice
     */
    void *device;

    /**
     * The queue family index of the graphics queue that libplacebo may use.
     * This queue must support VK_QUEUE_GRAPHICS_BIT.
     */
    uint32_t queue_family_index;

    /**
     * The index of the graphics queue within the above family.
     */
    uint32_t queue_index;

    /**
     * Optional. The feature chain the VkDevice was actually created with
     * (VkDeviceCreateInfo::pNext), so libplacebo only uses what is truly
     * enabled. "Promoted to core" does not mean "enabled": without this,
     * libplacebo assumes any extension promoted at the device's apiVersion
     * is usable (e.g. synchronization2, pushDescriptor) — undefined
     * behavior if the device wasn't created with those features.
     *
     * May be NULL: libmpv then assumes only the libplacebo-required
     * features and caps the API version at 1.2 so no promoted extension
     * is used implicitly.
     *
     * ABI note: these three fields were appended to the struct; libmpv
     * builds that read them must not be mixed with callers compiled
     * against the older, shorter struct.
     *
     * Type: const VkPhysicalDeviceFeatures2 *
     */
    const void *enabled_features;

    /**
     * Optional. The extension names the VkDevice was created with
     * (VkDeviceCreateInfo::ppEnabledExtensionNames). May be NULL.
     */
    const char *const *enabled_extensions;
    uint32_t num_enabled_extensions;
} mpv_vulkan_init_params;

/**
 * For MPV_RENDER_PARAM_VULKAN_FBO.
 */
typedef struct mpv_vulkan_fbo {
    /**
     * The VkImage to render into.  Must be a 2D image created on the same
     * VkDevice passed to mpv_render_context_create() via
     * mpv_vulkan_init_params.  Must have been created with at least
     * VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
     * | VK_IMAGE_USAGE_TRANSFER_DST_BIT.
     *
     * The image is owned by the caller. libmpv does not take ownership.
     *
     * Type: VkImage
     */
    void *image;

    /**
     * The Vulkan format of the image (e.g. VK_FORMAT_B8G8R8A8_UNORM).
     */
    int format;

    /**
     * The usage flags the image was created with.
     */
    int usage;

    /**
     * Dimensions of the render target in pixels. Must always be set, and must
     * match the actual size of the image.
     */
    int w, h;

    /**
     * Written back by mpv_render_context_render(): the VkImageLayout the
     * image is left in when the call returns. The caller must transition
     * from this layout itself before consuming the image (see the
     * Synchronization section above).
     *
     * ABI note: this field was appended to the struct; libmpv builds that
     * write it must not be mixed with callers compiled against the older,
     * shorter struct.
     *
     * Type: VkImageLayout
     */
    int out_layout;
} mpv_vulkan_fbo;

#ifdef __cplusplus
}
#endif

#endif
