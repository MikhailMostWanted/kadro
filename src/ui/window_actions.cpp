#include <QActionGroup>
#include <QLineEdit>
#include "window.h"
#include "platform/desktop.h"
#include <QApplication>
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
  for(int i=0;i<labels.size();++i) {
    auto* a=sort->addAction(labels[i]); a->setCheckable(true); group->addAction(a); a->setChecked(i==0);
    connect(a,&QAction::triggered,this,[this,i]{filter_->setOrder(static_cast<MediaFilter::Sort>(i));syncSelection();updateLabels();});
  }
  stripAction_=action("Лента миниатюр",QKeySequence("T"),[this]{strip_->setVisible(stripAction_->isChecked()&&!chromeHidden_);QSettings().setValue("view/filmstrip",stripAction_->isChecked());});
  stripAction_->setCheckable(true); stripAction_->setChecked(QSettings().value("view/filmstrip",false).toBool()); strip_->setVisible(stripAction_->isChecked());
  infoAction_=action("Сведения о файле",QKeySequence("I"),[this]{info_->setVisible(infoAction_->isChecked());updateInfo();}); infoAction_->setCheckable(true);
  slideshowAction_=action("Слайд-шоу · 4 секунды",QKeySequence("S"),[this]{toggleSlideshow();}); slideshowAction_->setCheckable(true);
  action("Полный экран",QKeySequence(Qt::Key_F11),[this]{toggleFullscreen();});
  action("Скрыть / показать панели",QKeySequence(Qt::Key_Tab),[this]{toggleChrome();});
  action("Светлая / тёмная тема",{},[this]{QSettings s;s.setValue("view/light",!s.value("view/light",false).toBool());applyTheme();});
  menu_->addSeparator();
  action("Приложение по умолчанию…",{},[this]{defaultApplication();});
  action("Забыть позиции видео",{},[this]{QSettings().remove("video/positions");toast("Сохранённые позиции удалены. Текущее видео сохранит новую позицию при закрытии.");});
  action("Горячие клавиши",QKeySequence("?"),[this]{
    QMessageBox::information(this,"Управление KADRO","← / → — предыдущий / следующий файл\nHome / End — первый / последний\nКолесо — масштаб; перетаскивание — движение\nF — вписать; 1 — исходный масштаб\nR — повернуть вид, не меняя оригинал\nПробел — пауза видео или анимации\nCtrl/Cmd + ← / → — перемотка на 5 секунд\nT — лента; I — сведения; S — слайд-шоу\nF11 / двойной щелчок — полный экран\nTab — скрыть панели; Esc — вернуть / выйти\nCtrl/Cmd+O — открыть файл\nCtrl/Cmd+R — показать в папке\nCtrl/Cmd+C — скопировать файл\nCtrl/Cmd+Shift+C — скопировать путь\nF2 — переименовать; Delete — в корзину");
  });
  action("О KADRO",{},[this]{QMessageBox::about(this,"KADRO","KADRO 0.1.0\nФото и видео в одном окне.\n\nЛокальное приложение без аккаунтов, серверов и телеметрии.\n\nC++20 · Qt 6\nКод приложения: MIT. Лицензии зависимостей — THIRD_PARTY.md.");});
  key(QKeySequence(Qt::Key_Left),[this]{navigate(-1);}); key(QKeySequence(Qt::Key_Right),[this]{navigate(1);});
  key(QKeySequence(Qt::Key_Home),[this]{if(filter_->rowCount())showFile(pathAt(0));}); key(QKeySequence(Qt::Key_End),[this]{if(filter_->rowCount())showFile(pathAt(filter_->rowCount()-1));});
  key(QKeySequence("F"),[this]{canvas_->fit();}); key(QKeySequence("1"),[this]{requestNative(true);}); key(QKeySequence("R"),[this]{canvas_->rotateClockwise();});
  key(QKeySequence("+"),[this]{canvas_->zoomBy(1.25);}); key(QKeySequence("="),[this]{canvas_->zoomBy(1.25);}); key(QKeySequence("-"),[this]{canvas_->zoomBy(0.8);});
  key(QKeySequence(Qt::Key_Space),[this]{if(video_&&stage_->currentWidget()==video_)video_->toggle();else canvas_->toggleAnimation();});
  key(QKeySequence("Ctrl+Left"),[this]{if(video_&&stage_->currentWidget()==video_)video_->seekRelative(-5000);});
  key(QKeySequence("Ctrl+Right"),[this]{if(video_&&stage_->currentWidget()==video_)video_->seekRelative(5000);});
  key(QKeySequence(Qt::Key_Escape),[this]{
    if(chromeHidden_)toggleChrome(); else if(isFullScreen())showNormal();
    else if(slideshowAction_->isChecked()){slideshowAction_->setChecked(false);slideshowTimer_.stop();}
  });
}
void Window::toggleFullscreen() { if(isFullScreen())showNormal();else showFullScreen(); }
void Window::toggleChrome() {
  chromeHidden_=!chromeHidden_; top_->setVisible(!chromeHidden_); bottom_->setVisible(!chromeHidden_);
  strip_->setVisible(!chromeHidden_&&stripAction_->isChecked());
  if(chromeHidden_)statusBar()->hide();
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
  bool ok=false;
  auto name=QInputDialog::getText(this,"Переименовать","Новое имя файла",QLineEdit::Normal,QFileInfo(old).fileName(),&ok);
  if(!ok || name==QFileInfo(old).fileName())return;
  if(QFileInfo(name).suffix().compare(QFileInfo(old).suffix(),Qt::CaseInsensitive)!=0) {
    QMessageBox::warning(this,"Расширение файла","В этой версии переименование сохраняет расширение. Измените только имя до последней точки.");return;
  }
  if(video_)video_->stop(); canvas_->clearImage();
  QString next; auto error=desktop::renameFile(old,name,&next);
  if(!error.isEmpty()){showFile(old,true);QMessageBox::warning(this,"Переименование",error);return;}
  showFile(next,true);refresh(next);
}
void Window::trashCurrent() {
  if(current_.isEmpty())return;
  const auto answer=QMessageBox::question(this,"Переместить в корзину?",QFileInfo(current_).fileName()+"\n\nФайл можно будет восстановить из системной корзины.",QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel);
  if(answer!=QMessageBox::Yes)return;
  const auto old=current_; const int row=currentRow();
  QString next=filter_->rowCount()>1?pathAt(nextIndex(row,filter_->rowCount(),1)):QString();
  if(video_)video_->stop(); canvas_->clearImage(); images_->clearPending();
  auto error=desktop::trashFile(old);
  if(!error.isEmpty()){showFile(old,true);QMessageBox::warning(this,"Корзина",error);return;}
  current_.clear();
  if(!next.isEmpty()&&next!=old)showFile(next,true);else showMessage("Файл в корзине","Выберите другой файл или папку.");
  refresh(next==old?QString():next);
}
void Window::defaultApplication() {
  if(!desktop::registerViewer()){QMessageBox::warning(this,"Приложение по умолчанию","Не удалось зарегистрировать приложение для текущего пользователя.");return;}
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
void Window::applyTheme() {
  const bool light=QSettings().value("view/light",false).toBool();
  const QString bg=light?"#f7f7f8":"#18191c", text=light?"#24262b":"#e8e9ed", muted=light?"#6c717b":"#959aa5", border=light?"#dedfe3":"#303238", hover=light?"#e9eaed":"#303238", selected=light?"#d9dce3":"#414550";
  setStyleSheet(QString(R"(
    QMainWindow, QWidget { background: %1; color: %2; font-family: "Segoe UI", ".AppleSystemUIFont", "DejaVu Sans"; font-size: 13px; }
    QWidget#topBar { border-bottom: 1px solid %4; }
    QWidget#bottomBar, QWidget#videoBar { border-top: 1px solid %4; }
    QLabel#brand { font-size: 15px; font-weight: 700; padding-right: 12px; }
    QLabel#fileTitle { font-weight: 600; }
    QLabel#muted { color: %3; }
    QWidget#segments { background: %5; border-radius: 9px; }
    QPushButton, QToolButton { border: 1px solid transparent; border-radius: 7px; padding: 8px 10px; background: transparent; }
    QPushButton:hover, QToolButton:hover { background: %5; }
    QPushButton:pressed { background: %6; }
    QPushButton#primary { background: %2; color: %1; padding: 9px 16px; font-weight: 600; }
    QPushButton#primary:hover { border: 1px solid %3; }
    QPushButton#segment { padding: 7px 13px; }
    QPushButton#segment:checked { background: %1; }
    QToolButton::menu-indicator { image: none; }
    QWidget#messagePage { background: %1; }
    QLabel#emptyMark { font-size: 72px; font-weight: 300; color: %3; margin-bottom: 12px; }
    QLabel#emptyHeading { font-size: 25px; font-weight: 600; }
    QListView#filmstrip { border: none; border-top: 1px solid %4; padding: 6px; outline: none; }
    QListView::item { padding: 4px; border-radius: 7px; }
    QListView::item:selected { background: %6; }
    QListView::item:hover { background: %5; }
    QPlainTextEdit#infoPanel { border: none; border-left: 1px solid %4; padding: 18px; line-height: 1.5; }
    QMenu { border: 1px solid %4; padding: 7px; }
    QMenu::item { padding: 8px 24px; border-radius: 5px; }
    QMenu::item:selected { background: %5; }
    QMenu::separator { height: 1px; background: %4; margin: 5px; }
    QSlider::groove:horizontal { height: 4px; background: %4; border-radius: 2px; }
    QSlider::sub-page:horizontal { background: %3; border-radius: 2px; }
    QSlider::handle:horizontal { width: 12px; margin: -4px 0; background: %2; border-radius: 6px; }
    QComboBox { border: 1px solid %4; border-radius: 5px; padding: 4px 8px; }
    QScrollBar:horizontal { height: 7px; background: transparent; }
    QScrollBar::handle:horizontal { background: %4; border-radius: 3px; min-width: 30px; }
    QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
    QStatusBar { border-top: 1px solid %4; color: %3; padding: 4px; }
  )").arg(bg,text,muted,border,hover,selected));
}
}
