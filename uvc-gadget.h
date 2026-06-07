/*
 *	uvc_gadget.h  --  USB Video Class Gadget driver
 *
 *	Copyright (C) 2009-2010
 *	    Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 */

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/usb/ch9.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define max(a, b) (((a) > (b)) ? (a) : (b))

#define clamp(val, min, max)                        \
    ({                                              \
        typeof(val) __val = (val);                  \
        typeof(min) __min = (min);                  \
        typeof(max) __max = (max);                  \
        (void)(&__val == &__min);                   \
        (void)(&__val == &__max);                   \
        __val = __val < __min ? __min : __val;      \
        __val > __max ? __max : __val;              \
    })

#define ARRAY_SIZE(a) ((sizeof(a) / sizeof(a[0])))
#define pixfmtstr(x) (x) & 0xff, ((x) >> 8) & 0xff, ((x) >> 16) & 0xff, ((x) >> 24) & 0xff

enum gpio {
    GPIO_EXPORT = 0,
    GPIO_DIRECTION,
    GPIO_VALUE,
};

#define GPIO_DIRECTION_OUT "out"
#define GPIO_DIRECTION_IN "in"
#define GPIO_DIRECTION_LOW "low"
#define GPIO_DIRECTION_HIGH "high"

#define GPIO_VALUE_OFF "0"
#define GPIO_VALUE_ON "1"

enum leds {
    LED_TRIGGER = 1,
    LED_BRIGHTNESS,
};

#define LED_TRIGGER_NONE "none"
#define LED_BRIGHTNESS_LOW "0"
#define LED_BRIGHTNESS_HIGH "1"

#define UVC_EVENT_FIRST        (V4L2_EVENT_PRIVATE_START + 0)
#define UVC_EVENT_CONNECT      (V4L2_EVENT_PRIVATE_START + 0)
#define UVC_EVENT_DISCONNECT   (V4L2_EVENT_PRIVATE_START + 1)
#define UVC_EVENT_STREAMON     (V4L2_EVENT_PRIVATE_START + 2)
#define UVC_EVENT_STREAMOFF    (V4L2_EVENT_PRIVATE_START + 3)
#define UVC_EVENT_SETUP	       (V4L2_EVENT_PRIVATE_START + 4)
#define UVC_EVENT_DATA         (V4L2_EVENT_PRIVATE_START + 5)
#define UVC_EVENT_LAST         (V4L2_EVENT_PRIVATE_START + 5)

struct uvc_request_data
{
	__s32 length;
	__u8 data[60];
};

struct uvc_event
{
	union {
		enum usb_device_speed speed;
		struct usb_ctrlrequest req;
		struct uvc_request_data data;
	};
};

#define UVCIOC_SEND_RESPONSE		_IOW('U', 1, struct uvc_request_data)

#define UVC_INTF_CONTROL		0
#define UVC_INTF_STREAMING		1

// UVC - Request Error Code Control
#define REQEC_NO_ERROR 0x00
#define REQEC_NOT_READY 0x01
#define REQEC_WRONG_STATE 0x02
#define REQEC_POWER 0x03
#define REQEC_OUT_OF_RANGE 0x04
#define REQEC_INVALID_UNIT 0x05
#define REQEC_INVALID_CONTROL 0x06
#define REQEC_INVALID_REQUEST 0x07
#define REQEC_INVALID_VALUE 0x08

enum video_stream_action {
    STREAM_OFF,
    STREAM_ON,
};

enum stream_control_action {
    STREAM_CONTROL_INIT,
    STREAM_CONTROL_MIN,
    STREAM_CONTROL_MAX,
    STREAM_CONTROL_SET,
};

/* Buffer representing one video frame */
struct buffer {
    struct v4l2_buffer buf;
    void * start;
    size_t length;
};

/* ---------------------------------------------------------------------------
 * UVC specific stuff
 */

struct uvc_frame_format {
    bool defined;

    enum usb_device_speed usb_speed;
    int video_format;
    const char * format_name;

    unsigned int bFormatIndex;
    unsigned int bFrameIndex;

    unsigned int dwDefaultFrameInterval;
    unsigned int dwMaxVideoFrameBufferSize;
    unsigned int dwMaxBitRate;
    unsigned int dwMinBitRate;
    unsigned int wHeight;
    unsigned int wWidth;
    unsigned int bmCapabilities;
    
    unsigned int dwFrameInterval;
};

int last_format_index = 0;

struct uvc_frame_format uvc_frame_format[30];

enum uvc_frame_format_getter {
    FORMAT_INDEX_MIN,
    FORMAT_INDEX_MAX,
    FRAME_INDEX_MIN,
    FRAME_INDEX_MAX,
};

unsigned int streaming_maxburst = 0;
unsigned int streaming_maxpacket = 1023;
unsigned int streaming_interval = 1;

/* ---------------------------------------------------------------------------
 * UVC device instance
 */

bool uvc_shutdown_requested = false;

