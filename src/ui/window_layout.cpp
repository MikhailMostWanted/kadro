#include "window.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace kadro {
class FileLabel final : public QLabel {
public:
  explicit FileLabel(QWidget* parent) : QLabel(parent) {
    setTextFormat(Qt::PlainText);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  }

  void setFullText(const QString& text) {
    fullText_ = text;
    setAccessibleName(text);
    setToolTip(text.isEmpty() ? QString() : "<span>" + text.toHtmlEscaped() + "</span>");
    updateText();
  }

protected:
  void resizeEvent(QResizeEvent* event) override {
    QLabel::resizeEvent(event);
    updateText();
  }

  void changeEvent(QEvent* event) override {
    QLabel::changeEvent(event);
    if (event->type() == QEvent::FontChange) updateText();
  }

private:
  void updateText() {
    setText(fontMetrics().elidedText(fullText_, Qt::ElideMiddle, contentsRect().width()));
  }
  QString fullText_;
};

namespace {
QPushButton* button(const QString& text, QWidget* parent, const QString& label = {}) {
  auto* control = new QPushButton(text, parent);
  control->setMinimumSize(44, 44);
  control->setCursor(Qt::PointingHandCursor);
  control->setFocusPolicy(Qt::StrongFocus);
  if (!label.isEmpty()) control->setAccessibleName(label);
  return control;
}

// The open build uses its own neutral palette. The preview reads the pinned kit
// from resources and needs neither a checkout nor network access at runtime.
QJsonObject viewColors(bool light) {
  const QJsonObject fallback = light
    ? QJsonObject{{"background", "#F3F3F1"}, {"surface", "#FFFFFF"}, {"surface_subtle", "#EEEFEC"},
                  {"text", "#1A1A1A"}, {"muted", "#575852"}, {"border", "#D6D7D2"},
                  {"control_border", "#71726B"}, {"action", "#20211F"}, {"action_hover", "#383A36"},
                  {"on_action", "#FFFFFF"}, {"focus", "#1648B9"}, {"error", "#852529"}}
    : QJsonObject{{"background", "#131313"}, {"surface", "#1C1C1C"}, {"surface_subtle", "#292929"},
                  {"text", "#F1F1EF"}, {"muted", "#BABCB5"}, {"border", "#3D3E39"},
                  {"control_border", "#83867E"}, {"action", "#F1F1EF"}, {"action_hover", "#D9DBD4"},
                  {"on_action", "#131313"}, {"focus", "#B3CAFF"}, {"error", "#F4AAAA"}};
#ifdef KADRO_PRIVATE_PREVIEW_BRAND
  QFile tokens(":/wanted-view/preview/native/tokens.json");
  if (tokens.open(QIODevice::ReadOnly)) {
    const auto colors = QJsonDocument::fromJson(tokens.readAll()).object().value("colors").toObject();
    return colors.value(light ? "light" : "dark").toObject();
  }
#endif
  return fallback;
}
}

