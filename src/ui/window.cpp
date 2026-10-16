#include "window.h"
#include "platform/desktop.h"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QLocale>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>
namespace kadro {
static QPushButton* button(const QString& text,QWidget* parent) {
  auto* b=new QPushButton(text,parent); b->setCursor(Qt::PointingHandCursor); b->setFocusPolicy(Qt::NoFocus); return b;
}
Window::Window(QWidget* parent) : QMainWindow(parent), model_(new FolderModel(this)), filter_(new MediaFilter(this)), images_(new ImageStore(128,3,this)) {
  setWindowTitle("KADRO"); setMinimumSize(820,520); resize(1200,800); setAcceptDrops(true);
  filter_->setSourceModel(model_);
  buildUi(); buildActions(); applyTheme();
  QSettings settings;
  if (settings.contains("window/geometry")) restoreGeometry(settings.value("window/geometry").toByteArray());
  refreshTimer_.setSingleShot(true); refreshTimer_.setInterval(250);
  connect(&files_,&QFileSystemWatcher::directoryChanged,this,[this]{ refreshTimer_.start(); });
  connect(&files_,&QFileSystemWatcher::fileChanged,this,[this]{ refreshTimer_.start(); });
  connect(&refreshTimer_,&QTimer::timeout,this,[this]{ refresh(current_); });
  connect(model_,&FolderModel::scanFailed,this,[this](const QString& error){ toast(error); });
  connect(model_,&FolderModel::scanFinished,this,[this]{
    if (closing_) return;
    QString chosen=preferred_; preferred_.clear();
    const bool matches=filter_->kind()==Kind::Unknown || kindForExtension(QFileInfo(chosen).suffix().toStdString())==filter_->kind();
    if (!chosen.isEmpty() && QFileInfo(chosen).isFile() && matches) {
      const bool changed=identity_!=imageKey(chosen,requestedEdge_);
      if (chosen!=current_ || changed) showFile(chosen,true);
    } else if (currentRow()<0) {
      if (filter_->rowCount()>0) showFile(pathAt(0));
      else { current_.clear(); if(video_) video_->stop(); canvas_->clearImage(); showMessage("Здесь пока пусто","В этой папке нет файлов выбранного типа. Попробуйте другой фильтр или другую папку."); }
    }
    syncSelection(); updateLabels(); prefetch();
  });
  connect(images_,&ImageStore::ready,this,[this](const QString& path,int edge,const Decoded& value){
    if (closing_ || path!=current_ || edge!=requestedEdge_) return;
    if (kindForExtension(QFileInfo(path).suffix().toStdString())!=Kind::Photo) return;
    decoded_=value;
    if (value.image.isNull()) { showMessage("Файл не открылся",value.error); return; }
    stage_->setCurrentWidget(canvas_);
    // Clear flags first: display() emits zoomChanged synchronously.
    bool actual=actualPending_, preserve=preservePending_;
    actualPending_=false; preservePending_=false;
    canvas_->display(path,value,preserve);
    if (actual) canvas_->actualSize();
    updateLabels(); updateInfo(); prefetch();
  });
  connect(canvas_,&Canvas::fullScreenRequested,this,&Window::toggleFullscreen);
  connect(canvas_,&Canvas::zoomChanged,this,[this](int percent){
    scale_->setText(QString::number(percent)+"%");
    const double available=decoded_.original.width()>0 ? 100.0*decoded_.image.width()/decoded_.original.width() : 100.0;
    if (percent>available+1 && requestedEdge_>0 && canvas_->hasImage()) requestNative(false);
  });
  connect(strip_->selectionModel(),&QItemSelectionModel::currentChanged,this,[this](const QModelIndex& index){
    if(index.isValid()) showFile(index.data(FolderModel::PathRole).toString());
  });
  slideshowTimer_.setInterval(4000);
  connect(&slideshowTimer_,&QTimer::timeout,this,[this]{ navigate(1); });
  showMessage("Фото и видео. В одном окне.","Откройте любой файл — соседние фото и видео будут рядом.\nБез импорта, библиотеки и ожидания индексации.");
}
Window::~Window() { closing_=true; }
void Window::buildUi() {
  auto* root=new QWidget(this); setCentralWidget(root);
  auto* layout=new QVBoxLayout(root); layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
  top_=new QWidget(root); top_->setObjectName("topBar");
  auto* header=new QHBoxLayout(top_); header->setContentsMargins(22,16,18,16); header->setSpacing(16);
  auto* brand=new QLabel("K A D R O",top_); brand->setObjectName("brand"); header->addWidget(brand);
  auto* meta=new QVBoxLayout; meta->setSpacing(3);
  title_=new QLabel("Просмотрщик медиа",top_); title_->setTextFormat(Qt::PlainText); title_->setObjectName("fileTitle"); title_->setMinimumWidth(120); title_->setMaximumWidth(420);
  subtitle_=new QLabel("Локально. Только ваши файлы.",top_); subtitle_->setObjectName("muted"); subtitle_->setTextFormat(Qt::PlainText);
  meta->addWidget(title_); meta->addWidget(subtitle_); header->addLayout(meta,1);
  kinds_=new QButtonGroup(this);
  auto* segmented=new QWidget(top_); segmented->setObjectName("segments");
  auto* segments=new QHBoxLayout(segmented); segments->setContentsMargins(3,3,3,3); segments->setSpacing(2);
  QStringList labels={"Все","Фото","Видео"};
  for(int i=0;i<labels.size();++i){ auto* b=button(labels[i],segmented); b->setObjectName("segment"); b->setCheckable(true); kinds_->addButton(b,i); segments->addWidget(b); }
  kinds_->button(0)->setChecked(true); header->addWidget(segmented);
  connect(kinds_,&QButtonGroup::idClicked,this,[this](int id){ selectKind(id==1?Kind::Photo:id==2?Kind::Video:Kind::Unknown); });
  auto* open=button("Открыть",top_); open->setObjectName("primary"); header->addWidget(open); connect(open,&QPushButton::clicked,this,&Window::chooseFile);
  auto* more=new QToolButton(top_); more->setText("•••"); more->setToolTip("Действия и настройки"); more->setAccessibleName("Действия и настройки"); more->setPopupMode(QToolButton::InstantPopup);
  more->setFocusPolicy(Qt::NoFocus); menu_=new QMenu(more); more->setMenu(menu_); header->addWidget(more); layout->addWidget(top_);
  auto* body=new QHBoxLayout; body->setContentsMargins(0,0,0,0); body->setSpacing(0);
  stage_=new QStackedWidget(root); stage_->setObjectName("stage");
  canvas_=new Canvas(stage_); stage_->addWidget(canvas_);
  messagePage_=new QWidget(stage_); messagePage_->setObjectName("messagePage");
  auto* empty=new QVBoxLayout(messagePage_); empty->setAlignment(Qt::AlignCenter); empty->setContentsMargins(40,40,40,40); empty->setSpacing(18);
  auto* mark=new QLabel("K",messagePage_); mark->setAlignment(Qt::AlignCenter); mark->setObjectName("emptyMark"); empty->addWidget(mark);
  heading_=new QLabel(messagePage_); heading_->setTextFormat(Qt::PlainText); heading_->setObjectName("emptyHeading"); heading_->setAlignment(Qt::AlignCenter); heading_->setWordWrap(true); empty->addWidget(heading_);
  message_=new QLabel(messagePage_); message_->setTextFormat(Qt::PlainText); message_->setAlignment(Qt::AlignCenter); message_->setWordWrap(true); message_->setObjectName("muted"); empty->addWidget(message_);
  auto* row=new QHBoxLayout; row->addStretch();
  auto* file=button("Открыть файл",messagePage_); file->setObjectName("primary"); row->addWidget(file); connect(file,&QPushButton::clicked,this,&Window::chooseFile);
  auto* folder=button("Выбрать папку",messagePage_); row->addWidget(folder); connect(folder,&QPushButton::clicked,this,&Window::chooseFolder); row->addStretch(); empty->addLayout(row);
  auto* hint=new QLabel("Можно просто перетащить файл или папку в окно",messagePage_); hint->setObjectName("muted"); hint->setAlignment(Qt::AlignCenter); hint->setWordWrap(true); empty->addWidget(hint);
  stage_->addWidget(messagePage_); body->addWidget(stage_,1);
  info_=new QPlainTextEdit(root); info_->setReadOnly(true); info_->setFixedWidth(280); info_->setObjectName("infoPanel"); info_->setPlaceholderText("Сведения о файле"); info_->hide(); body->addWidget(info_); layout->addLayout(body,1);
  bottom_=new QWidget(root); bottom_->setObjectName("bottomBar"); auto* tools=new QHBoxLayout(bottom_); tools->setContentsMargins(18,10,18,10); tools->setSpacing(8);
  auto* prev=button("←",bottom_); prev->setToolTip("Предыдущий файл (←)"); prev->setAccessibleName("Предыдущий файл"); tools->addWidget(prev); connect(prev,&QPushButton::clicked,this,[this]{ navigate(-1); });
  counter_=new QLabel("0 / 0",bottom_); counter_->setMinimumWidth(75); counter_->setAlignment(Qt::AlignCenter); tools->addWidget(counter_);
  auto* next=button("→",bottom_); next->setToolTip("Следующий файл (→)"); next->setAccessibleName("Следующий файл"); tools->addWidget(next); connect(next,&QPushButton::clicked,this,[this]{ navigate(1); });
  auto* reveal=button("Показать в папке",bottom_); reveal->setToolTip("Показать в папке (Ctrl/Cmd+R)"); tools->addWidget(reveal); connect(reveal,&QPushButton::clicked,this,&Window::revealCurrent);
  tools->addStretch(); scale_=new QLabel(bottom_); scale_->setObjectName("muted"); tools->addWidget(scale_);
  auto* fit=button("Вписать",bottom_); tools->addWidget(fit); connect(fit,&QPushButton::clicked,canvas_,&Canvas::fit);
  auto* native=button("1:1",bottom_); tools->addWidget(native); connect(native,&QPushButton::clicked,this,[this]{ requestNative(true); });
  auto* rotate=button("↻",bottom_); rotate->setToolTip("Повернуть вид, не меняя файл (R)"); rotate->setAccessibleName("Повернуть изображение"); tools->addWidget(rotate); connect(rotate,&QPushButton::clicked,canvas_,&Canvas::rotateClockwise);
  auto* stripButton=button("Лента",bottom_); tools->addWidget(stripButton); connect(stripButton,&QPushButton::clicked,this,[this]{ stripAction_->trigger(); });
  auto* infoButton=button("Сведения",bottom_); tools->addWidget(infoButton); connect(infoButton,&QPushButton::clicked,this,[this]{ infoAction_->trigger(); });
  auto* full=button("⛶",bottom_); full->setToolTip("Полный экран (F11)"); full->setAccessibleName("Полный экран"); tools->addWidget(full); connect(full,&QPushButton::clicked,this,&Window::toggleFullscreen); layout->addWidget(bottom_);
  strip_=new QListView(root); strip_->setObjectName("filmstrip"); strip_->setModel(filter_); strip_->setViewMode(QListView::IconMode);
  strip_->setFlow(QListView::LeftToRight); strip_->setWrapping(false); strip_->setResizeMode(QListView::Adjust); strip_->setMovement(QListView::Static);
  strip_->setUniformItemSizes(true); strip_->setIconSize(QSize(116,70)); strip_->setGridSize(QSize(134,100)); strip_->setFixedHeight(122);
  strip_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel); strip_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  strip_->setTextElideMode(Qt::ElideMiddle); strip_->setEditTriggers(QAbstractItemView::NoEditTriggers); strip_->setSelectionMode(QAbstractItemView::SingleSelection); strip_->setFocusPolicy(Qt::NoFocus);
  strip_->hide(); layout->addWidget(strip_);
  statusBar()->setSizeGripEnabled(false); statusBar()->hide();
}
void Window::showMessage(const QString& heading,const QString& body) { heading_->setText(heading); message_->setText(body); stage_->setCurrentWidget(messagePage_); }
void Window::chooseFile() {
  const auto path=QFileDialog::getOpenFileName(this,"Открыть фото или видео",QSettings().value("browse/lastFolder",QDir::homePath()).toString(),"Фото и видео (*.jpg *.jpeg *.png *.gif *.bmp *.webp *.tif *.tiff *.heic *.heif *.avif *.ico *.mp4 *.m4v *.mov *.mkv *.webm *.avi *.wmv *.mpg *.mpeg *.mts *.m2ts *.3gp);;Все файлы (*)");
  if(!path.isEmpty()) openPath(path);
}
void Window::chooseFolder() { auto path=QFileDialog::getExistingDirectory(this,"Выбрать папку",QSettings().value("browse/lastFolder",QDir::homePath()).toString()); if(!path.isEmpty()) openPath(path); }
void Window::openPath(const QString& input) {
  if(input.isEmpty()) { showNormal(); raise(); activateWindow(); return; }
  QFileInfo f(QDir::cleanPath(QFileInfo(input).absoluteFilePath()));
  if(!f.exists() || !f.isReadable()) { toast("Файл или папка недоступны."); return; }
  if(!f.isDir() && kindForExtension(f.suffix().toStdString())==Kind::Unknown) { toast("Этот тип файла пока не поддерживается."); return; }
  slideshowAction_->setChecked(false); slideshowTimer_.stop();
  const auto folder=f.isDir()?f.absoluteFilePath():f.absolutePath();
  QSettings().setValue("browse/lastFolder",folder);
  if(!files_.directories().isEmpty()) files_.removePaths(files_.directories());
  files_.addPath(folder);
  if(f.isDir()) { current_.clear(); if(video_)video_->stop(); canvas_->clearImage(); showMessage("Открываем папку","Фото и видео появятся здесь."); preferred_.clear(); }
  else {
    const auto kind=kindForExtension(f.suffix().toStdString());
    if(filter_->kind()!=Kind::Unknown && filter_->kind()!=kind){filter_->setKind(Kind::Unknown);kinds_->button(0)->setChecked(true);}
    preferred_=f.absoluteFilePath(); showFile(preferred_,true);
  }
  model_->scan(folder); updateLabels();
}
void Window::refresh(const QString& preferred) { if(model_->folder().isEmpty())return; preferred_=preferred; model_->scan(model_->folder()); updateLabels(); }
QString Window::pathAt(int row) const { return filter_->index(row,0).data(FolderModel::PathRole).toString(); }
int Window::currentRow() const {
  const int source=model_->rowForPath(current_); if(source<0)return -1;
  auto proxy=filter_->mapFromSource(model_->index(source)); return proxy.isValid()?proxy.row():-1;
}
void Window::navigate(int delta) {
  if(filter_->rowCount()==0)return;
  const int row=nextIndex(currentRow(),filter_->rowCount(),delta);
  showFile(pathAt(row)); syncSelection();
}
void Window::selectKind(Kind kind) {
  filter_->setKind(kind); kinds_->button(kind==Kind::Photo?1:kind==Kind::Video?2:0)->setChecked(true);
  if(currentRow()<0 && !model_->isScanning()) {
    if(filter_->rowCount()>0) showFile(pathAt(0));
    else { current_.clear(); if(video_)video_->stop(); canvas_->clearImage(); showMessage("Нет файлов этого типа","Переключитесь на «Все» или выберите другую папку."); }
  }
  syncSelection(); updateLabels();
}
void Window::showFile(const QString& path,bool force) {
  if(path.isEmpty() || (!force && path==current_))return;
  if(!QFileInfo(path).isFile()) { toast("Файл перемещён или удалён. Обновляю папку."); refresh(); return; }
  if(video_)video_->stop();
  if(!files_.files().isEmpty())files_.removePaths(files_.files()); files_.addPath(path);
  images_->clearPending(); canvas_->clearImage(); decoded_={}; current_=path; actualPending_=false; preservePending_=false;
  requestedEdge_=qBound(1024,qRound(qMax(stage_->width(),stage_->height())*devicePixelRatioF()),4096);
  identity_=imageKey(path,requestedEdge_);
  scale_->clear();
  const auto kind=kindForExtension(QFileInfo(path).suffix().toStdString());
  if(kind==Kind::Video) {
    slideshowTimer_.stop();
    if(!video_) {
      video_=new VideoPane(stage_); stage_->addWidget(video_);
      connect(video_,&VideoPane::failed,this,[this](const QString& error){ if(stage_->currentWidget()==video_)showMessage("Видео не открылось",error); });
      connect(video_,&VideoPane::ended,this,[this]{ if(slideshowAction_->isChecked())navigate(1); });
      connect(video_,&VideoPane::fullScreenRequested,this,&Window::toggleFullscreen);
    }
    stage_->setCurrentWidget(video_); video_->open(path);
  } else {
    showMessage("Открываем изображение",QFileInfo(path).fileName());
    images_->request(path,requestedEdge_,true);
    if(slideshowAction_->isChecked())slideshowTimer_.start();
  }
  syncSelection(); updateLabels(); updateInfo();
}
void Window::syncSelection() {
  QSignalBlocker blocker(strip_->selectionModel());
  auto idx=filter_->index(currentRow(),0); strip_->setCurrentIndex(idx);
  if(idx.isValid())strip_->scrollTo(idx,QAbstractItemView::PositionAtCenter);
}
void Window::updateLabels() {
  const int row=currentRow();
  counter_->setText(model_->isScanning()?"…":QString("%1 / %2").arg(row<0?0:row+1).arg(filter_->rowCount()));
  const QFileInfo f(current_);
  title_->setText(current_.isEmpty()?"Просмотрщик медиа":title_->fontMetrics().elidedText(f.fileName(),Qt::ElideMiddle,qMax(120,title_->width())));
  title_->setToolTip(current_); setWindowTitle(current_.isEmpty()?"KADRO":f.fileName()+" — KADRO");
  if(current_.isEmpty()) { subtitle_->setText("Локально. Только ваши файлы."); scale_->clear(); info_->clear(); return; }
  QString meta=f.suffix().toUpper()+"  ·  "+QLocale().formattedDataSize(f.size());
  if(decoded_.original.isValid())meta+=QString("  ·  %1 × %2").arg(decoded_.original.width()).arg(decoded_.original.height());
  subtitle_->setText(meta);
}
void Window::updateInfo() {
  if(current_.isEmpty()){info_->clear();return;}
  QFileInfo f(current_);
  QString text=f.fileName()+"\n\n"+f.absoluteFilePath()+"\n\nРазмер: "+QLocale().formattedDataSize(f.size())+"\nИзменён: "+f.lastModified().toString("dd.MM.yyyy HH:mm:ss");
  if(decoded_.original.isValid())text+=QString("\nРазрешение: %1 × %2\nВ памяти: %3 × %4").arg(decoded_.original.width()).arg(decoded_.original.height()).arg(decoded_.image.width()).arg(decoded_.image.height());
  if(video_ && stage_->currentWidget()==video_)text+="\n\n"+video_->details();
  text+="\n\nПоворот и масштаб меняют только просмотр. Оригинал остаётся нетронутым.";
  info_->setPlainText(text);
}
void Window::prefetch() {
  if(requestedEdge_==0 || currentRow()<0 || filter_->rowCount()<2)return;
  for(int delta:{1,-1}) {
    auto path=pathAt(nextIndex(currentRow(),filter_->rowCount(),delta));
    if(path!=current_ && kindForExtension(QFileInfo(path).suffix().toStdString())==Kind::Photo && !images_->cached(path,requestedEdge_))images_->request(path,requestedEdge_);
  }
}
void Window::requestNative(bool actual) {
  if(current_.isEmpty() || !canvas_->hasImage() || stage_->currentWidget()!=canvas_)return;
  if(requestedEdge_==0){if(actual)canvas_->actualSize();return;}
  requestedEdge_=0; identity_=imageKey(current_,0); actualPending_=actual; preservePending_=true;
  images_->request(current_,0,true);
}
void Window::toast(const QString& text) {
  statusBar()->show(); statusBar()->showMessage(text,7000);
  QTimer::singleShot(7200,this,[this]{if(statusBar()->currentMessage().isEmpty())statusBar()->hide();});
}
}
