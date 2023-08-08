# Copyright (c) 2007 Intel Corporation. All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sub license, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
# 
# The above copyright notice and this permission notice (including the
# next paragraph) shall be included in all copies or substantial portions
# of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
# IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
# ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

# For libva
# =====================================================

LOCAL_PATH:= $(call my-dir)

LIBVA_DRIVERS_PATH_32 := /vendor/lib
LIBVA_DRIVERS_PATH_64 := /vendor/lib64

include $(CLEAR_VARS)

#LIBVA_MINOR_VERSION := 31
#LIBVA_MAJOR_VERSION := 0

IGNORED_WARNNING = \
	-Wno-sign-compare \
	-Wno-missing-field-initializers \
	-Wno-unused-parameter \

LOCAL_SRC_FILES := \
	va.c \
	va_trace.c \
	va_str.c \
	drm/va_drm.c \
	drm/va_drm_auth.c \
	drm/va_drm_utils.c

LOCAL_CFLAGS_32 += \
	-DVA_DRIVERS_PATH="\"$(LIBVA_DRIVERS_PATH_32)\"" \

LOCAL_CFLAGS_64 += \
	-DVA_DRIVERS_PATH="\"$(LIBVA_DRIVERS_PATH_64)\"" \

LOCAL_CFLAGS := \
	$(IGNORED_WARNNING) \
	-DLOG_TAG=\"libva\" \
	-DSYSCONFDIR='"$(sysconfdir)"'

LOCAL_C_INCLUDES := $(LOCAL_PATH)/..

LOCAL_COPY_HEADERS := \
     va.h \
     va_android.h \
     va_version.h \
     va_dec_hevc.h \
     va_dec_jpeg.h \
     va_dec_vp8.h \
     va_dec_vp9.h \
     va_enc_hevc.h \
     va_enc_h264.h \
     va_enc_jpeg.h \
     va_enc_vp8.h \
     va_enc_av1.h \
     va_backend.h \
     va_drmcommon.h \
     va_vpp.h \
     va_backend_prot.h \
     va_backend_vpp.h \
     va_enc_mpeg2.h \
     sysdeps.h \
     va_compat.h \
     va_egl.h \
     va_prot.h \
     va_enc_vp9.h \
     va_fei.h \
     va_fei_h264.h \
     va_fei_hevc.h \
     va_internal.h \
     va_str.h \
     va_tpi.h \
     va_trace.h \
     va_dec_av1.h \
     drm/va_drm.h

LOCAL_COPY_HEADERS_TO := libva/va

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libva
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_PROPRIETARY_MODULE := true

LOCAL_SHARED_LIBRARIES := libdl libdrm libcutils liblog
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 27; echo $$?), 0)
LOCAL_HEADER_LIBRARIES += libutils_headers
endif

intermediates := $(call local-generated-sources-dir)

LOCAL_EXPORT_C_INCLUDE_DIRS := \
	$(intermediates) \
	$(LOCAL_C_INCLUDES)

include $(BUILD_SHARED_LIBRARY)

# For libva-android
# =====================================================

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	android/va_android.cpp \
	drm/va_drm_utils.c

LOCAL_CFLAGS += \
	-DLOG_TAG=\"libva-android\" \
	$(IGNORED_WARNNING)

LOCAL_C_INCLUDES += \
	$(LOCAL_PATH)/drm

LOCAL_COPY_HEADERS_TO := libva/va
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libva-android
LOCAL_PROPRIETARY_MODULE := true

LOCAL_SHARED_LIBRARIES := libva libdrm liblog

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 27; echo $$?), 0)
LOCAL_STATIC_LIBRARIES += libarect
LOCAL_HEADER_LIBRARIES += libnativebase_headers libutils_headers
endif

include $(BUILD_SHARED_LIBRARY)
