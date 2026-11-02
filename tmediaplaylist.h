#ifndef TMEDIAPLAYLIST_H
#define TMEDIAPLAYLIST_H

#include <QLabel>
#include <QListWidgetItem>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QStandardItemModel>


/*
 * 播放器播放列表类
 * 实现播放列表的查看，文件的添加，删除，拖动等
 * 播放器可以根据播放列表实现单文件播放，循环播放等
 *
 */
class TMediaPlayList : public QWidget
{
    Q_OBJECT
public:
    explicit TMediaPlayList(QWidget *parent=nullptr);
    ~TMediaPlayList();

    QStringList playListFiles() const; //获取当前播放列表

    int rowCount() const; //列表文件个数
    int indexOfPath(const QString &path) const;//某文件在列表中的行号，找不到返回-1

    //把某行标记成“正在播放”（前缀符号+高亮色），row传-1清除所有标记
    void setCurrentPlayingRow(int row);
    int currentPlayingRow() const { return m_currentRow; }


    //向列表批量添加文件（打开文件按钮/拖文件进面板复用）
    void addFiles(const QStringList &files);

    //动画开关
    bool isSlideOut() const { return m_slideOut; }
    void slideOut();
    void slideIn(); //缩回右侧
    void slideToggle(); //切换


    //父窗口尺寸变化时重算停靠位置（收起态贴在右边界外）
    void reposition();

protected:
    void mousePressEvent(QMouseEvent *event) override; //吞掉面板内点击，避免误触发父窗口逻辑


private slots:
    void onAddFileBtnClicked();
    void onRemoveFileBtnClicked();
    void onUpFileBtnClicked();//播放文件上移
    void onDownFileBtnClicked(); //播放文件下移
    void onItemDoubleClicked(const QModelIndex &index); //播放文件双击



signals:
    void playFileRequeted(const QString &filePath);

private:

    static constexpr int PanelWidth = 270; //面板宽度（滑出动画与定位用）
    static constexpr int BottomReserved = 92; //底部预留：避开进度条+控制条

    QListView *playListView = nullptr; //播放列表
    QStandardItemModel *playListModel = nullptr;//列表关联模型，为后续关联本地文件列表做准备
    QPushButton *addFileBtn = nullptr;
    QPushButton *removeFileBtn = nullptr;
    QPushButton *upFileBtn = nullptr;
    QPushButton *downFileBtn = nullptr;
    QLabel *titleLab = nullptr;

    QPropertyAnimation *slideAnim = nullptr; //pos 属性动画：滑出/缩回

    bool m_slideOut = false;
    int m_currentRow = -1;


    void refreshTitle();
    void applyPlayingStyle(); //把当前播放行刷成高亮文本


    enum ItemRole{
        FilePathRole = Qt::UserRole
    };


};

#endif // TMEDIAPLAYLIST_H
