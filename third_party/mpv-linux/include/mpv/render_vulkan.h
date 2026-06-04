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
 * The host application is responsible for ensuring the VkImage passed to
 * mpv_render_context_render() is in a suitable layout for rendering
 * (VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL or VK_IMAGE_LAYOUT_GENERAL).
 * The backend will transition the image as needed internally and leave it
 * in VK_IMAGE_LAYOUT_GENERAL when done.
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
} mpv_vulkan_fbo;

#ifdef __cplusplus
}
#endif

#endif
