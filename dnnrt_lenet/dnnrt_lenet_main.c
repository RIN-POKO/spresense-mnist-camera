#include "dnnrt_lenet_main.h"

/****************************************************************************
 * 関数: convert_datatype
 *
 * 画像データのデータ型を変換
 ****************************************************************************/
static void convert_datatype(dnn_runtime_t * rt)
{
  /* get datatype which this dnn_runtime_t expects */
  nn_variable_t *var = dnn_runtime_input_variable(rt, 0);
  float coefficient = (float)(1 << var->fp_pos);

  if (var->type == NN_DATA_TYPE_FLOAT)
    {
      /* do nothing since the image data is stored as float */
    }
  else if (var->type == NN_DATA_TYPE_INT16)
    {
      /* convert the image data in-place to 16-bit fixed-point values */
      int16_t *int16_buffer = (int16_t *) s_img_buffer;
      uint16_t px;
      for (px = 0u; px < MNIST_SIZE_PX; px++)
        {
          int16_buffer[px] = (int16_t) (coefficient * s_img_buffer[px]);
        }
    }
  else
    {
      /* convert the image data in-place to 8-bit fixed-point values */
      int8_t *int8_buffer = (int8_t *) s_img_buffer;
      uint16_t px;
      for (px = 0u; px < MNIST_SIZE_PX; px++)
        {
          int8_buffer[px] = (int8_t) (coefficient * s_img_buffer[px]);
        }
    }
}

/****************************************************************************
 * 関数: normalize_image
 *
 * キャプチャデータを正規化して推論用バッファに格納
 ****************************************************************************/
static void normalize_image(float *normalized_buffer, FAR uint8_t *capture_data) {

  // //capture_data: 320x240の画像データの配列の大きさ
  // printf("capture_data: %d\n", sizeof(capture_data)/sizeof(uint8_t));


  imageproc_rect_t inrect = {
    .x1 = OFFSET_X,
    .y1 = OFFSET_Y,
    .x2 = OFFSET_X + CLIP_WIDTH - 1,
    .y2 = OFFSET_Y + CLIP_HEIGHT - 1
  };

  printf("start normalize_image\n");

  // クリップ用バッファの確保
  uint8_t *clip_buffer = (uint8_t *)malloc(MNIST_SIZE_PX * sizeof(uint16_t));
  if (!clip_buffer) {
    printf("clip_buffer allocation failed\n");
    return;
  }

  // 画像の切り抜きとリサイズ
  int ret = imageproc_clip_and_resize(capture_data, VIDEO_HSIZE_QVGA, VIDEO_VSIZE_QVGA,
                                      clip_buffer, DNN_WIDTH, DNN_HEIGHT, 16, &inrect);
  if (ret) {
    printf("Failed to clip and resize image: %d\n", ret);
    free(clip_buffer);
    return;
  }

  //  グレースケール画像のバッファを確保
  uint8_t *gray_buffer = (uint8_t *)malloc(MNIST_SIZE_PX * sizeof(uint8_t));
  if (!gray_buffer) {
    printf("gray_buffer allocation failed\n");
    free(clip_buffer); 
    return;
  }

  // カラー画像 (YUV422) から輝度成分 (Y) を抽出
  imageproc_convert_yuv2gray(clip_buffer, gray_buffer, DNN_WIDTH, DNN_HEIGHT);

  // 輝度成分を正規化
  for (int i = 0; i < MNIST_SIZE_PX; i++) {
    normalized_buffer[i] = (float)(gray_buffer[i]) / 255.0f;
  }

  // //for debug
  // for (int i = 0; i < DNN_HEIGHT; i++){
  //   for (int j = 0; j < DNN_WIDTH; j++){
  //     printf("%d ", gray_buffer[i * DNN_WIDTH + j]);
  //   }
  //   printf("\n");
  // }

  free(gray_buffer);
  free(clip_buffer);
}

/****************************************************************************
 * 関数: main
 *
 * メイン関数
 ****************************************************************************/
