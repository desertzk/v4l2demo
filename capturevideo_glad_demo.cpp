// v4l2_glad_demo.cpp
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

#include <glad/glad.h>
#include <GLFW/glfw3.h>
//g++ capturevideo_glad_demo.cpp glad/src/glad.c -I./glad/include  -o v4l2_glad_demo     `pkg-config --cflags --libs glfw3` -lv4l2 -ldl

//
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

    // 1) Query capabilities
    v4l2_capability caps{};
    ioctl(v4l2_fd, VIDIOC_QUERYCAP, &caps);

    // 2) Set format (YUYV @ WIDTH×HEIGHT)
    v4l2_format fmt{};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = WIDTH;
    fmt.fmt.pix.height      = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
    ioctl(v4l2_fd, VIDIOC_S_FMT, &fmt);

    // 3) Request MMAP buffers
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

    // 4) Queue all buffers
    for (int i = 0; i < (int)buffers.size(); ++i) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        ioctl(v4l2_fd, VIDIOC_QBUF, &buf);
    }

    // 5) Start streaming
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(v4l2_fd, VIDIOC_STREAMON, &type);
}

// Simple YUYV→RGB conversion
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

// Grab one frame into rgb_buf
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
// === GLAD + GLFW + OPENGL 4.6 SETUP ===
//

// Compile a shader of given type
GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetShaderInfoLog(s,512,nullptr,buf);
        std::cerr<<"Shader compile error: "<<buf<<'\n';
        exit(-1);
    }
    return s;
}

// Link vertex+fragment into a program
GLuint linkProgram(const char* vs_src, const char* fs_src) {
    GLuint vs = compileShader(GL_VERTEX_SHADER,   vs_src);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fs_src);
    GLuint p  = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetProgramInfoLog(p,512,nullptr,buf);
        std::cerr<<"Link error: "<<buf<<'\n';
        exit(-1);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

int main(){
    // 1) V4L2 init
    init_v4l2();

    // 2) GLFW + GLAD init
    if (!glfwInit()) exit(-1);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE,        GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow* win = glfwCreateWindow(WIDTH, HEIGHT, "V4L2 + OpenGL 4.6", nullptr, nullptr);
    if (!win) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr<<"Failed to load OpenGL with GLAD\n"; return -1;
    }

    // 3) Build shaders
    const char* vs_src = R"GLSL(
        #version 460 core
        layout(location=0) in vec2 aPos;
        layout(location=1) in vec2 aUV;
        out vec2 vUV;
        void main(){
            vUV = aUV;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )GLSL";
    const char* fs_src = R"GLSL(
        #version 460 core
        in vec2 vUV;
        out vec4 FragColor;
        uniform sampler2D tex;
        void main(){
            FragColor = texture(tex, vUV);
        }
    )GLSL";
    GLuint program = linkProgram(vs_src, fs_src);

    // 4) Fullscreen quad setup
    float quad[] = {
        //  x,    y,    u,   v
        -1.0f,-1.0f, 0.0f,1.0f,
         1.0f,-1.0f, 1.0f,1.0f,
        -1.0f, 1.0f, 0.0f,0.0f,
         1.0f, 1.0f, 1.0f,0.0f,
    };
    GLuint VAO, VBO;
    glGenVertexArrays(1,&VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
      glBindBuffer(GL_ARRAY_BUFFER, VBO);
      glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float),(void*)0);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float),(void*)(2*sizeof(float)));
    glBindVertexArray(0);

    // 5) Create GL texture
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, WIDTH, HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    std::vector<uint8_t> rgb_buf(WIDTH*HEIGHT*3);

    // 6) Main loop
    while (!glfwWindowShouldClose(win)) {
        if (!grab_frame(rgb_buf)) break;

        // Upload new frame
        glBindTexture(GL_TEXTURE_2D, texID);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,WIDTH,HEIGHT,
                        GL_RGB,GL_UNSIGNED_BYTE,rgb_buf.data());

        // Render quad
        int w,h; glfwGetFramebufferSize(win,&w,&h);
        glViewport(0,0,w,h);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texID);
        glUniform1i(glGetUniformLocation(program,"tex"), 0);
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    // Cleanup (stream off, munmap, close, glfwTerminate)…
    return 0;
}
