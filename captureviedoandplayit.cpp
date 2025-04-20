#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <SDL2/SDL.h>
#include <vector>
#include <iostream>
//g++ -o v4l2_sdl_capture captureviedoandplayit.cpp -lv4l2 -lSDL2
struct Buffer { void* start; size_t length; };

static inline uint8_t clip(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

int main() {
    // 1) Open & query device
    int fd = open("/dev/video0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    v4l2_capability caps = {};
    ioctl(fd, VIDIOC_QUERYCAP, &caps);

    // 2) Set format
    v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 1280; fmt.fmt.pix.height = 720;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    ioctl(fd, VIDIOC_S_FMT, &fmt);

    // 3) Request & mmap buffers
    v4l2_requestbuffers req = {};
    req.count = 4; req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(fd, VIDIOC_REQBUFS, &req);
    std::vector<Buffer> bufs(req.count);
    for (auto i = 0u; i < req.count; ++i) {
        v4l2_buffer buf = {}; buf.type = req.type;
        buf.memory = req.memory; buf.index = i;
        ioctl(fd, VIDIOC_QUERYBUF, &buf);
        bufs[i].length = buf.length;
        bufs[i].start = mmap(nullptr, buf.length,
                             PROT_READ|PROT_WRITE,
                             MAP_SHARED, fd, buf.m.offset);
    }

    // 4) SDL init
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* win = SDL_CreateWindow("Capture",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        fmt.fmt.pix.width, fmt.fmt.pix.height, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, 0);
    SDL_Texture* tex = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
        fmt.fmt.pix.width, fmt.fmt.pix.height);

    // 5) Start capture
    for (uint32_t i = 0; i < bufs.size(); ++i) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        
        ioctl(fd, VIDIOC_QBUF, &buf);
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMON, &type);

    // 6) Capture & display loop
    while (true) {
        v4l2_buffer buf = { .type = req.type,
                            .memory = req.memory };
        ioctl(fd, VIDIOC_DQBUF, &buf);
        // Convert YUYV→RGB
        auto yuyv = (uint8_t*)bufs[buf.index].start;

        int width  = fmt.fmt.pix.width;
        int height = fmt.fmt.pix.height;
        int frame_bytes = width * height * 2;          // YUYV is 2 bytes per pixel
        std::vector<uint8_t> rgb(width * height * 3);  // 3 bytes per pixel for RGB

        for (int i = 0, j = 0; i < frame_bytes; i += 4) {
            int y0 = yuyv[i + 0];
            int u  = yuyv[i + 1];
            int y1 = yuyv[i + 2];
            int v  = yuyv[i + 3];

            // pixel 0
            rgb[j++] = clip((298*(y0-16) + 409*(v-128) + 128) >> 8);
            rgb[j++] = clip((298*(y0-16) - 100*(u-128) - 208*(v-128) + 128) >> 8);
            rgb[j++] = clip((298*(y0-16) + 516*(u-128) + 128) >> 8);
            // pixel 1
            rgb[j++] = clip((298*(y1-16) + 409*(v-128) + 128) >> 8);
            rgb[j++] = clip((298*(y1-16) - 100*(u-128) - 208*(v-128) + 128) >> 8);
            rgb[j++] = clip((298*(y1-16) + 516*(u-128) + 128) >> 8);
        }
        SDL_UpdateTexture(tex, nullptr, rgb.data(), 1280*3);

// create a YUY2 texture instead of RGB24
// SDL_Texture* tex = SDL_CreateTexture(
//     ren,
//     SDL_PIXELFORMAT_YUY2,
//     SDL_TEXTUREACCESS_STREAMING,
//     width, height);
// // in the loop, after DQBUF:
// SDL_UpdateTexture(tex, nullptr, yuyv, width * 2);


        
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
        ioctl(fd, VIDIOC_QBUF, &buf);
        SDL_Event e;
        if (SDL_PollEvent(&e) && e.type == SDL_QUIT) break;
    }

    // 7) Cleanup
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    close(fd);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
