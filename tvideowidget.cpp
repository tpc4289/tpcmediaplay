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
//     yuv.x = texture(tex_y, textureOut).r;
//     yuv.y = texture(tex_u, textureOut).r - 0.5;
//     yuv.z = texture(tex_v, textureOut).r - 0.5;

//     const mat3 yuv2rgb = mat3(
//         1.0,      1.0,      1.0,
//         0.0,     -0.34414,  1.772,
//         1.402,   -0.71414,  0.0
//     );
//     vec3 rgb = yuv2rgb * yuv;
//     fragColor = vec4(rgb, 1.0);
// }
// )";

//片元 shader：YUV -> RGB。
//uFullRange 决定用哪套矩阵：
//  sws_scale 默认不改变数据范围——源是 limited(16-235) 输出还是 limited，
//  源是 full(0-255)（游戏录制/屏幕录制常见）输出就是 full。
//  渲染端必须用与源一致的矩阵：full 数据套 limited 矩阵会死黑裁边，
//  limited 数据套 full 矩阵会整体发灰（本次多格式支持的常见问题）。
//色彩空间同样要匹配：手机/网络视频主流是 BT.709，老 DVD/广电素材是 BT.601；
//bt709 数据套 601 矩阵红蓝通道肉眼可辨偏色。uColorSpace: 0=BT.601, 1=BT.709
const char *texString = R"(
#version 330 core
in vec2 textureOut;
uniform sampler2D tex_y;
uniform sampler2D tex_u;
uniform sampler2D tex_v;
uniform int uFullRange;
uniform int uColorSpace;
out vec4 fragColor;
void main(void)
{
    float Y = texture(tex_y, textureOut).r * 255.0;
    float U = texture(tex_u, textureOut).r * 255.0 - 128.0;
    float V = texture(tex_v, textureOut).r * 255.0 - 128.0;

    //系数表按 (色彩空间 × 范围) 四种组合取
    float ky, kr, kb, gu, gv, bu, bv;
    if(uColorSpace == 1){
        //BT.709
        if(uFullRange == 1){
            ky = 1.0; kr = 1.2814; kb = 1.8556;
            gu = -0.1746; gv = -0.2966; bu = 0.5329; bv = 0.0;
        }else{
            ky = 1.1644; kr = 1.4746; kb = 1.8756;
            gu = -0.1618; gv = -0.5736; bu = 0.3045; bv = 0.0;
        }
    }else{
        //BT.601
        if(uFullRange == 1){
            ky = 1.0; kr = 1.402; kb = 1.772;
            gu = -0.344136; gv = -0.714136; bu = 0.0; bv = 0.0;
        }else{
            ky = 1.164383; kr = 1.596027; kb = 2.017232;
            gu = -0.391762; gv = -0.812968; bu = 0.0; bv = 0.0;
        }
    }
    float yy = (uFullRange == 1) ? Y : ky * (Y - 16.0);
    vec3 rgb = vec3(yy + kr * V,
                    yy + gu * U + gv * V,
                    yy + kb * U);
    fragColor = vec4(clamp(rgb / 255.0, 0.0, 1.0), 1.0);
}
)";







TVideoWidget::TVideoWidget(QWidget *parent) : QOpenGLWidget(parent) {

}

TVideoWidget::~TVideoWidget()
{
    freeDatas();
}

