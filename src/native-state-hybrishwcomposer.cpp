// SPDX-License-Identifier: GPL-3.0+
//
// Copyright (C) 2019 by noblock
//
// Initialization mainly from the libhybris project.

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cstdlib>
#include <cstdio>

#include <EGL/egl.h>
#include <hwcomposer_window.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <sync/sync.h>

#include "native-state-hybrishwcomposer.h"

class HWComposer : public HWComposerNativeWindow
{
private:
  hwc_layer_1_t *fblayer;
  hwc_composer_device_1_t *hwcdevice;
  hwc_display_contents_1_t **mlist;
protected:
  void present(HWComposerNativeWindowBuffer *buffer);

public:

  HWComposer(unsigned int width, unsigned int height, unsigned int format, hwc_composer_device_1_t *device, hwc_display_contents_1_t **mList, hwc_layer_1_t *layer);
  void set();	
};

HWComposer::HWComposer(unsigned int width, unsigned int height, unsigned int format, hwc_composer_device_1_t *device, hwc_display_contents_1_t **mList, hwc_layer_1_t *layer) : HWComposerNativeWindow(width, height, format)
{
  fblayer = layer;
  hwcdevice = device;
  mlist = mList;
}

void HWComposer::present(HWComposerNativeWindowBuffer *buffer)
{
  int oldretire = mlist[0]->retireFenceFd;
  mlist[0]->retireFenceFd = -1;
  fblayer->handle = buffer->handle;
  fblayer->acquireFenceFd = getFenceBufferFd(buffer);
  fblayer->releaseFenceFd = -1;
  int err = hwcdevice->prepare(hwcdevice, HWC_NUM_DISPLAY_TYPES, mlist);
  assert(err == 0);

  err = hwcdevice->set(hwcdevice, HWC_NUM_DISPLAY_TYPES, mlist);
  // in android surfaceflinger ignores the return value as not all display types may be supported
  setFenceBufferFd(buffer, fblayer->releaseFenceFd);

  if (oldretire != -1)
    {   
      sync_wait(oldretire, -1);
      close(oldretire);
    }
}

/*****************************
 * procs                     *
 ****************************/

//#define DEF_USE_PROCS 1
#ifdef DEF_USE_PROCS
hwc_procs_t procs;

static void hook_invalidate(const struct hwc_procs* procs)
{
  procs = procs;
  printf("invalidate\n");
}

static void hook_vsync(const struct hwc_procs* procs, int disp, int64_t timestamp)
{
  procs = procs;
  disp = disp;
  timestamp = timestamp;
  fprintf(stderr, "vsync\n");
}

static void hook_hotplug(const struct hwc_procs* procs, int disp, int connected)
{
  procs = procs;
  disp = disp;
  connected = connected;
  printf("hotplug\n");
}
#endif /*DEF_USE_PROCS*/

/*****************************
 *                           *
 ****************************/

inline static uint32_t interpreted_version(hw_device_t *hwc_device)
{
  uint32_t version = hwc_device->version;

  if ((version & 0xffff0000) == 0) {
    // Assume header version is always 1
    uint32_t header_version = 1;

    // Legacy version encoding
    version = (version << 16) | header_version;
  }
  return version;
}


/*****************************
 * NativeStateFB             *
 ****************************/

NativeStateFB::~NativeStateFB()
{
}

