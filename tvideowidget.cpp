#include "tvideowidget.h"
#include <QOpenGLVertexArrayObject>



//自动加双引号
#define GET_STR(x) #x

// #define A_VER 3     //顶点属性在 shader 中的位置
// #define T_VER 4     //纹理属性在 shader 中的位置
#define A_VER 0     //顶点属性在 shader 中的位置
#define T_VER 1     //纹理属性在 shader 中的位置

//顶点shader
// const char * vexString = GET_STR(
//     attribute vec4 vertexIn;
//     attribute vec2 textureIn;
//     varying vec2 textureOut;
//     void main(void)
//     {
//         gl_Position = vertexIn;
//         textureOut = textureIn;
//     }
//     );
//优化顶点shader
const char *vexString = R"(
    #version 330 core
    layout(location = 0) in vec4 vertexIn;
    layout(location = 1) in vec2 textureIn;
    out vec2 textureOut;
    void main(void)
    {
        gl_Position = vertexIn;
        textureOut = textureIn;
    }
    )";
//片元 shader（BT.601 全范围 YUV -> RGB；数据来自 sws 转出的 YUV420P）
// const char *texString = GET_STR(
//     varying vec2 textureOut;
//     uniform sampler2D tex_y;
//     uniform sampler2D tex_u;
//     uniform sampler2D tex_v;
//     void main(void)
//     {
//         vec3 yuv;
//         vec3 rgb;
//         yuv.x = texture2D(tex_y, textureOut).r;
//         yuv.y = texture2D(tex_u, textureOut).r - 0.5;
//         yuv.z = texture2D(tex_v, textureOut).r - 0.5;
//         rgb = mat3(1.0, 1.0, 1.0,
//                    0.0, -0.39465, 2.03211,
//                    1.13983, -0.58060, 0.0) * yuv;
//         gl_FragColor = vec4(rgb, 1.0);
//     }
//     );

//在#verion后面移除precision mediump float;
// const char *texString = R"(
// #version 330 core
// in vec2 textureOut;
// uniform sampler2D tex_y;
// uniform sampler2D tex_u;
// uniform sampler2D tex_v;
// out vec4 fragColor;
// void main(void)
// {
//     vec3 yuv;
//     yuv.x = texture(tex_y, textureOut).r - 16.0 / 255.0;
//     yuv.y = texture(tex_u, textureOut).r - 0.5;
//     yuv.z = texture(tex_v, textureOut).r - 0.5;

//     const mat3 yuv2rgb = mat3(
//         1.164, 1.164, 1.164,
//         0.0,   -0.391, 2.017,
//         1.596, -0.813, 0.0
//     );
//     vec3 rgb = yuv2rgb * yuv;
//     fragColor = vec4(rgb, 1.0);
// }
// )";

//但是 `sws_scale` 默认输出 **full range（0~255）** 的 YUV！
//推荐，改 shader 适配 full range，sws 输出默认就是 full
const char *texString = R"(
#version 330 core
in vec2 textureOut;
uniform sampler2D tex_y;
uniform sampler2D tex_u;
uniform sampler2D tex_v;
out vec4 fragColor;
void main(void)
{
    vec3 yuv;
    yuv.x = texture(tex_y, textureOut).r;
    yuv.y = texture(tex_u, textureOut).r - 0.5;
    yuv.z = texture(tex_v, textureOut).r - 0.5;

    const mat3 yuv2rgb = mat3(
        1.0,      1.0,      1.0,
        0.0,     -0.34414,  1.772,
        1.402,   -0.71414,  0.0
    );
    vec3 rgb = yuv2rgb * yuv;
    fragColor = vec4(rgb, 1.0);
}
)";
TVideoWidget::TVideoWidget(QWidget *parent) : QOpenGLWidget(parent) {

}

TVideoWidget::~TVideoWidget()
{
    freeDatas();
}

void TVideoWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
    mux.lock();
    if(videoWidth <=0 || videoHeight <= 0){
        mux.unlock();
        return;
    }
    if (!texReady || texWidth != videoWidth || texHeigth != videoHeight)
        ensureTextures();

    //还没有任何一帧：只清屏
    if (curData.isEmpty() || !datas[0]) {
        mux.unlock();
        return;
    }

    //把最新帧从打包缓冲拆到三平面（swap 保证拷贝期间不被覆盖）
    // int cw = (videoWidth + 1) / 2, ch = (videoHeight + 1) / 2;
    int cw = videoWidth / 2, ch = videoHeight / 2;
    int ySize = videoWidth * videoHeight, uvSize = cw * ch;
    const uchar *src = (const uchar *)curData.constData();
    memcpy(datas[0], src, size_t(ySize));
    memcpy(datas[1], src + ySize, size_t(uvSize));
    memcpy(datas[2], src + ySize + uvSize, size_t(uvSize));

    mux.unlock();

    program.bind();

    glBindVertexArray(vao);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texs[0]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoWidth, videoHeight, GL_RED, GL_UNSIGNED_BYTE, datas[0]);
    glUniform1i(unis[0], 0);

    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, texs[1]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cw, ch, GL_RED, GL_UNSIGNED_BYTE, datas[1]);
    glUniform1i(unis[1], 1);

    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, texs[2]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cw, ch, GL_RED, GL_UNSIGNED_BYTE, datas[2]);
    glUniform1i(unis[2], 2);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    //画一帧解绑一帧
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    program.release();

    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
        qDebug() << "paintGL GL error:" << err;
}

