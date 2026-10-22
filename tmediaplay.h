#ifndef TMEDIAPLAY_H
#define TMEDIAPLAY_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class tmediaplay;
}
QT_END_NAMESPACE

class tmediaplay : public QWidget
{
    Q_OBJECT

public:
    explicit tmediaplay(QWidget *parent = nullptr);
    ~tmediaplay() override;

private:
    Ui::tmediaplay *ui;
};
#endif // TMEDIAPLAY_H
