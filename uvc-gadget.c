/*
 * UVC gadget application
 *
 * Modified by Petr Vavrin (peterbay) <pvavrin@gmail.com>
 *
 * Github: https://github.com/peterbay/uvc-gadget
 *
 * Added:   video controls
 *          resolution changing
 *          framebuffer as source device
 *          code refactoring
 *          enhanced logging
 *          etc.
 *
 * Source tree:
 *   Primary source - wlhe                    -
 * https://github.com/wlhe/uvc-gadget Forked source  - climberhunt (Dave Hunt) -
 * https://github.com/climberhunt/uvc-gadget Forked source  - peterbay (Petr
 * Vavrin)  - https://github.com/peterbay/uvc-gadget
 *
 * Original author:
 * Copyright (C) 2010 Ideas on board SPRL <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 */

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <ftw.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/usb/ch9.h>
#include <linux/usb/video.h>
#include <linux/videodev2.h>

#include <gphoto2/gphoto2.h>

#include "uvc-gadget.h"

volatile sig_atomic_t terminate = 0;
volatile atomic_int is_camera_ready = 0;
volatile atomic_int is_initialized = 0;
static pthread_mutex_t lock;

static GPContext *context;
static Camera *camera;

/* Double-buffered camera frames.
 * capture_thread writes to cam_files[write_buf] without holding the lock.
 * Once a frame is complete it briefly locks, flips ready_buf/write_buf, and
 * signals the condition variable.  uvc_camera_video_process holds the lock
 * only for the fast memcpy, never during USB I/O.
 */
static CameraFile *cam_files[2];
static const char   *cam_rawdata[2] = {NULL, NULL};
static size_t        cam_sizes[2]   = {0, 0};
static int cam_write_buf = 0;   /* buffer currently being written by capture thread */
static int cam_ready_buf = -1;  /* buffer holding the latest complete frame (-1 = none yet) */
static uint64_t cam_frame_seq = 0; /* incremented each time a new frame is published */
static pthread_cond_t cam_frame_cond = PTHREAD_COND_INITIALIZER;

void term(int signum) {
  (void)(signum); /* avoid warning: unused parameter 'signum' */
  terminate = 1;
}

static void ctx_error_func(GPContext *ctx, const char *str, void *data) {
  /* If there's a context error it means the camera isn't ready */
  atomic_store(&is_camera_ready, 0);
}

/**
 * @brief Creates a gphoto context.
 *
 * @return GPContext* the newly created context.
 */
static GPContext *create_context() {
  GPContext *context;
  context = gp_context_new();

  gp_context_set_error_func(context, ctx_error_func, NULL);

  return context;
}

static Camera *get_camera() {
  Camera *camera;
  int result = gp_camera_new(&camera);

  if (result < GP_OK) {
    printf("CAMERA: Error getting new camera (%i)\n", result);
    return NULL;
  }

  return camera;
}

static int camera_start(Camera **camera, GPContext *context) {
  int result = gp_camera_init(*camera, context);

  if (result < GP_OK) {
    if (result != GP_ERROR_MODEL_NOT_FOUND) {
      printf("CAMERA: Error initializing camera (%i)\n", result);
    }
  }

  return result;
}

static void camera_setup_loop() {
  int is_cam_ready;
  printf("CAMERA: SETUP LOOP\n");

  while (!(is_cam_ready = atomic_load(&is_camera_ready))) {
    if (terminate) {
      return;
    }

    GPContext *new_context = create_context();

    /* Allocate both double-buffer CameraFile objects */
    int result;
    result = gp_file_new(&cam_files[0]);
    if (result < GP_OK) {
      printf("CAMERA: Failed to create file[0] (%i)\n", result);
      gp_context_unref(new_context);
      sleep(1);
      continue;
    }
    result = gp_file_new(&cam_files[1]);
    if (result < GP_OK) {
      printf("CAMERA: Failed to create file[1] (%i)\n", result);
      gp_file_free(cam_files[0]);
      gp_context_unref(new_context);
      sleep(1);
      continue;
    }

    Camera *new_camera = get_camera();
    if (!new_camera) {
      gp_file_free(cam_files[0]);
      gp_file_free(cam_files[1]);
      gp_context_unref(new_context);
      sleep(1);
      continue;
    }

    result = camera_start(&new_camera, new_context);
    if (result < GP_OK) {
      gp_camera_exit(new_camera, new_context);
      gp_file_free(cam_files[0]);
      gp_file_free(cam_files[1]);
      gp_context_unref(new_context);
      sleep(1);
      continue;
    }

    /* Commit - replace globals only once everything succeeded */
    context = new_context;
    camera  = new_camera;

    cam_write_buf = 0;
    cam_ready_buf = -1;
    cam_rawdata[0] = cam_rawdata[1] = NULL;
    cam_sizes[0]   = cam_sizes[1]   = 0;

    atomic_store(&is_camera_ready, 1);
    printf("CAMERA: Setup finished\n");
  }
}

static int sys_gpio_write(unsigned int type, char pin[], char value[]) {
  FILE *sys_file;
  char path[255];

  strcpy(path, "/sys/class/gpio/");

  switch (type) {
  case GPIO_EXPORT:
    strcat(path, "export");
    value = pin;
    break;

  case GPIO_DIRECTION:
    strcat(path, "gpio");
    strcat(path, pin);
    strcat(path, "/direction");
    break;

  case GPIO_VALUE:
    strcat(path, "gpio");
    strcat(path, pin);
    strcat(path, "/value");
    break;
  }

  printf("GPIO WRITE: Path: %s, Value: %s\n", path, value);

  sys_file = fopen(path, "w");
  if (!sys_file) {
    printf("GPIO ERROR: File write failed: %s (%d).\n", strerror(errno), errno);
    return -1;
  }

  fwrite(value, 1, strlen(value), sys_file);
  fclose(sys_file);

  return 0;
}

static int sys_led_write(unsigned int type, char value[]) {
  FILE *sys_file;
  char path[255];

  strcpy(path, "/sys/class/leds/led0/");

  switch (type) {
  case LED_TRIGGER:
    strcat(path, "trigger");
    break;

  case LED_BRIGHTNESS:
    strcat(path, "brightness");
    break;
  }

  printf("LED WRITE: Path: %s, Value: %s\n", path, value);

  sys_file = fopen(path, "w");
  if (!sys_file) {
    printf("LED ERROR: File write failed: %s (%d).\n", strerror(errno), errno);
    return -1;
  }

  fwrite(value, 1, strlen(value), sys_file);
  fclose(sys_file);

  return 0;
}

static void streaming_status_enable() {
  int ret;
  if (!settings.streaming_status_enabled && settings.streaming_status_pin) {
    ret = sys_gpio_write(GPIO_EXPORT, settings.streaming_status_pin, NULL);
    if (ret < 0) {
      return;
    }

    ret = sys_gpio_write(GPIO_DIRECTION, settings.streaming_status_pin,
                         GPIO_DIRECTION_OUT);
    if (ret < 0) {
      return;
    }

    ret = sys_gpio_write(GPIO_VALUE, settings.streaming_status_pin,
                         GPIO_VALUE_OFF);
    if (ret < 0) {
      return;
    }

    settings.streaming_status_enabled = true;
  }

  if (settings.streaming_status_onboard) {
    ret = sys_led_write(LED_TRIGGER, LED_TRIGGER_NONE);
    if (ret < 0) {
      return;
    }

    ret = sys_led_write(LED_BRIGHTNESS, LED_BRIGHTNESS_LOW);
    if (ret < 0) {
      return;
    }
    settings.streaming_status_onboard_enabled = true;
  }
  return;
}

