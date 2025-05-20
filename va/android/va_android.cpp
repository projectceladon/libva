/*
 * Copyright (c) 2007 Intel Corporation. All Rights Reserved.
 * Copyright (c) 2023 Emil Velikov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define _GNU_SOURCE 1
#include "sysdeps.h"
#include "va.h"
#include "va_backend.h"
#include "va_internal.h"
#include "va_trace.h"
#include "va_android.h"
#include "va_drmcommon.h"
#include "va_drm_utils.h"
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <errno.h>
#include <xf86drm.h>
#include <i915_drm.h>
#include <xe_drm.h>

#if defined(ANDROID)
#include <cutils/properties.h>
#endif


#define CHECK_SYMBOL(func) { if (!func) printf("func %s not found\n", #func); return VA_STATUS_ERROR_UNKNOWN; }

static void va_DisplayContextDestroy(
    VADisplayContextP pDisplayContext
)
{
    struct drm_state *drm_state;

    if (pDisplayContext == NULL)
        return;

    /* close the open-ed DRM fd */
    drm_state = (struct drm_state *)pDisplayContext->pDriverContext->drm_state;
    close(drm_state->fd);

    free(pDisplayContext->pDriverContext->drm_state);
    free(pDisplayContext->pDriverContext);
    free(pDisplayContext);
}

static int va_IsIntelDgpu(int fd)
{
    struct drm_i915_query_item item = {
        .query_id = DRM_I915_QUERY_MEMORY_REGIONS,
    };

    struct drm_i915_query query = {
        .num_items = 1, .items_ptr = (uintptr_t)&item,
    };
    if (drmIoctl(fd, DRM_IOCTL_I915_QUERY, &query)) {
        va_loge("drv: Failed to DRM_IOCTL_I915_QUERY");
        return 0;
    }

    struct drm_i915_query_memory_regions *meminfo = (struct drm_i915_query_memory_regions *)calloc(1, item.length);
    if (!meminfo) {
        va_loge("drv: %s Exit due to memory allocation failure", __func__);
        return 0;
    }

    item.data_ptr = (uintptr_t)meminfo;
    if (drmIoctl(fd, DRM_IOCTL_I915_QUERY, &query) || item.length <= 0) {
        free(meminfo);
        va_loge("%s:%d DRM_IOCTL_I915_QUERY error", __FUNCTION__, __LINE__);
        return 0;
    }

    int has_sys = 0, has_local = 0;
    for (uint32_t i = 0; i < meminfo->num_regions; i++) {
        const struct drm_i915_memory_region_info *mem = &meminfo->regions[i];
        switch (mem->region.memory_class) {
        case I915_MEMORY_CLASS_SYSTEM:
            has_sys = 1;
            break;
        case I915_MEMORY_CLASS_DEVICE:
            has_local = 1;
            break;
        default:
            break;
        }
    }

    free(meminfo);
    return has_local;
}

static int va_IsIntelXeDgpu(int fd) {
    struct drm_xe_device_query query = {
        .query = DRM_XE_DEVICE_QUERY_MEM_REGIONS,
    };

    if (drmIoctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query)) {
        va_loge("ioctl: DRM_IOCTL_XE_QUERY query fail\n");
        return false;
    }

    struct drm_xe_query_mem_regions *regions = (struct drm_xe_query_mem_regions *)calloc(1, query.size);

    if (!regions) {
        va_loge("calloc drm_xe_query_memory_regions fail\n");
        return false;
    }

    query.data = (uintptr_t)regions;
    if (drmIoctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query)) {
        free(regions);
        va_loge("ioctl: DRM_IOCTL_XE_QUERY alloc fail\n");
        return false;
    }

    bool has_lmem = false;
    for (int i = 0; i < regions->num_mem_regions; i++) {
        if (regions->mem_regions[i].mem_class == DRM_XE_MEM_REGION_CLASS_VRAM) {
            has_lmem = true;
            break;
        }
    }

    free(regions);
    return has_lmem;
}

