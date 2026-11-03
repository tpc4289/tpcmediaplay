#include "tmediaplaylist.h"

#include <QAbstractItemModel>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPushButton>
#include <QStandardItemModel>


/*
 * 播放器播放列表类
 * 实现播放列表的查看，文件的添加，删除，拖动等
 * 播放器可以根据播放列表实现单文件播放，循环播放等
 *
 */

//面板整体样式：深色半透明卡片风，与播放器控制条统一
static const char *kPanelStyle = R"(
TMediaPlayList {
    background-color: rgba(24, 26, 32, 235);
    border-left: 1px solid rgba(255,255,255,40);
}
QLabel#listTitle {
    color: #eaeaea;
    font-size: 14px;
    font-weight: bold;
    padding: 6px 2px;
}
QListView {
    background-color: rgba(0,0,0,0);
    border: none;
    outline: 0;
    color: #d5d7db;
    font-size: 12px;
}
QListView::item {
    padding: 7px 8px;
    border-radius: 6px;
    margin: 1px 4px;
}
QListView::item:hover {
    background-color: rgba(255,255,255,28);
}
QListView::item:selected {
    background-color: rgba(33,150,243,90);
    color: #ffffff;
}
QPushButton {
    color: #d5d7db;
    background-color: rgba(255,255,255,22);
    border: 1px solid rgba(255,255,255,28);
    border-radius: 6px;
    padding: 5px 0px;
    font-size: 12px;
}
QPushButton:hover {
    background-color: rgba(255,255,255,45);
    border-color: rgba(255,255,255,60);
}
QPushButton:pressed {
    background-color: rgba(33,150,243,120);
}
)";

TMediaPlayList::TMediaPlayList(QWidget *parent):QWidget(parent)
{
    setWindowTitle("播放列表");
    setFixedWidth(PanelWidth);
    setAutoFillBackground(true);
    setStyleSheet(QString::fromLatin1(kPanelStyle));
    //作为主窗口的覆盖子控件，默认不可见（收起态）
    hide();

    //顶部标题（显示文件数量）
    titleLab = new QLabel("播放列表 (0)", this);
    titleLab->setObjectName("listTitle");

    playListView = new QListView(this); //播放列表
    playListModel = new QStandardItemModel(this);
    playListView->setModel(playListModel);
    //playListView->setSelectionModel(QAbstractItemView::ExtendedSelection);

    // 启用多选
    playListView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    playListView->setSelectionBehavior(QAbstractItemView::SelectRows);

    //启用内部拖拽排序
    playListView->setDragEnabled(true);
    playListView->setAcceptDrops(true);
    playListView->setDragDropMode(QAbstractItemView::InternalMove);
    playListView->setDefaultDropAction(Qt::MoveAction);
    playListView->setDropIndicatorShown(true);
    playListView->setUniformItemSizes(true);
    playListView->setWordWrap(true);
    playListView->setEditTriggers(QAbstractItemView::NoEditTriggers); //禁止编辑

    //添加文件按钮
    addFileBtn = new QPushButton("添加",this);
    addFileBtn->setToolTip("向播放列表添加媒体文件");
    //删除文件按钮
    removeFileBtn = new QPushButton("移除",this);
    removeFileBtn->setToolTip("移除勾选的文件");
    //上移文件按钮
    upFileBtn = new QPushButton("上移",this);
    //下移文件按钮
    downFileBtn = new QPushButton("下移",this);

    //按钮水平分布
    QHBoxLayout *btnHBoxLay = new QHBoxLayout();
    btnHBoxLay->setContentsMargins(0, 0, 0, 0);
    btnHBoxLay->setSpacing(6);
    btnHBoxLay->addWidget(addFileBtn);
    btnHBoxLay->addWidget(removeFileBtn);
    btnHBoxLay->addWidget(upFileBtn);
    btnHBoxLay->addWidget(downFileBtn);


    //面板按钮不接受键盘焦点：否则点完"添加"后焦点留在按钮上，
    //再按空格会重复触发同一按钮（与主窗口控制条同一类误触问题）。
    //列表视图本身保留焦点，键盘上下选择文件不受影响
    for(QPushButton *b : {addFileBtn, removeFileBtn, upFileBtn, downFileBtn}){
        b->setFocusPolicy(Qt::NoFocus);
    }

    //主分布
    QVBoxLayout *mainVBoxLay = new QVBoxLayout(this);
    mainVBoxLay->setContentsMargins(10, 8, 10, 8);
    mainVBoxLay->setSpacing(6);
    mainVBoxLay->addWidget(titleLab);
    mainVBoxLay->addWidget(playListView, 1);
    mainVBoxLay->addLayout(btnHBoxLay);

    //滑出/缩回动画
    slideAnim = new QPropertyAnimation(this, "pos", this);
    slideAnim->setDuration(240);
    slideAnim->setEasingCurve(QEasingCurve::OutCubic);

    //添加事件响应
    connect(addFileBtn, &QPushButton::clicked, this, &TMediaPlayList::onAddFileBtnClicked);
    connect(removeFileBtn, &QPushButton::clicked, this, &TMediaPlayList::onRemoveFileBtnClicked);
    connect(upFileBtn, &QPushButton::clicked, this, &TMediaPlayList::onUpFileBtnClicked);
    connect(downFileBtn, &QPushButton::clicked, this, &TMediaPlayList::onDownFileBtnClicked);

    connect(playListView, &QListView::doubleClicked, this, &TMediaPlayList::onItemDoubleClicked);

    //行数变化（添加/移除/排序）时同步标题计数
    connect(playListModel, &QStandardItemModel::rowsInserted, this, [this]{ refreshTitle(); });
    connect(playListModel, &QStandardItemModel::rowsRemoved, this, [this]{ refreshTitle(); });
    connect(playListModel, &QStandardItemModel::rowsMoved, this, [this]{ refreshTitle(); applyPlayingStyle(); });

    reposition();

}