static void streaming_status_value(bool state) {
  char *gpio_value = (state) ? GPIO_VALUE_ON : GPIO_VALUE_OFF;
  char *led_value = (state) ? LED_BRIGHTNESS_HIGH : LED_BRIGHTNESS_LOW;

  if (settings.streaming_status_enabled) {
    sys_gpio_write(GPIO_VALUE, settings.streaming_status_pin, gpio_value);
  }

  if (settings.streaming_status_onboard_enabled) {
    sys_led_write(LED_BRIGHTNESS, led_value);
  }
}

static char *uvc_request_code_name(unsigned int uvc_control) {
  switch (uvc_control) {
  case UVC_RC_UNDEFINED:
    return "RC_UNDEFINED";

  case UVC_SET_CUR:
    return "SET_CUR";

  case UVC_GET_CUR:
    return "GET_CUR";

  case UVC_GET_MIN:
    return "GET_MIN";

  case UVC_GET_MAX:
    return "GET_MAX";

  case UVC_GET_RES:
    return "GET_RES";

  case UVC_GET_LEN:
    return "GET_LEN";

  case UVC_GET_INFO:
    return "GET_INFO";

  case UVC_GET_DEF:
    return "GET_DEF";

  default:
    return "UNKNOWN";
  }
}

static char *uvc_vs_interface_control_name(unsigned int interface) {
  switch (interface) {
  case UVC_VS_CONTROL_UNDEFINED:
    return "CONTROL_UNDEFINED";

  case UVC_VS_PROBE_CONTROL:
    return "PROBE";

  case UVC_VS_COMMIT_CONTROL:
    return "COMMIT";

  case UVC_VS_STILL_PROBE_CONTROL:
    return "STILL_PROBE";

  case UVC_VS_STILL_COMMIT_CONTROL:
    return "STILL_COMMIT";

  case UVC_VS_STILL_IMAGE_TRIGGER_CONTROL:
    return "STILL_IMAGE_TRIGGER";

  case UVC_VS_STREAM_ERROR_CODE_CONTROL:
    return "STREAM_ERROR_CODE";

  case UVC_VS_GENERATE_KEY_FRAME_CONTROL:
    return "GENERATE_KEY_FRAME";

  case UVC_VS_UPDATE_FRAME_SEGMENT_CONTROL:
    return "UPDATE_FRAME_SEGMENT";

  case UVC_VS_SYNC_DELAY_CONTROL:
    return "SYNC_DELAY";

  default:
    return "UNKNOWN";
  }
}

static unsigned int get_frame_size(int pixelformat, int width, int height) {
  switch (pixelformat) {
  case V4L2_PIX_FMT_YUYV:
    return width * height * 2;

  case V4L2_PIX_FMT_MJPEG:
    return width * height;
    break;
  }

  return width * height;
}

static int uvc_open(char *devname, unsigned int nbufs) {
  struct v4l2_capability cap;
  const char *type_name = "DEVICE_UVC";

  printf("%s: Opening %s device\n", type_name, devname);

  uvc_dev.fd = open(devname, O_RDWR | O_NONBLOCK, 0);
  if (uvc_dev.fd == -1) {
    printf("%s: Device open failed: %s (%d).\n", type_name, strerror(errno),
           errno);
    return -EINVAL;
  }

  if (ioctl(uvc_dev.fd, VIDIOC_QUERYCAP, &cap) < 0) {
    printf("%s: VIDIOC_QUERYCAP failed: %s (%d).\n", type_name, strerror(errno),
           errno);
    goto err;
  }

  if (!(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT)) {
    printf("%s: %s is no video output device\n", type_name, devname);
    goto err;
  }

  printf("%s: Device is %s on bus %s\n", type_name, cap.card, cap.bus_info);

  uvc_dev.device_type = DEVICE_TYPE_UVC;
  uvc_dev.device_type_name = type_name;
  uvc_dev.buffer_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  uvc_dev.memory_type = V4L2_MEMORY_USERPTR;
  uvc_dev.nbufs = nbufs;
  return 1;

err:
  close(uvc_dev.fd);
  uvc_dev.fd = -1;
  return -EINVAL;
}

/* ---------------------------------------------------------------------------
 * UVC streaming related
 */

static void uvc_uninit_device() {
  unsigned int i;
  if (uvc_dev.dummy_buf) {
    printf("%s: Uninit device\n", uvc_dev.device_type_name);

    for (i = 0; i < uvc_dev.nbufs; ++i) {
      free(uvc_dev.dummy_buf[i].start);
      uvc_dev.dummy_buf[i].start = NULL;
    }
    free(uvc_dev.dummy_buf);
    uvc_dev.dummy_buf = NULL;
  }
}

static int uvc_video_stream_control(struct uvc_device *dev,
                                     enum video_stream_action action) {
  int type = dev->buffer_type;
  int ret;

  if (action == STREAM_ON) {
    ret = ioctl(dev->fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
      printf("%s: STREAM ON failed: %s (%d).\n", dev->device_type_name,
             strerror(errno), errno);
      return ret;
    }

    printf("%s: STREAM ON success\n", dev->device_type_name);
    dev->is_streaming = 1;
    uvc_shutdown_requested = false;

  } else if (dev->is_streaming) {
    ret = ioctl(dev->fd, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
      printf("%s: STREAM OFF failed: %s (%d).\n", dev->device_type_name,
             strerror(errno), errno);
      return ret;
    }

    printf("%s: STREAM OFF success\n", dev->device_type_name);
    dev->is_streaming = 0;
  }
  return 0;
}

static int uvc_video_stream(enum video_stream_action action) {
  return uvc_video_stream_control(&uvc_dev, action);
}

static int uvc_init_buffers(struct uvc_device *dev,
                             struct v4l2_requestbuffers *req,
                             unsigned int count) {
  int ret;
  req->count = count;
  req->type = dev->buffer_type;
  req->memory = dev->memory_type;

  ret = ioctl(dev->fd, VIDIOC_REQBUFS, req);
  if (ret < 0) {
    if (ret == -EINVAL) {
      printf("%s: Does not support %s\n", dev->device_type_name,
             (dev->memory_type == V4L2_MEMORY_USERPTR) ? "user pointer i/o"
                                                       : "memory mapping");

    } else {
      printf("%s: VIDIOC_REQBUFS error: %s (%d).\n", dev->device_type_name,
             strerror(errno), errno);
    }
    return ret;
  }
  return count;
}

