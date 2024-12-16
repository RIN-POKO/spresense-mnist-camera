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
#define MNIST_SIZE_PX (DNN_WIDTH * DNN_HEIGHT)

/****************************************************************************
 * Private Data
 ****************************************************************************/
static float s_img_buffer[MNIST_SIZE_PX];  // 推論用バッファ
static volatile bool run_inference = true;  // 推論ループの制御

/****************************************************************************
 * Private Functions
 ****************************************************************************/

// キャプチャデータを正規化して推論用バッファに格納
static void normalize_image(float *buffer, uint8_t *capture_data) {
  imageproc_rect_t inrect;
  inrect.x1 = OFFSET_X;
  inrect.y1 = OFFSET_Y;
  inrect.x2 = OFFSET_X + CLIP_WIDTH - 1;
  inrect.y2 = OFFSET_Y + CLIP_HEIGHT - 1;
  int buffersize = MNIST_SIZE_PX * sizeof(uint16_t); // ピクセル数 × バイト数


  printf("start normalize_image\n");

  // 32バイトアラインメントでバッファを確保
  uint8_t *clip_buffer = (uint8_t *)memalign(32, buffersize);
  if (!clip_buffer) {
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
  // カラー画像 (YUV422) から輝度成分 (Y) を抽出して正規化
  for (int i = 0; i < MNIST_SIZE_PX; i++) {
      // YUV422形式: Y (偶数バイト)、U/V (奇数バイト)
      buffer[i] = (float)(clip_buffer[2 * i]) / 255.0f;
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


  int v_fd;
  struct v4l2_buffer v4l2_buf;
  v_buffer_t *buffers_video = NULL;
  void *dest_buffer;

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
  ret = video_initialize("/dev/video");
  if (ret != 0)
  {
    printf("ERROR: Failed to initialize video: errno = %d\n", errno);
    return ERROR;
  }

  v_fd = open("/dev/video", 0);
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

  // 画像処理ライブラリの初期化
  imageproc_initialize();

  // 推論ループ
  printf("Starting inference...\n");
  while (run_inference) {

  ret = get_camimage(v_fd, &v4l2_buf, V4L2_BUF_TYPE_VIDEO_CAPTURE, dest_buffer, IMAGE_RGB_SIZE);
  if (ret != OK)
  {
    free(dest_buffer);
    free_buffer(buffers_video, 3);
    close(v_fd);
    return ERROR;
  }    

    // キャプチャデータを正規化
    normalize_image(s_img_buffer, dest_buffer);


  ret = release_camimage(v_fd, &v4l2_buf);
  if (ret != OK)
  {
    free(dest_buffer);
    free_buffer(buffers_video, 3);
    close(v_fd);
    return ERROR;
  }
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

  // 後処理
  printf("Shutting down...\n");
  dnn_runtime_finalize(&rt);
  dnn_finalize();
  destroy_nnb_network(network);
  imageproc_finalize();
  free(dest_buffer);
  free_buffer(buffers_video, 3);
  close(v_fd);

  return 0;
}