void Window::buildUi() {
  auto* root = new QWidget(this);
  setCentralWidget(root);
  auto* layout = new QVBoxLayout(root);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  top_ = new QWidget(root);
  top_->setObjectName("topBar");
  auto* header = new QHBoxLayout(top_);
  header->setContentsMargins(12, 8, 12, 8);
  header->setSpacing(8);
  auto* icon = new QLabel(top_);
  icon->setPixmap(QIcon(":/wanted-view/wanted-view.png").pixmap(QSize(36, 36), devicePixelRatioF()));
  header->addWidget(icon);
  auto* brand = new QLabel(QApplication::applicationDisplayName(), top_);
  brand->setObjectName("brand");
  header->addWidget(brand);

  auto* divider = new QFrame(top_);
  headerDivider_ = divider;
  divider->setObjectName("divider");
  divider->setFixedSize(1, 28);
  header->addSpacing(4);
  header->addWidget(divider);
  header->addSpacing(4);
  auto* meta = new QVBoxLayout;
  meta->setSpacing(0);
  title_ = new FileLabel(top_);
  title_->setObjectName("fileTitle");
  subtitle_ = new FileLabel(top_);
  subtitle_->setObjectName("muted");
  meta->addStretch();
  meta->addWidget(title_);
  meta->addWidget(subtitle_);
  meta->addStretch();
  header->addLayout(meta, 1);

  kinds_ = new QButtonGroup(this);
  filters_ = new QWidget(top_);
  filters_->setObjectName("segments");
  auto* segments = new QHBoxLayout(filters_);
  segments->setContentsMargins(4, 4, 4, 4);
  segments->setSpacing(2);
  const QStringList labels = {"Все", "Фото", "Видео"};
  for (int i = 0; i < labels.size(); ++i) {
    auto* control = button(labels.at(i), filters_);
    control->setObjectName("segment");
    control->setCheckable(true);
    kinds_->addButton(control, i);
    segments->addWidget(control);
  }
  kinds_->button(0)->setChecked(true);
  header->addWidget(filters_);
  connect(kinds_, &QButtonGroup::idClicked, this, [this](int id) {
    selectKind(id == 1 ? Kind::Photo : id == 2 ? Kind::Video : Kind::Unknown);
  });
  open_ = button("Открыть…", top_);
  open_->setObjectName("openFile");
  open_->setToolTip("Открыть файл (" + QKeySequence(QKeySequence::Open).toString(QKeySequence::NativeText) + ")");
  header->addWidget(open_);
  connect(open_, &QPushButton::clicked, this, &Window::chooseFile);
  auto* more = new QToolButton(top_);
  more->setText("⋯");
  more->setMinimumSize(44, 44);
  more->setToolTip("Действия и настройки (F10)");
  more->setAccessibleName("Действия и настройки");
  more->setFocusPolicy(Qt::StrongFocus);
  more->setPopupMode(QToolButton::InstantPopup);
  menu_ = new QMenu(more);
  more->setMenu(menu_);
  header->addWidget(more);
  new QShortcut(QKeySequence(Qt::Key_F10), more, [more] { more->showMenu(); });
  layout->addWidget(top_);

  auto* body = new QHBoxLayout;
  body->setContentsMargins(0, 0, 0, 0);
  body->setSpacing(0);
  stage_ = new QStackedWidget(root);
  stage_->setObjectName("stage");
  canvas_ = new Canvas(stage_);
  stage_->addWidget(canvas_);

  messagePage_ = new QWidget(stage_);
  messagePage_->setObjectName("messagePage");
  auto* empty = new QVBoxLayout(messagePage_);
  empty->setAlignment(Qt::AlignVCenter);
  empty->setContentsMargins(24, 24, 24, 24);
  empty->setSpacing(12);
  welcomeMark_ = new QLabel(messagePage_);
  welcomeMark_->setFixedSize(76, 64);
  welcomeMark_->setAlignment(Qt::AlignCenter);
  empty->addWidget(welcomeMark_, 0, Qt::AlignHCenter);
  heading_ = new QLabel(messagePage_);
  heading_->setTextFormat(Qt::PlainText);
  heading_->setObjectName("emptyHeading");
  heading_->setAlignment(Qt::AlignCenter);
  heading_->setWordWrap(true);
  heading_->setMinimumWidth(372);
  heading_->setMaximumWidth(432);
  QFont headingFont = heading_->font();
  headingFont.setPointSizeF(qMax(22.0, headingFont.pointSizeF() * 1.8));
  headingFont.setWeight(QFont::DemiBold);
  heading_->setFont(headingFont);
  empty->addWidget(heading_, 0, Qt::AlignHCenter);
  message_ = new QLabel(messagePage_);
  message_->setTextFormat(Qt::PlainText);
  message_->setAlignment(Qt::AlignCenter);
  message_->setWordWrap(true);
  message_->setMinimumWidth(340);
  message_->setMaximumWidth(432);
  message_->setObjectName("muted");
  empty->addWidget(message_, 0, Qt::AlignHCenter);

  messageActions_ = new QWidget(messagePage_);
  auto* row = new QHBoxLayout(messageActions_);
  row->setContentsMargins(0, 4, 0, 0);
  row->setSpacing(8);
  auto* file = button("Открыть файл", messageActions_);
  file->setObjectName("primary");
  row->addWidget(file);
  connect(file, &QPushButton::clicked, this, &Window::chooseFile);
  auto* folder = button("Выбрать папку", messageActions_);
  row->addWidget(folder);
  connect(folder, &QPushButton::clicked, this, &Window::chooseFolder);
  empty->addWidget(messageActions_, 0, Qt::AlignHCenter);
  stage_->addWidget(messagePage_);
  body->addWidget(stage_, 1);

  info_ = new QPlainTextEdit(root);
  info_->setReadOnly(true);
  info_->setFixedWidth(260);
  info_->setObjectName("infoPanel");
  info_->setAccessibleName("Сведения о файле");
  info_->hide();
  body->addWidget(info_);
  layout->addLayout(body, 1);

  bottom_ = new QWidget(root);
  bottom_->setObjectName("bottomBar");
  auto* controls = new QHBoxLayout(bottom_);
  controls->setContentsMargins(12, 8, 12, 8);
  controls->setSpacing(6);
  previous_ = button("←", bottom_, "Предыдущий файл");
  previous_->setToolTip("Предыдущий файл (←)");
  controls->addWidget(previous_);
  connect(previous_, &QPushButton::clicked, this, [this] { navigate(-1); });
  counter_ = new QLabel(bottom_);
  counter_->setAlignment(Qt::AlignCenter);
  counter_->setMinimumWidth(56);
  controls->addWidget(counter_);
  next_ = button("→", bottom_, "Следующий файл");
  next_->setToolTip("Следующий файл (→)");
  controls->addWidget(next_);
  connect(next_, &QPushButton::clicked, this, [this] { navigate(1); });
  reveal_ = button("В папке", bottom_, "Показать в папке");
  reveal_->setToolTip("Показать в папке (" + QKeySequence("Ctrl+R").toString(QKeySequence::NativeText) + ")");
  controls->addWidget(reveal_);
  connect(reveal_, &QPushButton::clicked, this, &Window::revealCurrent);
  controls->addStretch();

  viewTools_ = new QWidget(bottom_);
  auto* photoTools = new QHBoxLayout(viewTools_);
  photoTools->setContentsMargins(0, 0, 0, 0);
  photoTools->setSpacing(6);
  scale_ = new QLabel(viewTools_);
  scale_->setObjectName("muted");
  scale_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  scale_->setMinimumWidth(46);
  photoTools->addWidget(scale_);
  auto* fit = button("Вписать", viewTools_);
  fit->setToolTip("Вписать в окно (F)");
  photoTools->addWidget(fit);
  connect(fit, &QPushButton::clicked, canvas_, &Canvas::fit);
  auto* native = button("1:1", viewTools_, "Исходный масштаб");
  native->setToolTip("Исходный масштаб (1)");
  photoTools->addWidget(native);
  connect(native, &QPushButton::clicked, this, [this] { requestNative(true); });
  auto* rotate = button("↻", viewTools_, "Повернуть изображение");
  rotate->setToolTip("Повернуть изображение (R)");
  photoTools->addWidget(rotate);
  connect(rotate, &QPushButton::clicked, canvas_, &Canvas::rotateClockwise);
  controls->addWidget(viewTools_);

  stripButton_ = button("Лента", bottom_, "Лента миниатюр");
  stripButton_->setCheckable(true);
  stripButton_->setToolTip("Лента миниатюр (T)");
  controls->addWidget(stripButton_);
  infoButton_ = button("Сведения", bottom_);
  infoButton_->setCheckable(true);
  infoButton_->setToolTip("Сведения о файле (I)");
  controls->addWidget(infoButton_);
  auto* full = button("⛶", bottom_, "Полный экран");
  full->setToolTip("Полный экран (F11)");
  controls->addWidget(full);
  connect(full, &QPushButton::clicked, this, &Window::toggleFullscreen);
  layout->addWidget(bottom_);

  strip_ = new QListView(root);
  strip_->setObjectName("filmstrip");
  strip_->setAccessibleName("Лента миниатюр");
  strip_->setModel(filter_);
  strip_->setViewMode(QListView::IconMode);
  strip_->setFlow(QListView::LeftToRight);
  strip_->setWrapping(false);
  strip_->setResizeMode(QListView::Adjust);
  strip_->setMovement(QListView::Static);
  strip_->setUniformItemSizes(true);
  strip_->setIconSize(QSize(112, 64));
  strip_->setGridSize(QSize(128, 92));
  strip_->setFixedHeight(112);
  strip_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  strip_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  strip_->setTextElideMode(Qt::ElideMiddle);
  strip_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  strip_->setSelectionMode(QAbstractItemView::SingleSelection);
  strip_->hide();
  layout->addWidget(strip_);

  statusBar()->setSizeGripEnabled(false);
  statusBar()->hide();
  setFocusPolicy(Qt::StrongFocus);
  setFocus();
}

