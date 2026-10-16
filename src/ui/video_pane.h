#pragma once
#include <QAudioOutput>
#include <QComboBox>
#include <QLabel>
#include <QMediaPlayer>
#include <QPushButton>
#include <QSlider>
#include <QVideoWidget>
namespace kadro {
class VideoPane : public QWidget {
  Q_OBJECT
public:
  explicit VideoPane(QWidget* parent = nullptr);
  ~VideoPane() override;
  void open(const QString& path);
  void stop();
  void toggle();
  void seekRelative(qint64 milliseconds);
  QString details() const;
  QMediaPlayer* player() const { return player_; }
signals:
  void failed(const QString& message);
  void ended();
  void fullScreenRequested();
protected:
  bool eventFilter(QObject*,QEvent*) override;
private:
  void savePosition();
  void updateTime();
  QMediaPlayer* player_;
  QAudioOutput* audio_;
  QVideoWidget* surface_;
  QPushButton* play_;
  QSlider* seek_;
  QLabel* time_;
  QString resumeKey_;
  qint64 resume_ = 0;
};
}
