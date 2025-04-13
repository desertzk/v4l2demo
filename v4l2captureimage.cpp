#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <cstdio>
#include <unistd.h>

int main() {
    const char* dev_name = "/dev/video0";
    const char* out_name = "output1.jpg";
    const int width = 1280;
    const int height = 720;

    // Open device
    int fd = open(dev_name, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    // Set video format
    v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("Failed to set format");
        close(fd);
        return 1;
    }

    // Request buffer
    v4l2_requestbuffers req = {};
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = 1;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("Failed to request buffers");
        close(fd);
        return 1;
    }

    // Query and map buffer
    v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;

    if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
        perror("Failed to query buffer");
        close(fd);
        return 1;
    }

    void* buffer = mmap(NULL, buf.length, 
                       PROT_READ | PROT_WRITE, MAP_SHARED,
                       fd, buf.m.offset);
    if (buffer == MAP_FAILED) {
        perror("Failed to map buffer");
        close(fd);
        return 1;
    }

    // Queue buffer
    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        perror("Failed to queue buffer");
        munmap(buffer, buf.length);
        close(fd);
        return 1;
    }

    // Start streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("Failed to start streaming");
        munmap(buffer, buf.length);
        close(fd);
        return 1;
    }

    // Dequeue buffer (capture frame)
    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("Failed to dequeue buffer");
        ioctl(fd, VIDIOC_STREAMOFF, &type);
        munmap(buffer, buf.length);
        close(fd);
        return 1;
    }

    // Write to file
    FILE* fp = fopen(out_name, "wb");
    if (!fp) {
        perror("Failed to open output file");
        ioctl(fd, VIDIOC_STREAMOFF, &type);
        munmap(buffer, buf.length);
        close(fd);
        return 1;
    }

    fwrite(buffer, buf.bytesused, 1, fp);
    fclose(fp);

    // Cleanup
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    munmap(buffer, buf.length);
    close(fd);

    printf("Image captured to %s\n", out_name);
    return 0;
}
