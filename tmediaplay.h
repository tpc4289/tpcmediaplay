#ifndef TMEDIAPLAY_H
#define TMEDIAPLAY_H

#include <QWidget>


class TDemuxThread;
class TDecode;

QT_BEGIN_NAMESPACE
namespace Ui {
class tmediaplay;
}
QT_END_NAMESPACE

class tmediaplay : public QWidget
{
    Q_OBJECT

private:
    TDemuxThread *demuxThread;
    TDecode *videoDecode;
    TDecode *audioDecode;

public:
    explicit tmediaplay(QWidget *parent = nullptr);
    ~tmediaplay() override;
    //打开并播放（在 show 之后调用）。重复调用不支持换源，仅播一次
    void play(const QString &url);

private:
    Ui::tmediaplay *ui;
};
#endif // TMEDIAPLAY_H
