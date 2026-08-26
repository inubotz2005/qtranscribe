#include <QByteArray>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>

#include "AbstractSttClient.h"
#include "AudioRecorder.h"
#include "GlobalShortcutManager.h"
#include "SpeechController.h"
#include "TranscriptionPipeline.h"

using namespace Qt::StringLiterals;

class TestShortcutManager : public GlobalShortcutManager {
    Q_OBJECT

public:
    explicit TestShortcutManager(QObject* parent = nullptr)
        : GlobalShortcutManager(parent) { }

    using GlobalShortcutManager::setAvailable;
    using GlobalShortcutManager::setStatusMessage;
    using GlobalShortcutManager::setSupported;

    void triggerShortcutActivated(const QString& shortcutId) {
        emit shortcutActivated(shortcutId);
    }

    void triggerShortcutDeactivated(const QString& shortcutId) {
        emit shortcutDeactivated(shortcutId);
    }
};

class DummySttClient : public AbstractSttClient {
    Q_OBJECT

public:
    explicit DummySttClient(QObject* parent = nullptr)
        : AbstractSttClient(parent) { }

    void transcribe(const QByteArray& wavData) override {
        Q_UNUSED(wavData);
        m_busy = true;
        emit busyChanged();
    }

    void cancel() override {
        m_busy = false;
        emit busyChanged();
    }

    void retryLast() override { }

    bool isReady() const override {
        return true;
    }

    bool isBusy() const override {
        return m_busy;
    }

    void complete(const QString& text) {
        m_busy = false;
        emit busyChanged();
        emit transcriptionReady(text);
    }

private:
    bool m_busy = false;
};

class FakeAudioRecorder : public AudioRecorder {
    Q_OBJECT

public:
    explicit FakeAudioRecorder(QObject* parent = nullptr)
        : AudioRecorder(parent) {
        setHasAudioInputDevice(true);
    }

    void startRecording() override {
        setRecording(true);
    }

    void stopRecording() override {
        setRecording(false);
        emit recordingFinished(QByteArray("RIFFfakeWavData"));
    }

    void cancelRecording() override {
        setRecording(false);
    }
};

class TestSpeechController : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testDefaultModeIsToggle();
    void testUnsupportedPortalRejectsPushToTalk();
    void testSupportedPortalAllowsPushToTalk();
    void testToggleModeShortcutHandling();
    void testPushToTalkShortcutHandling();
    void testSupportLossRevertsPushToTalkToToggle();

private:
    SpeechController* m_controller = nullptr;
    TranscriptionPipeline* m_pipeline = nullptr;
    TestShortcutManager* m_shortcutMgr = nullptr;
    DummySttClient* m_sttClient = nullptr;
    FakeAudioRecorder* m_recorder = nullptr;
};

void TestSpeechController::initTestCase() {
    QCoreApplication::setOrganizationName(u"QTranscribeTestOrg"_s);
    QCoreApplication::setApplicationName(u"QTranscribeTestApp"_s);
}

void TestSpeechController::cleanupTestCase() { }

void TestSpeechController::init() {
    m_controller = new SpeechController(this);
    m_pipeline = new TranscriptionPipeline(this);
    m_shortcutMgr = new TestShortcutManager(this);
    m_sttClient = new DummySttClient(this);
    m_recorder = new FakeAudioRecorder(this);

    m_pipeline->setAudioRecorder(m_recorder);
    m_pipeline->registerBackend(TranscriptionPipeline::Backend::WhisperCpp, m_sttClient);
    m_controller->setPipeline(m_pipeline);
    m_controller->setShortcutManager(m_shortcutMgr);
    m_controller->initialize();
}

void TestSpeechController::cleanup() {
    delete m_controller;
    m_controller = nullptr;

    delete m_pipeline;
    m_pipeline = nullptr;

    delete m_shortcutMgr;
    m_shortcutMgr = nullptr;

    delete m_sttClient;
    m_sttClient = nullptr;

    delete m_recorder;
    m_recorder = nullptr;
}

