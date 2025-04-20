// v4l2_sdl_glad_demo.cpp
#include <iostream>
#include <vector>
#include <array>
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include <SDL2/SDL.h>
#include <glad/glad.h>

//g++ capturevideo_sdlopengl_demo.cpp glad/src/glad.c -I./glad/include  -o v4l2_sdlopengl_demo \
    `pkg-config --cflags --libs sdl2` -lv4l2 -ldl
// === V4L2 VIDEO CAPTURE SETUP ===
//
const char* VIDEO_DEVICE = "/dev/video0";
const int WIDTH  = 640;
const int HEIGHT = 480;
const int NUM_BUFFERS = 4;

struct Buffer {
    void*  start;
    size_t length;
};
std::vector<Buffer> buffers;
int v4l2_fd = -1;

void init_v4l2() {
    v4l2_fd = open(VIDEO_DEVICE, O_RDWR);
    if (v4l2_fd < 0) { perror("Opening V4L2 device"); exit(EXIT_FAILURE); }

    v4l2_capability caps{};
    ioctl(v4l2_fd, VIDIOC_QUERYCAP, &caps);

    v4l2_format fmt{};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = WIDTH;
    fmt.fmt.pix.height      = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
    ioctl(v4l2_fd, VIDIOC_S_FMT, &fmt);

    v4l2_requestbuffers req{};
    req.count  = NUM_BUFFERS;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(v4l2_fd, VIDIOC_REQBUFS, &req);

    buffers.resize(req.count);
    for (int i = 0; i < (int)req.count; ++i) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        ioctl(v4l2_fd, VIDIOC_QUERYBUF, &buf);

        buffers[i].length = buf.length;
        buffers[i].start = mmap(nullptr, buf.length,
                                 PROT_READ | PROT_WRITE,
                                 MAP_SHARED, v4l2_fd, buf.m.offset);
    }

    for (int i = 0; i < (int)buffers.size(); ++i) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        ioctl(v4l2_fd, VIDIOC_QBUF, &buf);
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(v4l2_fd, VIDIOC_STREAMON, &type);
}

void yuyv_to_rgb(const uint8_t* yuyv, uint8_t* rgb) {
    for (int i = 0; i < WIDTH * HEIGHT * 2; i += 4) {
        int y0 = yuyv[i+0] << 8, u = yuyv[i+1] - 128;
        int y1 = yuyv[i+2] << 8, v = yuyv[i+3] - 128;
        auto conv = [&](int Y){
            int r = (Y + 359*v) >> 8;
            int g = (Y -  88*u - 183*v) >> 8;
            int b = (Y + 454*u) >> 8;
            return std::array<uint8_t,3>{
                (uint8_t)std::clamp(r,0,255),
                (uint8_t)std::clamp(g,0,255),
                (uint8_t)std::clamp(b,0,255)
            };
        };
        auto p0 = conv(y0), p1 = conv(y1);
        rgb[0]=p0[0]; rgb[1]=p0[1]; rgb[2]=p0[2];
        rgb[3]=p1[0]; rgb[4]=p1[1]; rgb[5]=p1[2];
        rgb += 6;
    }
}

bool grab_frame(std::vector<uint8_t>& rgb_buf) {
    v4l2_buffer buf{};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(v4l2_fd, VIDIOC_DQBUF, &buf) < 0) return false;
    yuyv_to_rgb((uint8_t*)buffers[buf.index].start, rgb_buf.data());
    ioctl(v4l2_fd, VIDIOC_QBUF, &buf);
    return true;
}

//
// === SHADERS, QUAD SETUP ===
//
GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetShaderInfoLog(s,512,nullptr,buf);
        std::cerr<<"Shader compile error: "<<buf<<"\n";
        exit(-1);
    }
    return s;
}

GLuint linkProgram(const char* vs, const char* fs) {
    GLuint V = compileShader(GL_VERTEX_SHADER,   vs);
    GLuint F = compileShader(GL_FRAGMENT_SHADER, fs);
    GLuint P = glCreateProgram();
    glAttachShader(P, V);
    glAttachShader(P, F);
    glLinkProgram(P);
    GLint ok; glGetProgramiv(P, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetProgramInfoLog(P,512,nullptr,buf);
        std::cerr<<"Link error: "<<buf<<"\n";
        exit(-1);
    }
    glDeleteShader(V);
    glDeleteShader(F);
    return P;
}

int main(){
    // 1) V4L2
    init_v4l2();

    // 2) SDL2 + GLAD
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr<<"SDL_Init Error: "<<SDL_GetError()<<"\n";
        return -1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,  SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,         SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

    SDL_Window* win = SDL_CreateWindow(
        "V4L2 + OpenGL 4.6 (SDL2)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIDTH, HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
    );
    if (!win) {
        std::cerr<<"SDL_CreateWindow Error: "<<SDL_GetError()<<"\n";
        return -1;
    }
    SDL_GLContext glctx = SDL_GL_CreateContext(win);
    if (!glctx) {
        std::cerr<<"SDL_GL_CreateContext Error: "<<SDL_GetError()<<"\n";
        return -1;
    }
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr<<"Failed to load OpenGL via GLAD\n";
        return -1;
    }

    // 3) Build shader program
    const char* vs_src = R"GLSL(
        #version 330 core
        layout(location=0) in vec2 aPos;
        layout(location=1) in vec2 aUV;
        out vec2 vUV;
        void main(){
            vUV = aUV;
            gl_Position = vec4(aPos,0,1);
        }
    )GLSL";
    const char* fs_src = R"GLSL(
        #version 330 core
        in vec2 vUV;
        out vec4 FragColor;
        uniform sampler2D tex;
        void main(){
            FragColor = texture(tex, vUV);
        }
    )GLSL";
    GLuint program = linkProgram(vs_src, fs_src);

    // 4) Quad VAO/VBO
    float quad[] = {
      -1,-1, 0,1,
       1,-1, 1,1,
      -1, 1, 0,0,
       1, 1, 1,0,
    };
    GLuint VAO, VBO;
    glGenVertexArrays(1,&VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
      glBindBuffer(GL_ARRAY_BUFFER, VBO);
      glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));
    glBindVertexArray(0);

    // 5) Texture
    GLuint texID;
    glGenTextures(1,&texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,WIDTH,HEIGHT,0,GL_RGB,GL_UNSIGNED_BYTE,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);

    std::vector<uint8_t> rgb_buf(WIDTH*HEIGHT*3);

    // 6) Main loop
    bool running = true;
    while(running){
        SDL_Event ev;
        while(SDL_PollEvent(&ev)){
            if(ev.type==SDL_QUIT) running=false;
        }

        if(!grab_frame(rgb_buf)) break;

        glBindTexture(GL_TEXTURE_2D, texID);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,WIDTH,HEIGHT,GL_RGB,GL_UNSIGNED_BYTE,rgb_buf.data());

        glViewport(0,0,WIDTH,HEIGHT);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texID);
        glUniform1i(glGetUniformLocation(program,"tex"),0);
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);

        SDL_GL_SwapWindow(win);
    }

    // Cleanup (omitted for brevity)...
    SDL_GL_DeleteContext(glctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
