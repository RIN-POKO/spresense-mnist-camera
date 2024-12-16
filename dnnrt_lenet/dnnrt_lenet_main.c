/****************************************************************************
 * dnnrt_lenet/dnnrt_lenet_main.c
 *
 *   Copyright 2018 Sony Corporation
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <nuttx/config.h>
#include <dnnrt/runtime.h>
#include <arch/board/cxd56_imageproc.h>
#include "loader_nnb.h"
#include "camera_capture.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
#define DNN_NNB_PATH "/mnt/sd0/lenet-5.nnb"
#define OFFSET_X  (104)
#define OFFSET_Y  (0)
#define CLIP_WIDTH (112)
#define CLIP_HEIGHT (224)
#define DNN_WIDTH  (28)
#define DNN_HEIGHT  (28)
#define MNIST_SIZE_PX DNN_WIDTH * DNN_HEIGHT

/****************************************************************************
 * Private Data
 ****************************************************************************/
static float s_img_buffer[MNIST_SIZE_PX];  // 推論用バッファ
static volatile bool run_inference = true;  // 推論ループの制御

/****************************************************************************
 * Private Functions
 ****************************************************************************/

// キャプチャデータを正規化して推論用バッファに格納
static void normalize_image(float *buffer, const uint16_t *capture_data) {
  imageproc_rect_t inrect;
  inrect.x1 = OFFSET_X;
  inrect.y1 = OFFSET_Y;
  inrect.x2 = OFFSET_X + CLIP_WIDTH - 1;
  inrect.y2 = OFFSET_Y + CLIP_HEIGHT - 1;
  int buffersize = MNIST_SIZE_PX * sizeof(uint16_t); // ピクセル数 × バイト数


  printf("start normalize_image\n");

  // 32バイトアラインメントでバッファ確保
  uint16_t *clip_buffer = NULL;
  if (posix_memalign((void **)&clip_buffer, 32, buffersize) != 0 || !clip_buffer) {
      printf("clip_buffer allocation failed\n");
      return;
  }

  printf("clip and resize\n");

  // 画像の切り抜きとリサイズ
  int ret = imageproc_clip_and_resize(capture_data, VIDEO_HSIZE_QVGA, VIDEO_VSIZE_QVGA,
                                      clip_buffer, DNN_WIDTH, DNN_HEIGHT, 16, &inrect);
  if (ret) {
    printf("Failed to clip and resize image: %d\n", ret);
    free(clip_buffer);
    return;
  }

  printf("monochrome and normalize\n");

  // 画像のモノクロ化と正規化
  for (int i = 0; i < MNIST_SIZE_PX; i++) {
    // 高ビットと低ビットからモノクロ値を作成し、正規化
    buffer[i] = (float)(((clip_buffer[i] & 0xf000) >> 8)   // 高ビットシフト
                      | ((clip_buffer[i] & 0x00f0) >> 4))  // 低ビットシフト
                / 255.0f;
  }

  // バッファ解放
  free(clip_buffer);
}

/****************************************************************************
 * Main Function
 ****************************************************************************/
int main(int argc, char *argv[]) {
  int ret;
  float *output_buffer;
  float proc_time;
  dnn_runtime_t rt;
  dnn_config_t config = {.cpu_num = 1};
  nn_network_t *network;
  struct timeval begin, end;
  pthread_t capture_thread;

  // ニューラルネットワークモデルのロード
  printf("Loading neural network model: %s\n", DNN_NNB_PATH);
  network = alloc_nnb_network(DNN_NNB_PATH);
  if (network == NULL) {
    printf("Failed to load network.\n");
    return -1;
  }

  // DNNランタイムの初期化
  ret = dnn_initialize(&config);
  if (ret) {
    printf("Failed to initialize DNN runtime: %d\n", ret);
    destroy_nnb_network(network);
    return -1;
  }

  // DNNランタイムオブジェクトの初期化
  ret = dnn_runtime_initialize(&rt, network);
  if (ret) {
    printf("Failed to initialize DNN runtime object: %d\n", ret);
    dnn_finalize();
    destroy_nnb_network(network);
    return -1;
  }

  // カメラの初期化
  ret = camera_prepare();
  if (ret) {
    printf("Failed to initialize camera: %d\n", ret);
    dnn_runtime_finalize(&rt);
    dnn_finalize();
    destroy_nnb_network(network);
    return -1;
  }

  // 画像処理ライブラリの初期化
  imageproc_initialize();

  // // キャプチャスレッドの作成
  // printf("Starting capture thread...\n");
  // ret = pthread_create(&capture_thread, NULL, capture_thread_func, NULL);
  // if (ret != 0) {
  //   printf("Failed to create capture thread: %d\n", ret);
  //   dnn_runtime_finalize(&rt);
  //   dnn_finalize();
  //   destroy_nnb_network(network);
  //   return -1;
  // }

  // 推論ループ
  printf("Starting inference...\n");
  while (run_inference) {
    capture_func(NULL);
    // キャプチャデータを正規化
    normalize_image(s_img_buffer, capture_buffer);

    // 推論処理
    printf("Running inference...\n");
    gettimeofday(&begin, 0);
    ret = dnn_runtime_forward(&rt, (const void **)&s_img_buffer, 1);
    gettimeofday(&end, 0);
    if (ret) {
      printf("Inference failed: %d\n", ret);
      break;
    }

    // 推論結果を取得
    output_buffer = dnn_runtime_output_buffer(&rt, 0);
    printf("Inference result:\n");
    for (int i = 0; i < 10; i++) {
      printf("output[%d]=%.6f\n", i, output_buffer[i]);
    }

    // 推論処理時間の表示
    proc_time = (float)end.tv_sec + (float)end.tv_usec / 1.0e6;
    proc_time -= (float)begin.tv_sec + (float)begin.tv_usec / 1.0e6;
    printf("Inference time: %.3f seconds\n", proc_time);

    // 次の推論まで1秒待機
    sleep(1);
  }

  // // キャプチャスレッドの終了
  // pthread_cancel(capture_thread);
  // pthread_join(capture_thread, NULL);

  // 後処理
  printf("Shutting down...\n");
  dnn_runtime_finalize(&rt);
  dnn_finalize();
  destroy_nnb_network(network);
  imageproc_finalize();

  return 0;
}