static int va_SelectIntelDevice()
{
    int use_dgpu = 1;
#if defined(ANDROID)
    char value[PROPERTY_VALUE_MAX] = {};

    property_get("video.hw.dgpu", value, "1");
    use_dgpu = atoi(value);
#endif

    int intel_gpu_index = -1;
    for (int i = 0; i < 16; ++i) {
        char device_path[64];
        sprintf(device_path, "/dev/dri/renderD%d", 128 + i);
        int temp = open(device_path, O_RDWR | O_CLOEXEC);
        if (temp == -1) {
            continue;
        }
        drmVersionPtr version = drmGetVersion(temp);
        if (version == nullptr) {
            close(temp);
            continue;
        }
        if (strncmp(version->name, "i915", strlen("i915")) == 0) {
            intel_gpu_index = i;
            // If specify to use dgpu and found dgpu, return the first found dgpu,
            // else if specify to use igpu and found igpu, return the first found igpu,
            // otherwise use the last available intel node for codec
            if (use_dgpu && va_IsIntelDgpu(temp)) {
                drmFreeVersion(version);
                close(temp);
                va_logd("%s:%d find dgpu", __FUNCTION__, __LINE__);
                break;
            }
            if (!use_dgpu && !va_IsIntelDgpu(temp)) {
                va_logd("%s:%d find igpu", __FUNCTION__, __LINE__);
                drmFreeVersion(version);
                close(temp);
                break;
            }
        } else if (strncmp(version->name, "xe", strlen("xe")) == 0) {
            intel_gpu_index = i;
            if (use_dgpu && va_IsIntelXeDgpu(temp)) {
                drmFreeVersion(version);
                close(temp);
                va_logd("%s:%d find xe dgpu", __FUNCTION__, __LINE__);
                break;
            }
            if (!use_dgpu && !va_IsIntelXeDgpu(temp)) {
                va_logd("%s:%d find xe igpu", __FUNCTION__, __LINE__);
                drmFreeVersion(version);
                close(temp);
                break;
            }
        }
        drmFreeVersion(version);
        close(temp);
    }
    return intel_gpu_index;
}

static VAStatus va_DisplayContextConnect(
    VADisplayContextP pDisplayContext
)
{
    VADriverContextP const ctx = pDisplayContext->pDriverContext;
    struct drm_state * const drm_state = (struct drm_state *)ctx->drm_state;
    int device_node_id = va_SelectIntelDevice();
    if (device_node_id < 0) {
        va_loge("Cannot find candidate DRM device\n");
        return VA_STATUS_ERROR_UNKNOWN;
    }

    char device_name[64];
    sprintf(device_name, "/dev/dri/renderD%d", 128 + device_node_id);
    drm_state->fd = open(device_name, O_RDWR | O_CLOEXEC);
    if (drm_state->fd < 0) {
        va_loge("Cannot open DRM device '%s': %d, %s\n",
                device_name, errno, strerror(errno));
        return VA_STATUS_ERROR_UNKNOWN;
    }
    drm_state->auth_type = VA_DRM_AUTH_CUSTOM;
    return VA_STATUS_SUCCESS;
}

static VAStatus
va_DisplayContextGetDriverNames(
    VADisplayContextP pDisplayContext,
    char            **drivers,
    unsigned         *num_drivers
)
{
    VADriverContextP const ctx = pDisplayContext->pDriverContext;
    VAStatus status = va_DisplayContextConnect(pDisplayContext);
    if (status != VA_STATUS_SUCCESS)
        return status;

    return VA_DRM_GetDriverNames(ctx, drivers, num_drivers);
}

VADisplay vaGetDisplay(
    void *native_dpy /* implementation specific */
)
{
    VADisplayContextP pDisplayContext;
    VADriverContextP  pDriverContext;
    struct drm_state *drm_state;

    if (!native_dpy)
        return NULL;

    pDisplayContext = va_newDisplayContext();
    if (!pDisplayContext)
        return NULL;

    pDisplayContext->vaDestroy       = va_DisplayContextDestroy;
    pDisplayContext->vaGetDriverNames = va_DisplayContextGetDriverNames;

    pDriverContext = va_newDriverContext(pDisplayContext);
    if (!pDriverContext) {
        free(pDisplayContext);
        return NULL;
    }

    pDriverContext->native_dpy   = (void *)native_dpy;
    pDriverContext->display_type = VA_DISPLAY_ANDROID;

    drm_state = (struct drm_state*)calloc(1, sizeof(*drm_state));
    if (!drm_state) {
        free(pDisplayContext);
        free(pDriverContext);
        return NULL;
    }

    pDriverContext->drm_state = drm_state;

    return (VADisplay)pDisplayContext;
}
