#pragma once
#include "decoder.h"
#include <QGraphicsPixmapItem>
#include <QGraphicsView>
#include <QMovie>
namespace kadro {
class Canvas : public QGraphicsView {
  Q_OBJECT
public:
  explicit Canvas(QWidget* parent = nullptr);
  void display(const QString& path, const Decoded& decoded, bool preserveView = false);
  void clearImage();
  void fit();
  void actualSize();
  void rotateClockwise();
  void zoomBy(double factor);
  void toggleAnimation();
  QSize originalSize() const { return original_; }
  bool hasImage() const { return !item_->pixmap().isNull(); }
signals:
  void fullScreenRequested();
  void zoomChanged(int percent);
protected:
  void resizeEvent(QResizeEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  void mouseDoubleClickEvent(QMouseEvent*) override;
private:
  void notifyZoom();
  QGraphicsScene scene_;
  QGraphicsPixmapItem* item_;
  QMovie* movie_ = nullptr;
  QSize original_;
  bool fitted_ = true;
};
}