static int uvc_reqbufs_mmap(struct uvc_device *dev,
                             struct v4l2_requestbuffers req) {
  int ret;
  unsigned int i = 0;

  /* Map the buffers. */
  dev->mem = calloc(req.count, sizeof dev->mem[0]);
  if (!dev->mem) {
    printf("%s: Out of memory\n", dev->device_type_name);
    ret = -ENOMEM;
    goto err;
  }

  for (i = 0; i < req.count; ++i) {
    CLEAR(dev->mem[i].buf);

    dev->mem[i].buf.type = dev->buffer_type;
    dev->mem[i].buf.memory = V4L2_MEMORY_MMAP;
    dev->mem[i].buf.index = i;

    ret = ioctl(dev->fd, VIDIOC_QUERYBUF, &(dev->mem[i].buf));
    if (ret < 0) {
      printf("%s: VIDIOC_QUERYBUF failed for buf %d: %s (%d).\n",
             dev->device_type_name, i, strerror(errno), errno);

      ret = -EINVAL;
      goto err_free;
    }

    dev->mem[i].start =
        mmap(NULL /* start anywhere */, dev->mem[i].buf.length,
             PROT_READ | PROT_WRITE /* required */,
             MAP_SHARED /* recommended */, dev->fd, dev->mem[i].buf.m.offset);

    if (MAP_FAILED == dev->mem[i].start) {
      printf("%s: Unable to map buffer %u: %s (%d).\n", dev->device_type_name,
             i, strerror(errno), errno);

      dev->mem[i].length = 0;
      ret = -EINVAL;
      goto err_free;
    }

    dev->mem[i].length = dev->mem[i].buf.length;
    printf("%s: Buffer %u mapped at address %p, length %d.\n",
           dev->device_type_name, i, dev->mem[i].start, dev->mem[i].length);
  }

  return 0;

err_free:
  free(dev->mem);
err:
  return ret;
}

static int uvc_reqbufs_userptr(struct uvc_device *dev,
                                struct v4l2_requestbuffers req) {
  unsigned int payload_size;
  unsigned int i;

  printf("Allocate buffers to hold dummy data pattern.\n");
  dev->dummy_buf = calloc(req.count, sizeof dev->dummy_buf[0]);
  if (!dev->dummy_buf) {
    printf("%s: Out of memory\n", dev->device_type_name);
    return -ENOMEM;
  }

  payload_size = 1056 * 704 * 2; // TODO: make configurable

  for (i = 0; i < req.count; ++i) {
    dev->dummy_buf[i].length = payload_size;
    dev->dummy_buf[i].start = calloc(1, payload_size);
    if (!dev->dummy_buf[i].start) {
      /* Free already-allocated buffers before returning */
      unsigned int j;
      for (j = 0; j < i; ++j) {
        free(dev->dummy_buf[j].start);
      }
      free(dev->dummy_buf);
      dev->dummy_buf = NULL;
      printf("%s: Out of memory\n", dev->device_type_name);
      return -ENOMEM;
    }
  }

  dev->mem = dev->dummy_buf;
  return 0;
}

static int uvc_reqbufs(struct uvc_device *dev, int nbufs) {
  int ret = 0;
  struct v4l2_requestbuffers req;
  CLEAR(req);

  dev->dqbuf_count = 0;
  dev->qbuf_count = 0;

  ret = uvc_init_buffers(dev, &req, nbufs);
  if (ret < 1) {
    return ret;
  }

  if (!req.count) {
    return 0;
  }

  if (dev->memory_type == V4L2_MEMORY_MMAP) {
    if (req.count < 2) {
      printf("%s: Insufficient buffer memory.\n", dev->device_type_name);
      return -EINVAL;
    }

    ret = uvc_reqbufs_mmap(dev, req);
    if (ret < 0) {
      return -EINVAL;
    }
  }

  if (dev->memory_type == V4L2_MEMORY_USERPTR) {
    if (req.count < 1) {
      printf("%s: Insufficient buffer memory.\n", dev->device_type_name);
      return -EINVAL;
    }

    ret = uvc_reqbufs_userptr(dev, req);
    if (ret < 0) {
      return -EINVAL;
    }
  }

  dev->nbufs = req.count;
  printf("%s: %u buffers allocated.\n", dev->device_type_name, req.count);

  return ret;
}

static int uvc_request_bufs(int nbufs) { return uvc_reqbufs(&uvc_dev, nbufs); }

static int uvc_video_qbuf() {
  unsigned int i;
  int ret;

  for (i = 0; i < uvc_dev.nbufs; ++i) {
    struct v4l2_buffer buf;

    CLEAR(buf);
    buf.type = uvc_dev.buffer_type;
    buf.memory = uvc_dev.memory_type;
    buf.m.userptr = (unsigned long)uvc_dev.dummy_buf[i].start;
    buf.length = uvc_dev.dummy_buf[i].length;
    buf.index = i;

    ret = ioctl(uvc_dev.fd, VIDIOC_QBUF, &buf);
    if (ret < 0) {
      printf("UVC: VIDIOC_QBUF failed : %s (%d).\n", strerror(errno), errno);
      return ret;
    }

    uvc_dev.qbuf_count++;
  }
  return 0;
}

/* ---------------------------------------------------------------------------
 * UVC format management
 */

static int uvc_get_format(struct uvc_device *dev) {
  struct v4l2_format fmt;
  int ret;

  CLEAR(fmt);
  fmt.type = dev->buffer_type;

  ret = ioctl(dev->fd, VIDIOC_G_FMT, &fmt);
  if (ret < 0) {
    return ret;
  }

  printf("%s: Getting current format: %c%c%c%c %ux%u\n", dev->device_type_name,
         pixfmtstr(fmt.fmt.pix.pixelformat), fmt.fmt.pix.width,
         fmt.fmt.pix.height);

  return 0;
}

static int uvc_set_format(struct uvc_device *dev, struct v4l2_format *fmt) {
  int ret;

  ret = ioctl(dev->fd, VIDIOC_S_FMT, fmt);
  if (ret < 0) {
    printf("%s: Unable to set format %s (%d).\n", dev->device_type_name,
           strerror(errno), errno);
    return ret;
  }

  printf("%s: Setting format to: %c%c%c%c %ux%u\n", dev->device_type_name,
         pixfmtstr(fmt->fmt.pix.pixelformat), fmt->fmt.pix.width,
         fmt->fmt.pix.height);

  return 0;
}

static int uvc_apply_format(struct uvc_device *dev, unsigned int pixelformat,
                             unsigned int width, unsigned int height) {
  struct v4l2_format fmt;
  int ret = -EINVAL;

  if (dev->is_streaming || dev->fd < 0) {
    return ret;
  }

  CLEAR(fmt);
  fmt.type = dev->buffer_type;
  fmt.fmt.pix.width = width;
  fmt.fmt.pix.height = height;
  fmt.fmt.pix.sizeimage = get_frame_size(pixelformat, width, height);
  fmt.fmt.pix.pixelformat = pixelformat;
  fmt.fmt.pix.field = V4L2_FIELD_ANY;

  ret = uvc_set_format(dev, &fmt);
  if (ret < 0) {
    return ret;
  }

  return uvc_get_format(dev);
}

static void uvc_close() {
  if (uvc_dev.fd >= 0) {
    close(uvc_dev.fd);
    uvc_dev.fd = -1;
  }
}

/* ---------------------------------------------------------------------------
 * UVC streaming related
 */