TMediaPlayList::~TMediaPlayList()
{

}

QStringList TMediaPlayList::playListFiles() const
{
    //按行序返回全部文件路径
    QStringList files;
    for(int i = 0; i < playListModel->rowCount(); ++i){
        QStandardItem *item = playListModel->item(i);
        if(item){
            QString path = item->data(FilePathRole).toString();
            if(!path.isEmpty())
                files.append(path);
        }
    }
    return files;
}

//返回列表行总数
int TMediaPlayList::rowCount() const
{
    return playListModel->rowCount();
}

//通过文件全路径名获取对应行数的索引
int TMediaPlayList::indexOfPath(const QString &path) const
{
    for(int i = 0; i < playListModel->rowCount(); i++){
        QStandardItem *item = playListModel->item(i);
        if(item && item->data(FilePathRole).toString() == path)
            return i;
    }
    return -1;
}


void TMediaPlayList::setCurrentPlayingRow(int row)
{
    //播放模式/当前项反馈，这里加高亮标记
    //上移/下移/拖拽重排/删除都会让行号漂移，存路径在任何重排后都能反查到正确行
    m_currentPath.clear();
    if(row >= 0 && row < playListModel->rowCount()){
        QStandardItem *item = playListModel->item(row);
        if(item)
            m_currentPath = item->data(FilePathRole).toString();
    }
    applyPlayingStyle();
    const int curRow = currentPlayingRow();
    if(curRow >= 0)
        playListView->scrollTo(playListModel->index(m_currentRow, 0), QAbstractItemView::PositionAtCenter);

}