void Window::showMessage(MessageState state, const QString& heading, const QString& body) {
  messageState_ = state;
  const bool error = state == MessageState::Error;
  if (heading_->property("error").toBool() != error) {
    heading_->setProperty("error", error);
    heading_->style()->unpolish(heading_);
    heading_->style()->polish(heading_);
  }
  heading_->setText(heading);
  message_->setText(body);
  welcomeMark_->setVisible(state == MessageState::Welcome);
  messageActions_->setVisible(state != MessageState::Loading);
  stage_->setCurrentWidget(messagePage_);
  open_->setVisible(state == MessageState::Loading);
}

void Window::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  if (!reveal_) return;
  reveal_->setVisible(width() >= 860);
  info_->setFixedWidth(width() >= 920 ? 260 : 212);
}

void Window::syncChrome() {
  const bool hasFolder = !model_->folder().isEmpty();
  top_->setVisible(!chromeHidden_);
  bottom_->setVisible(!chromeHidden_ && hasFolder);
  strip_->setVisible(!chromeHidden_ && stripAction_->isChecked() && filter_->rowCount() > 0);
  info_->setVisible(!chromeHidden_ && infoAction_->isChecked() && !current_.isEmpty());
  if (video_) video_->setControlsVisible(!chromeHidden_);
}