int main(int argc, char *argv[]) {
  int ret;
  float *output_buffer;
  float proc_time;
  dnn_runtime_t rt;
  dnn_config_t config = {.cpu_num = 1};
  void *inputs[1] = {s_img_buffer};
  nn_network_t *network;
  struct timeval begin, end;

  int v_fd;
  struct v4l2_buffer v4l2_buf;
  v_buffer_t *buffers_video = NULL;

  // ニューラルネットワークモデルのロード
  printf("Loading neural network model: %s\n", DNN_NNB_PATH);
  network = alloc_nnb_network(DNN_NNB_PATH);
  if (!network) {
    printf("load nnb file failed\n");
    return -1;
  }

  // すべてのDNNサブシステムの初期化
  ret = dnn_initialize(&config);
  if (ret) {
    printf("dnn_initialize() failed due to %d", ret);
    destroy_nnb_network(network);
    return -1;
  }

  // DNNランタイムの初期化  
  ret = dnn_runtime_initialize(&rt, network);
  if (ret) {
    printf("dnn_runtime_initialize() failed due to %d\n", ret);
    dnn_finalize();
    destroy_nnb_network(network);
    return -1;
  }

  // カメラの初期化
  ret = video_initialize("/dev/video");
  if (ret != 0) {
    printf("ERROR: Failed to initialize video: errno = %d\n", errno);
    return -1;
  }

  /* convert the image data to datatype this dnn_runtime_t expects */
  convert_datatype(&rt);

  v_fd = open("/dev/video", 0);
  if (v_fd < 0) {
    printf("ERROR: Failed to open video device: errno = %d\n", errno);
    return -1;
  }

  ret = camera_prepare(v_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_BUF_MODE_RING,
                       V4L2_PIX_FMT_UYVY, VIDEO_HSIZE_QVGA, VIDEO_VSIZE_QVGA,
                       &buffers_video, 3, IMAGE_RGB_SIZE);
  if (ret != OK) {
    close(v_fd);
    return -1;
  }

  imageproc_initialize();  // 画像処理ライブラリの初期化

  // 推論ループ
  printf("Starting inference...\n");
  while (run_inference) {
    // キャプチャデータの取得
    ret = get_camimage(v_fd, &v4l2_buf, V4L2_BUF_TYPE_VIDEO_CAPTURE);
    if (ret != OK) break;

    // キャプチャデータを正規化
    normalize_image(s_img_buffer, (uint8_t *)v4l2_buf.m.userptr);

    ret = release_camimage(v_fd, &v4l2_buf);
    if (ret != OK) break;


    // 推論処理
    printf("Running inference...\n");
    gettimeofday(&begin, 0);
    ret = dnn_runtime_forward(&rt, inputs, 1);
    gettimeofday(&end, 0);
    if (ret) break;

    // 推論結果の出力
    float max_score = -1e10;
    int max_label = -1;
    output_buffer = dnn_runtime_output_buffer(&rt, 0);
    printf("Inference result:\n");
    for (int i = 0; i < 10; i++) {
      printf("output[%d]=%.6f\n", i, output_buffer[i]);
      if (output_buffer[i] > max_score) {
        max_label = i;
        max_score = output_buffer[i];
      }
    }
    if (max_score > 0.7f){
      printf("Predicted label: %d (score: %.6f)\n", max_label, max_score);
    }
    else{
      printf("Predicted label: None\n");
    }

    // 処理時間の表示
    proc_time = (float)(end.tv_sec - begin.tv_sec) + (float)(end.tv_usec - begin.tv_usec) / 1e6;
    printf("Inference time: %.3f seconds\n", proc_time);

    sleep(1);  // 次の推論まで待機
  }

  // 後処理
  printf("Shutting down...\n");
  dnn_runtime_finalize(&rt);
  dnn_finalize();
  destroy_nnb_network(network);
  imageproc_finalize();
  free_buffer(buffers_video, 3);
  close(v_fd);

  return 0;
}