//把 4 个顶点的 uv 按旋转次数重排后重传 VBO。
//顶点顺序固定：BL(-1,-1) BR(1,-1) TL(-1,1) TR(1,1)
//显示=存储图像逆时针转 rotQ*90°，采样坐标即对顶点做顺时针旋转再取（推导）：
//  q0:(x,1-y)  q1:(y,x)  q2:(1-x,y)  q3:(1-y,1-x)
//（1-y 的翻转变量来自 GL 纹理 v=0 在数据首行即图像顶部）
void TVideoWidget::updateUvCoords(int rotateNum)
{
    //顶点 clip 坐标（固定）与对应显示归一化坐标 c=(x,y)：BL(0,0) BR(1,0) TL(0,1) TR(1,1)
    static const float corners[4][2] = {{0,0},{1,0},{0,1},{1,1}};
    static const float verts[4][2] = {{-1,-1},{1,-1},{-1,1},{1,1}};
    float data[16];
    for(int i = 0; i < 4; i++){
        const float x = corners[i][0], y = corners[i][1];
        float u, v;
        switch (rotateNum & 3) {
        case 1:  u = y;     v = x;     break;   //逆时针 90
        case 2:  u = 1-x;   v = y;     break;   //180
        case 3:  u = 1-y;   v = 1-x;   break;   //逆时针270(=顺时针90)
        default: u = x;     v = 1-y;   break;   //不转
        }
        data[i*4+0] = verts[i][0];
        data[i*4+1] = verts[i][1];
        data[i*4+2] = u;
        data[i*4+3] = v;
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void TVideoWidget::paintGL()
{
    //高 DPI 下 FBO 尺寸是逻辑尺寸 * devicePixelRatio，viewport 换算要带上
    const qreal dpr = devicePixelRatio();
    const int fbW = int(width() * dpr);
    const int fbH = int(height() * dpr);

    //先全屏黑色清掉 letterbox 区域
    glViewport(0, 0, fbW, fbH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
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

    //显示宽高比：解码线程已把 SAR（变形像素）与旋转（竖拍视频）折算进来；
    //缺失时按存储比例回退（90/270 旋转的素材存储比≠显示比，回退也要换轴）
    double ar;
    if(curAspect > 0)
        ar = curAspect;
    else if(curRotate == 1 || curRotate == 3)
        ar = double(videoHeight) / double(videoWidth);
    else
        ar = double(videoWidth) / double(videoHeight);
    const bool fullRange = curFullRange;
    //设置BT709值
    const bool is709 = curIs709;
    const int rotQ = curRotate;
    const int drawW = videoWidth, drawH = videoHeight;

    mux.unlock();

    //等比缩放 + 居中（letterbox）：窗口比画面更宽就顶底黑边，反之左右黑边
    double contentW, contentH;
    if(double(fbW) / double(fbH) > ar){
        contentH = fbH;
        contentW = fbH * ar;
    }else{
        contentW = fbW;
        contentH = fbW / ar;
    }
    glViewport(int((fbW - contentW) / 2.0), int((fbH - contentH) / 2.0),
               int(contentW), int(contentH));

    program.bind();

    if(rotQ != loaderRotate || vboDirty){
        updateUvCoords(rotQ);
        loaderRotate = rotQ;
        vboDirty = false;
    }


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

    if(uniFullRange >= 0)
        glUniform1i(uniFullRange, fullRange ? 1 : 0);
    //色彩空间
    if(uniColorSpace >= 0)
        glUniform1i(uniColorSpace, is709 ? 1 : 0);

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
    uniFullRange = program.uniformLocation("uFullRange");
    uniColorSpace = program.uniformLocation("uColorSpace");
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
    // static const GLfloat coords[] = {
    //     -1.0f, -1.0f,  0.0f, 1.0f,
    //     1.0f, -1.0f,  1.0f, 1.0f,
    //     -1.0f,  1.0f,  0.0f, 0.0f,
    //     1.0f,  1.0f,  1.0f, 0.0f,
    // };

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    updateUvCoords(0);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    // glBufferData(GL_ARRAY_BUFFER, sizeof(coords), coords, GL_STATIC_DRAW);

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
    Q_UNUSED(width);
    Q_UNUSED(height);
    //尺寸变化由 paintGL 里按 devicePixelRatio 重算 viewport，这里无需处理
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
void TVideoWidget::slotRepaint(QByteArray yuv, int width, int height, double dispAspect, int vRotate, bool fullRange, bool is709)
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

    //SAR/旋转/色彩范围随帧更新
    if(vRotate != curRotate)
        vboDirty = true;
    curAspect = dispAspect;
    curRotate = vRotate;
    curFullRange = fullRange;
    curIs709 = is709;

    mux.unlock();

    //由解码帧驱动刷新；paintGL 在主线程执行，一定有 GL 上下文
    update();
}
