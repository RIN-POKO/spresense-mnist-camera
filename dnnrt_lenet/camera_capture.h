#ifndef CAMERA_CAPTURE_H
#define CAMERA_CAPTURE_H

#include <nuttx/config.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <nuttx/video/video.h>



#include <sched.h>
#include <nuttx/video/isx012.h>
#include <nuttx/video/isx019.h>
#include <arch/chip/cisif.h>



// キャプチャ設定
// #define IMAGE_WIDTH  28    // 推論に使用する画像の幅
// #define IMAGE_HEIGHT 28    // 推論に使用する画像の高さ
#define IMAGE_SIZE   (VIDEO_HSIZE_QVGA * VIDEO_VSIZE_QVGA) // ピクセル数

// キャプチャ用データバッファ
extern uint16_t *capture_buffer;

void stop_capture(pthread_t capture_thread);

int camera_prepare(void);

void camera_release(void);

// スレッド関数
void *capture_thread_func(void *arg);

// 非スレッド関数
void *capture_func(void *arg);

#endif // CAMERA_CAPTURE_H
