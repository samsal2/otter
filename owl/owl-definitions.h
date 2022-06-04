#ifndef OWL_DEFINITIONS_H_
#define OWL_DEFINITIONS_H_

#include <stdint.h>

#if defined(__cplusplus)
#define OWL_BEGIN_DECLS extern "C" {
#define OWL_END_DECLS }
#else
#define OWL_BEGIN_DECLS
#define OWL_END_DECLS
#endif

OWL_BEGIN_DECLS

#define owl_public extern
#define owl_global static
#define owl_private static
#define owl_local_persist static

typedef float owl_v2[2];
typedef float owl_v3[3];
typedef float owl_v4[4];
typedef float owl_q4[4];

typedef int32_t owl_v2i[2];
typedef int32_t owl_v3i[3];
typedef int32_t owl_v4i[4];

typedef owl_v2 owl_m2[2];
typedef owl_v3 owl_m3[3];
typedef owl_v4 owl_m4[4];

typedef int owl_code;

#define OWL_OK 0
#define OWL_ERROR_FATAL -1
#define OWL_ERROR_NO_FRAME_MEMORY -2
#define OWL_ERROR_NO_UPLOAD_MEMORY -3
#define OWL_ERROR_NO_MEMORY -4
#define OWL_ERROR_NOT_FOUND -5
#define OWL_ERROR_NO_SPACE -6

#define OWL_NUM_IN_FLIGHT_FRAMES 3
#define OWL_MAX_SWAPCHAIN_IMAGES 8

OWL_END_DECLS

#endif
