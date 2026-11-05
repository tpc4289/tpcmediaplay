#ifndef TVIDEOWIDGET_H
#define TVIDEOWIDGET_H

#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QMutex>


class TVideoWidget : public QOpenGLWidget, protected QOpenGLExtraFunctions
{
    Q_OBJECT
public:
    explicit TVideoWidget(QWidget *parent = nullptr);
    ~TVideoWidget();

    //重载openGL中的函数，实现图像相关的方法
protected:
    void paintGL() override;
    void initializeGL() override;
    void resizeGL(int width, int height) override;

private:

    QOpenGLShaderProgram program;
    //shader 中 y/u/v sampler 的位置
    GLuint unis[3] = {0, 0, 0};
    //色彩范围 uniform（1=full 0-255，0=limited 16-235）
    //色彩 uniform：范围（full/limited）与空间（BT.601/BT.709），四种组合选系数
    int uniFullRange = -1;
    int uniColorSpace = -1;
    //opengl 纹理 id
    GLuint texs[3] = {0, 0, 0};

    //顶点/纹理坐标 VAO
    GLuint vao = 0;
    //顶点/纹理坐标 VBO
    GLuint vbo = 0;

    //双缓冲：解码线程拷贝进来的一帧 + paintGL 正在显示的一帧
    QByteArray curData;             //最新收到的帧
    QByteArray drawData;            //paintGL 使用

    //最新收到的帧，paintGL使用，宽和高
    int curWidth = 0;
    int curHeight = 0;

    unsigned char *datas[3] = {nullptr, nullptr, nullptr}; //拆分后的三平面缓存

    //当前缓存对应的视频尺寸
    int videoWidth = 0;
    int videoHeight = 0;
    //已创建纹理对应的尺寸；与 vidW/vidH 不一致时需要在 paintGL 重建
    int texWidth = 0;
    int texHeigth = 0;
    bool texReady = false;

    //====== 显示几何/色彩状态（由送显帧携带，SAR+旋转+色彩范围修复新增） ======
    //显示宽高比（0 表示未知，按存储比例）
    double curAspect = 0;
    //逆时针 90° 旋转次数 0-3
    double curRotate = 0;
    //帧色彩范围：full(0-255)/limited(16-235)
    bool curFullRange = true;
    //帧色彩空间：true=BT.709，false=BT.601
    bool curIs709 = false;
    //旋转变化后需重传顶点缓冲
    bool vboDirty = true;
    int loaderRotate = -1;

    //按旋转次数重排 4 顶点的 uv（纹素坐标）
    void updateUvCoords(int rotQ);

    QMutex mux;


    //仅在 paintGL 内调用：此时一定有 current context
    void ensureTextures();
    void freeDatas();

public slots:
    //队列连接槽（主线程执行）：拷贝 YUV 数据并请求重绘
    void slotRepaint(QByteArray yuv, int width, int height, double dispAspect, int vRotate, bool fullRange, bool is709);


};

#endif // TVIDEOWIDGET_H
