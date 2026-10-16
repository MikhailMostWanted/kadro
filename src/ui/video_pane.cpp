#include "video_pane.h"
#include "decoder.h"
#include <QCryptographicHash>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaMetaData>
#include <QMouseEvent>
#include <QSettings>
#include <QVBoxLayout>
namespace kadro {
static QString timeText(qint64 ms) {
  auto s = qMax<qint64>(0,ms/1000);
  return s>=3600 ? QString("%1:%2:%3").arg(s/3600).arg(s/60%60,2,10,QChar('0')).arg(s%60,2,10,QChar('0'))
                 : QString("%1:%2").arg(s/60).arg(s%60,2,10,QChar('0'));
}
VideoPane::VideoPane(QWidget* parent) : QWidget(parent) {
  auto* box = new QVBoxLayout(this); box->setContentsMargins(0,0,0,0); box->setSpacing(0);
  player_ = new QMediaPlayer(this); audio_ = new QAudioOutput(this);
  audio_->setVolume(QSettings().value("video/volume",0.7).toFloat());
  audio_->setMuted(QSettings().value("video/muted",false).toBool());
  player_->setAudioOutput(audio_);
  surface_ = new QVideoWidget(this); surface_->setFocusPolicy(Qt::NoFocus);
  surface_->installEventFilter(this); player_->setVideoOutput(surface_); box->addWidget(surface_,1);
  auto* bar = new QWidget(this); bar->setObjectName("videoBar");
  auto* controls = new QHBoxLayout(bar); controls->setContentsMargins(18,10,18,10);
  play_ = new QPushButton("Пауза",bar); play_->setMinimumWidth(80); play_->setFocusPolicy(Qt::NoFocus);
  seek_ = new QSlider(Qt::Horizontal,bar); seek_->setRange(0,10000); seek_->setFocusPolicy(Qt::NoFocus);
  seek_->setAccessibleName("Позиция видео");
  time_ = new QLabel("0:00 / 0:00",bar); time_->setMinimumWidth(115);
  auto* mute = new QPushButton("Звук",bar); mute->setCheckable(true); mute->setChecked(!audio_->isMuted()); mute->setFocusPolicy(Qt::NoFocus);
  auto* volume = new QSlider(Qt::Horizontal,bar); volume->setRange(0,100); volume->setValue(qRound(audio_->volume()*100));
  volume->setFixedWidth(80); volume->setAccessibleName("Громкость"); volume->setFocusPolicy(Qt::NoFocus);
  auto* speed = new QComboBox(bar); speed->setFocusPolicy(Qt::NoFocus); speed->setAccessibleName("Скорость воспроизведения");
  for (double rate : {0.5,0.75,1.,1.25,1.5,2.}) speed->addItem(QString::number(rate)+"×",rate);
  speed->setCurrentIndex(2);
  controls->addWidget(play_); controls->addWidget(seek_,1); controls->addWidget(time_); controls->addWidget(mute); controls->addWidget(volume); controls->addWidget(speed);
  box->addWidget(bar);
  connect(play_,&QPushButton::clicked,this,&VideoPane::toggle);
  connect(mute,&QPushButton::toggled,this,[this](bool on){ audio_->setMuted(!on); QSettings().setValue("video/muted",!on); });
  connect(volume,&QSlider::valueChanged,this,[this](int value){ audio_->setVolume(value/100.0f); QSettings().setValue("video/volume",value/100.0f); });
  connect(speed,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,speed](int){ player_->setPlaybackRate(speed->currentData().toDouble()); });
  connect(seek_,&QSlider::sliderReleased,this,[this]{ player_->setPosition(qint64(seek_->value()/10000.0*player_->duration())); });
  connect(player_,&QMediaPlayer::positionChanged,this,[this]{ updateTime(); });
  connect(player_,&QMediaPlayer::durationChanged,this,[this]{ updateTime(); });
  connect(player_,&QMediaPlayer::seekableChanged,seek_,&QSlider::setEnabled);
  connect(player_,&QMediaPlayer::playbackStateChanged,this,[this](auto state){ play_->setText(state==QMediaPlayer::PlayingState ? "Пауза" : "Играть"); });
  connect(player_,&QMediaPlayer::errorOccurred,this,[this](auto,const QString& error){ emit failed("Не удалось воспроизвести видео. " + error); });
  connect(player_,&QMediaPlayer::mediaStatusChanged,this,[this](auto status){
    if (status==QMediaPlayer::LoadedMedia && resume_>0 && player_->isSeekable()) { player_->setPosition(qMin(resume_,qMax<qint64>(0,player_->duration()-5000))); resume_=0; }
    if (status==QMediaPlayer::EndOfMedia) { resume_=0; emit ended(); }
  });
}
VideoPane::~VideoPane() { savePosition(); }
void VideoPane::open(const QString& path) {
  stop();
  resumeKey_ = QString::fromLatin1(QCryptographicHash::hash(imageKey(path,0).toUtf8(),QCryptographicHash::Sha256).toHex());
  auto object = QJsonDocument::fromJson(QSettings().value("video/positions").toByteArray()).object();
  resume_ = QSettings().value("video/resume",true).toBool() ? qint64(object.value(resumeKey_).toDouble()) : 0;
  seek_->setValue(0); seek_->setEnabled(false);
  player_->setSource(QUrl::fromLocalFile(path)); player_->play();
}
void VideoPane::savePosition() {
  if (resumeKey_.isEmpty() || player_->duration()<=0) return;
  QSettings settings;
  auto object = QJsonDocument::fromJson(settings.value("video/positions").toByteArray()).object();
  if (object.size()>=64 && !object.contains(resumeKey_)) object.remove(object.begin().key());
  qint64 p=player_->position();
  if (p<3000 || p>player_->duration()-5000) object.remove(resumeKey_); else object.insert(resumeKey_,double(p));
  settings.setValue("video/positions",QJsonDocument(object).toJson(QJsonDocument::Compact));
}
void VideoPane::stop() { savePosition(); resumeKey_.clear(); resume_=0; player_->stop(); player_->setSource(QUrl()); }
void VideoPane::toggle() { if (player_->playbackState()==QMediaPlayer::PlayingState) player_->pause(); else player_->play(); }
void VideoPane::seekRelative(qint64 delta) { if (player_->isSeekable()) player_->setPosition(qBound<qint64>(0,player_->position()+delta,player_->duration())); }
void VideoPane::updateTime() {
  time_->setText(timeText(player_->position())+" / "+timeText(player_->duration()));
  if (!seek_->isSliderDown() && player_->duration()>0) seek_->setValue(int(player_->position()*10000.0/player_->duration()));
}
QString VideoPane::details() const {
  auto m = player_->metaData(); const auto size=m.value(QMediaMetaData::Resolution).toSize();
  return QString("%1 × %2\nДлительность: %3\nСкорость: %4×").arg(size.width()).arg(size.height()).arg(timeText(player_->duration())).arg(player_->playbackRate());
}
bool VideoPane::eventFilter(QObject* o,QEvent* e) {
  if (o==surface_ && e->type()==QEvent::MouseButtonDblClick) { emit fullScreenRequested(); return true; }
  return QWidget::eventFilter(o,e);
}
}
