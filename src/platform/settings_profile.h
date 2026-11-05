#pragma once
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryFile>

namespace kadro {
inline void addDiagnosticOptions(QCommandLineParser& parser) {
  parser.addOption({"isolated", "Отдельный процесс (для диагностики)"});
  parser.addOption({"settings-dir", "Отдельная папка настроек без общего IPC (диагностика)", "directory"});
  parser.addOption({"smoke-screenshot", "Сохранить снимок окна и выйти (диагностика)", "path"});
}

inline bool diagnosticInstance(const QCommandLineParser& parser) {
  return parser.isSet("isolated") || parser.isSet("settings-dir") || parser.isSet("smoke-screenshot");
}

// Configure before constructing any application QSettings/Window/VideoPane.
// This isolates app preferences, not arbitrary OS or filesystem access.
inline bool configureSettingsProfile(const QString& directory, QString* error = nullptr) {
  const auto fail = [&](const QString& message) {
    if(error)*error=message;
    return false;
  };
  if(directory.isEmpty() || !QDir::isAbsolutePath(directory))
    return fail("Папка настроек должна быть задана абсолютным путём.");
  const QString cleaned=QDir::cleanPath(directory);
  QDir profile(cleaned);
  if(profile.isRoot() || (!profile.canonicalPath().isEmpty() && QDir(profile.canonicalPath()).isRoot()))
    return fail("Корень диска нельзя использовать как папку настроек.");
  if(QFileInfo::exists(cleaned) && !QFileInfo(cleaned).isDir())
    return fail("Путь настроек не является папкой.");
  if(!QDir().mkpath(cleaned))return fail("Не удалось создать папку настроек.");
  profile.refresh();
  const QString canonical=profile.canonicalPath();
  if(canonical.isEmpty())return fail("Не удалось определить папку настроек.");
  const QString user=QDir(canonical).filePath("user");
  const QString system=QDir(canonical).filePath("system");
  for(const auto& scope : {user,system}) {
    // A pre-existing symlink must not silently redirect one scope outside this profile.
    const QFileInfo info(scope);
    if(info.isSymLink() || (info.exists() && !info.isDir()))
      return fail("Подпапка настроек должна быть обычной папкой: "+scope);
    if(!QDir().mkpath(scope))return fail("Не удалось создать подпапку настроек: "+scope);
    QTemporaryFile probe(QDir(scope).filePath(".kadro-write-check-XXXXXX"));
    if(!probe.open())return fail("Нет доступа на запись в папку настроек: "+scope);
  }
  // Both scopes must be usable before any global destination is changed.
  // Qt's organisation/system fallback stays inside this explicit INI profile.
  QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,user);
  QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,system);
  QSettings::setDefaultFormat(QSettings::IniFormat);
  if(error)error->clear();
  return true;
}
}
