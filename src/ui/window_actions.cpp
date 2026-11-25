#include <QActionGroup>
#include <QLineEdit>
#include "window.h"
#include "platform/desktop.h"
#include <QApplication>
#include <QPushButton>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
namespace kadro {
void Window::buildActions() {
  auto action=[this](const QString& name,const QKeySequence& shortcut,auto callback) {
    auto* a=new QAction(name,this); if(!shortcut.isEmpty())a->setShortcut(shortcut);
    addAction(a); menu_->addAction(a); connect(a,&QAction::triggered,this,callback); return a;
  };
  auto key=[this](const QKeySequence& seq,auto callback) { auto* s=new QShortcut(seq,this); connect(s,&QShortcut::activated,this,callback); };
  action("Открыть файл…",QKeySequence::Open,[this]{chooseFile();});
  action("Открыть папку…",QKeySequence("Ctrl+Shift+O"),[this]{chooseFolder();});
  action("Показать в папке",QKeySequence("Ctrl+R"),[this]{revealCurrent();});
  menu_->addSeparator();
  action("Копировать файл",QKeySequence::Copy,[this]{if(!desktop::copyFile(current_))toast("Сначала откройте файл.");});
  action("Копировать путь",QKeySequence("Ctrl+Shift+C"),[this]{if(!current_.isEmpty())QApplication::clipboard()->setText(current_);});
  action("Переименовать…",QKeySequence(Qt::Key_F2),[this]{renameCurrent();});
  action("В корзину…",QKeySequence(Qt::Key_Delete),[this]{trashCurrent();});
  menu_->addSeparator();
  auto* sort=menu_->addMenu("Сортировка");
  auto* group=new QActionGroup(this); group->setExclusive(true);
  const QStringList labels={"По имени","Сначала новые","Сначала старые","Сначала большие"};
  const int savedSort = qBound(0, QSettings().value("view/sort", 0).toInt(), int(labels.size()) - 1);
  filter_->setOrder(static_cast<MediaFilter::Sort>(savedSort));
  for(int i=0;i<labels.size();++i) {
    auto* a=sort->addAction(labels[i]); a->setCheckable(true); group->addAction(a); a->setChecked(i==savedSort);
    connect(a, &QAction::triggered, this, [this, i] {
      filter_->setOrder(static_cast<MediaFilter::Sort>(i));
      QSettings().setValue("view/sort", i);
      syncSelection();
      updateLabels();
      prefetch();
    });
  }
  stripAction_ = action("Лента миниатюр", QKeySequence("T"), [this](bool checked) {
    QSettings().setValue("view/filmstrip", checked);
    syncChrome();
    syncSelection();
  });
  infoAction_ = action("Сведения о файле", QKeySequence("I"), [this](bool checked) {
    QSettings().setValue("view/info", checked);
    syncChrome();
    updateInfo();
  });
  stripAction_->setCheckable(true);
  infoAction_->setCheckable(true);
  connect(stripButton_, &QPushButton::clicked, stripAction_, &QAction::trigger);
  connect(infoButton_, &QPushButton::clicked, infoAction_, &QAction::trigger);
  connect(stripAction_, &QAction::toggled, stripButton_, &QPushButton::setChecked);
  connect(infoAction_, &QAction::toggled, infoButton_, &QPushButton::setChecked);
  stripAction_->setChecked(QSettings().value("view/filmstrip", false).toBool());
  infoAction_->setChecked(QSettings().value("view/info", false).toBool());
  slideshowAction_=action("Слайд-шоу · 4 секунды",QKeySequence("S"),[this]{toggleSlideshow();}); slideshowAction_->setCheckable(true);
  action("Полный экран",QKeySequence(Qt::Key_F11),[this]{toggleFullscreen();});
  action("Скрыть / показать панели",QKeySequence("H"),[this]{toggleChrome();});
  action("Светлая / тёмная тема",{},[this]{QSettings s;s.setValue("view/light",!s.value("view/light",false).toBool());applyTheme();});
  menu_->addSeparator();
  action("Приложение по умолчанию…",{},[this]{defaultApplication();});
  auto* remember = action("Запоминать позиции видео", {}, [](bool checked) {
    QSettings().setValue("video/resume", checked);
  });
  remember->setCheckable(true);
  remember->setChecked(QSettings().value("video/resume", true).toBool());
  action("Забыть позиции видео", {}, [this] {
    QSettings().remove("video/positions");
    if (video_) video_->forgetPosition();
    toast("Сохранённые позиции видео удалены.");
  });
  action("Горячие клавиши",QKeySequence("?"),[this]{
    QMessageBox::information(this, "Управление Wanted View",
      "← / → — предыдущий / следующий файл\nHome / End — первый / последний\n"
      "Колесо, + / − — масштаб\nF — вписать, 1 — исходный масштаб, R — повернуть вид\n"
      "Пробел — воспроизведение или пауза при фокусе в области просмотра\n"
      "Ctrl/Cmd + ← / → — перемотка на 5 секунд\n"
      "Tab / Shift+Tab — перейти между элементами управления\n"
      "T — лента, I — сведения, S — слайд-шоу\n"
      "F11 / двойной щелчок — полный экран\nH — скрыть панели, Esc — вернуть или выйти\n"
      "Ctrl/Cmd+O — открыть файл, Ctrl/Cmd+Shift+O — открыть папку\n"
      "Ctrl/Cmd+R — показать в папке\nCtrl/Cmd+C — скопировать файл\n"
      "Ctrl/Cmd+Shift+C — скопировать путь\nF2 — переименовать, Delete — в корзину\n"
      "F10 — меню действий и настроек");
  });
  action("О Wanted View", {}, [this] { showAbout(); });
  key(QKeySequence("F"),[this]{canvas_->fit();}); key(QKeySequence("1"),[this]{requestNative(true);}); key(QKeySequence("R"),[this]{canvas_->rotateClockwise();});
  key(QKeySequence("+"),[this]{canvas_->zoomBy(1.25);}); key(QKeySequence("="),[this]{canvas_->zoomBy(1.25);}); key(QKeySequence("-"),[this]{canvas_->zoomBy(0.8);});
  key(QKeySequence("Ctrl+Left"),[this]{if(video_&&stage_->currentWidget()==video_)video_->seekRelative(-5000);});
  key(QKeySequence("Ctrl+Right"),[this]{if(video_&&stage_->currentWidget()==video_)video_->seekRelative(5000);});
  key(QKeySequence(Qt::Key_Escape),[this]{
    if(chromeHidden_)toggleChrome(); else if(isFullScreen())showNormal();
    else if(slideshowAction_->isChecked()){slideshowAction_->setChecked(false);slideshowTimer_.stop();}
  });
}
void Window::toggleFullscreen() { if(isFullScreen())showNormal();else showFullScreen(); }
void Window::toggleChrome() {
  chromeHidden_ = !chromeHidden_;
  if (chromeHidden_) {
    setFocus();
    statusBar()->hide();
  }
  syncChrome();
  syncSelection();
}
void Window::toggleSlideshow() {
  if(slideshowAction_->isChecked() && filter_->rowCount()==0){slideshowAction_->setChecked(false);return;}
  if(slideshowAction_->isChecked() && stage_->currentWidget()!=video_)slideshowTimer_.start();else slideshowTimer_.stop();
  toast(slideshowAction_->isChecked()?"Слайд-шоу включено. S или Esc — остановить.":"Слайд-шоу остановлено.");
}
void Window::revealCurrent() { if(current_.isEmpty()||!desktop::reveal(current_))toast("Не удалось показать файл в папке."); }
void Window::renameCurrent() {
  if(current_.isEmpty())return;
  const QString old=current_;
  const QString revision = imageKey(old, 0);
  bool ok=false;
  auto name=QInputDialog::getText(this,"Переименовать","Новое имя файла",QLineEdit::Normal,QFileInfo(old).fileName(),&ok);
  if(!ok || name==QFileInfo(old).fileName())return;
  if (current_ != old || revision != imageKey(old, 0)) {
    toast("Файл изменился. Откройте его ещё раз перед переименованием.");
    return;
  }
  if(QFileInfo(name).suffix().compare(QFileInfo(old).suffix(),Qt::CaseInsensitive)!=0) {
    QMessageBox::warning(this,"Расширение файла","В этой версии переименование сохраняет расширение. Измените только имя до последней точки.");return;
  }
  if(video_)video_->stop(); canvas_->clearImage();
  QString next; auto error=desktop::renameFile(old,name,&next);
  if(!error.isEmpty()){showFile(old,true);QMessageBox::warning(this,"Переименование",error);return;}
  showFile(next,true);refresh(next);
}
void Window::trashCurrent() {
  if (current_.isEmpty()) return;
  const QString old = current_;
  const QString revision = imageKey(old, 0);
  const auto created = QFileInfo(old).birthTime();
  QMessageBox confirmation(QMessageBox::Question, "Переместить в корзину?",
    QFileInfo(old).fileName() + "\n\nФайл можно восстановить из системной корзины.",
    QMessageBox::NoButton, this);
  confirmation.setTextFormat(Qt::PlainText);
  auto* move = confirmation.addButton("В корзину", QMessageBox::AcceptRole);
  auto* cancel = confirmation.addButton("Отмена", QMessageBox::RejectRole);
  confirmation.setDefaultButton(cancel);
  confirmation.setEscapeButton(cancel);
  confirmation.exec();
  if (confirmation.clickedButton() != move) return;
  if (current_ != old) {
    toast("Открыт другой файл. Повторите действие для нужного файла.");
    return;
  }
  if (revision != imageKey(old, 0) || created != QFileInfo(old).birthTime()) {
    toast("Файл изменился. Откройте его ещё раз перед переносом в корзину.");
    return;
  }

  const int row = currentRow();
  const QString next = filter_->rowCount() > 1 ? pathAt(nextIndex(row, filter_->rowCount(), 1)) : QString();
  if (video_) video_->stop();
  canvas_->clearImage();
  images_->clearPending();
  const auto error = desktop::trashFile(old);
  if (!error.isEmpty()) {
    showFile(old, true);
    QMessageBox::warning(this, "Корзина", error);
    return;
  }

  current_.clear();
  if (!next.isEmpty() && next != old) showFile(next, true);
  else showMessage(MessageState::Notice, "Файл в корзине", "Его можно восстановить из системной корзины.");
  refresh(next == old ? QString() : next);
}
void Window::defaultApplication() {
  if(!desktop::registerViewer(QCoreApplication::applicationFilePath())){QMessageBox::warning(this,"Приложение по умолчанию","Не удалось зарегистрировать приложение для текущего пользователя.");return;}
  QMessageBox::information(this,"Приложение по умолчанию",desktop::defaultAppInstructions());
#ifdef Q_OS_WIN
  QDesktopServices::openUrl(QUrl("ms-settings:defaultapps"));
#endif
}
void Window::dragEnterEvent(QDragEnterEvent* event) {
  if(event->mimeData()->hasUrls())for(const auto& url:event->mimeData()->urls())if(url.isLocalFile()){event->acceptProposedAction();return;}
}
void Window::dropEvent(QDropEvent* event) {
  for(const auto& url:event->mimeData()->urls())if(url.isLocalFile()){openPath(url.toLocalFile());event->acceptProposedAction();return;}
}
void Window::closeEvent(QCloseEvent* event) {
  closing_=true; slideshowTimer_.stop(); refreshTimer_.stop();
  if(video_)video_->stop();
  if(!isFullScreen())QSettings().setValue("window/geometry",saveGeometry());
  QMainWindow::closeEvent(event);
}
}