void TestSpeechController::testDefaultModeIsToggle() {
    m_shortcutMgr->setSupported(false);
    m_controller->setRecordingMode(SpeechController::RecordingMode::Toggle);
    QCOMPARE(m_controller->recordingMode(), SpeechController::RecordingMode::Toggle);
    QCOMPARE(m_controller->pushToTalkSupported(), false);
}

void TestSpeechController::testUnsupportedPortalRejectsPushToTalk() {
    m_shortcutMgr->setSupported(false);
    QCOMPARE(m_controller->pushToTalkSupported(), false);

    QSignalSpy modeSpy(m_controller, &SpeechController::recordingModeChanged);
    m_controller->setRecordingMode(SpeechController::RecordingMode::PushToTalk);

    QCOMPARE(m_controller->recordingMode(), SpeechController::RecordingMode::Toggle);
    QCOMPARE(modeSpy.count(), 0);
}

void TestSpeechController::testSupportedPortalAllowsPushToTalk() {
    m_shortcutMgr->setSupported(true);
    QCOMPARE(m_controller->pushToTalkSupported(), true);

    QSignalSpy modeSpy(m_controller, &SpeechController::recordingModeChanged);
    m_controller->setRecordingMode(SpeechController::RecordingMode::PushToTalk);

    QCOMPARE(m_controller->recordingMode(), SpeechController::RecordingMode::PushToTalk);
    QCOMPARE(modeSpy.count(), 1);

    m_controller->setRecordingMode(SpeechController::RecordingMode::Toggle);
    QCOMPARE(m_controller->recordingMode(), SpeechController::RecordingMode::Toggle);
    QCOMPARE(modeSpy.count(), 2);
}

void TestSpeechController::testToggleModeShortcutHandling() {
    m_shortcutMgr->setSupported(true);
    m_controller->setRecordingMode(SpeechController::RecordingMode::Toggle);

    QVERIFY(!m_controller->recording());

    m_shortcutMgr->triggerShortcutActivated(u"toggle-recording"_s);
    QVERIFY(m_controller->recording());

    m_shortcutMgr->triggerShortcutDeactivated(u"toggle-recording"_s);
    QVERIFY(m_controller->recording());

    m_shortcutMgr->triggerShortcutActivated(u"toggle-recording"_s);
    QVERIFY(!m_controller->recording());
}

void TestSpeechController::testPushToTalkShortcutHandling() {
    m_shortcutMgr->setSupported(true);
    m_controller->setRecordingMode(SpeechController::RecordingMode::PushToTalk);
    QCOMPARE(m_controller->recordingMode(), SpeechController::RecordingMode::PushToTalk);

    QVERIFY(!m_controller->recording());

    m_shortcutMgr->triggerShortcutActivated(u"toggle-recording"_s);
    QVERIFY(m_controller->recording());

    m_shortcutMgr->triggerShortcutDeactivated(u"toggle-recording"_s);
    QVERIFY(!m_controller->recording());

    m_shortcutMgr->triggerShortcutDeactivated(u"toggle-recording"_s);
    QVERIFY(!m_controller->recording());
}

void TestSpeechController::testSupportLossRevertsPushToTalkToToggle() {
    m_shortcutMgr->setSupported(true);
    m_controller->setRecordingMode(SpeechController::RecordingMode::PushToTalk);
    QCOMPARE(m_controller->recordingMode(), SpeechController::RecordingMode::PushToTalk);

    QSignalSpy modeSpy(m_controller, &SpeechController::recordingModeChanged);

    m_shortcutMgr->setSupported(false);

    QCOMPARE(m_controller->recordingMode(), SpeechController::RecordingMode::Toggle);
    QCOMPARE(modeSpy.count(), 1);
}

QTEST_MAIN(TestSpeechController)
#include "test_speech_controller.moc"
