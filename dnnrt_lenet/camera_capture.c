#include "camera_capture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <nuttx/video/video.h>

// グローバル変数: キャプチャされたデータを格納
uint8_t capture_buffer[IMAGE_SIZE];

// スレッド関数: カメラからデータをキャプチャ
void *capture_thread_func(void *arg) {
  int ret, v_fd;
  struct v4l2_buffer v4l2_buf;

  // カメラデバイスの初期化
  ret = video_initialize("/dev/video");
  if (ret < 0) {
    printf("Failed to initialize video: errno = %d\n", errno);
    return NULL;
  }

  // デバイスを開く
  v_fd = open("/dev/video", O_RDWR);
  if (v_fd < 0) {
    printf("Failed to open video device: errno = %d\n", errno);
    return NULL;
  }

  // キャプチャ処理
  while (1) {
    // バッファからデータを取得
    memset(&v4l2_buf, 0, sizeof(v4l2_buf));
    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_buf.memory = V4L2_MEMORY_USERPTR;

    ret = ioctl(v_fd, VIDIOC_DQBUF, (unsigned long)&v4l2_buf);
    if (ret < 0) {
      printf("Failed to dequeue buffer: errno = %d\n", errno);
      break;
    }

    // キャプチャデータをコピー
    memcpy(capture_buffer, (void *)v4l2_buf.m.userptr, IMAGE_SIZE);

    // バッファを再キュー
    ret = ioctl(v_fd, VIDIOC_QBUF, (unsigned long)&v4l2_buf);
    if (ret < 0) {
      printf("Failed to requeue buffer: errno = %d\n", errno);
      break;
    }

    // 1秒待機 (次のキャプチャまで)
    sleep(1);
  }

  close(v_fd);
  return NULL;
}
