#pragma once
#include "displaymode.h"
#include "../../shared/progressiveopening.h"
#include "instancebroker.h"
#include "profile.h"
#include "videotransform.h"
#include <QByteArray>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QMap>
#include <QSet>
#include <functional>
#include <memory>
class QHostAddress;
class QListWidgetItem;
class QImage;
class PlaylistPanel;
class FramePreview;
class MediaRemote;
class WebControl;
class QTabWidget;
class QFrame;
class QAction;
class QTimer;
class Player;
class Timeline;
class QLabel;
class QSlider;
class QListWidget;
class QDockWidget;
class QMenu;
class QToolButton;
class QProcess;
class QProgressDialog;
class QSystemTrayIcon;
class ProgressiveSource;
class Window : public QMainWindow {
  Q_OBJECT
public:
  explicit Window(Profile &profile,
                  std::function<void(const QString &)> systemHandler = {},
                  DisplayMode::Command displayCommand = {},
                  std::function<bool()> trayAvailability = {},
                  int initialWebPort = -1);
  ~Window() override;
  void webControlDialog();
  void setupWebControl(int temporaryPort = -1);
  bool setTemporaryWebPort(quint16);
  bool replaceWebControl(quint16, const QHostAddress &, QString *error = nullptr);
  QVariantMap webState(int offset = 0) const;
  QByteArray webSnapshot();
  bool webCommand(const QString &, const QVariant &);
  WebControl *webControl = nullptr;
  bool openStdin(int descriptor, const QJsonObject &options = {});
  void openFiles(const QStringList &files, bool usePlayingList = false);
  void addFiles(const QStringList &files, QListWidget *target = nullptr);
  bool openDirectory(const QString &directory);
  bool loadExternalAudio(const QString &file, bool select = true);
  bool loadExternalSubtitles(const QString &file, bool select = true);
  void reopenCurrent();
  void closeCurrent();
  void openClipboard();
  bool loadOnlinePlaylist(const QString &url);
  // Returns admission; the result arrives through progressiveOpeningFinished.
  bool openProgressiveSession(const QString &manifest);
  bool progressiveOpening() const { return bool(legacyOpening); }
  bool openProgressiveSource(const std::shared_ptr<ProgressiveSource> &);
  std::shared_ptr<ProgressiveSource> progressiveSession() const { return progressive; }
  QJsonObject progressivePlaybackStatus() const;
  bool openDeviceSource(const QString &source);
  bool seekTo(double seconds);
  void reloadProgressive(double seconds,const QString &audioPath);
  bool setAbLoop(double a, double b, int plays = 0);
  void clearAbLoop();
  bool loadPlaylist(const QString &path);
  bool savePlaylist(const QString &path);
  bool saveFrame(const QString &path, bool includeSubtitles = true);
  bool captureImage(const QString &path = {}, bool clipboard = false,
                    bool displayArea = false);
  QVariantMap mediaProperties() const;
  void maintenanceDialog(bool checkUpdates = false);
  QVariantMap diagnosticRuntime() const;
  QStringList diagnosticMessages;
  void handleLaunchRequest(const LaunchRequest &);
  void restoreFromTray();
  void exitApplication();
  bool requestedProfileReset(bool *keepLibrary) const;
signals:
  void progressiveOpeningFinished(bool success, const QString &error);
  void playbackIntentChanged();
  void webControlChanged();
  void frameSaved(const QString &path, bool success);
  void imageCaptured(bool success);
  void onlinePlaylistFinished(bool success, const QString &message);
  void progressiveSessionEnded(const QString &sessionId, const QString &state,
                               const QString &error);

protected:
#ifdef Q_OS_WIN
  bool nativeEvent(const QByteArray &eventType, void *message,
                   qintptr *result) override;
#endif
  bool event(QEvent *) override;
  bool eventFilter(QObject *, QEvent *) override;
  void closeEvent(QCloseEvent *) override;
  void dragEnterEvent(QDragEnterEvent *) override;
  void dropEvent(QDropEvent *) override;
  void changeEvent(QEvent *) override;
  void resizeEvent(QResizeEvent *) override;
  void moveEvent(QMoveEvent *) override;

private:
  friend class ProgressiveApiTest;
  std::shared_ptr<foxicmp::progressive::SourceOpening<ProgressiveSource>> legacyOpening;
  std::function<void()> legacyOpeningHook;
  void cancelProgressiveOpening();
  void playIndex(int index, double start = -1, bool resetOrder = true,
                 bool preservePasses = false, bool hold = false);
  bool advancePlaylist(bool forward, bool natural = false);
  bool advanceDirectory(bool forward);
  void navigateSkip(int direction);
  void syncPlaylist();
  void selectPlaybackList(QListWidget *list);
  void removePlaylistSelection();
  void movePlaylistSelection(int delta);
  void refreshLibraryMenus();
  void addFavoriteDialog();
  void manageFavorites();
  void showHistory();
  void attachLibraryPreferences(QTabWidget *, QList<std::function<void()>> *);
  void openFavorite(const Favorite &entry);
  void resetShuffle();
  void savePosition();
  void toggleFullscreen();
  void buildMenus();
  void applyClassicMenus();
  void buildOrganizedMenus(QMenu *videoStreams, QMenu *language);
  void buildAfterPlaybackMenu(QMenu *playback);
  void refreshAfterPlaybackMenu();
  void finishPlayback();
  void closePlaybackFile();
  bool nextInDirectory(bool loop);
  void buildActions();
  void openDialog();
  void openUrlDialog();
  void openDirectoryDialog();
  void openDiscDialog();
  void openCaptureDialog();
  void openTelevisionDialog();
  void changeDiscTitle();
  void openExternalAudioDialog();
  void openOnlinePlaylistDialog();
  void goToTime();
  void resizeVideo(double scale);
  void showFullscreenControls();
  void applyShortcuts();
  void buildInputActions();
  void setupMediaRemote();
  MediaRemote *mediaRemote = nullptr;
  void attachInputPreferences(QTabWidget *, QList<std::function<void()>> *);
  bool inputEvent(QObject *, QEvent *);
  QString mouseCommand(const QString &gesture,
                       Qt::KeyboardModifiers modifiers = Qt::NoModifier) const;
  void dispatchInputCommand(const QString &id);
  void cycleTrack(bool audio, int direction);
  void adjustDelay(bool audio, int direction);
  void loadSubtitlesDialog();
  void buildSubtitleActions();
  void setupSubtitleTools();
  void applySubtitleOptions();
  void enforceSubtitlePolicy();
  bool subtitleTrackAllowed(const QVariantMap &) const;
  QVariantMap selectedSubtitleTrack() const;
  void subtitleEditorDialog();
  void reloadSubtitles();
  void watchSubtitleFile();
  void attachSubtitlePreferences(QTabWidget *, QList<std::function<void()>> *);
  void attachAudioProcessingPreferences(QTabWidget *,
                                        QList<std::function<void()>> *);
  void audioProcessingDialog();
  void videoOutputDialog();
  void showRenderStatistics();
  void attachVideoShaderPreferences(QTabWidget *,
                                    QList<std::function<void()>> *);
  bool validateVideoShaderPreferences(QDialog *);
  void attachVideoOutputPreferences(QTabWidget *,
                                    QList<std::function<void()>> *);
  void attachAudioEncodingPreferences(QTabWidget *,
                                      QList<std::function<void()>> *);
  void attachAudioFilterPreferences(QTabWidget *,
                                    QList<std::function<void()>> *);
  bool validateAudioFilterPreferences(QDialog *);
  bool validateVideoDisplayPreferences(QDialog *);
  void attachVideoDisplayPreferences(QTabWidget *,
                                     QList<std::function<void()>> *);
  void applyAudioSourceOptions();
  void enforceAudioPolicy();
  void attachAudioSourcePreferences(QTabWidget *,
                                    QList<std::function<void()>> *);
  bool audioUserSelection = false;
  QString audioOptionsStamp;
  void attachSubtitleStylePreferences(QTabWidget *,
                                      QList<std::function<void()>> *);
  void attachSubtitleRenderPreferences(QTabWidget *,
                                       QList<std::function<void()>> *);
  void subtitleStyleDialog();
  void applySubtitleStyle();
  QByteArray subtitleStyleStamp;
  QString subtitleStylePath;
  bool subtitleUserSelection = false;
  QSet<QString> progressiveAttachedAudio;
  QMap<quint64,QString> progressiveAudioRequests;
  QMap<QString,QString> progressiveAudioLoadErrors;
  quint64 progressiveAudioSequence = 0;
  QString progressiveMediaMenuStamp;
  QSet<QString> progressiveAttachedSubtitles;
  QMap<quint64,QString> progressiveSubtitleRequests;
  QMap<QString,QString> progressiveSubtitleLoadErrors;
  quint64 progressiveSubtitleSequence = 0;
  QString progressiveSubtitlePath(const QString &selection) const;
  QString watchedSubtitle, subtitleOptionsStamp, subtitleCodepage;
  QByteArray watchedSubtitleHash, pendingSubtitleHash;
  void showPlaybackInformation();
  void openLastFile();
  void bossKey();
  void setupMouseHold();
  bool cancelMouseHold();
  double seekAcceleration() const;
  void refreshTracks();
  void refreshChapters();
  void updatePlaylistChapters();
  bool changeChapter(int direction, double backThreshold = 3.);
  void rememberTrack(const char *name, const QString &selection);
  void restorePlaylistTracks();
  void handlePlaylistFailure();
  bool playlistItemSkipped(int row) const;
  bool playlistPassEnded() const;
  void attachPlaylistPreferences(QTabWidget *, QList<std::function<void()>> *);
  void setupPlaylistDock();
  void preferences();
  void setupPlaybackTools();
  void applyAppearance();
  VideoTransform videoTransform, lastAppliedTransform;
  QString transformStereoHint;
  bool applyingTransform = false;
  void buildTransformActions();
  void buildTransformMenu(QMenu *);
  void transformDialog();
  void attachTransformPreferences(QTabWidget *, QList<std::function<void()>> *);
  bool applyVideoTransform();
  void applyMediaOptions();
  QString previewVideoFilters() const;
  void transformChanged();
  void captureTransformGeometry();
  void refreshTransformPresets(QMenu *);
  void saveTransformPreset();
  void attachAppearancePreferences(QTabWidget *,
                                   QList<std::function<void()>> *);
  void updateOverlays();
  void positionOverlays();
  void requestPreview(double seconds, const QPoint &, const QString &);
  bool seekAvailable(double start, double end) const;
  void seekStep(int direction, const QString &key, int fallback);
  void seekKeyframe(int direction);
  void setSpeed(double speed);
  void changeSpeed(int direction);
  void setAbPoint(bool first);
  void abLoopDialog();
  void refreshPlaybackControls();
  void updateMinimizePause();
  void attachPlaybackPreferences(QTabWidget *, QList<std::function<void()>> *);
  void positionPreview();
  void showOsd(const QString &text);
  void applyWindowMode();
  void buildWindowActions();
  void buildWindowMenu(QMenu *);
  void applyWindowPreferences();
  void updateWindowTitle();
  void updatePlayerStatus();
  QString mediaStatusTitle() const;
  QString chapterStatus() const;
  void attachTitlePreferences(QTabWidget *, QList<std::function<void()>> *);
  QString titleMetadata;
  void applyPanelVisibility(bool reveal = true);
  void attachWindowPreferences(QTabWidget *, QList<std::function<void()>> *);
  void setupWindowPresentation();
  void keepWindowOnScreen();
  void windowMediaLoaded();
  void autoSizeVideo();
  void restoreClosedGeometry();
  void exitFullscreenAtEnd();
  void resizeVideoArea(const QSize &);
  QSize videoSize() const;
  void snapWindow();
  void restoreDisplayMode();
  void showVideoSettings();
  void snapshotDialog();
  void showMediaProperties();
  void captureSettingsDialog();
  void quickSnapshot();
  void displaySnapshotDialog();
  void contactSheetDialog();
  void saveMediaCopyDialog();
  QString captureName() const;
  void finishCapture(const QImage &, const QString &, bool clipboard);
  QMap<quint64, QPair<QString, bool>> imageRequests;
  void attachMediaPreferences(QTabWidget *tabs,
                              QList<std::function<void()>> *apply);
  void attachDesktopPreferences(QTabWidget *, QList<std::function<void()>> *);
  void applyDesktopPreferences();
  void desktopSettingsDialog();
  bool hideToTray();
  void minimizeToTray();
  QSystemTrayIcon *trayIcon = nullptr;
  std::function<bool()> trayAvailability;
  Qt::WindowStates trayWindowState;
  bool hiddenToTray = false, forceExit = false;
  bool resetProfileOnExit = false, keepResetLibrary = true;
  void updateClock();
  void integrationPrompt();
  void setIntegration(bool enabled);
  void updateProgressive();
  void releaseProgressive(const QString &state = QStringLiteral("stopped"));
  std::shared_ptr<ProgressiveSource> progressive;
  double progressivePendingSeek = -1, progressiveSeekTarget = -1;
  QString progressiveRestoreAudio, progressiveRestoreSubtitle;
  bool progressiveTracksReady=false;
  bool progressiveStarted = false, progressiveBuffering = true,
       progressiveUserPause = false, progressiveStreaming = false;
  std::function<void(const QString &)> systemActionHandler;
  QString onceAfterPlayback;
  bool completionArmed = false;
  QMenu *afterMenu = nullptr;
  QProcess *onlineProcess = nullptr;
  QProgressDialog *onlineProgress = nullptr;
  QByteArray onlineOutput;
  Profile &profile;
  Player *player = nullptr;
  Timeline *timeline;
  QLabel *state, *time, *logo, *gpuIndicator = nullptr;
  QSlider *volume;
  QToolButton *play, *mute;
  QWidget *controls, *statusArea;
  QString hoverStatus;
  QLabel *osd = nullptr, *osdInfo = nullptr, *osdClock = nullptr,
         *previewImage = nullptr, *previewText = nullptr;
  QPoint previewAnchor;
  QFrame *previewPopup = nullptr;
  FramePreview *preview = nullptr;
  QTimer *osdTimer = nullptr;
  QMap<quint64, QString> screenshots;
  quint64 screenshotId = 100;
  DisplayMode displayMode;
  QByteArray beforeFullscreenGeometry;
  QRect beforePlaybackGeometry, beforeFullscreenRect;
  bool changingWindow = false, presentationReady = false;
  bool screenRecoveryPending = false;
  bool resizePending = false, movePending = false;
  quint64 autoSizedGeneration = ~quint64(0);
  bool seekingNotice = false, draggingWindow = false;
  QPoint windowDragOffset, mousePressPoint;
  bool dragCandidate = false, dragConsumesClick = false;
  bool suppressMouseRelease = false;
  QString pendingMouseCommand, heldSeekCommand;
  Qt::MouseButton pressedMouseButton = Qt::NoButton;
  QElapsedTimer heldSeekTime;
  Qt::WindowStates bossWindowState;
  int horizontalWheelRemainder = 0;
  bool wheelWasFullscreen = false;
  Qt::KeyboardModifiers lastWheelModifiers = Qt::NoModifier;
  QTimer *hideControls = nullptr, *clickTimer = nullptr,
         *mouseHoldTimer = nullptr;
  bool mouseHoldActive = false, mouseHoldConsumed = false;
  double mouseHoldPreviousSpeed = 1;
  quint64 mouseHoldGeneration = 0;
  QMap<QString, QAction *> actions;
  QMap<QString, QString> defaultKeys;
  QMenu *contextMenu;
  bool restorePlaylist = false;
  bool advancingAfterError = false;
  QSet<QString> failedEntries;
  bool stdinPlayback = false;
  quint64 playbackGeneration = 0;
  QVariantMap pendingTrackSelections;
  QJsonObject openingLaunchOptions, pendingLaunchOptions;
  quint64 launchOptionsGeneration = 0;
  void applyLaunchPlaybackOptions(const QJsonObject &);
  void applyLaunchWindowOptions(const QJsonObject &);
  void finishLaunchOptions();
  int wheelRemainder = 0;
  QListWidget *playlist = nullptr;
  PlaylistPanel *queuePanel = nullptr;
  QDockWidget *dock;
  QMenu *audioMenu, *subtitleMenu, *chapterMenu, *historyMenu, *favoritesMenu;
  QListWidgetItem *currentItem = nullptr;
  bool changingPlaylist = false;
  double pendingResume = -1;
  QSet<QString> shuffled;
  QStringList shuffleTrail;
  int shuffleCursor = -1;
  QString current, currentId, lastError, expectedPlaybackSource;
  int currentIndex = -1;
  bool paused = false, stopped = true, wasMaximized = false;
  double pos = 0, duration = 0;
  double loopA = -1, loopB = -1, sessionSpeed = 1;
  int filePlays = 0, playlistPlays = 0;
  bool pausedByMinimize = false, holdAfterLoad = false,
       repeatSameSource = false;
};
