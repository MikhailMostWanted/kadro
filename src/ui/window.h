#pragma once
#include "folder_model.h"
#include "canvas.h"
#include "video_pane.h"
#include <QAction>
#include <QButtonGroup>
#include <QFileSystemWatcher>
#include <QLabel>
#include <QListView>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
namespace kadro {
class Window : public QMainWindow {
  Q_OBJECT
public:
  explicit Window(QWidget* parent=nullptr);
  ~Window() override;
  void openPath(const QString& path);
  QString currentPath() const { return current_; }
  FolderModel* folderModel() const { return model_; }
  MediaFilter* mediaFilter() const { return filter_; }
  Canvas* canvas() const { return canvas_; }
  void navigate(int delta);
  bool hasDecodedMedia() const {
    return (canvas_ && stage_->currentWidget()==canvas_ && canvas_->hasImage()) ||
      (video_ && stage_->currentWidget()==video_ && video_->player()->videoSink() && video_->player()->videoSink()->videoFrame().isValid());
  }
protected:
  void dragEnterEvent(QDragEnterEvent*) override;
  void dropEvent(QDropEvent*) override;
  void closeEvent(QCloseEvent*) override;
private:
  void buildUi();
  void buildActions();
  void applyTheme();
  void chooseFile();
  void chooseFolder();
  void refresh(const QString& preferred={});
  void showFile(const QString& path,bool force=false);
  void showMessage(const QString& heading,const QString& body);
  void selectKind(Kind kind);
  void syncSelection();
  int currentRow() const;
  QString pathAt(int row) const;
  void updateLabels();
  void updateInfo();
  void prefetch();
  void requestNative(bool actual);
  void toggleFullscreen();
  void toggleChrome();
  void toggleSlideshow();
  void renameCurrent();
  void trashCurrent();
  void revealCurrent();
  void defaultApplication();
  void toast(const QString& text);
  FolderModel* model_;
  MediaFilter* filter_;
  ImageStore* images_;
  Canvas* canvas_ = nullptr;
  VideoPane* video_ = nullptr; // intentionally lazy: photo launch never creates a player
  QStackedWidget* stage_ = nullptr;
  QWidget* messagePage_ = nullptr;
  QWidget* top_ = nullptr;
  QWidget* bottom_ = nullptr;
  QLabel* heading_ = nullptr;
  QLabel* message_ = nullptr;
  QLabel* title_ = nullptr;
  QLabel* subtitle_ = nullptr;
  QLabel* counter_ = nullptr;
  QLabel* scale_ = nullptr;
  QPlainTextEdit* info_ = nullptr;
  QListView* strip_ = nullptr;
  QButtonGroup* kinds_ = nullptr;
  QMenu* menu_ = nullptr;
  QAction* stripAction_ = nullptr;
  QAction* infoAction_ = nullptr;
  QAction* slideshowAction_ = nullptr;
  QFileSystemWatcher files_;
  QTimer refreshTimer_, slideshowTimer_;
  QString current_, preferred_, identity_;
  int requestedEdge_ = 2048;
  bool actualPending_ = false, preservePending_ = false;
  bool chromeHidden_ = false, closing_ = false;
  Decoded decoded_;
};
}