void TMediaPlayList::addFiles(const QStringList &files)
{
    //选择文件为空，直接返回
    if(files.isEmpty())
        return;
    for(const QString &filePath : files){
        //去重，同一路径不重复入队列
        if(indexOfPath(filePath) >= 0)
            continue;
        QFileInfo info(filePath);
        QStandardItem *item = new QStandardItem(info.fileName());
        item->setData(filePath, FilePathRole);
        //开启复选框
        item->setCheckable(true);
        item->setCheckState(Qt::Unchecked);
        //QStandardItem 默认带 ItemIsDropEnabled，InternalMove 拖放到
        //另一条目"上面"时会被 QStandardItemModel 当成放入该条目的子行——
        //列表只渲染顶层行，被嵌套的条目就"凭空消失"了。
        //取消每个条目的可放置标志后，只允许落到行与行之间，杜绝吞项
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
        playListModel->appendRow(item);
    }
    //新加入项可能被前缀逻辑漏掉，统一刷一遍
    applyPlayingStyle();
}

//滑出：定位到窗口右缘内，动画移入
void TMediaPlayList::slideOut()
{
    if(!parentWidget())
        return;
    m_slideOut = true;
    const QSize pSize = parentWidget()->size();
    const int h = qMax(0, pSize.height() - BottomReserved -4);
    setFixedHeight(h);
    raise();
    show();
    slideAnim->stop();
    slideAnim->setStartValue(pos());
    slideAnim->setEndValue(QPoint(pSize.width() - PanelWidth, 4));
    slideAnim->start();
}


//缩回：动画移出窗口右缘后隐藏
void TMediaPlayList::slideIn()
{
    if(!parentWidget())
        return;
    m_slideOut = false;
    slideAnim->stop();
    slideAnim->setStartValue(pos());
    slideAnim->setEndValue(QPoint(parentWidget()->width(), y()));
    slideAnim->start();
    //动画结束后再 hide，避免中途消失闪烁
    //先断开旧连接再挂新的，防止多次 slideIn 叠加 finished 槽
    disconnect(slideAnim, &QPropertyAnimation::finished, this, nullptr);
    connect(slideAnim, &QPropertyAnimation::finished, this, [this]{
        if(!m_slideOut)
            hide();
    });

}

void TMediaPlayList::slideToggle()
{
    if(m_slideOut)
        slideIn();
    else
        slideOut();
}

//父窗口尺寸变化时重算停靠位置（收起态贴在右边界外）
void TMediaPlayList::reposition()
{
    if(!parentWidget())
        return;
    const QSize pSize = parentWidget()->size();
    //顶部留 4px 边，底部避开进度条与控制条区域
    const int h = qMax(0, pSize.height() - BottomReserved -4);
    setFixedHeight(h);
    const int xOut = pSize.width() - PanelWidth;
    const int xHide = pSize.width(); //完全贴到窗口边界处
    slideAnim->stop();
    move(m_slideOut ? QPoint(xOut, 4) : QPoint(xHide, 4));
}

//面板内点击事件继续，不隐藏面板
void TMediaPlayList::mousePressEvent(QMouseEvent *event)
{
    event->accept();
}

void TMediaPlayList::onAddFileBtnClicked()
{
    QString path = QDir::rootPath();
    qDebug()<<"the path is "<< path;
    QString dlgTitle = "添加媒体文件";
    QString filter = "媒体文件(*.mp4 *.avi *.mkv *.mov);;所有文件(*.*)";
    QStringList fileList = QFileDialog::getOpenFileNames(this, dlgTitle, path, filter);
    //qDebug()<<fileList;
    addFiles(fileList);

}

void TMediaPlayList::onRemoveFileBtnClicked()
{
    // //获取选中的行对应的models
    // QModelIndexList indexList = playListView->selectionModel()->selectedIndexes();
    // if(indexList.isEmpty())
    //     return; //没有选中的，直接返回
    // //去重并按行号从大到小进行删除，避免删除后行号变化
    // QList<int> rows;
    // for(const QModelIndex &index : indexList){
    //     int row = index.row();
    //     if(!rows.contains(row)){
    //         rows.append(row);
    //     }
    // }
    // std::sort(rows.begin(), rows.end(), [](int a, int b) {
    //     return a > b;
    // });

    // for (int row : rows) {
    //     playListModel->removeRow(row);
    // }

    //修改成勾选项删除
    QList<int> rows;
    //获取播放列表所有项行数
    int count = playListModel->rowCount();
    for(int i = count -1; i>=0; --i){
        QStandardItem *item = playListModel->item(i);
        if(item && item->checkState() == Qt::Checked){
            rows.append(i);
        }
    }
    if(rows.isEmpty()) return;

    for(int row:rows){
        playListModel->removeRow(row);
    }
    //正在播放的文件若被移除，清除播放标记（按路径判断，与行号无关）
    if(!m_currentPath.isEmpty() && indexOfPath(m_currentPath) < 0)
        m_currentPath.clear();
    applyPlayingStyle();
}