//初始化上下文
void TVideoWidget::initializeGL()
{
    //初始化 opengl 函数指针
    initializeOpenGLFunctions();
    //只放不依赖视频尺寸的 GL 初始化
    qDebug() << "add frag shader:" << program.addShaderFromSourceCode(QOpenGLShader::Fragment, texString);
    qDebug() << "add vert shader:" << program.addShaderFromSourceCode(QOpenGLShader::Vertex, vexString);
    //必须在 link 之前绑定属性位置
    program.bindAttributeLocation("vertexIn", A_VER);
    program.bindAttributeLocation("textureIn", T_VER);
    if (!program.link()) {
        qDebug() << "program.link() FAILED:" << program.log();
        return;
    }
    qDebug() << "program.link() = true";
    //从 shader 获取 sampler 位置
    unis[0] = program.uniformLocation("tex_y");
    unis[1] = program.uniformLocation("tex_u");
    unis[2] = program.uniformLocation("tex_v");
    //用 VBO 上传坐标：立即模式（客户端指针）在 core profile 下非法，
    //Qt6 可能给到 core profile，那时 glDrawArrays 什么都不画 -> 黑屏
    // static const GLfloat coords[] = {
    //     //顶点
    //     -1.0f, -1.0f,
    //     1.0f, -1.0f,
    //     -1.0f,  1.0f,
    //     1.0f,  1.0f,
    //     //纹理坐标
    //     0.0f, 1.0f,
    //     1.0f, 1.0f,
    //     0.0f, 0.0f,
    //     1.0f, 0.0f
    // };
    //改成交叉模式
    // 交错布局：x,y,u,v 每 4 个 float 一组
    static const GLfloat coords[] = {
        -1.0f, -1.0f,  0.0f, 1.0f,
        1.0f, -1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 0.0f,
        1.0f,  1.0f,  1.0f, 0.0f,
    };

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(coords), coords, GL_STATIC_DRAW);

    //glVertexAttribPointer(A_VER, 2, GL_FLOAT, 0, 0, (const void *)0);
    //交叉模式下 stride 为 4 个 float，纹理坐标偏移 2 个 float
    glVertexAttribPointer(A_VER, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat), (const void *)0);
    glEnableVertexAttribArray(A_VER);

    //glVertexAttribPointer(T_VER, 2, GL_FLOAT, 0, 0,(const void *)(8 * sizeof(GLfloat)));
    glVertexAttribPointer(T_VER, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat), (const void *)(2*sizeof(GLfloat)));
    glEnableVertexAttribArray(T_VER);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    //纹理需要视频尺寸，延迟到 paintGL 创建
    texReady = false;
}

//只允许在 paintGL 中调用（有 current context）
void TVideoWidget::resizeGL(int width, int height)
{

}

void TVideoWidget::ensureTextures()
{
    if(texs[0]){
        glDeleteTextures(3, texs);
        texs[0] = texs[1] = texs[2] = 0;
    }
    glGenTextures(3, texs);
    for(int i = 0; i < 3; i++){
        // int w = i ? (videoWidth + 1) / 2 : videoWidth;
        // int h = i ? (videoHeight + 1) / 2 : videoHeight;
        int w = i ? videoWidth / 2 : videoWidth;
        int h = i ? videoHeight / 2 : videoHeight;
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, texs[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        //只分配显存，数据由每帧 glTexSubImage2D 填充
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
        qDebug() << "ensureTextures GL error:" << err;

    texWidth = videoWidth;
    texHeigth = videoHeight;
    texReady = true;
    qDebug() << "textures created, size" << videoWidth <<":"<< videoHeight;
}

void TVideoWidget::freeDatas()
{
    delete [] datas[0];
    delete [] datas[1];
    delete [] datas[2];
    datas[0] = datas[1] = datas[2] = nullptr;
    videoWidth = videoHeight = 0;
}

//槽函数，实现获取帧的数据的重绘
void TVideoWidget::slotRepaint(QByteArray yuv, int width, int height)
{
    if(yuv.isEmpty() || width <=0 || height <= 0){
        qDebug()<<"slotRepaint get bad frame: width is "<<width<<",height is "<<height<<", yuv size is "<<yuv.size();
        return;
    }
    // int cw = (width + 1) / 2;
    // int ch = (height + 1) / 2;
    int cw = width / 2;
    int ch = height / 2;
    int need = width * height + cw * ch * 2;
    if(yuv.size() < need){
        qDebug() << "slotRepaint data too small, yuv.size is "<< yuv.size() << ", need size is " << need;
        return;
    }

    mux.lock();
    //尺寸变化：重建 CPU 三平面缓存与纹理标记
    if (width != videoWidth || height != videoHeight || !datas[0]) {
        qDebug() << "video size ->" << width << height;
        freeDatas();
        videoWidth = width;
        videoHeight = height;
        datas[0] = new unsigned char[size_t(width) * height];
        datas[1] = new unsigned char[size_t(cw) * ch];
        datas[2] = new unsigned char[size_t(cw) * ch];
        texReady = false;
    }
    curData = std::move(yuv);
    curWidth = width;
    curHeight = height;

    mux.unlock();

    //由解码帧驱动刷新；paintGL 在主线程执行，一定有 GL 上下文
    update();
}