bool
NativeStateFB::init_display()
{
  if (!native_window)
    {
      int err;
      hw_module_t const* module = NULL;
      err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module);
      assert(err == 0);
      framebuffer_device_t* fbDev = NULL;
      framebuffer_open(module, &fbDev);

      hw_module_t *hwcModule = 0;
      hwc_composer_device_1_t *hwcDevicePtr = 0;

      err = hw_get_module(HWC_HARDWARE_MODULE_ID, (const hw_module_t **) &hwcModule);
      assert(err == 0);

      err = hwc_open_1(hwcModule, &hwcDevicePtr);
      assert(err == 0);

      hw_device_t *hwcDevice = &hwcDevicePtr->common;

      uint32_t hwc_version = interpreted_version(hwcDevice);

#ifdef DEF_USE_PROCS
  if(hwcDevicePtr->registerProcs) {
	procs.invalidate = &hook_invalidate;
	procs.vsync = &hook_vsync;
	procs.hotplug = &hook_hotplug;
	hwcDevicePtr->registerProcs(hwcDevicePtr, &procs);
      }
#endif /*DEF_USE_PROCS*/

#ifdef HWC_DEVICE_API_VERSION_1_4
      if (hwc_version == HWC_DEVICE_API_VERSION_1_4) {
	hwcDevicePtr->setPowerMode(hwcDevicePtr, 0, HWC_POWER_MODE_NORMAL);
      } else
#endif
#ifdef HWC_DEVICE_API_VERSION_1_5
	if (hwc_version == HWC_DEVICE_API_VERSION_1_5) {
	  hwcDevicePtr->setPowerMode(hwcDevicePtr, 0, HWC_POWER_MODE_NORMAL);
	} else
#endif
	  hwcDevicePtr->blank(hwcDevicePtr, 0, 0);

      uint32_t configs[5];
      size_t numConfigs = 5;

      err = hwcDevicePtr->getDisplayConfigs(hwcDevicePtr, 0, configs, &numConfigs);
      assert (err == 0);

      int32_t attr_values[2];
      uint32_t attributes[] = { HWC_DISPLAY_WIDTH, HWC_DISPLAY_HEIGHT, HWC_DISPLAY_NO_ATTRIBUTE }; 

      hwcDevicePtr->getDisplayAttributes(hwcDevicePtr, 0,
					 configs[0], attributes, attr_values);

      //fprintf(stderr, "width: %i height: %i\n", attr_values[0], attr_values[1]);
      size_w = attr_values[0]; size_h = attr_values[1];

      size_t size = sizeof(hwc_display_contents_1_t) + 2 * sizeof(hwc_layer_1_t);
      hwc_display_contents_1_t *list = (hwc_display_contents_1_t *) malloc(size);
      hwc_display_contents_1_t **mList = (hwc_display_contents_1_t **) malloc(HWC_NUM_DISPLAY_TYPES * sizeof(hwc_display_contents_1_t *));
      const hwc_rect_t r = { 0, 0, attr_values[0], attr_values[1] };

      int counter = 0;
      for (; counter < HWC_NUM_DISPLAY_TYPES; counter++)
	mList[counter] = NULL;

      // Assign the layer list only to the first display,
      // otherwise HWC might freeze if others are disconnected
      mList[0] = list;

      hwc_layer_1_t *layer = &list->hwLayers[0];
      memset(layer, 0, sizeof(hwc_layer_1_t));
      layer->compositionType = HWC_FRAMEBUFFER;
      layer->hints = 0;
      layer->flags = 0;
      layer->handle = 0;
      layer->transform = 0;
      layer->blending = HWC_BLENDING_NONE;
#if 0 //def HWC_DEVICE_API_VERSION_1_3
      layer->sourceCropf.top = 0.0f;
      layer->sourceCropf.left = 0.0f;
      layer->sourceCropf.bottom = (float) attr_values[1];
      layer->sourceCropf.right = (float) attr_values[0];
#else
      layer->sourceCrop = r;
#endif
      layer->displayFrame = r;
      layer->visibleRegionScreen.numRects = 1;
      layer->visibleRegionScreen.rects = &layer->displayFrame;
      layer->acquireFenceFd = -1;
      layer->releaseFenceFd = -1;
#if (ANDROID_VERSION_MAJOR >= 4) && (ANDROID_VERSION_MINOR >= 3) || (ANDROID_VERSION_MAJOR >= 5)
      // We've observed that qualcomm chipsets enters into compositionType == 6
      // (HWC_BLIT), an undocumented composition type which gives us rendering
      // glitches and warnings in logcat. By setting the planarAlpha to non-
      // opaque, we attempt to force the HWC into using HWC_FRAMEBUFFER for this
      // layer so the HWC_FRAMEBUFFER_TARGET layer actually gets used.
      bool tryToForceGLES = getenv("QPA_HWC_FORCE_GLES") != NULL;
      layer->planeAlpha = tryToForceGLES ? 1 : 255;
#endif
#ifdef HWC_DEVICE_API_VERSION_1_5
      layer->surfaceDamage.numRects = 0;
#endif

      layer = &list->hwLayers[1];
      memset(layer, 0, sizeof(hwc_layer_1_t));
      layer->compositionType = HWC_FRAMEBUFFER_TARGET;
      layer->hints = 0;
      layer->flags = 0;
      layer->handle = 0;
      layer->transform = 0;
      layer->blending = HWC_BLENDING_NONE;
#if 0 //def HWC_DEVICE_API_VERSION_1_3
      layer->sourceCropf.top = 0.0f;
      layer->sourceCropf.left = 0.0f;
      layer->sourceCropf.bottom = (float) attr_values[1];
      layer->sourceCropf.right = (float) attr_values[0];
#else
      layer->sourceCrop = r;
#endif
      layer->displayFrame = r;
      layer->visibleRegionScreen.numRects = 1;
      layer->visibleRegionScreen.rects = &layer->displayFrame;
      layer->acquireFenceFd = -1;
      layer->releaseFenceFd = -1;
#if (ANDROID_VERSION_MAJOR >= 4) && (ANDROID_VERSION_MINOR >= 3) || (ANDROID_VERSION_MAJOR >= 5)
      layer->planeAlpha = 0xff;
#endif
#ifdef HWC_DEVICE_API_VERSION_1_5
      layer->surfaceDamage.numRects = 0;
#endif

      list->retireFenceFd = -1;
      list->flags = HWC_GEOMETRY_CHANGED;
      list->numHwLayers = 2;

      HWComposer *win = new HWComposer(attr_values[0], attr_values[1], HAL_PIXEL_FORMAT_RGBA_8888, hwcDevicePtr, mList, &list->hwLayers[1]);
      native_window = static_cast<ANativeWindow *> (win);

      egl_display = eglGetDisplay(NULL);
      assert(eglGetError() == EGL_SUCCESS);
      assert(egl_display != EGL_NO_DISPLAY);
    }

  return true;
}

void*
NativeStateFB::display()
{
  return (void *)egl_display;
}

bool
NativeStateFB::create_window(WindowProperties const& properties)
{
  properties_ = properties;

  if (properties_.fullscreen) {
    properties_.width = size_w;
    properties_.height = size_h;
  }

  if (properties_.width > size_w || properties_.height > size_h)
    return false;

  return true;
}

bool
NativeStateFB::should_quit()
{
  return false;
}

void*
NativeStateFB::window(WindowProperties& properties)
{
  properties = properties_;
  return native_window;
}

void
NativeStateFB::visible(bool /*visible*/)
{
}
