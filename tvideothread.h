#ifndef TVIDEOTHREAD_H
#define TVIDEOTHREAD_H

#include <QThread>

class TVideoThread : public QThread
{
    Q_OBJECT
public:
    explicit TVideoThread(QObject *parent = nullptr);
    ~TVideoThread();
};

#endif // TVIDEOTHREAD_H
