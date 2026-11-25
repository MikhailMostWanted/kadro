#include "decoder.h"
#include "folder_model.h"
#include "image_store.h"
#include "platform/desktop.h"
#include "platform/settings_profile.h"
#include "ui/window.h"
#include <QApplication>
#include <QBuffer>
#include <QFile>
#include <QImageReader>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVideoSink>
#include <QVideoFrame>
using namespace kadro;
namespace {
void u32(QByteArray& b,quint32 n){for(int i=0;i<4;++i)b.append(char(n>>(8*i)));}
void u16(QByteArray& b,quint16 n){b.append(char(n));b.append(char(n>>8));}
QByteArray chunk(const char* type,QByteArray data){QByteArray b(type,4);u32(b,quint32(data.size()));b+=data;if(data.size()%2)b.append('\0');return b;}
QByteArray list(const char* type,QByteArray data){return chunk("LIST",QByteArray(type,4)+data);}
// Self-contained MJPEG AVI fixture: real video frames, no ffmpeg CLI/download.
bool makeVideo(const QString& path,int count=12){
  const int w=320,h=180;
  QByteArray movi,idx;quint32 offset=4;
  for(int i=0;i<count;++i){
    QImage image(w,h,QImage::Format_RGB32);image.fill(QColor::fromHsv(i*24%360,180,190));
    QPainter painter(&image);painter.setPen(Qt::white);painter.drawText(image.rect(),Qt::AlignCenter,QString::number(i));painter.end();
    QByteArray jpeg;QBuffer buffer(&jpeg);buffer.open(QIODevice::WriteOnly);if(!image.save(&buffer,"JPEG"))return false;
    auto data=chunk("00dc",jpeg);idx.append("00dc",4);u32(idx,0x10);u32(idx,offset);u32(idx,quint32(jpeg.size()));offset+=quint32(data.size());movi+=data;
  }
  QByteArray avih;for(quint32 n:{200000u,1000000u,0u,0x10u,quint32(count),0u,1u,quint32(w*h*3),quint32(w),quint32(h),0u,0u,0u,0u})u32(avih,n);
  QByteArray strh("vidsMJPG",8);u32(strh,0);u16(strh,0);u16(strh,0);
  for(quint32 n:{0u,1u,5u,0u,quint32(count),quint32(w*h*3),0xffffffffu,0u})u32(strh,n);
  u16(strh,0);u16(strh,0);u16(strh,w);u16(strh,h);
  QByteArray strf;u32(strf,40);u32(strf,w);u32(strf,h);u16(strf,1);u16(strf,24);strf.append("MJPG",4);
  for(quint32 n:{quint32(w*h*3),0u,0u,0u,0u})u32(strf,n);
  auto body=QByteArray("AVI ",4)+list("hdrl",chunk("avih",avih)+list("strl",chunk("strh",strh)+chunk("strf",strf)))+list("movi",movi)+chunk("idx1",idx);
  QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(chunk("RIFF",body))>0;
}
bool photo(const QString& path,QSize size=QSize(800,600)){
  QImage image(size,QImage::Format_RGB32);QPainter p(&image);
  QLinearGradient g(0,0,size.width(),size.height());g.setColorAt(0,QColor("#214354"));g.setColorAt(1,QColor("#bed7d0"));p.fillRect(image.rect(),g);
  p.setPen(QPen(Qt::white,2));p.drawEllipse(QPointF(size.width()*.65,size.height()*.4),size.width()*.14,size.width()*.14);p.end();return image.save(path);
}
}
class UiTests : public QObject {
  Q_OBJECT
  QTemporaryDir settings_;
private slots:
  void initTestCase(){
    QVERIFY(settings_.isValid());QCoreApplication::setOrganizationName("KadroTests");QCoreApplication::setApplicationName("KadroTests");
      QApplication::setApplicationDisplayName("Wanted View");
      QApplication::setStyle("Fusion");
    QString error;QVERIFY2(configureSettingsProfile(settings_.path(),&error),qPrintable(error));
    QImageReader::setAllocationLimit(128);
  }
  void decodeAndCorruption(){
    QTemporaryDir dir;QVERIFY(photo(dir.filePath("image.png")));
    auto d=decodeImage(dir.filePath("image.png"),200);QVERIFY(d.error.isEmpty());QCOMPARE(d.original,QSize(800,600));QCOMPARE(d.image.size(),QSize(200,150));
    QFile broken(dir.filePath("broken.png"));QVERIFY(broken.open(QIODevice::WriteOnly));broken.write("not an image");broken.close();
    auto b=decodeImage(broken.fileName(),200);QVERIFY(b.image.isNull());QVERIFY(!b.error.isEmpty());
    QVERIFY(decodeImage(dir.filePath("missing.png"),100).image.isNull());
  }
  void folderFilterAndNaturalSort(){
    QTemporaryDir dir;for(const QString name:{"IMG10.png","IMG2.png","IMG1.png"})QVERIFY(photo(dir.filePath(name),QSize(32,24)));
    QFile video(dir.filePath("clip.mov"));QVERIFY(video.open(QIODevice::WriteOnly));video.write("classification fixture only");video.close();
    QFile text(dir.filePath("notes.txt"));QVERIFY(text.open(QIODevice::WriteOnly));text.close();
    QDir().mkpath(dir.filePath("nested"));QVERIFY(photo(dir.filePath("nested/ignored.png")));
    FolderModel model;MediaFilter filter;filter.setSourceModel(&model);QSignalSpy done(&model,&FolderModel::scanFinished);
    model.scan(dir.path());QTRY_COMPARE(done.count(),1);QCOMPARE(model.rowCount(),4);
    filter.setKind(Kind::Photo);QCOMPARE(filter.rowCount(),3);QCOMPARE(filter.index(0,0).data().toString(),QString("IMG1.png"));QCOMPARE(filter.index(1,0).data().toString(),QString("IMG2.png"));
    filter.setKind(Kind::Video);QCOMPARE(filter.rowCount(),1);QCOMPARE(filter.index(0,0).data().toString(),QString("clip.mov"));
      filter.setKind(Kind::Unknown);QCOMPARE(filter.rowCount(),4);
      filter.setOrder(MediaFilter::Sort::Size);
      QCOMPARE(filter.index(0, 0).data().toString(), QString("IMG1.png"));
      filter.setOrder(MediaFilter::Sort::Name);
      QCOMPARE(filter.index(0, 0).data().toString(), QString("clip.mov"));
      QCOMPARE(filter.index(2, 0).data().toString(), QString("IMG2.png"));
  }
  void staleFolderScanIsIgnored(){
    QTemporaryDir first,second;for(int i=0;i<400;++i){QFile f(first.filePath(QString::number(i)+".jpg"));QVERIFY(f.open(QIODevice::WriteOnly));}
    QVERIFY(photo(second.filePath("latest.png")));
    FolderModel model;model.scan(first.path());model.scan(second.path());QTRY_VERIFY(!model.isScanning());QCOMPARE(model.folder(),second.path());QCOMPARE(model.rowCount(),1);QCOMPARE(model.index(0).data().toString(),QString("latest.png"));
  }
  void boundedQueueAndCachedErrors(){
    QTemporaryDir dir;QStringList paths;for(int i=0;i<8;++i){auto path=dir.filePath(QString::number(i)+".png");QVERIFY(photo(path));paths<<path;}
    ImageStore store(1,2);QSignalSpy done(&store,&ImageStore::ready);
    for(const auto& path:paths){store.request(path,100);QVERIFY(store.queuedCount()<=2);}
    store.request(paths.last(),100,true);QTRY_VERIFY(store.cached(paths.last(),100)!=nullptr);QVERIFY(store.cacheCost()<=1024);
    const QString bad=dir.filePath("bad.png");store.request(bad,100,true);QTRY_VERIFY(store.cached(bad,100)!=nullptr);QVERIFY(!store.cached(bad,100)->error.isEmpty());QVERIFY(done.count()>0);
  }
  void safeRename(){
    QTemporaryDir dir;const auto source=dir.filePath("Оригинал.png");QVERIFY(photo(source));QVERIFY(photo(dir.filePath("occupied.png")));QString target;
    QVERIFY(!desktop::renameFile(source,"occupied.png",&target).isEmpty());QVERIFY(QFileInfo::exists(source));
    QVERIFY(!desktop::renameFile(source,"../escape.png",&target).isEmpty());QVERIFY(!desktop::renameFile(source,"CON.png",&target).isEmpty());
    QVERIFY(desktop::renameFile(source,"Новое имя.png",&target).isEmpty());QVERIFY(QFileInfo::exists(target));QVERIFY(!QFileInfo::exists(source));
  }
  void safeTrashRejectsNonFiles(){
    QTemporaryDir dir;
    const auto folder = dir.filePath("photo.png");
    QVERIFY(QDir().mkpath(folder));
    const auto child = QDir(folder).filePath("keep.png");
    QVERIFY(photo(child));
    QVERIFY(!desktop::trashFile(folder).isEmpty());
    QVERIFY(QFileInfo(child).isFile());
    QVERIFY(!desktop::trashFile(dir.filePath("absent.png")).isEmpty());
  }
#ifdef Q_OS_WIN
    void windowsAssociation() {
      const auto installed = qEnvironmentVariable("KADRO_CI_INSTALLED_EXE");
      if (installed.isEmpty()) QSKIP("Requires the disposable Windows installer job");
      QVERIFY(QFileInfo(installed).isFile());
      QSettings classes("HKEY_CURRENT_USER\\Software\\Classes", QSettings::NativeFormat);
      QSettings registered("HKEY_CURRENT_USER\\Software\\RegisteredApplications", QSettings::NativeFormat);
      QSettings cap("HKEY_CURRENT_USER\\Software\\Kadro\\Capabilities", QSettings::NativeFormat);
      QSettings choice("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.png\\UserChoice",
                       QSettings::NativeFormat);
      const auto currentDefault = choice.value("ProgId");
      QVERIFY(!registered.contains("KADRO") && cap.allKeys().isEmpty());
      QVERIFY(!classes.contains("Kadro.Media/shell/open/command/Default"));
      QVERIFY(!classes.contains("Applications/Kadro.exe/shell/open/command/Default"));
      QVERIFY(desktop::registerViewer(installed));
      const QString executable = QDir::toNativeSeparators(installed);
      const QString command = '"' + executable + "\" \"%1\"";
      QCOMPARE(classes.value("Kadro.Media/shell/open/command/Default").toString(), command);
      QCOMPARE(classes.value("Applications/Kadro.exe/shell/open/command/Default").toString(), command);
      QCOMPARE(classes.value("Kadro.Media/DefaultIcon/Default").toString(), '"' + executable + "\",0");
      QCOMPARE(registered.value("KADRO").toString(), QString("Software\\Kadro\\Capabilities"));
      QCOMPARE(cap.value("FileAssociations/.png").toString(), QString("Kadro.Media"));
      QCOMPARE(cap.value("FileAssociations/.mp4").toString(), QString("Kadro.Media"));
      QCOMPARE(choice.value("ProgId"), currentDefault);
    }
    void windowsTrashOnDisposableRunner() {
      if (!qEnvironmentVariableIsSet("KADRO_CI_INSTALLED_EXE"))
        QSKIP("Requires the disposable Windows installer job");
      QTemporaryDir dir;
      const auto fixture = dir.filePath("Wanted View recycle bin fixture.png");
      QVERIFY(photo(fixture));
      const QString error = desktop::trashFile(fixture);
      if (!error.isEmpty()) {
        QVERIFY(QImageReader(fixture).canRead());
        QSKIP("The runner did not accept a recycle-bin move. The fixture was preserved.");
      }
      QVERIFY(!QFileInfo::exists(fixture));
    }
#endif
  void windowNavigationAndScreenshot(){
    QTemporaryDir dir;QVERIFY(photo(dir.filePath("Кадр1.png"),QSize(1600,900)));QVERIFY(photo(dir.filePath("Кадр2.png"),QSize(600,800)));
    Window window;window.show();window.openPath(dir.filePath("Кадр1.png"));
    QCOMPARE(window.currentPath(),dir.filePath("Кадр1.png")); // selected file assigned without awaiting folder scan
    QTRY_VERIFY(!window.folderModel()->isScanning());QTRY_VERIFY(window.canvas()->hasImage());
    window.navigate(1);QCOMPARE(window.currentPath(),dir.filePath("Кадр2.png"));QTRY_COMPARE(window.canvas()->originalSize(),QSize(600,800));
    for(int i=0;i<11;++i)window.navigate(1);QTRY_COMPARE(window.canvas()->originalSize(),QSize(1600,900));
    window.canvas()->rotateClockwise();window.canvas()->fit();QVERIFY(window.canvas()->hasImage());
    window.canvas()->rotateClockwise();window.canvas()->rotateClockwise();window.canvas()->rotateClockwise();
    QDir().mkpath("test-artifacts");QVERIFY(window.grab().save("test-artifacts/viewer.png"));
    QFile::remove("test-artifacts/photo.png");QVERIFY(QFile::copy(dir.filePath("Кадр1.png"),"test-artifacts/photo.png"));
    window.close();
  }
    void keyboardAndNarrowWindow() {
      QTemporaryDir dir;
      const QString first = dir.filePath(QString(140, QChar('a')) + ".png");
      const QString second = dir.filePath("2.png");
      QVERIFY(photo(first));
      QVERIFY(photo(second));
      QSettings().setValue("view/filmstrip", false);
      QSettings().setValue("view/info", false);
      Window window;
      window.resize(720, 480);
      window.show();
      window.activateWindow();
      window.setFocus();
      window.openPath(first);
      QTRY_VERIFY(window.hasDecodedMedia() && !window.folderModel()->isScanning());
      QTRY_COMPARE(window.width(), 720);
      auto* title = window.findChild<QLabel*>("fileTitle");
      QVERIFY(title);
      QCOMPARE(title->accessibleName(), QFileInfo(first).fileName());
      QVERIFY(title->text().contains(QChar(0x2026)));
      auto* header = window.findChild<QWidget*>("topBar");
      QVERIFY(header && header->isVisible());
      QTest::keyClick(&window, Qt::Key_Tab);
      QTRY_VERIFY(window.focusWidget() && window.focusWidget() != &window);
      QVERIFY(header->isVisible());
      QTest::keyClick(window.focusWidget(), Qt::Key_H);
      QTRY_VERIFY(!header->isVisible());
      QTest::keyClick(&window, Qt::Key_Escape);
      QTRY_VERIFY(header->isVisible());
      QTest::keyClick(&window, Qt::Key_Right);
      QCOMPARE(window.currentPath(), second);
      QTRY_VERIFY(window.hasDecodedMedia());
      QTest::keyClick(&window, Qt::Key_T);
      auto* strip = window.findChild<QListView*>("filmstrip");
      QTRY_VERIFY(strip && strip->isVisible());
      strip->setFocus();
      QTest::keyClick(strip, Qt::Key_Right);
      QTRY_COMPARE(window.currentPath(), first);
      QTRY_VERIFY(window.hasDecodedMedia());
      QDir().mkpath("test-artifacts");
      QVERIFY(window.grab().save("test-artifacts/viewer-compact.png"));
      window.close();
      QSettings().remove("window/geometry");
      QSettings().remove("view/filmstrip");
    }
    void errorStatesRecover() {
      QTemporaryDir dir;
      const QString damaged = dir.filePath("broken.heic");
      QFile file(damaged);
      QVERIFY(file.open(QIODevice::WriteOnly));
      QCOMPARE(file.write("corrupt"), 7);
      file.close();
      const auto good = dir.filePath("valid.png");
      QVERIFY(photo(good));
      Window window;
      window.show();
      auto* heading = window.findChild<QLabel*>("emptyHeading");
      QVERIFY(heading && heading->isVisible());
      window.openPath(dir.filePath("missing.png"));
      QCOMPARE(heading->text(), QString("Файл или папка недоступны."));
      window.openPath(damaged);
      QTRY_COMPARE(heading->text(), QString("Не удалось открыть изображение"));
      const auto* body = window.findChild<QLabel*>("messageBody");
      QVERIFY(body && body->text().contains("кодек"));
      QTRY_VERIFY(heading->height() >= heading->heightForWidth(heading->width()));
      QTRY_VERIFY(body->height() >= body->heightForWidth(body->width()));
      QVERIFY(!window.hasDecodedMedia());
      QTRY_VERIFY(!window.folderModel()->isScanning());
      window.navigate(1);
      QTRY_COMPARE(window.currentPath(), good);
      QTRY_VERIFY(window.hasDecodedMedia());
      QVERIFY(!heading->isVisible());
    }
    void trashDialogCancelsAndRejectsReplacement() {
      QTemporaryDir dir;
      const QString file = dir.filePath("Trash test.png");
      const QString original = dir.filePath("Original copy.png");
      QVERIFY(photo(file));
      Window window;
      window.show();
      window.openPath(file);
      QTRY_VERIFY(window.hasDecodedMedia() && !window.folderModel()->isScanning());
      QAction* trash = nullptr;
      for (auto* action : window.actions()) {
        if (action->text() == "В корзину…") trash = action;
      }
      QVERIFY(trash);
      bool cancelledByDefault = false;
      QTimer::singleShot(0, &window, [&] {
        if (auto* dialog = window.findChild<QMessageBox*>()) {
          cancelledByDefault = dialog->defaultButton() && dialog->defaultButton()->text() == "Отмена";
          if (cancelledByDefault) QTest::keyClick(dialog, Qt::Key_Return);
          else dialog->reject();
        }
      });
      trash->trigger();
      QVERIFY(cancelledByDefault);
      QVERIFY(QFileInfo::exists(file));

      bool replaced = false;
      QTimer::singleShot(0, &window, [&] {
        if (auto* dialog = window.findChild<QMessageBox*>()) {
          replaced = QFile::rename(file, original) && photo(file, QSize(320, 180));
          for (auto* control : dialog->buttons()) {
            if (replaced && dialog->buttonRole(control) == QMessageBox::AcceptRole) {
              control->click();
              return;
            }
          }
          dialog->reject();
        }
      });
      trash->trigger();
      QVERIFY(replaced);
      QVERIFY(QFileInfo::exists(original));
      QCOMPARE(QImageReader(file).size(), QSize(320, 180));
    }
  void actualVideoPlayback(){
    QTemporaryDir dir;auto path=dir.filePath("sample.avi");QVERIFY(makeVideo(path));
    QDir().mkpath("test-artifacts");QFile::remove("test-artifacts/video.avi");QVERIFY(QFile::copy(path,"test-artifacts/video.avi"));
    VideoPane pane;pane.resize(640,400);pane.show();bool frame=false;
    QVERIFY(pane.player()->videoSink()!=nullptr);
    connect(pane.player()->videoSink(),&QVideoSink::videoFrameChanged,&pane,[&frame](const QVideoFrame& value){if(value.isValid())frame=true;});
    pane.open(path);QTRY_VERIFY_WITH_TIMEOUT(frame,15000);QVERIFY(pane.player()->error()==QMediaPlayer::NoError);
    pane.toggle();QCOMPARE(pane.player()->playbackState(),QMediaPlayer::PausedState);pane.seekRelative(300);pane.stop();QCOMPARE(pane.player()->source(),QUrl());
  }
    void keyboardSeekAndForgottenPosition() {
      QTemporaryDir dir;
      const auto file = dir.filePath("seek.avi");
      QVERIFY(makeVideo(file, 60));
      QSettings().setValue("video/resume", true);
      QSettings().remove("video/positions");
      VideoPane pane;
      pane.show();
      pane.open(file);
      QTRY_VERIFY(pane.player()->isSeekable());
      pane.player()->pause();
      QSlider* seek = nullptr;
      for (auto* slider : pane.findChildren<QSlider*>()) {
        if (slider->accessibleName() == "Позиция видео") seek = slider;
      }
      QVERIFY(seek && seek->isEnabled());
      QTest::keyClick(seek, Qt::Key_Home);
      QTRY_COMPARE(pane.player()->position(), qint64(0));
      QTest::keyClick(seek, Qt::Key_PageUp);
      QTRY_COMPARE(pane.player()->position(), qint64(1200));
      pane.player()->setPosition(5000);
      pane.forgetPosition();
      pane.stop();
      QVERIFY(!QSettings().contains("video/positions"));
    }
  void resumeTwelveSecondVideoAtFiveSeconds(){
    QTemporaryDir dir;const auto path=dir.filePath("resume12.avi");QVERIFY(makeVideo(path,60));
    QSettings settings;settings.setValue("video/resume",true);settings.setValue("video/positions",QByteArray("{}"));settings.sync();
    VideoPane pane;pane.open(path);
    QTRY_COMPARE_WITH_TIMEOUT(pane.player()->duration(),12000,5000);
    QTRY_VERIFY_WITH_TIMEOUT(pane.player()->isSeekable(),5000);
    pane.player()->pause();pane.player()->setPosition(5000);
    QCOMPARE(pane.player()->position(),5000);pane.stop();
    const auto persisted=QSettings().value("video/positions").toByteArray();
    QVERIFY(persisted.contains(":5000"));
    bool restored=false;QList<qint64> positions;
    connect(pane.player(),&QMediaPlayer::positionChanged,&pane,[&](qint64 position){
      positions.append(position);
      if(position>=5000 && position<5600)restored=true;
    });
    pane.open(path);
    // Starting from zero cannot naturally reach 5 seconds within this bound.
    QTRY_VERIFY_WITH_TIMEOUT(restored,2000);
    pane.player()->pause();QVERIFY(pane.player()->position()>=5000);
    QVERIFY(positions.contains(5000));qInfo()<<"Resume position signals (ms):"<<positions;
    pane.stop();
  }
};
QTEST_MAIN(UiTests)
#include "ui_tests.moc"