/* device type */
enum device_type {
    DEVICE_TYPE_UVC,
    DEVICE_TYPE_CAMERA
};

/* Represents the UVC output device */
struct uvc_device {
    enum device_type device_type;
    const char * device_type_name;

    /* device specific */
    int fd;
    int is_streaming;

    /* buffer specific */
    struct buffer * mem;
    unsigned int nbufs;
    unsigned int buffer_type;
    unsigned int memory_type;

    /* buffer queue and dequeue counters */
    unsigned long long int qbuf_count;
    unsigned long long int dqbuf_count;

    /* uvc specific */
    int run_standalone;
    int control;
    struct uvc_streaming_control probe;
    struct uvc_streaming_control commit;
    unsigned char request_error_code;
    unsigned int control_interface;
    unsigned int control_type;

    /* uvc specific flags */
    int uvc_shutdown_requested;

    struct buffer * dummy_buf;

    long long last_time_video_process;
    int buffers_processed;
};

static struct uvc_device uvc_dev;

struct uvc_settings {
    char * uvc_devname;
    unsigned int nbufs;
    bool show_fps;
    bool streaming_status_onboard;
    bool streaming_status_onboard_enabled;
    char * streaming_status_pin;
    bool streaming_status_enabled;
    unsigned int blink_on_startup;
};

struct uvc_settings settings = {
    .uvc_devname = "/dev/video1",
    .nbufs = 2,
    .show_fps = false,
    .streaming_status_onboard = false,
    .streaming_status_onboard_enabled = false,
    .streaming_status_enabled = false,
    .blink_on_startup = 0
};

struct control_mapping_pair {
    unsigned int type;
    unsigned int uvc;
    const char * uvc_name;
    unsigned int v4l2;
    const char * v4l2_name;
    bool enabled;
    unsigned int control_type;
    unsigned int value;
    unsigned int length;
    unsigned int minimum;
    unsigned int maximum;
    unsigned int step;
    unsigned int default_value;
};