void TMediaPlayList::onUpFileBtnClicked()
{
    moveCurrentRow(-1);
    //获取选中行
    // QModelIndex currentIdx = playListView->currentIndex();
    // if(!currentIdx.isValid() || currentIdx.row() <=0)
    //     return;
    // int row = currentIdx.row();
    // playListModel->insertRow(row - 1, playListModel->takeItem(row));
    // playListView->setCurrentIndex(playListModel->index(row - 1, 0));


    // QModelIndex currentIdx = playListView->currentIndex();
    // if(!currentIdx.isValid())
    //     return;
    // int curRow = currentIdx.row();
    // if(curRow <= 0)
    //     return;
    // //moveRow 移动整行
    // QModelIndex parentIdx;
    // playListModel->moveRow(parentIdx, curRow, parentIdx, curRow - 1);
    // //同步当前播放标记
    // if(m_currentRow == curRow)
    //     m_currentRow = curRow -1;
    // else if(m_currentRow = curRow - 1)
    //     m_currentRow = curRow;
    // playListView->setCurrentIndex(playListModel->index(curRow - 1, 0));


    // QStandardItem *curItem = playListModel->item(curRow);
    // QStandardItem *prevItem = playListModel->item(curRow - 1);

    // if(!curItem || !prevItem)
    //     return;

    // //交换显示文本
    // QString curText = curItem->text();
    // QString prevText = prevItem->text();
    // curItem->setText(prevText);
    // prevItem->setText(curText);
    // //交换文件路径
    // QVariant curPath = curItem->data(FilePathRole);
    // QVariant prevPath = prevItem->data(FilePathRole);

    // curItem->setData(prevPath, FilePathRole);
    // prevItem->setData(curPath, FilePathRole);

    // //交换勾选状态
    // Qt::CheckState curCheck = curItem->checkState();
    // Qt::CheckState prevCheck = prevItem->checkState();

    // curItem->setCheckState(prevCheck);
    // prevItem->setCheckState(curCheck);

    // //选中下一项
    // playListView->setCurrentIndex(playListModel->index(curRow - 1, 0));


}

