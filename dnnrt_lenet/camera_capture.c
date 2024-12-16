
#include "camera_capture.h"

/****************************************************************************
 * Function Implementations
 ****************************************************************************/

int camera_prepare(int fd, enum v4l2_buf_type type, uint32_t buf_mode, uint32_t pixformat, uint16_t hsize, uint16_t vsize, FAR v_buffer_t **vbuf, uint8_t buffernum, int buffersize)
{
  int ret;
  struct v4l2_format fmt = {0};
  struct v4l2_requestbuffers req = {0};
  struct v4l2_buffer buf = {0};

  fmt.type = type;
  fmt.fmt.pix.width = hsize;
  fmt.fmt.pix.height = vsize;
  fmt.fmt.pix.field = V4L2_FIELD_ANY;
  fmt.fmt.pix.pixelformat = pixformat;

  ret = ioctl(fd, VIDIOC_S_FMT, (uintptr_t)&fmt);
  if (ret < 0)
  {
    printf("Failed to VIDIOC_S_FMT: errno = %d\n", errno);
    return ret;
  }

  req.type = type;
  req.memory = V4L2_MEMORY_USERPTR;
  req.count = buffernum;
  req.mode = buf_mode;

  ret = ioctl(fd, VIDIOC_REQBUFS, (uintptr_t)&req);
  if (ret < 0)
  {
    printf("Failed to VIDIOC_REQBUFS: errno = %d\n", errno);
    return ret;
  }

  *vbuf = malloc(sizeof(v_buffer_t) * buffernum);
  if (!(*vbuf))
  {
    printf("Out of memory for v_buffer array\n");
    return ERROR;
  }

  for (int cnt = 0; cnt < buffernum; cnt++)
  {
    (*vbuf)[cnt].length = buffersize;
    (*vbuf)[cnt].start = memalign(32, buffersize);
    if (!(*vbuf)[cnt].start)
    {
      printf("Out of memory for image buffer\n");
      free_buffer(*vbuf, cnt);
      return ERROR;
    }
  }

  for (int cnt = 0; cnt < buffernum; cnt++)
  {
    memset(&buf, 0, sizeof(buf));
    buf.type = type;
    buf.memory = V4L2_MEMORY_USERPTR;
    buf.index = cnt;
    buf.m.userptr = (uintptr_t)(*vbuf)[cnt].start;
    buf.length = (*vbuf)[cnt].length;

    ret = ioctl(fd, VIDIOC_QBUF, (uintptr_t)&buf);
    if (ret)
    {
      printf("Fail QBUF %d\n", errno);
      free_buffer(*vbuf, buffernum);
      return ERROR;
    }
  }

  ret = ioctl(fd, VIDIOC_STREAMON, (uintptr_t)&type);
  if (ret < 0)
  {
    printf("Failed to VIDIOC_STREAMON: errno = %d\n", errno);
    free_buffer(*vbuf, buffernum);
    return ret;
  }

  return OK;
}

void free_buffer(FAR v_buffer_t *buffers, uint8_t bufnum)
{
  if (buffers)
  {
    for (uint8_t cnt = 0; cnt < bufnum; cnt++)
    {
      if (buffers[cnt].start)
      {
        free(buffers[cnt].start);
      }
    }
    free(buffers);
  }
}

int get_camimage(int fd, FAR struct v4l2_buffer *v4l2_buf, enum v4l2_buf_type buf_type, void *dest_buffer, size_t dest_size)
{
  int ret;
  memset(v4l2_buf, 0, sizeof(struct v4l2_buffer));
  v4l2_buf->type = buf_type;
  v4l2_buf->memory = V4L2_MEMORY_USERPTR;

  ret = ioctl(fd, VIDIOC_DQBUF, (uintptr_t)v4l2_buf);
  if (ret)
  {
    printf("Fail DQBUF %d\n", errno);
    return ERROR;
  }

  if (v4l2_buf->length > dest_size)
  {
    printf("Destination buffer is too small\n");
    return ERROR;
  }

  memcpy(dest_buffer, (void *)v4l2_buf->m.userptr, v4l2_buf->length);
  return OK;
}

int release_camimage(int fd, FAR struct v4l2_buffer *v4l2_buf)
{
  int ret = ioctl(fd, VIDIOC_QBUF, (uintptr_t)v4l2_buf);
  if (ret)
  {
    printf("Fail QBUF %d\n", errno);
    return ERROR;
  }
  return OK;
}

/****************************************************************************
 * Main Function
 ****************************************************************************/

int main(void)
{
  int ret;
  int v_fd;
  struct v4l2_buffer v4l2_buf;
  v_buffer_t *buffers_video = NULL;
  void *dest_buffer;

  ret = video_initialize("/dev/video");
  if (ret != 0)
  {
    printf("ERROR: Failed to initialize video: errno = %d\n", errno);
    return ERROR;
  }

  v_fd = open("/dev/video", O_RDWR);
  if (v_fd < 0)
  {
    printf("ERROR: Failed to open video device: errno = %d\n", errno);
    return ERROR;
  }

  ret = camera_prepare(v_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_BUF_MODE_RING, V4L2_PIX_FMT_UYVY, VIDEO_HSIZE_QVGA, VIDEO_VSIZE_QVGA, &buffers_video, 3, IMAGE_RGB_SIZE);
  if (ret != OK)
  {
    close(v_fd);
    return ERROR;
  }

  dest_buffer = malloc(IMAGE_RGB_SIZE);
  if (!dest_buffer)
  {
    printf("ERROR: Failed to allocate memory for image buffer\n");
    free_buffer(buffers_video, 3);
    close(v_fd);
    return ERROR;
  }

  ret = get_camimage(v_fd, &v4l2_buf, V4L2_BUF_TYPE_VIDEO_CAPTURE, dest_buffer, IMAGE_RGB_SIZE);
  if (ret != OK)
  {
    free(dest_buffer);
    free_buffer(buffers_video, 3);
    close(v_fd);
    return ERROR;
  }

  printf("Captured image size: %d bytes\n", v4l2_buf.length);

  ret = release_camimage(v_fd, &v4l2_buf);
  if (ret != OK)
  {
    free(dest_buffer);
    free_buffer(buffers_video, 3);
    close(v_fd);
    return ERROR;
  }

  free(dest_buffer);
  free_buffer(buffers_video, 3);
  close(v_fd);

  return OK;
}