static void *uvc_camera_video_process() {
  struct v4l2_buffer ubuf;
  /*
   * Return immediately if UVC video output device has not started
   * streaming yet.
   */
  if (!uvc_dev.is_streaming) {
    return NULL;
  }

  /* Prepare a v4l2 buffer to be dequeued from UVC domain. */
  CLEAR(ubuf);
  ubuf.type = uvc_dev.buffer_type;
  ubuf.memory = uvc_dev.memory_type;

  if (ioctl(uvc_dev.fd, VIDIOC_DQBUF, &ubuf) < 0) {
    if (errno == EAGAIN)
      return NULL;
    printf("%s: Unable to dequeue buffer: %s (%d).\n", uvc_dev.device_type_name,
           strerror(errno), errno);
    return NULL;
  }

  pthread_mutex_lock(&lock);
  if (cam_ready_buf >= 0 && cam_rawdata[cam_ready_buf] != NULL) {
    size_t copy_size = cam_sizes[cam_ready_buf];
    if (copy_size > uvc_dev.mem[ubuf.index].length) {
      copy_size = uvc_dev.mem[ubuf.index].length;
    }
    memcpy(uvc_dev.mem[ubuf.index].start, cam_rawdata[cam_ready_buf], copy_size);
    ubuf.bytesused = copy_size;
  } else {
    ubuf.bytesused = 0;
  }
  pthread_mutex_unlock(&lock);

  ubuf.m.userptr = (unsigned long)uvc_dev.mem[ubuf.index].start;

  if (ioctl(uvc_dev.fd, VIDIOC_QBUF, &ubuf) < 0) {
    printf("%s: Unable to queue buffer: %s (%d).\n", uvc_dev.device_type_name,
           strerror(errno), errno);
    return NULL;
  }

  uvc_dev.qbuf_count++;

  if (settings.show_fps) {
    uvc_dev.buffers_processed++;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long long now_ms = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;

    if (uvc_dev.last_time_video_process == 0) {
      uvc_dev.last_time_video_process = now_ms;
    } else if (now_ms - uvc_dev.last_time_video_process >= 1000) {
      double elapsed = (now_ms - uvc_dev.last_time_video_process) / 1000.0;
      printf("FPS: %.1f\n", uvc_dev.buffers_processed / elapsed);
      uvc_dev.buffers_processed = 0;
      uvc_dev.last_time_video_process = now_ms;
    }
  }

  return NULL;
}

static void uvc_handle_streamon_event() {
  printf("STREAM ON EVENT REQUESTED\n");

  if (uvc_request_bufs(uvc_dev.nbufs) < 0) {
    return;
  }

  if (uvc_video_qbuf() < 0) {
    return;
  }

  uvc_video_stream(STREAM_ON);
  settings.blink_on_startup = 0;
  streaming_status_value(uvc_dev.is_streaming);
}

static void uvc_handle_streamoff_event() {
  uvc_uninit_device();
  uvc_video_stream(STREAM_OFF);
  uvc_request_bufs(0);
  streaming_status_value(uvc_dev.is_streaming);
}

/* ---------------------------------------------------------------------------
 * UVC Request processing
 */
static void dump_uvc_streaming_control(struct uvc_streaming_control *ctrl) {
  printf("DUMP: uvc_streaming_control: format: %d, frame: %d, frame interval: "
         "%d\n",
         ctrl->bFormatIndex, ctrl->bFrameIndex, ctrl->dwFrameInterval);
}

static int uvc_get_frame_format_index(int format_index,
                                      enum uvc_frame_format_getter getter) {
  int index = -1;
  int value;
  int i;

  for (i = 0; i <= last_format_index; i++) {
    if (format_index == -1 ||
        format_index == (int)uvc_frame_format[i].bFormatIndex) {

      switch (getter) {
      case FORMAT_INDEX_MIN:
      case FORMAT_INDEX_MAX:
        value = uvc_frame_format[i].bFormatIndex;
        break;

      case FRAME_INDEX_MIN:
      case FRAME_INDEX_MAX:
        value = uvc_frame_format[i].bFrameIndex;
        break;
      }
      if (index == -1) {
        index = value;

      } else {
        switch (getter) {
        case FORMAT_INDEX_MIN:
        case FRAME_INDEX_MIN:
          if (value < index) {
            index = value;
          }
          break;

        case FORMAT_INDEX_MAX:
        case FRAME_INDEX_MAX:
          if (value > index) {
            index = value;
          }
          break;
        }
      }
    }
  }
  return index;
}

static int uvc_get_frame_format(struct uvc_frame_format **frame_format,
                                unsigned int iFormat, unsigned int iFrame) {
  int i;
  for (i = 0; i <= last_format_index; i++) {
    if (uvc_frame_format[i].bFormatIndex == iFormat &&
        uvc_frame_format[i].bFrameIndex == iFrame) {
      *frame_format = &uvc_frame_format[i];
      return 0;
    }
  }
  return -1;
}

static void uvc_dump_frame_format(struct uvc_frame_format *frame_format,
                                  const char *title) {
  printf("%s: format: %d, frame: %d, resolution: %dx%d, frame_interval: %d,  "
         "bitrate: [%d, %d]\n",
         title, frame_format->bFormatIndex, frame_format->bFrameIndex,
         frame_format->wWidth, frame_format->wHeight,
         frame_format->dwDefaultFrameInterval, frame_format->dwMinBitRate,
         frame_format->dwMaxBitRate);
}

static void uvc_fill_streaming_control(struct uvc_streaming_control *ctrl,
                                       enum stream_control_action action,
                                       int iformat, int iframe) {
  int format_first;
  int format_last;
  int frame_first;
  int frame_last;
  int format_frame_first;
  int format_frame_last;
  unsigned int frame_interval;
  unsigned int dwMaxPayloadTransferSize;

  switch (action) {
  case STREAM_CONTROL_INIT:
    printf("UVC: Streaming control: action: INIT\n");
    break;

  case STREAM_CONTROL_MIN:
    printf("UVC: Streaming control: action: GET MIN\n");
    break;

  case STREAM_CONTROL_MAX:
    printf("UVC: Streaming control: action: GET MAX\n");
    break;

  case STREAM_CONTROL_SET:
    printf("UVC: Streaming control: action: SET, format: %d, frame: %d\n",
           iformat, iframe);
    break;
  }

  format_first = uvc_get_frame_format_index(-1, FORMAT_INDEX_MIN);
  format_last = uvc_get_frame_format_index(-1, FORMAT_INDEX_MAX);

  frame_first = uvc_get_frame_format_index(-1, FRAME_INDEX_MIN);
  frame_last = uvc_get_frame_format_index(-1, FRAME_INDEX_MAX);

  if (action == STREAM_CONTROL_MIN) {
    iformat = format_first;
    iframe = frame_first;

  } else if (action == STREAM_CONTROL_MAX) {
    iformat = format_last;
    iframe = frame_last;

  } else {
    iformat = clamp(iformat, format_first, format_last);

    format_frame_first = uvc_get_frame_format_index(iformat, FRAME_INDEX_MIN);
    format_frame_last = uvc_get_frame_format_index(iformat, FRAME_INDEX_MAX);

    iframe = clamp(iframe, format_frame_first, format_frame_last);
  }

  struct uvc_frame_format *frame_format;
  uvc_get_frame_format(&frame_format, iformat, iframe);

  uvc_dump_frame_format(frame_format, "FRAME");

  if (frame_format->dwDefaultFrameInterval >= 100000) {
    frame_interval = frame_format->dwDefaultFrameInterval;
  } else {
    frame_interval = 400000;
  }

  dwMaxPayloadTransferSize = streaming_maxpacket;
  if (streaming_maxpacket > 1024 && streaming_maxpacket % 1024 != 0) {
    dwMaxPayloadTransferSize -= (streaming_maxpacket / 1024) * 128;
  }

  memset(ctrl, 0, sizeof *ctrl);
  ctrl->bmHint = 1;
  ctrl->bFormatIndex = iformat;
  ctrl->bFrameIndex = iframe;
  ctrl->dwMaxVideoFrameSize = get_frame_size(
      frame_format->video_format, frame_format->wWidth, frame_format->wHeight);
  ctrl->dwMaxPayloadTransferSize = dwMaxPayloadTransferSize;
  ctrl->dwFrameInterval = frame_interval;
  ctrl->bmFramingInfo = 3;
  ctrl->bMinVersion = format_first;
  ctrl->bMaxVersion = format_last;
  ctrl->bPreferedVersion = format_last;

  dump_uvc_streaming_control(ctrl);

  if (uvc_dev.control == UVC_VS_COMMIT_CONTROL &&
      action == STREAM_CONTROL_SET) {
    uvc_apply_format(&uvc_dev, frame_format->video_format,
                      frame_format->wWidth, frame_format->wHeight);
  }
}