void Window::updateLabels() {
  const int row = currentRow();
  const int count = filter_->rowCount();
  counter_->setText(model_->isScanning() ? "…" : QString("%1 / %2").arg(row + 1).arg(count));
  counter_->setAccessibleName(model_->isScanning() ? "Читаем папку" : QString("Файл %1 из %2").arg(row + 1).arg(count));
  const bool canNavigate = count > 1 || (count == 1 && row < 0);
  previous_->setEnabled(canNavigate);
  next_->setEnabled(canNavigate);
  reveal_->setEnabled(!current_.isEmpty());
  stripButton_->setEnabled(count > 0);
  infoButton_->setEnabled(!current_.isEmpty());
  filters_->setVisible(!model_->folder().isEmpty());
  headerDivider_->setVisible(!current_.isEmpty());
  open_->setVisible(stage_->currentWidget() != messagePage_ || messageState_ == MessageState::Loading);
  viewTools_->setVisible(stage_->currentWidget() == canvas_ && canvas_->hasImage());
  syncChrome();

  const QFileInfo file(current_);
  const QString name = QApplication::applicationDisplayName();
  title_->setFullText(file.fileName());
  setWindowTitle(current_.isEmpty() ? name : file.fileName() + " · " + name);
  if (current_.isEmpty()) {
    subtitle_->setFullText({});
    scale_->clear();
    info_->clear();
    return;
  }
  QString meta = file.suffix().toUpper() + "  ·  " + QLocale().formattedDataSize(file.size());
  if (decoded_.original.isValid())
    meta += QString("  ·  %1 × %2").arg(decoded_.original.width()).arg(decoded_.original.height());
  subtitle_->setFullText(meta);
}

