#ifndef TMEDIAPLAYLIST_H
#define TMEDIAPLAYLIST_H

#include <QDialog>
#include <QListWidgetItem>
#include <QStandardItemModel>


/*
 * 播放器播放列表类
 * 实现播放列表的查看，文件的添加，删除，拖动等
 * 播放器可以根据播放列表实现单文件播放，循环播放等
 *
 */
class TMediaPlayList : public QDialog
{
    Q_OBJECT
public:
    explicit TMediaPlayList(QWidget *parent=nullptr);
    ~TMediaPlayList();

    QStringList playListFiles() const; //获取当前播放列表

private slots:
    void onAddFileBtnClicked();
    void onRemoveFileBtnClicked();
    void onUpFileBtnClicked();//播放文件上移
    void onDownFileBtnClicked(); //播放文件下移
    void onItemDoubleClicked(const QModelIndex &index); //播放文件双击

signals:
    void playFileRequeted(const QString &filePath);

private:
    QListView *playListView = nullptr; //播放列表
    QStandardItemModel *playListModel = nullptr;//列表关联模型，为后续关联本地文件列表做准备
    QPushButton *addFileBtn = nullptr;
    QPushButton *removeFileBtn = nullptr;
    QPushButton *upFileBtn = nullptr;
    QPushButton *downFileBtn = nullptr;


    enum ItemRole{
        FilePathRole = Qt::UserRole
    };


};

#endif // TMEDIAPLAYLIST_H
