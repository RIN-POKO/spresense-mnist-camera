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
 * 定義
 ****************************************************************************/
#define DNN_NNB_PATH    "/mnt/sd0/lenet-5.nnb"
#define OFFSET_X        (104)
#define OFFSET_Y        (0)
#define CLIP_WIDTH      (112)
#define CLIP_HEIGHT     (224)
#define DNN_WIDTH       (28)
#define DNN_HEIGHT      (28)
#define MNIST_SIZE_PX   (DNN_WIDTH * DNN_HEIGHT)

/****************************************************************************
 * グローバル変数
 ****************************************************************************/
static float s_img_buffer[MNIST_SIZE_PX];  // 推論用バッファ
static volatile bool run_inference = true;  // 推論ループの制御

/****************************************************************************
 * 関数プロトタイプ
 ****************************************************************************/
static void convert_datatype(dnn_runtime_t * rt);
static void normalize_image(float *normalized_buffer, FAR uint8_t *capture_data);