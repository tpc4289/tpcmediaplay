#ifndef TAUDIOTHREAD_H
#define TAUDIOTHREAD_H

#include <QThread>

class TAudioThread : public QThread
{
    Q_OBJECT
public:
    explicit TAudioThread(QObject *parent = nullptr);
    ~TAudioThread();

protected:
    void run() override;
};

#endif // TAUDIOTHREAD_H