static void uvc_interface_control(unsigned int interface, uint8_t req,
                                  uint8_t cs, uint8_t len,
                                  struct uvc_request_data *resp) {
  int i;
  bool found = false;
  const char *request_code_name = uvc_request_code_name(req);
  const char *interface_name = (interface == UVC_VC_INPUT_TERMINAL)
                                   ? "INPUT_TERMINAL"
                                   : "PROCESSING_UNIT";

  for (i = 0; i < control_mapping_size; i++) {
    if (control_mapping[i].type == interface && control_mapping[i].uvc == cs) {
      found = true;
      break;
    }
  }

  if (!found) {
    printf("UVC: %s - %s - %02x - UNSUPPORTED\n", interface_name,
           request_code_name, cs);
    resp->length = -EL2HLT;
    uvc_dev.request_error_code = REQEC_INVALID_CONTROL;
    return;
  }

  if (!control_mapping[i].enabled) {
    printf("UVC: %s - %s - %s - DISABLED\n", interface_name, request_code_name,
           control_mapping[i].uvc_name);
    resp->length = -EL2HLT;
    uvc_dev.request_error_code = REQEC_INVALID_CONTROL;
    return;
  }

  printf("UVC: %s - %s - %s\n", interface_name, request_code_name,
         control_mapping[i].uvc_name);

  switch (req) {
  case UVC_SET_CUR:
    resp->data[0] = 0x0;
    resp->length = len;
    uvc_dev.control_interface = interface;
    uvc_dev.control_type = cs;
    uvc_dev.request_error_code = REQEC_NO_ERROR;
    break;

  case UVC_GET_MIN:
    resp->length = 4;
    memcpy(&resp->data[0], &control_mapping[i].minimum, resp->length);
    uvc_dev.request_error_code = REQEC_NO_ERROR;
    break;

  case UVC_GET_MAX:
    resp->length = 4;
    memcpy(&resp->data[0], &control_mapping[i].maximum, resp->length);
    uvc_dev.request_error_code = REQEC_NO_ERROR;
    break;

  case UVC_GET_CUR:
    resp->length = 4;
    memcpy(&resp->data[0], &control_mapping[i].value, resp->length);
    uvc_dev.request_error_code = REQEC_NO_ERROR;
    break;

  case UVC_GET_INFO:
    resp->data[0] = (uint8_t)(UVC_CONTROL_CAP_GET | UVC_CONTROL_CAP_SET);
    resp->length = 1;
    uvc_dev.request_error_code = REQEC_NO_ERROR;
    break;

  case UVC_GET_DEF:
    resp->length = 4;
    memcpy(&resp->data[0], &control_mapping[i].default_value, resp->length);
    uvc_dev.request_error_code = REQEC_NO_ERROR;
    break;

  case UVC_GET_RES:
    resp->length = 4;
    memcpy(&resp->data[0], &control_mapping[i].step, resp->length);
    uvc_dev.request_error_code = REQEC_NO_ERROR;
    break;

  default:
    resp->length = -EL2HLT;
    uvc_dev.request_error_code = REQEC_INVALID_REQUEST;
    break;
  }
  return;
}

static void uvc_events_process_streaming(uint8_t req, uint8_t cs,
                                         struct uvc_request_data *resp) {
  printf("UVC: Streaming request CS: %s, REQ: %s\n",
         uvc_vs_interface_control_name(cs), uvc_request_code_name(req));

  if (cs != UVC_VS_PROBE_CONTROL && cs != UVC_VS_COMMIT_CONTROL) {
    return;
  }

  struct uvc_streaming_control *ctrl =
      (struct uvc_streaming_control *)&resp->data;
  struct uvc_streaming_control *target =
      (cs == UVC_VS_PROBE_CONTROL) ? &(uvc_dev.probe) : &(uvc_dev.commit);

  int ctrl_length = sizeof *ctrl;
  resp->length = ctrl_length;

  switch (req) {
  case UVC_SET_CUR:
    uvc_dev.control = cs;
    resp->length = ctrl_length;
    break;

  case UVC_GET_MAX:
    uvc_fill_streaming_control(ctrl, STREAM_CONTROL_MAX, 0, 0);
    break;

  case UVC_GET_CUR:
    memcpy(ctrl, target, ctrl_length);
    break;

  case UVC_GET_MIN:
  case UVC_GET_DEF:
    uvc_fill_streaming_control(ctrl, STREAM_CONTROL_MIN, 0, 0);
    break;

  case UVC_GET_RES:
    CLEAR(ctrl);
    break;

  case UVC_GET_LEN:
    resp->data[0] = 0x00;
    resp->data[1] = ctrl_length;
    resp->length = 2;
    break;

  case UVC_GET_INFO:
    resp->data[0] = (uint8_t)(UVC_CONTROL_CAP_GET | UVC_CONTROL_CAP_SET);
    resp->length = 1;
    break;
  }
}

static void uvc_events_process_class(struct usb_ctrlrequest *ctrl,
                                     struct uvc_request_data *resp) {
  uint8_t type = ctrl->wIndex & 0xff;
  uint8_t interface = ctrl->wIndex >> 8;
  uint8_t control = ctrl->wValue >> 8;
  uint8_t length = ctrl->wLength;

  if ((ctrl->bRequestType & USB_RECIP_MASK) != USB_RECIP_INTERFACE) {
    return;
  }

  switch (type) {
  case UVC_INTF_CONTROL:
    switch (interface) {
    case 0:
      if (control == UVC_VC_REQUEST_ERROR_CODE_CONTROL) {
        resp->data[0] = uvc_dev.request_error_code;
        resp->length = 1;
      }
      break;

    case 1:
      uvc_interface_control(UVC_VC_INPUT_TERMINAL, ctrl->bRequest, control,
                            length, resp);
      break;

    case 2:
      uvc_interface_control(UVC_VC_PROCESSING_UNIT, ctrl->bRequest, control,
                            length, resp);
      break;

    default:
      break;
    }
    break;

  case UVC_INTF_STREAMING:
    uvc_events_process_streaming(ctrl->bRequest, control, resp);
    break;

  default:
    break;
  }
}

