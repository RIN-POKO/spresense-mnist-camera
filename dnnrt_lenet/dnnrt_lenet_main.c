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
#include "loader_nnb.h"
#include "camera_capture.h"  // キャプチャスレッド用ヘッダー

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
#define DNN_NNB_PATH "/mnt/sd0/lenet-5.nnb"
#define MNIST_SIZE_PX (28 * 28)

/****************************************************************************
 * Private Data
 ****************************************************************************/
static float s_img_buffer[MNIST_SIZE_PX];  // 推論用バッファ

/****************************************************************************
 * Private Functions
 ****************************************************************************/

// キャプチャデータを正規化して推論用バッファに格納
static void normalize_image(float *buffer, const uint8_t *capture_data) {
  for (int i = 0; i < MNIST_SIZE_PX; i++) {
    buffer[i] = (float)capture_data[i] / 255.0f;
  }
}


// int clipAndResizeImageByHW(CamImage& inputImg, CamImage& outputImg,
//                            int lefttop_x, int lefttop_y,
//                            int rightbottom_x, int rightbottom_y,
//                            int dst_width, int dst_height) {
//     // 入力画像が有効か確認
//     if (!inputImg.isAvailable()) {
//         return CAM_ERR_INVALID_PARAM;
//     }

//     // クリップ領域の幅と高さを計算
//     int clip_width = rightbottom_x - lefttop_x + 1;
//     int clip_height = rightbottom_y - lefttop_y + 1;

//     // 入力画像の範囲外か確認
//     if (lefttop_x < 0 || lefttop_y < 0 ||
//         rightbottom_x >= inputImg.getWidth() || rightbottom_y >= inputImg.getHeight() ||
//         clip_width <= 0 || clip_height <= 0) {
//         return CAM_ERR_INVALID_PARAM;
//     }

//     // 出力画像の準備
//     outputImg.setDimensions(dst_width, dst_height);

//     // クリップ領域の情報を構造体に格納
//     imageproc_rect_t rect = {lefttop_x, lefttop_y, rightbottom_x, rightbottom_y};

//     // 画像の切り抜きとリサイズを実行
//     int result = imageproc_clip_and_resize(
//         inputImg.getImgBuff(), inputImg.getWidth(), inputImg.getHeight(),
//         outputImg.getImgBuff(), dst_width, dst_height,
//         16, // ビット深度
//         &rect);

//     if (result != 0) {
//         return CAM_ERR_ILLEGAL_DEVERR;
//     }

//     return CAM_ERR_SUCCESS;
// }


/****************************************************************************
 * Main Function
 ****************************************************************************/
int main(int argc, char *argv[]) {
  int ret;
  unsigned char i;
  float *output_buffer, proc_time;
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

  ret = dnn_runtime_initialize(&rt, network);
  if (ret) {
    printf("Failed to initialize DNN runtime object: %d\n", ret);
    dnn_finalize();
    destroy_nnb_network(network);
    return -1;
  }

  // キャプチャスレッドの作成
  printf("Starting capture thread...\n");
  ret = pthread_create(&capture_thread, NULL, capture_thread_func, NULL);
  if (ret != 0) {
    printf("Failed to create capture thread: %d\n", ret);
    dnn_runtime_finalize(&rt);
    dnn_finalize();
    destroy_nnb_network(network);
    return -1;
  }

  // 推論ループ
  printf("Starting inference...\n");
  while (1) {
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
    for (i = 0; i < 10; i++) {
      printf("output[%u]=%.6f\n", i, output_buffer[i]);
    }

    // 推論処理時間の表示
    proc_time = (float)end.tv_sec + (float)end.tv_usec / 1.0e6;
    proc_time -= (float)begin.tv_sec + (float)begin.tv_usec / 1.0e6;
    printf("Inference time: %.3f seconds\n", proc_time);

    // 次の推論まで1秒待機
    sleep(1);
  }

  // キャプチャスレッドの終了
  pthread_cancel(capture_thread);
  pthread_join(capture_thread, NULL);

  // 後処理
  printf("Shutting down...\n");
  dnn_runtime_finalize(&rt);
  dnn_finalize();
  destroy_nnb_network(network);

  return 0;
}
