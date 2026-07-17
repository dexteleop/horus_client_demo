//
// Created by fcx
// 2025-10-09 18:02
//
#pragma once
#ifndef TALITE_XRSTREAM_INCLUDE
#define TALITE_XRSTREAM_INCLUDE

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(_WIN32)
	#ifdef TALITE_EXPORTS
		#define TALITE_API __declspec(dllexport)
	#else
		#define TALITE_API __declspec(dllimport)
	#endif
	#define DC_CALL __stdcall
#else
	#define TALITE_API __attribute__((visibility("default")))
	#define DC_CALL
#endif

#define TALITE_VERSION_MAJOR        1
#define TALITE_VERSION_MINOR	    0
#define TALITE_VERSION_REVISE		0
#define TALITE_VERSION_BUILD	    5

typedef void* TALITE_CONTEXT;

typedef enum talite_result_
{
    talite_result_success                 = 0,
    talite_result_invalid_param           = 1,
    talite_result_not_initialized         = 2,
    talite_result_not_connected           = 3,
    talite_result_transport_not_supported = 4,
    talite_result_already_active          = 5,
    talite_result_out_of_memory           = 6,
    talite_result_internal_error          = 7
} talite_result;

typedef struct talite_h265_stream_params_ {
    const uint8_t* data;
    size_t size;
    uint64_t timestamp_us;
    bool key_frame;
} talite_h265_stream_params;

typedef void (*talite_on_h265_stream)(
    const talite_h265_stream_params* params
);

typedef struct talite_device_info_ {
    const char* device_id;
    const char* device_name;
    const char* device_ip;
} talite_device_info;

typedef struct talite_device_list_ {
    const talite_device_info* devices;
    size_t device_count;
} talite_device_list;

typedef void (*talite_on_device_list)(
    const talite_device_list* device_list
);

typedef void (*talite_on_error)(
    TALITE_CONTEXT context,
    talite_result result,
    const char* message
);

typedef enum talite_data_type_ {
    talite_data_type_camera_calibration = 1
} talite_data_type;

typedef struct talite_float_array_ {
    const float* data;
    size_t size;
} talite_float_array;

typedef struct talite_int32_array_ {
    const int32_t* data;
    size_t size;
} talite_int32_array;

typedef struct talite_camera_calibration_ {
    talite_float_array r_raw_l_row;
    talite_float_array r_raw_r_row;
    talite_float_array k_l;
    talite_float_array k_r;
    talite_float_array d_l;
    talite_float_array d_r;
    talite_int32_array image_size;
    talite_float_array yaw_deg;
    talite_float_array pitch_deg;
} talite_camera_calibration;

typedef union talite_data_value_ {
    talite_camera_calibration camera_calibration;
} talite_data_value;

typedef struct talite_data_ {
    talite_data_type type;
    talite_data_value value;
} talite_data;

typedef void (*talite_on_data)(const talite_data* data);

TALITE_API TALITE_CONTEXT DC_CALL talite_create_context(void);

TALITE_API talite_result DC_CALL talite_stop(TALITE_CONTEXT context);

TALITE_API talite_result DC_CALL talite_disconnect(TALITE_CONTEXT context);

TALITE_API talite_result DC_CALL talite_destroy_context(TALITE_CONTEXT context);

TALITE_API talite_result DC_CALL talite_discover_devices(void);

TALITE_API talite_result DC_CALL talite_set_h265_stream_callback(
    TALITE_CONTEXT context,
    talite_on_h265_stream callback
);

TALITE_API talite_result DC_CALL talite_connect(
    TALITE_CONTEXT context,
    const char* streamer_ip
);

TALITE_API talite_result DC_CALL talite_set_error_callback(
    TALITE_CONTEXT context,
    talite_on_error callback
);

TALITE_API const char* DC_CALL talite_get_last_error_string(TALITE_CONTEXT context);

TALITE_API talite_result DC_CALL talite_set_data_callback(
    TALITE_CONTEXT context,
    talite_on_data callback
);

TALITE_API talite_result DC_CALL talite_set_device_list_callback(
    TALITE_CONTEXT context,
    talite_on_device_list callback
);

TALITE_API talite_result DC_CALL talite_send_data(
    TALITE_CONTEXT context,
    const uint8_t* data,
    size_t size
);

#ifdef __cplusplus
}
#endif

#endif // TALITE_XRSTREAM_INCLUDE
