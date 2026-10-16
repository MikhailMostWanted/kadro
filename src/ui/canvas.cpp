#include "canvas.h"
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <cmath>
namespace kadro {
Canvas::Canvas(QWidget* parent) : QGraphicsView(parent), item_(scene_.addPixmap({})) {
  setScene(&scene_); setFrameShape(QFrame::NoFrame);
  setBackgroundBrush(QColor("#101113"));
  setRenderHint(QPainter::SmoothPixmapTransform);
  setDragMode(QGraphicsView::ScrollHandDrag);
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorViewCenter);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setFocusPolicy(Qt::NoFocus);
}
void Canvas::clearImage() {
  if (movie_) { movie_->stop(); delete movie_; movie_ = nullptr; }
  item_->setPixmap({}); item_->setRotation(0); scene_.setSceneRect({}); original_ = {}; resetTransform();
}
void Canvas::display(const QString& path,const Decoded& decoded,bool preserve) {
  if (movie_) { movie_->stop(); delete movie_; movie_ = nullptr; }
  const auto oldCenter = mapToScene(viewport()->rect().center());
  const auto oldSize = item_->pixmap().size();
  item_->setPixmap(QPixmap::fromImage(decoded.image)); original_ = decoded.original;
  if (!preserve) item_->setRotation(0);
  item_->setTransformOriginPoint(item_->boundingRect().center());
  scene_.setSceneRect(item_->sceneBoundingRect());
  if (!preserve || fitted_) fit();
  else if (!oldSize.isEmpty()) {
    double ratio = double(oldSize.width()) / decoded.image.width();
    scale(ratio,ratio); centerOn(oldCenter / ratio); notifyZoom();
  }
  if (decoded.animated) {
    movie_ = new QMovie(path,QByteArray(),this);
    movie_->setCacheMode(QMovie::CacheNone);
    movie_->setScaledSize(decoded.image.size());
    connect(movie_,&QMovie::frameChanged,this,[this] { item_->setPixmap(movie_->currentPixmap()); });
    movie_->start();
  }
}
void Canvas::fit() {
  if (!hasImage()) return;
  fitted_ = true; resetTransform();
  const auto r = item_->sceneBoundingRect();
  const double fitScale = std::min(double(qMax(1,viewport()->width()-32))/r.width(),double(qMax(1,viewport()->height()-32))/r.height());
  const double native = original_.isValid() ? double(original_.width())/item_->pixmap().width()/devicePixelRatioF() : 1.0;
  const double s = std::min(fitScale,native); scale(s,s); centerOn(r.center()); notifyZoom();
}
void Canvas::actualSize() {
  if (!hasImage()) return;
  fitted_ = false; resetTransform();
  const double s = double(original_.width())/item_->pixmap().width()/devicePixelRatioF();
  scale(s,s); centerOn(item_->sceneBoundingRect().center()); notifyZoom();
}
void Canvas::rotateClockwise() {
  if (!hasImage()) return;
  item_->setRotation(int(item_->rotation()+90)%360); scene_.setSceneRect(item_->sceneBoundingRect()); fit();
}
void Canvas::zoomBy(double factor) {
  if (!hasImage()) return;
  const double current = transform().m11();
  const double ratio = original_.isValid() ? double(item_->pixmap().width())/original_.width() : 1.;
  const double next = current*factor*ratio*devicePixelRatioF();
  if (next < 0.01 || next > 32.) return;
  fitted_ = false; scale(factor,factor); notifyZoom();
}
void Canvas::notifyZoom() {
  if (!hasImage() || !original_.isValid()) return;
  emit zoomChanged(qRound(transform().m11()*item_->pixmap().width()/original_.width()*devicePixelRatioF()*100));
}
void Canvas::toggleAnimation() { if (movie_) movie_->setPaused(movie_->state() == QMovie::Running); }
void Canvas::resizeEvent(QResizeEvent* e) { QGraphicsView::resizeEvent(e); if (fitted_) fit(); }
void Canvas::wheelEvent(QWheelEvent* e) {
  const double delta = e->angleDelta().y() ? e->angleDelta().y()/120. : e->pixelDelta().y()/60.;
  zoomBy(std::pow(1.18,delta)); e->accept();
}
void Canvas::mouseDoubleClickEvent(QMouseEvent* e) { if (e->button()==Qt::LeftButton) emit fullScreenRequested(); else QGraphicsView::mouseDoubleClickEvent(e); }
}