static void uvc_events_process_setup(struct usb_ctrlrequest *ctrl,
                                     struct uvc_request_data *resp) {
  uvc_dev.control = 0;
  if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_CLASS) {
    uvc_events_process_class(ctrl, resp);
  }

  if (ioctl(uvc_dev.fd, UVCIOC_SEND_RESPONSE, resp) < 0) {
    printf("UVCIOC_SEND_RESPONSE failed: %s (%d)\n", strerror(errno), errno);
  }
}

static void
uvc_events_process_data_control(struct uvc_request_data *data,
                                struct uvc_streaming_control *target) {
  struct uvc_streaming_control *ctrl =
      (struct uvc_streaming_control *)&data->data;
  unsigned int iformat = (unsigned int)ctrl->bFormatIndex;
  unsigned int iframe = (unsigned int)ctrl->bFrameIndex;

  uvc_fill_streaming_control(target, STREAM_CONTROL_SET, iformat, iframe);
}

static void uvc_events_process_data(struct uvc_request_data *data) {
  printf("UVC: Control %s, length: %d\n",
         uvc_vs_interface_control_name(uvc_dev.control), data->length);

  switch (uvc_dev.control) {
  case UVC_VS_PROBE_CONTROL:
    uvc_events_process_data_control(data, &(uvc_dev.probe));
    break;

  case UVC_VS_COMMIT_CONTROL:
    uvc_events_process_data_control(data, &(uvc_dev.commit));
    break;

  case UVC_VS_CONTROL_UNDEFINED:
    /* Track the updated value in control_mapping for GET_CUR responses.
     * No V4L2 capture device to forward to — gphoto2 handles the camera. */
    if (data->length > 0 && data->length <= 4) {
      int i;
      for (i = 0; i < control_mapping_size; i++) {
        if (control_mapping[i].type == uvc_dev.control_interface &&
            control_mapping[i].uvc == uvc_dev.control_type &&
            control_mapping[i].enabled) {
          control_mapping[i].value = 0x00000000;
          control_mapping[i].length = data->length;
          memcpy(&control_mapping[i].value, data->data, data->length);
        }
      }
    }
    break;

  default:
    printf("UVC: Setting unknown control, length = %d\n", data->length);
    break;
  }
}

static void uvc_events_process() {
  struct v4l2_event v4l2_event;
  struct uvc_event *uvc_event = (void *)&v4l2_event.u.data;
  struct uvc_request_data resp;

  if (ioctl(uvc_dev.fd, VIDIOC_DQEVENT, &v4l2_event) < 0) {
    printf("%s: VIDIOC_DQEVENT failed: %s (%d)\n", uvc_dev.device_type_name,
           strerror(errno), errno);
    return;
  }

  CLEAR(resp);
  resp.length = -EL2HLT;

  switch (v4l2_event.type) {
  case UVC_EVENT_CONNECT:
    printf("%s: UVC_EVENT_CONNECT\n", uvc_dev.device_type_name);
    break;

  case UVC_EVENT_DISCONNECT:
    printf("%s: UVC_EVENT_DISCONNECT\n", uvc_dev.device_type_name);
    uvc_shutdown_requested = true;
    break;

  case UVC_EVENT_SETUP:
    uvc_events_process_setup(&uvc_event->req, &resp);
    break;

  case UVC_EVENT_DATA:
    uvc_events_process_data(&uvc_event->data);
    break;

  case UVC_EVENT_STREAMON:
    uvc_handle_streamon_event();
    break;

  case UVC_EVENT_STREAMOFF:
    uvc_handle_streamoff_event();
    break;

  default:
    break;
  }
}

static void uvc_events(int action) {
  struct v4l2_event_subscription sub;
  CLEAR(sub);

  sub.type = UVC_EVENT_CONNECT;
  ioctl(uvc_dev.fd, action, &sub);
  sub.type = UVC_EVENT_DISCONNECT;
  ioctl(uvc_dev.fd, action, &sub);
  sub.type = UVC_EVENT_SETUP;
  ioctl(uvc_dev.fd, action, &sub);
  sub.type = UVC_EVENT_DATA;
  ioctl(uvc_dev.fd, action, &sub);
  sub.type = UVC_EVENT_STREAMON;
  ioctl(uvc_dev.fd, action, &sub);
  sub.type = UVC_EVENT_STREAMOFF;
  ioctl(uvc_dev.fd, action, &sub);
}

static void uvc_events_subscribe() { uvc_events(VIDIOC_SUBSCRIBE_EVENT); }

static void uvc_events_unsubscribe() { uvc_events(VIDIOC_UNSUBSCRIBE_EVENT); }

/* ---------------------------------------------------------------------------
 * main
 */

/* Persistent capture thread.
 * Runs freely without holding the lock during USB I/O.  Only locks briefly
 * to publish the completed frame pointer and signal the UVC side.
 * This decouples capture latency from UVC delivery entirely.
 */
static void *capture_thread_func(void *arg) {
  (void)arg;

  while (!terminate && atomic_load(&is_camera_ready)) {
    int buf = cam_write_buf;

    /* --- slow USB transfer: NO lock held --- */
    int result = gp_camera_capture_preview(camera, cam_files[buf], context);

    if (result == GP_ERROR_CAMERA_BUSY) {
      nanosleep((const struct timespec[]){{0, 5000000LL}}, NULL); /* 5 ms */
      continue;
    }
    if (result < GP_OK) {
      printf("CAMERA: Failed to get preview (%i)\n", result);
      nanosleep((const struct timespec[]){{0, 10000000L}}, NULL);
      atomic_store(&is_camera_ready, 0);
      break;
    }

    result = gp_file_get_data_and_size(cam_files[buf], &cam_rawdata[buf],
                                       &cam_sizes[buf]);
    if (result < GP_OK) {
      printf("CAMERA: Failed to get frame data (%i)\n", result);
      nanosleep((const struct timespec[]){{0, 10000000L}}, NULL);
      atomic_store(&is_camera_ready, 0);
      break;
    }

    /* --- brief lock: flip indices, bump sequence, signal --- */
    pthread_mutex_lock(&lock);
    cam_ready_buf = buf;
    cam_write_buf = buf ^ 1;
    cam_frame_seq++;
    pthread_cond_signal(&cam_frame_cond);
    pthread_mutex_unlock(&lock);
  }

  return NULL;
}

/**
 * @brief Executes the Camera to UVC loop.
 *
 * A single persistent capture thread runs gp_camera_capture_preview
 * continuously, double-buffering frames so the UVC side never has to wait
 * for a USB transfer to complete.  select() uses a 100 ms timeout so the
 * loop always notices a terminate signal promptly.
 */
