#ifndef CAMERA_CAPTURE_H
#define CAMERA_CAPTURE_H

#include <nuttx/config.h>
#include <pthread.h>
#include <stdint.h>

// キャプチャ設定
#define IMAGE_WIDTH  28    // 推論に使用する画像の幅
#define IMAGE_HEIGHT 28    // 推論に使用する画像の高さ
#define IMAGE_SIZE   (IMAGE_WIDTH * IMAGE_HEIGHT) // ピクセル数

// キャプチャ用データバッファ
extern uint8_t capture_buffer[IMAGE_SIZE];

// スレッド関数
void *capture_thread_func(void *arg);

#endif // CAMERA_CAPTURE_H
