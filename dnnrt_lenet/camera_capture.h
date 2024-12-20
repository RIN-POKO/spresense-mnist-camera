#ifndef CAMERA_CAPTURE_H
#define CAMERA_CAPTURE_H

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <nuttx/video/video.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define IMAGE_RGB_SIZE  (VIDEO_HSIZE_QVGA * VIDEO_VSIZE_QVGA * 2)

/****************************************************************************
 * Global Variables
 ****************************************************************************/



/****************************************************************************
 * Types
 ****************************************************************************/

typedef struct v_buffer
{
  FAR uint32_t *start;
  uint32_t length;
} v_buffer_t;

/****************************************************************************
 * Function Prototypes
 ****************************************************************************/

int camera_prepare(int fd, enum v4l2_buf_type type, uint32_t buf_mode, uint32_t pixformat, uint16_t hsize, uint16_t vsize, FAR v_buffer_t **vbuf, uint8_t buffernum, int buffersize);
void free_buffer(FAR v_buffer_t *buffers, uint8_t bufnum);
int get_camimage(int fd, FAR struct v4l2_buffer *v4l2_buf, enum v4l2_buf_type buf_type);
int release_camimage(int fd, FAR struct v4l2_buffer *v4l2_buf);


#endif // CAMERA_CAPTURE_H
