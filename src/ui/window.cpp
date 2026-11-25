#include "window.h"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLocale>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
namespace kadro {
Window::Window(QWidget* parent) : QMainWindow(parent), model_(new FolderModel(this)), filter_(new MediaFilter(this)), images_(new ImageStore(128,3,this)) {
  setWindowTitle(QApplication::applicationDisplayName()); setMinimumSize(720,480); resize(1200,800); setAcceptDrops(true);
  filter_->setSourceModel(model_);
  buildUi(); buildActions(); applyTheme();
  QSettings settings;
  if (settings.contains("window/geometry")) restoreGeometry(settings.value("window/geometry").toByteArray());
  refreshTimer_.setSingleShot(true); refreshTimer_.setInterval(250);
  connect(&files_,&QFileSystemWatcher::directoryChanged,this,[this]{ refreshTimer_.start(); });
  connect(&files_,&QFileSystemWatcher::fileChanged,this,[this]{ refreshTimer_.start(); });
  connect(&refreshTimer_,&QTimer::timeout,this,[this]{ refresh(current_); });
  connect(model_,&FolderModel::scanFailed,this,[this](const QString& error){ folderError_=error; });
  connect(model_,&FolderModel::scanFinished,this,[this]{
    if (closing_) return;
    QString chosen=preferred_; preferred_.clear();
    const bool matches=filter_->kind()==Kind::Unknown || kindForExtension(QFileInfo(chosen).suffix().toStdString())==filter_->kind();
    if (!chosen.isEmpty() && QFileInfo(chosen).isFile() && matches) {
      const bool changed=identity_!=imageKey(chosen,requestedEdge_);
      if (chosen!=current_ || changed) showFile(chosen,true);
    } else if (currentRow()<0) {
      if (filter_->rowCount()>0) showFile(pathAt(0));
      else {
        current_.clear();
        decoded_ = {};
        if (video_) video_->stop();
        canvas_->clearImage();
        if (!folderError_.isEmpty()) {
          showMessage(MessageState::Error, "Не удалось открыть папку", folderError_);
        } else if (messageState_ != MessageState::Notice) {
          showMessage(MessageState::Empty, "Нет файлов для просмотра",
                      "Выберите другую папку или переключите фильтр на «Все».");
        }
      }
    }
    syncSelection(); updateLabels(); prefetch();
  });
  connect(images_,&ImageStore::ready,this,[this](const QString& path,int edge,const Decoded& value,const QString& key){
    if (closing_ || path!=current_ || edge!=requestedEdge_ || key!=identity_) return;
    if (kindForExtension(QFileInfo(path).suffix().toStdString())!=Kind::Photo) return;
    if (value.image.isNull()) {
      if (stage_->currentWidget() == canvas_ && canvas_->hasImage()) {
        toast(value.error);
      } else {
        showMessage(MessageState::Error, "Не удалось открыть изображение", value.error);
        updateLabels();
      }
      actualPending_ = false;
      preservePending_ = false;
      return;
    }
    decoded_=value;
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
  showMessage(MessageState::Welcome, "Откройте фото или видео",
              "Перетащите файл или папку сюда. Соседние файлы можно листать стрелками.");
  updateLabels();
}
Window::~Window() { closing_=true; }
void Window::chooseFile() {
  const auto path=QFileDialog::getOpenFileName(this,"Открыть фото или видео",QSettings().value("browse/lastFolder",QDir::homePath()).toString(),"Фото и видео (*.jpg *.jpeg *.png *.gif *.bmp *.webp *.tif *.tiff *.heic *.heif *.avif *.ico *.mp4 *.m4v *.mov *.mkv *.webm *.avi *.wmv *.mpg *.mpeg *.mts *.m2ts *.3gp);;Все файлы (*)");
  if (!path.isEmpty()) { setFocus(); openPath(path); }
}
void Window::chooseFolder() {
  const auto path = QFileDialog::getExistingDirectory(this, "Выбрать папку",
    QSettings().value("browse/lastFolder", QDir::homePath()).toString());
  if (!path.isEmpty()) { setFocus(); openPath(path); }
}
void Window::openPath(const QString& input) {
  if(input.isEmpty()) { showNormal(); raise(); activateWindow(); return; }
  QFileInfo f(QDir::cleanPath(QFileInfo(input).absoluteFilePath()));
  QString problem;
  if (!f.isReadable() || (!f.isDir() && !f.isFile())) problem = "Файл или папка недоступны.";
  else if (!f.isDir() && kindForExtension(f.suffix().toStdString()) == Kind::Unknown)
    problem = "Этот формат не поддерживается.";
  if (!problem.isEmpty()) {
    if (current_.isEmpty()) showMessage(MessageState::Error, problem, "Выберите другой файл или папку.");
    else toast(problem);
    return;
  }
  slideshowAction_->setChecked(false); slideshowTimer_.stop();
  refreshTimer_.stop();
  folderError_.clear();
  const auto folder=f.isDir()?f.absoluteFilePath():f.absolutePath();
  QSettings().setValue("browse/lastFolder",folder);
  if(!files_.directories().isEmpty()) files_.removePaths(files_.directories());
  files_.addPath(folder);
  if (f.isDir()) {
    current_.clear();
    preferred_.clear();
    decoded_ = {};
    if (!files_.files().isEmpty()) files_.removePaths(files_.files());
    images_->clearPending();
    if (video_) video_->stop();
    canvas_->clearImage();
    showMessage(MessageState::Loading, "Читаем папку", f.fileName());
  }
  else {
    const auto kind=kindForExtension(f.suffix().toStdString());
    if(filter_->kind()!=Kind::Unknown && filter_->kind()!=kind){filter_->setKind(Kind::Unknown);kinds_->button(0)->setChecked(true);}
    preferred_=f.absoluteFilePath(); showFile(preferred_,true);
  }
  model_->scan(folder); updateLabels();
}
void Window::refresh(const QString& preferred) {
  if (model_->folder().isEmpty()) return;
  folderError_.clear();
  preferred_ = preferred;
  model_->scan(model_->folder());
  updateLabels();
}
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
    else {
      current_.clear();
      decoded_ = {};
      if (video_) video_->stop();
      canvas_->clearImage();
      showMessage(MessageState::Empty, "Нет файлов этого типа", "Переключите фильтр на «Все» или выберите другую папку.");
    }
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
      connect(video_, &VideoPane::failed, this, [this](const QString& error) {
        if (stage_->currentWidget() != video_) return;
        showMessage(MessageState::Error, "Не удалось открыть видео", error);
        updateLabels();
      });
      connect(video_,&VideoPane::ended,this,[this]{ if(slideshowAction_->isChecked())navigate(1); });
      connect(video_,&VideoPane::fullScreenRequested,this,&Window::toggleFullscreen);
      connect(video_->player(), &QMediaPlayer::metaDataChanged, this, [this] {
        if (stage_->currentWidget() == video_ && info_->isVisible()) updateInfo();
      });
    }
    messageState_ = MessageState::Loading;
    stage_->setCurrentWidget(video_); video_->open(path);
  } else {
    showMessage(MessageState::Loading, "Открываем изображение", QFileInfo(path).fileName());
    images_->request(path,requestedEdge_,true);
    if(slideshowAction_->isChecked())slideshowTimer_.start();
  }
  syncSelection(); updateLabels(); updateInfo();
}
void Window::syncSelection() {
  QSignalBlocker blocker(strip_->selectionModel());
  auto idx=filter_->index(currentRow(),0); strip_->setCurrentIndex(idx);
  if (idx.isValid() && strip_->isVisible()) strip_->scrollTo(idx,QAbstractItemView::PositionAtCenter);
}
void Window::keyPressEvent(QKeyEvent* event) {
  if ((event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
    switch (event->key()) {
      case Qt::Key_Left: navigate(-1); return;
      case Qt::Key_Right: navigate(1); return;
      case Qt::Key_Home: showFile(pathAt(0)); return;
      case Qt::Key_End: showFile(pathAt(filter_->rowCount() - 1)); return;
      case Qt::Key_Space:
        if (video_ && stage_->currentWidget() == video_) video_->toggle();
        else canvas_->toggleAnimation();
        return;
      default: break;
    }
  }
  QMainWindow::keyPressEvent(event);
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