struct control_mapping_pair control_mapping[] = {
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_BACKLIGHT_COMPENSATION_CONTROL,
		.uvc_name = "UVC_PU_BACKLIGHT_COMPENSATION_CONTROL",
        .v4l2 = V4L2_CID_BACKLIGHT_COMPENSATION,
        .v4l2_name = "V4L2_CID_BACKLIGHT_COMPENSATION",
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_BRIGHTNESS_CONTROL,
		.uvc_name = "UVC_PU_BRIGHTNESS_CONTROL",
        .v4l2 = V4L2_CID_BRIGHTNESS,
        .v4l2_name = "V4L2_CID_BRIGHTNESS"
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_CONTRAST_CONTROL,
		.uvc_name = "UVC_PU_CONTRAST_CONTROL",
        .v4l2 = V4L2_CID_CONTRAST,
        .v4l2_name = "V4L2_CID_CONTRAST",
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_GAIN_CONTROL,
		.uvc_name = "UVC_PU_GAIN_CONTROL",
        .v4l2 = V4L2_CID_GAIN,
        .v4l2_name = "V4L2_CID_GAIN",
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_POWER_LINE_FREQUENCY_CONTROL,
		.uvc_name = "UVC_PU_POWER_LINE_FREQUENCY_CONTROL",
        .v4l2 = V4L2_CID_POWER_LINE_FREQUENCY,
        .v4l2_name = "V4L2_CID_POWER_LINE_FREQUENCY",
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_HUE_CONTROL,
		.uvc_name = "UVC_PU_HUE_CONTROL",
        .v4l2 = V4L2_CID_HUE,
        .v4l2_name = "V4L2_CID_HUE",
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_SATURATION_CONTROL,
		.uvc_name = "UVC_PU_SATURATION_CONTROL",
        .v4l2 = V4L2_CID_SATURATION,
        .v4l2_name = "V4L2_CID_SATURATION",
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_SHARPNESS_CONTROL,
		.uvc_name = "UVC_PU_SHARPNESS_CONTROL",
        .v4l2 = V4L2_CID_SHARPNESS,
        .v4l2_name = "V4L2_CID_SHARPNESS",
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_GAMMA_CONTROL,
		.uvc_name = "UVC_PU_GAMMA_CONTROL",
        .v4l2 = V4L2_CID_GAMMA,
        .v4l2_name = "V4L2_CID_GAMMA",
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_WHITE_BALANCE_TEMPERATURE_CONTROL,
		.uvc_name = "UVC_PU_WHITE_BALANCE_TEMPERATURE_CONTROL",
        .v4l2 = V4L2_CID_WHITE_BALANCE_TEMPERATURE,
        .v4l2_name = "V4L2_CID_WHITE_BALANCE_TEMPERATURE",
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL,
		.uvc_name = "UVC_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL",
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_WHITE_BALANCE_COMPONENT_CONTROL,
		.uvc_name = "UVC_PU_WHITE_BALANCE_COMPONENT_CONTROL",
        .v4l2 = V4L2_CID_RED_BALANCE,
        .v4l2_name = "V4L2_CID_RED_BALANCE + V4L2_CID_BLUE_BALANCE"
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL,
		.uvc_name = "UVC_PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL",
        .v4l2 = V4L2_CID_AUTO_WHITE_BALANCE,
        .v4l2_name = "V4L2_CID_AUTO_WHITE_BALANCE",
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_DIGITAL_MULTIPLIER_CONTROL,
		.uvc_name = "UVC_PU_DIGITAL_MULTIPLIER_CONTROL",
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_DIGITAL_MULTIPLIER_LIMIT_CONTROL,
		.uvc_name = "UVC_PU_DIGITAL_MULTIPLIER_LIMIT_CONTROL",
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_HUE_AUTO_CONTROL,
		.uvc_name = "UVC_PU_HUE_AUTO_CONTROL",
        .v4l2 = V4L2_CID_HUE_AUTO,
        .v4l2_name = "V4L2_CID_HUE_AUTO",
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_ANALOG_VIDEO_STANDARD_CONTROL,
		.uvc_name = "UVC_PU_ANALOG_VIDEO_STANDARD_CONTROL",
	},
	{
        .type = UVC_VC_PROCESSING_UNIT,
		.uvc = UVC_PU_ANALOG_LOCK_STATUS_CONTROL,
		.uvc_name = "UVC_PU_ANALOG_LOCK_STATUS_CONTROL",
	},
	// {
    //     .type = UVC_VC_PROCESSING_UNIT,
	// 	.uvc = UVC_PU_CONTRAST_AUTO_CONTROL,
	// 	.uvc_name = "UVC_PU_CONTRAST_AUTO_CONTROL",
	// },
    {
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_CONTROL_UNDEFINED,
		.uvc_name = "UVC_CT_CONTROL_UNDEFINED",
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_SCANNING_MODE_CONTROL,
		.uvc_name = "UVC_CT_SCANNING_MODE_CONTROL",
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_AE_MODE_CONTROL,
		.uvc_name = "UVC_CT_AE_MODE_CONTROL",
        .v4l2 = V4L2_CID_EXPOSURE_AUTO,
        .v4l2_name = "V4L2_CID_EXPOSURE_AUTO"
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_AE_PRIORITY_CONTROL,
		.uvc_name = "UVC_CT_AE_PRIORITY_CONTROL",
        .v4l2 = V4L2_CID_EXPOSURE_AUTO_PRIORITY,
        .v4l2_name = "V4L2_CID_EXPOSURE_AUTO_PRIORITY"
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL,
		.uvc_name = "UVC_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL",
        .v4l2 = V4L2_CID_EXPOSURE_ABSOLUTE,
        .v4l2_name = "V4L2_CID_EXPOSURE_ABSOLUTE"
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_EXPOSURE_TIME_RELATIVE_CONTROL,
		.uvc_name = "UVC_CT_EXPOSURE_TIME_RELATIVE_CONTROL",
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_FOCUS_ABSOLUTE_CONTROL,
		.uvc_name = "UVC_CT_FOCUS_ABSOLUTE_CONTROL",
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_FOCUS_RELATIVE_CONTROL,
		.uvc_name = "UVC_CT_FOCUS_RELATIVE_CONTROL",
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_FOCUS_AUTO_CONTROL,
		.uvc_name = "UVC_CT_FOCUS_AUTO_CONTROL",
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_IRIS_ABSOLUTE_CONTROL,
		.uvc_name = "UVC_CT_IRIS_ABSOLUTE_CONTROL",
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_IRIS_RELATIVE_CONTROL,
		.uvc_name = "UVC_CT_IRIS_RELATIVE_CONTROL",
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_ZOOM_ABSOLUTE_CONTROL,
		.uvc_name = "UVC_CT_ZOOM_ABSOLUTE_CONTROL",
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_ZOOM_RELATIVE_CONTROL,
		.uvc_name = "UVC_CT_ZOOM_RELATIVE_CONTROL",
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_PANTILT_ABSOLUTE_CONTROL,
		.uvc_name = "UVC_CT_PANTILT_ABSOLUTE_CONTROL",
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_PANTILT_RELATIVE_CONTROL,
		.uvc_name = "UVC_CT_PANTILT_RELATIVE_CONTROL",
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_ROLL_ABSOLUTE_CONTROL,
		.uvc_name = "UVC_CT_ROLL_ABSOLUTE_CONTROL",
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_ROLL_RELATIVE_CONTROL,
		.uvc_name = "UVC_CT_ROLL_RELATIVE_CONTROL",
	},
	{
        .type = UVC_VC_INPUT_TERMINAL,
		.uvc = UVC_CT_PRIVACY_CONTROL,
		.uvc_name = "UVC_CT_PRIVACY_CONTROL",
	}
};

int control_mapping_size = sizeof(control_mapping) / sizeof(* control_mapping);