static void processing_loop_camera_uvc() {
  fd_set fdsu;
  struct timeval tv;
  int activity;

  printf("PROCESSING LOOP: CAMERA -> UVC\n");

  /* Start the persistent capture thread */
  pthread_t capture_t;
  if (pthread_create(&capture_t, NULL, capture_thread_func, NULL) != 0) {
    printf("PROCESSING: Failed to start capture thread\n");
    return;
  }

  while (!terminate) {
    if (!atomic_load(&is_camera_ready)) {
      break;
    }

    FD_ZERO(&fdsu);
    FD_SET(uvc_dev.fd, &fdsu);
    fd_set efds = fdsu;
    fd_set dfds = fdsu;

    /* 100 ms timeout so we wake up and check terminate even when idle */
    tv.tv_sec  = 0;
    tv.tv_usec = 100000;

    activity = select(uvc_dev.fd + 1, NULL, &dfds, &efds, &tv);

    if (activity == -1) {
      if (errno == EINTR) {
        continue;
      }
      printf("PROCESSING: Select error %d, %s\n", errno, strerror(errno));
      break;
    }

    if (activity == 0) {
      /* Timeout — loop back and check terminate */
      continue;
    }

    if (FD_ISSET(uvc_dev.fd, &efds)) {
      uvc_events_process();
    }

    if (FD_ISSET(uvc_dev.fd, &dfds)) {
      uvc_camera_video_process();
    }

    if (settings.show_fps) {
      struct timespec video_ts;
      clock_gettime(CLOCK_REALTIME, &video_ts);
      long long now = video_ts.tv_sec * 1000LL + (long long)(video_ts.tv_nsec * 1e-6);
      if (now - uvc_dev.last_time_video_process >= 1000) {
        printf("FPS: %d\n", uvc_dev.buffers_processed);
        uvc_dev.buffers_processed = 0;
        uvc_dev.last_time_video_process = now;
      }
    }
  }

  /* Wait for the capture thread to finish cleanly */
  pthread_join(capture_t, NULL);
}

/**
 * @brief Runs the relevant streaming loop based on the settings.
 *
 * @return int the execution status.
 */
static int init() {
  printf("Initializing\n");
  int ret;

  memset(&uvc_dev, 0, sizeof(uvc_dev));

  streaming_status_enable();

  /* Open the UVC device. */
  ret = uvc_open(settings.uvc_devname, settings.nbufs);
  if (ret < 0) {
    goto err;
  }

  printf("Initializing UVC events\n");
  /* Init UVC events. */
  uvc_fill_streaming_control(&(uvc_dev.probe), STREAM_CONTROL_INIT, 0, 0);
  uvc_fill_streaming_control(&(uvc_dev.commit), STREAM_CONTROL_INIT, 0, 0);

  uvc_events_subscribe();

  processing_loop_camera_uvc();

  uvc_events_unsubscribe();

  printf("\n*** UVC GADGET SHUTDOWN ***\n");

  uvc_handle_streamoff_event();

  if (camera) {
    gp_camera_exit(camera, context);
    gp_file_free(cam_files[0]);
    gp_file_free(cam_files[1]);
    cam_files[0] = cam_files[1] = NULL;
  }

  uvc_close();
  printf("*** UVC GADGET EXIT ***\n");
  return 0;

/**
 * @brief Error exit: closes resources without double-freeing.
 */
err:
  uvc_close();
  if (camera) {
    gp_camera_exit(camera, context);
  }
  printf("*** UVC GADGET EXIT (error) ***\n");
  return 1;
}

static int find_text_pos(const char *s, const char *f) {
  char *p = strstr(s, f);
  return (p) ? p - s : 0;
}

static int configfs_read_value(const char *path) {
  char buf[20];
  int fd;
  int ret;

  fd = open(path, O_RDONLY);
  if (fd == -1) {
    return -ENOENT;
  }
  ret = read(fd, buf, 20);
  close(fd);

  if (ret < 0 || ret > 10) {
    return -ENODATA;
  }
  buf[ret] = '\0';
  return strtol(buf, NULL, 10);
}

static void set_uvc_format_index(enum usb_device_speed usb_speed,
                                 int video_format, unsigned int bFormatIndex) {
  int i;
  for (i = 0; i <= last_format_index; i++) {
    if (uvc_frame_format[i].usb_speed == usb_speed &&
        uvc_frame_format[i].video_format == video_format) {
      uvc_frame_format[i].bFormatIndex = bFormatIndex;
    }
  }
}

static void set_uvc_format_value(const char *key_word, unsigned int index,
                                 int value) {
  if (!strncmp(key_word, "dwDefaultFrameInterval", 22)) {
    uvc_frame_format[index].dwDefaultFrameInterval = value;

  } else if (!strncmp(key_word, "dwMaxVideoFrameBufferSize", 25)) {
    uvc_frame_format[index].dwMaxVideoFrameBufferSize = value;

  } else if (!strncmp(key_word, "dwMaxBitRate", 12)) {
    uvc_frame_format[index].dwMaxBitRate = value;

  } else if (!strncmp(key_word, "dwMinBitRate", 12)) {
    uvc_frame_format[index].dwMinBitRate = value;

  } else if (!strncmp(key_word, "wHeight", 7)) {
    uvc_frame_format[index].wHeight = value;

  } else if (!strncmp(key_word, "wWidth", 6)) {
    uvc_frame_format[index].wWidth = value;

  } else if (!strncmp(key_word, "bmCapabilities", 14)) {
    uvc_frame_format[index].bmCapabilities = value;

  } else if (!strncmp(key_word, "bFrameIndex", 11)) {
    uvc_frame_format[index].bFrameIndex = value;
  }
}

static int configfs_usb_speed(const char *speed) {
  if (!strncmp(speed, "fs", 2)) {
    return USB_SPEED_FULL;

  } else if (!strncmp(speed, "hs", 2)) {
    return USB_SPEED_HIGH;

  } else if (!strncmp(speed, "ss", 2)) {
    return USB_SPEED_SUPER;
  }
  return USB_SPEED_UNKNOWN;
}

static int configfs_video_format(const char *format) {
  if (!strncmp(format, "m", 1)) {
    return V4L2_PIX_FMT_MJPEG;

  } else if (!strncmp(format, "u", 1)) {
    return V4L2_PIX_FMT_YUYV;
  }
  return 0;
}

static void configfs_fill_formats(const char *path, const char *part) {
  int index = 0;
  int value = 0;
  enum usb_device_speed usb_speed;
  int video_format;
  const char *format_name;
  char *copy = strdup(part);
  char *token = strtok(copy, "/");
  char *array[10];

  while (token != NULL) {
    array[index++] = token;
    token = strtok(NULL, "/");
  }

  if (index > 3) {
    format_name = array[3];

    usb_speed = configfs_usb_speed(array[0]);
    if (usb_speed == USB_SPEED_UNKNOWN) {
      printf("CONFIGFS: Unsupported USB speed: (%s) %s\n", array[0], path);
      goto free;
    }

    video_format = configfs_video_format(array[2]);
    if (video_format == 0) {
      printf("CONFIGFS: Unsupported format: (%s) %s\n", array[2], path);
      goto free;
    }

    value = configfs_read_value(path);
    if (value < 0) {
      goto free;
    }

    if (!strncmp(array[index - 1], "bFormatIndex", 12)) {
      set_uvc_format_index(usb_speed, video_format, value);
      goto free;
    }

    if (index != 5) {
      goto free;
    }

    if (uvc_frame_format[last_format_index].usb_speed != usb_speed ||
        uvc_frame_format[last_format_index].video_format != video_format ||
        strncmp(uvc_frame_format[last_format_index].format_name, format_name,
                strlen(format_name))) {
      if (uvc_frame_format[last_format_index].defined) {
        last_format_index++;

        /* too much defined formats */
        if (last_format_index > 29) {
          goto free;
        }
      }

      uvc_frame_format[last_format_index].usb_speed = usb_speed;
      uvc_frame_format[last_format_index].video_format = video_format;
      uvc_frame_format[last_format_index].format_name = strdup(format_name);
      uvc_frame_format[last_format_index].defined = true;
    }

    set_uvc_format_value(array[index - 1], last_format_index, value);
  }

free:
  free(copy);
}