void Window::applyTheme() {
  const bool light = QSettings().value("view/light", false).toBool();
  const auto colors = viewColors(light);
  QPalette palette = QApplication::palette();
  palette.setColor(QPalette::Window, QColor(colors.value("surface").toString()));
  palette.setColor(QPalette::Base, QColor(colors.value("surface").toString()));
  palette.setColor(QPalette::Button, QColor(colors.value("surface").toString()));
  palette.setColor(QPalette::WindowText, QColor(colors.value("text").toString()));
  palette.setColor(QPalette::Text, QColor(colors.value("text").toString()));
  palette.setColor(QPalette::ButtonText, QColor(colors.value("text").toString()));
  palette.setColor(QPalette::Highlight, QColor(colors.value("action").toString()));
  palette.setColor(QPalette::HighlightedText, QColor(colors.value("on_action").toString()));
  setPalette(palette);
  canvas_->setBackgroundBrush(QColor(colors.value("background").toString()));

#ifdef KADRO_PRIVATE_PREVIEW_BRAND
  const QString mark = light ? "mark-black-128.png" : "mark-white-128.png";
  welcomeMark_->setPixmap(QIcon(":/wanted-view/preview/assets/" + mark).pixmap(QSize(52, 42), devicePixelRatioF()));
  welcomeMark_->setStyleSheet(light ? "background: #FFFFFF; border-radius: 12px;" : "background: #000000; border-radius: 12px;");
#else
  welcomeMark_->setPixmap(QIcon(":/wanted-view/wanted-view.png").pixmap(QSize(64, 64), devicePixelRatioF()));
#endif

  QString style = R"(
    QMainWindow, QWidget#stage, QWidget#messagePage { background: @background@; }
    QWidget#topBar, QWidget#bottomBar, QWidget#videoBar, QStatusBar { background: @surface@; }
    QWidget#topBar { border-bottom: 1px solid @border@; }
    QWidget#bottomBar, QWidget#videoBar, QStatusBar { border-top: 1px solid @border@; }
    QFrame#divider { background: @border@; }
    QLabel#brand, QLabel#fileTitle { font-weight: 600; }
    QLabel#muted, QStatusBar { color: @muted@; }
    QLabel#emptyHeading[error="true"] { color: @error@; }
    QWidget#segments { background: @surface_subtle@; border-radius: 12px; }

    QPushButton, QToolButton {
      color: @text@;
      border: 2px solid transparent;
      border-radius: 8px;
      padding: 4px 8px;
      background: transparent;
    }
    QPushButton:hover, QToolButton:hover { background: @surface_subtle@; }
    QPushButton:pressed, QToolButton:pressed { background: @border@; }
    QPushButton:checked, QPushButton#primary { background: @action@; color: @on_action@; }
    QPushButton:checked:hover, QPushButton#primary:hover { background: @action_hover@; }
    QPushButton#primary { padding: 4px 14px; font-weight: 600; }
    QPushButton#openFile, QWidget#messagePage QPushButton { border-color: @control_border@; }
    QWidget#messagePage QPushButton#primary { border-color: transparent; }
    QPushButton:focus, QToolButton:focus, QWidget#messagePage QPushButton:focus,
    QWidget#messagePage QPushButton#primary:focus, QPushButton#openFile:focus { border-color: @focus@; }
    QPushButton:disabled, QToolButton:disabled { color: @muted@; background: transparent; }
    QToolButton { font-size: 20px; }
    QToolButton::menu-indicator { image: none; }

    QListView#filmstrip {
      background: @surface@;
      border: 2px solid @border@;
      border-left: none;
      border-right: none;
      padding: 4px;
    }
    QListView#filmstrip:focus { border-color: @focus@; }
    QListView::item { padding: 4px; border-radius: 8px; }
    QListView::item:hover { background: @surface_subtle@; }
    QListView::item:selected { background: @action@; color: @on_action@; }
    QPlainTextEdit#infoPanel {
      background: @surface@;
      border: 2px solid @border@;
      padding: 12px;
    }
    QPlainTextEdit#infoPanel:focus { border-color: @focus@; }
    QMenu { background: @surface@; color: @text@; border: 1px solid @border@; padding: 4px; }
    QMenu::item { padding: 8px 24px; border-radius: 4px; }
    QMenu::item:selected { background: @action@; color: @on_action@; }
    QMenu::separator { height: 1px; background: @border@; margin: 4px; }
    QSlider:horizontal { min-height: 40px; border: 2px solid transparent; border-radius: 8px; }
    QSlider:focus { border-color: @focus@; }
    QSlider::groove:horizontal { height: 4px; background: @control_border@; border-radius: 2px; }
    QSlider::sub-page:horizontal { background: @action@; border-radius: 2px; }
    QSlider::handle:horizontal { width: 14px; margin: -5px 0; background: @text@; border-radius: 7px; }
    QSlider::handle:horizontal:disabled { background: @control_border@; }
    QComboBox { border: 2px solid @control_border@; border-radius: 8px; padding: 4px 8px; }
    QComboBox:focus { border-color: @focus@; }
    QScrollBar:horizontal { height: 8px; background: @surface@; }
    QScrollBar::handle:horizontal { background: @control_border@; border-radius: 4px; min-width: 32px; }
    QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
  )";
  for (auto color = colors.constBegin(); color != colors.constEnd(); ++color)
    style.replace("@" + color.key() + "@", color.value().toString());
  setStyleSheet(style);
}

void Window::showAbout() {
  QMessageBox about(this);
  about.setWindowTitle("О Wanted View");
  about.setTextFormat(Qt::PlainText);
  about.setIconPixmap(QIcon(":/wanted-view/wanted-view.png").pixmap(QSize(72, 72), devicePixelRatioF()));
  about.setText(QApplication::applicationDisplayName() + " " + QApplication::applicationVersion());
  about.setInformativeText("Локальный просмотр фотографий и видео.\nПродукт WANTED Labs.\n\n"
                          "Код приложения: MIT. Лицензии зависимостей перечислены в THIRD_PARTY.md.");
  about.exec();
}
}
