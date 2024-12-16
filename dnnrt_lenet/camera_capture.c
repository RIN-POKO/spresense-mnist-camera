#include "camera_capture.h"

#define IMAGE_SIZE (VIDEO_HSIZE_QVGA * VIDEO_VSIZE_QVGA)  // QVGAサイズ、YUV422形式

struct v_buffer {
    uint16_t *start;  // バッファの開始アドレス
    uint32_t length;  // バッファの長さ（バイト単位）
};

typedef struct v_buffer v_buffer_t;

// グローバル変数
static v_buffer_t *buffers = NULL;  // バッファ配列
uint16_t *capture_buffer = NULL;   // キャプチャ用バッファ（動的確保）
static int buffernum = 3;          // バッファ数
static int fd = -1;                // デバイスハンドル
static volatile int capture_running = 1; // スレッド停止フラグ

// バッファを解放する関数
static void free_buffer(v_buffer_t *buffers, uint8_t bufnum) {
    if (buffers) {
        for (uint8_t cnt = 0; cnt < bufnum; cnt++) {
            if (buffers[cnt].start) {
                free(buffers[cnt].start);
            }
        }
        free(buffers);
    }
}

// キャプチャ停止関数
void stop_capture(pthread_t capture_thread) {
    capture_running = 0;
    pthread_join(capture_thread, NULL);  // スレッド終了待機
}

// カメラの準備関数
int camera_prepare(void) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    uint16_t hsize = VIDEO_HSIZE_QVGA;
    uint16_t vsize = VIDEO_VSIZE_QVGA;
    uint32_t pixformat = V4L2_PIX_FMT_UYVY;
    int buffersize = IMAGE_SIZE * sizeof(uint16_t); // ピクセル数 × バイト数
    int ret;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;

    // キャプチャバッファの動的確保
    if (posix_memalign((void **)&capture_buffer, 32, buffersize) != 0) {
        printf("Failed to allocate memory for capture_buffer\n");
        return -ENOMEM;
    }

    // カメラデバイスの初期化
    ret = video_initialize("/dev/video");
    if (ret < 0) {
        printf("Failed to initialize video: errno = %d\n", errno);
        free(capture_buffer);
        return ret;
    }

    // デバイスを開く
    fd = open("/dev/video", 0);
    if (fd < 0) {
        printf("Failed to open video device: errno = %d\n", errno);
        free(capture_buffer);
        return fd;
    }

    // フォーマット設定
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = type;
    fmt.fmt.pix.width = hsize;
    fmt.fmt.pix.height = vsize;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    fmt.fmt.pix.pixelformat = pixformat;

    ret = ioctl(fd, VIDIOC_S_FMT, (uintptr_t)&fmt);
    if (ret < 0) {
        printf("Failed to VIDIOC_S_FMT: errno = %d\n", errno);
        close(fd);
        free(capture_buffer);
        return ret;
    }

    // バッファリクエスト
    memset(&req, 0, sizeof(req));
    req.type = type;
    req.memory = V4L2_MEMORY_USERPTR;
    req.count = buffernum;
    req.mode = V4L2_BUF_MODE_RING;

    ret = ioctl(fd, VIDIOC_REQBUFS, (uintptr_t)&req);
    if (ret < 0) {
        printf("Failed to VIDIOC_REQBUFS: errno = %d\n", errno);
        close(fd);
        free(capture_buffer);
        return ret;
    }

    // バッファ配列の確保
    buffers = (v_buffer_t *)malloc(sizeof(v_buffer_t) * buffernum);
    if (!buffers) {
        printf("Failed to allocate buffers\n");
        close(fd);
        free(capture_buffer);
        return -ENOMEM;
    }

    // 各バッファの初期化
    for (int i = 0; i < buffernum; i++) {
        buffers[i].length = buffersize;

        // posix_memalignで32バイトアラインメントを確保
        if (posix_memalign((void **)&buffers[i].start, 32, buffers[i].length) != 0) {
            printf("Failed to allocate aligned buffer %d\n", i);
            free_buffer(buffers, buffernum);
            close(fd);
            free(capture_buffer);
            return -ENOMEM;
        }
    }

    // バッファをキューに登録
    for (int i = 0; i < buffernum; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = type;
        buf.memory = V4L2_MEMORY_USERPTR;
        buf.index = i;
        buf.m.userptr = (uintptr_t)buffers[i].start;
        buf.length = buffers[i].length;

        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            printf("Failed to queue buffer %d: errno = %d\n", i, errno);
            free_buffer(buffers, buffernum);
            close(fd);
            free(capture_buffer);
            return -EIO;
        }
    }

    // ストリームを開始
    ret = ioctl(fd, VIDIOC_STREAMON, (uintptr_t)&type);
    if (ret < 0) {
        printf("Failed to VIDIOC_STREAMON: errno = %d\n", errno);
        free_buffer(buffers, buffernum);
        close(fd);
        free(capture_buffer);
        return ret;
    }
    printf("Camera prepared\n");

    return 0;
}