static void configfs_fill_streaming_params(const char *path, const char *part) {
  int value = configfs_read_value(path);

  /*
   * streaming_maxburst   0..15 (ss only)
   * streaming_maxpacket  1..1023 (fs), 1..3072 (hs/ss)
   * streaming_interval   1..16
   */

  if (!strncmp(part, "maxburst", 8)) {
    streaming_maxburst = clamp(value, 0, 15);

  } else if (!strncmp(part, "maxpacket", 9)) {
    streaming_maxpacket = clamp(value, 1, 3072);

  } else if (!strncmp(part, "interval", 8)) {
    streaming_interval = clamp(value, 1, 16);
  }
}

static int configfs_path_check(const char *fpath, const struct stat *sb,
                               int tflag) {
  int uvc = find_text_pos(fpath, "/uvc");
  int streaming = find_text_pos(fpath, "streaming/class/");
  int streaming_params = find_text_pos(fpath, "/streaming_");
  (void)(tflag); /* avoid warning: unused parameter 'tflag' */

  if (!S_ISDIR(sb->st_mode)) {
    if (streaming && uvc) {
      configfs_fill_formats(fpath, fpath + streaming + 16);

    } else if (streaming_params) {
      configfs_fill_streaming_params(fpath, fpath + streaming_params + 11);
    }
  }
  return 0;
}

static int configfs_get_uvc_settings() {
  int i;
  const char *configfs_path = "/sys/kernel/config/usb_gadget";

  printf("CONFIGFS: Initial path: %s\n", configfs_path);

  if (ftw(configfs_path, configfs_path_check, 20) == -1) {
    return -1;
  }

  if (!uvc_frame_format[0].defined) {
    return -1;
  }

  for (i = 0; i <= last_format_index; i++) {
    uvc_dump_frame_format(&uvc_frame_format[i], "CONFIGFS: UVC");
  }

  printf("CONFIGFS: STREAMING maxburst:  %d\n", streaming_maxburst);
  printf("CONFIGFS: STREAMING maxpacket: %d\n", streaming_maxpacket);
  printf("CONFIGFS: STREAMING interval:  %d\n", streaming_interval);

  return 0;
}

/**
 * @brief Prints the usage.
 *
 * @param executableName the name of the executable.
 */
static void usage(const char *executableName) {
  fprintf(stderr, "Usage: %s [options]\n", executableName);
  fprintf(stderr, "Available options are\n");
  fprintf(stderr, " -b value    Blink X times on startup (b/w 1 and 20 with "
                  "led0 or GPIO pin if defined)\n");
  fprintf(stderr, " -h          Print this help screen and exit\n");
  fprintf(stderr,
          " -l          Use onboard led0 for streaming status indication\n");
  fprintf(stderr, " -n value    Number of Video buffers (b/w 2 and 32)\n");
  fprintf(stderr,
          " -p value    GPIO pin number for streaming status indication\n");
  fprintf(stderr, " -u device   UVC Video Output device\n");
  fprintf(stderr, " -x          Show FPS information\n");
}

static void show_settings() {
  printf("SETTINGS: Number of buffers requested: %d\n", settings.nbufs);
  printf("SETTINGS: Show FPS: %s\n",
         (settings.show_fps) ? "ENABLED" : "DISABLED");
  if (settings.streaming_status_pin) {
    printf("SETTINGS: GPIO pin for streaming status: %s\n",
           settings.streaming_status_pin);
  } else {
    printf("SETTINGS: GPIO pin for streaming status: not set\n");
  }
  printf("SETTINGS: Onboard led0 used for streaming status: %s\n",
         (settings.streaming_status_onboard_enabled) ? "ENABLED" : "DISABLED");
  printf("SETTINGS: Blink on startup: %d times\n", settings.blink_on_startup);

  printf("SETTINGS: UVC device name: %s\n", settings.uvc_devname);
  printf("SETTINGS: Source is camera\n");
}

static void *camera_loop() {
  while (!terminate) {
    int is_cam_ready = atomic_load(&is_camera_ready);

    if (is_cam_ready) {
      sleep(2);
    } else {
      /* Camera is NOT ready */
      printf("CAMERA: TRYING TO START CAMERA\n");
      camera_setup_loop();
    }
  }

  return NULL;
}

static void *uvc_loop() {
  while (!terminate) {
    int is_cam_ready = atomic_load(&is_camera_ready);
    int is_init = atomic_load(&is_initialized);

    if (is_cam_ready && !is_init) {
      printf("UVC: READY TO STREAM\n");
      atomic_store(&is_initialized, 1);
      init();
    } else if (is_init) {
      atomic_store(&is_initialized, 0);
      printf("UVC: RESETTING STREAM\n");
    } else {
      /* Camera is NOT ready */
      sleep(1);
    }
  }

  return NULL;
}

int main(int argc, char *argv[]) {
  int ret;
  int opt;

  struct sched_param sp = { .sched_priority = 50 };
  if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0)
    printf("WARNING: Failed to set real-time priority: %s\n", strerror(errno));

  struct sigaction action;
  CLEAR(action);
  action.sa_handler = term;
  sigaction(SIGTERM, &action, NULL);
  sigaction(SIGINT, &action, NULL);

  ret = configfs_get_uvc_settings();
  if (ret < 0) {
    printf("ERROR: configfs settings for uvc gadget not found!\n");
    return 1;
  }

  while ((opt = getopt(argc, argv, "hlb:n:p:u:x")) != -1) {
    switch (opt) {
    case 'b':
      if (atoi(optarg) < 1 || atoi(optarg) > 20) {
        fprintf(stderr, "ERROR: Blink x times on startup\n");
        goto err;
      }
      settings.blink_on_startup = atoi(optarg);
      break;

    case 'h':
      usage(argv[0]);
      return 1;

    case 'l':
      settings.streaming_status_onboard = true;
      break;

    case 'n':
      if (atoi(optarg) < 1 || atoi(optarg) > 32) {
        fprintf(stderr, "ERROR: Number of Video buffers value out of range\n");
        goto err;
      }
      settings.nbufs = atoi(optarg);
      break;

    case 'p':
      settings.streaming_status_pin = optarg;
      break;

    case 'u':
      settings.uvc_devname = optarg;
      break;

    case 'x':
      settings.show_fps = true;
      break;

    default:
      printf("ERROR: Invalid option '-%c'\n", opt);
      goto err;
    }
  }

  int rc;
  pthread_t t1, t2;

  show_settings();

  if (pthread_mutex_init(&lock, NULL) != 0) {
    printf("mutex init has failed\n");
    return 1;
  }

  rc = pthread_create(&t1, NULL, camera_loop, NULL);
  if (rc != 0) {
    fprintf(stderr, "pthread camera_loop failed\n");
    return EXIT_FAILURE;
  }

  rc = pthread_create(&t2, NULL, uvc_loop, NULL);
  if (rc != 0) {
    fprintf(stderr, "pthread uvc_loop failed\n");
    return EXIT_FAILURE;
  }

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  pthread_mutex_destroy(&lock);
  return 0;

err:
  usage(argv[0]);
  return 1;
}