void TMediaPlayList::onDownFileBtnClicked()
{
    moveCurrentRow(1);
    // QModelIndex currentIndex = playListView->currentIndex();
    // if (!currentIndex.isValid())
    //     return;

    // int row = currentIndex.row();
    // int lastRow = playListModel->rowCount() - 1;
    // //如果已经是最后一行，直接返回
    // if (row >= lastRow)
    //     return;
    // playListModel->insertRow(row + 1, playListModel->takeItem(row));
    // playListView->setCurrentIndex(playListModel->index(row + 1, 0));


    // QModelIndex currentIdx = playListView->currentIndex();
    // if(!currentIdx.isValid())
    //     return;
    // int curRow = currentIdx.row();
    // int lastRow = playListModel->rowCount() - 1;
    // if(curRow >= lastRow)
    //     return;
    // QModelIndex parentIdx;
    // //moveRow 到 curRow+2 处即"下移一格"（目标插到 next 之后）
    // playListModel->moveRow(parentIdx, curRow, parentIdx, curRow + 2);
    // if(m_currentRow == curRow)
    //     m_currentRow = curRow + 1;
    // else if(m_currentRow == curRow + 1)
    //     m_currentRow = curRow;
    // playListView->setCurrentIndex(playListModel->index(curRow + 1, 0));


    // QStandardItem *curItem = playListModel->item(curRow);
    // QStandardItem *nextItem = playListModel->item(curRow + 1);

    // if(!curItem || !nextItem)
    //     return;

    // //交换显示文本
    // QString curText = curItem->text();
    // QString nextText = nextItem->text();
    // curItem->setText(nextText);
    // nextItem->setText(curText);
    // //交换文件路径
    // QVariant curPath = curItem->data(FilePathRole);
    // QVariant nextPath = nextItem->data(FilePathRole);

    // curItem->setData(nextPath, FilePathRole);
    // nextItem->setData(curPath, FilePathRole);

    // //交换勾选状态
    // Qt::CheckState curCheck = curItem->checkState();
    // Qt::CheckState nextCheck = nextItem->checkState();

    // curItem->setCheckState(nextCheck);
    // nextItem->setCheckState(curCheck);

    // //选中下一项
    // playListView->setCurrentIndex(playListModel->index(curRow+1, 0));

    // QModelIndex index = QModelIndex();
    // playListModel->moveRow(index, curRow, index, curRow + 2);
}



void TMediaPlayList::onItemDoubleClicked(const QModelIndex &index)
{
    if(!index.isValid())
        return;

    QString filePath = index.data(FilePathRole).toString();
    //如果文件不为空，发起播放文件信号
    if(!filePath.isEmpty()){
        setCurrentPlayingRow(index.row());
        //等会发起
        emit playFileRequeted(filePath);
    }
}


//刷新播放列表元素个数
void TMediaPlayList::refreshTitle()
{
    titleLab->setText(QString("播放列表 (%1)").arg(playListModel->rowCount()));
}
//重算所有行的显示文本：文件名 +（仅当前播放行加"▶ "前缀）。
//文本统一由路径反推生成，天然不带历史前缀残留；
//拖拽/上移下移重排后调用即可让 ▶ 自动跟到该文件的新行
void TMediaPlayList::applyPlayingStyle()
{
    const int curRow = currentPlayingRow();
    for(int i = 0; i < playListModel->rowCount(); i++){
        QStandardItem *item = playListModel->item(i);
        if(!item)
            continue;
        const QString path = item->data(FilePathRole).toString();
        const QString name = QFileInfo(path).fileName();
        //去掉旧前缀再按需加新前缀，避免多次调用叠加
        if(i == curRow)
            item->setText(QString(QChar(0x25B6)) + " " + name);
        else
            item->setText(name); //把移动后之前位置的播放符号去掉
    }
}

//当前选中行上移/下移一格：takeRow 摘除整行 + insertRow 挂回目标位置。
//注意必须用 takeRow 而不是 takeItem：takeItem 只摘走该格的对象，
//原位会残留一个空白行，随后 insertRow 又使行数 +1，
//表现为"当前项连同下面所有项整体下移、列表凭空多出一行"（模型测试实证）。
//上移/下移的目标行号在摘除后自动前移一位，正好落在相邻行前后，无需特殊处理。
void TMediaPlayList::moveCurrentRow(int delta)
{
    QModelIndex currentIdx = playListView->currentIndex();
    if(!currentIdx.isValid())
        return;
    const int row = currentIdx.row();
    const int dst = row + delta;
    const int last = playListModel->rowCount() - 1;
    if(dst < 0 || dst > last)
        return;
    QList<QStandardItem*> itemList = playListModel->takeRow(row);
    if(itemList.isEmpty())
        return;
    playListModel->insertRow(dst, itemList);

    //选中跟随移动后的行
    playListView->setCurrentIndex(playListModel->index(dst, 0));
    //▶ 标记按路径存储，重排后自动指向新行，这里只需刷新文本前缀
    applyPlayingStyle();
    playListView->scrollTo(playListModel->index(dst, 0), QAbstractItemView::EnsureVisible);
}