// 非キャプチャスレッド関数
void *capture_func(void *arg) {
    struct v4l2_buffer buf;

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_USERPTR;

    // バッファをデキュー
    if (ioctl(fd, VIDIOC_DQBUF, (uintptr_t)&buf) < 0) {
        perror("Failed to dequeue buffer (VIDIOC_DQBUF)");
        return NULL;
    }

    // デキュー後のバッファ内容を検証
    printf("Dequeued buffer:\n");
    printf("  buf.index: %d\n", buf.index);
    printf("  buf.m.userptr: %p\n", (void *)buf.m.userptr);
    printf("  buf.bytesused: %d\n", buf.bytesused);
    printf("  buf.length: %d\n", buf.length);

        // バッファ検証
        if (buf.index < 0 || buf.index >= buffernum) {
            printf("Error: Invalid buffer index %d\n", buf.index);
            return NULL;
        }

        if ((uintptr_t)buf.m.userptr != (uintptr_t)buffers[buf.index].start) {
            printf("Error: Mismatched buffer address: buf.m.userptr=%p, expected=%p\n",
                   (void *)buf.m.userptr, (void *)buffers[buf.index].start);
            return NULL;
        }

        if (buf.bytesused > IMAGE_SIZE * sizeof(uint16_t)) {
            printf("Error: bytesused exceeds capture buffer size\n");
           return NULL;
        }

        // コピー前後のメモリ状態を確認
        printf("Before memcpy: buf.m.userptr: %p, capture_buffer: %p\n", 
               (void *)buf.m.userptr, (void *)capture_buffer);

    // デキューされたバッファのアドレスを検証
    bool valid_address = false;
    for (int i = 0; i < buffernum; i++) {
        if ((uintptr_t)buf.m.userptr == (uintptr_t)buffers[i].start) {
            valid_address = true;
            return NULL;
        }
    }
    if (!valid_address) {
        printf("Error: Dequeued buffer address is not valid\n");
        return NULL;
    }

    // データコピー前に範囲チェック
    if (buf.bytesused > IMAGE_SIZE * sizeof(uint16_t)) {
        printf("Error: bytesused exceeds capture buffer size\n");
        return NULL;
    }

    // データコピー
    memcpy(capture_buffer, (void *)buf.m.userptr, buf.bytesused);
    printf("Copied buffer data to capture_buffer\n");

    // バッファを再キュー
    if (ioctl(fd, VIDIOC_QBUF, (uintptr_t)&buf) < 0) {
        perror("Failed to requeue buffer (VIDIOC_QBUF)");
        return NULL;
    }

    printf("Buffer requeued successfully\n");
    return NULL;
}

void *capture_thread_func(void *arg) {
    struct v4l2_buffer buf;

    while (capture_running) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_USERPTR;

        // バッファをデキュー
        if (ioctl(fd, VIDIOC_DQBUF, (uintptr_t)&buf) < 0) {
            perror("Failed to dequeue buffer (VIDIOC_DQBUF)");
            break;  // デキューに失敗した場合、ループを終了
        }

        // デキュー後のバッファ内容を検証
        printf("Dequeued buffer:\n");
        printf("  buf.index: %d\n", buf.index);
        printf("  buf.m.userptr: %p\n", (void *)buf.m.userptr);
        printf("  buf.bytesused: %d\n", buf.bytesused);
        printf("  buf.length: %d\n", buf.length);

        if (buf.m.userptr == 0 || buf.length == 0) {
            printf("Error: Invalid buffer dequeued\n");
            break;  // 異常なバッファの場合、ループを終了
        }

        // デキューされたバッファのアドレスを検証
        bool valid_address = false;
        for (int i = 0; i < buffernum; i++) {
            if ((uintptr_t)buf.m.userptr == (uintptr_t)buffers[i].start) {
                valid_address = true;
                break;
            }
        }
        if (!valid_address) {
            printf("Error: Dequeued buffer address is not valid\n");
            break;  // 無効なアドレスの場合、ループを終了
        }

        // データコピー前に範囲チェック
        if (buf.bytesused > IMAGE_SIZE * sizeof(uint16_t)) {
            printf("Error: bytesused exceeds capture buffer size\n");
            break;  // データサイズが不正な場合、ループを終了
        }

        // データコピー
        memcpy(capture_buffer, (void *)buf.m.userptr, buf.bytesused);
        printf("Copied buffer data to capture_buffer\n");

        // 必要に応じてキャプチャデータを処理
        for (int i = 0; i < 16; i++) {
            printf("%04x ", capture_buffer[i]);  // デバッグ用に先頭16要素を出力
        }
        printf("...\n");

        // バッファを再キュー
        if (ioctl(fd, VIDIOC_QBUF, (uintptr_t)&buf) < 0) {
            perror("Failed to requeue buffer (VIDIOC_QBUF)");
            break;  // 再キューに失敗した場合、ループを終了
        }

        // 必要に応じてスリープ（省略可能）
        usleep(10000);  // 10msスリープ（必要に応じて調整）
    }

    printf("Capture thread exiting\n");
    return NULL;
}



// 解放関数
void camera_release(void) {
    if (buffers) {
        free_buffer(buffers, buffernum);
    }
    if (capture_buffer) {
        free(capture_buffer);
    }
    if (fd >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(fd, VIDIOC_STREAMOFF, &type); // ストリーム停止
        close(fd);
    }
}
