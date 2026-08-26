#include <QByteArray>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include "AbstractSttClient.h"
#include "AudioRecorder.h"
#include "GroqApiClient.h"
#include "GroqLlmClient.h"
#include "SpeechController.h"
#include "TranscriptionPipeline.h"

using namespace Qt::StringLiterals;

class FakeSttClient : public AbstractSttClient {
    Q_OBJECT

public:
    explicit FakeSttClient(QObject* parent = nullptr)
        : AbstractSttClient(parent) { }

    void transcribe(const QByteArray& wavData) override {
        m_lastReceivedWav = wavData;
        m_transcribeCallCount++;
        m_busy = true;
        emit busyChanged();
    }

    void cancel() override {
        m_busy = false;
        m_cancelled = true;
        emit busyChanged();
    }

    void retryLast() override {
        m_retryCallCount++;
        if (!m_lastReceivedWav.isEmpty()) {
            transcribe(m_lastReceivedWav);
        }
    }

    bool isReady() const override {
        return m_ready;
    }

    bool isBusy() const override {
        return m_busy;
    }

    void setReady(bool ready) {
        if (m_ready != ready) {
            m_ready = ready;
            emit readyChanged();
        }
    }

    void simulateSuccess(const QString& text) {
        m_busy = false;
        emit busyChanged();
        emit transcriptionReady(text);
    }

    void simulateError(const QString& error) {
        m_busy = false;
        m_lastError = error;
        emit busyChanged();
        emit errorOccurred(error);
    }

    QString lastError() const override {
        return m_lastError;
    }

    QByteArray m_lastReceivedWav;
    QString m_lastError;
    int m_transcribeCallCount = 0;
    int m_retryCallCount = 0;
    bool m_ready = true;
    bool m_busy = false;
    bool m_cancelled = false;
};

class TestTranscriptionPipeline : public QObject {
    Q_OBJECT

private slots:
    void testInitialState() {
        TranscriptionPipeline pipeline;
        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Idle);
        QCOMPARE(pipeline.isBusy(), false);
        QCOMPARE(pipeline.statusMessage(), u"Ready"_s);
        QVERIFY(pipeline.lastError().isEmpty());
        QVERIFY(pipeline.lastTranscription().isEmpty());
    }

    void testBackendSwitching() {
        TranscriptionPipeline pipeline;
        FakeSttClient groqClient;
        FakeSttClient whisperClient;

        pipeline.registerBackend(TranscriptionPipeline::Backend::Groq, &groqClient);
        pipeline.registerBackend(TranscriptionPipeline::Backend::WhisperCpp, &whisperClient);

        pipeline.setActiveBackend(TranscriptionPipeline::Backend::Groq);
        QCOMPARE(pipeline.activeBackend(), TranscriptionPipeline::Backend::Groq);
        QCOMPARE(pipeline.activeSttClient(), &groqClient);

        pipeline.setActiveBackend(TranscriptionPipeline::Backend::WhisperCpp);
        QCOMPARE(pipeline.activeBackend(), TranscriptionPipeline::Backend::WhisperCpp);
        QCOMPARE(pipeline.activeSttClient(), &whisperClient);
    }

    void testTranscriptionDispatchAndCompletion() {
        TranscriptionPipeline pipeline;
        FakeSttClient groqClient;
        pipeline.registerBackend(TranscriptionPipeline::Backend::Groq, &groqClient);
        pipeline.setActiveBackend(TranscriptionPipeline::Backend::Groq);
        pipeline.initialize();

        QSignalSpy finishedSpy(&pipeline, &TranscriptionPipeline::transcriptionFinished);
        QSignalSpy stateSpy(&pipeline, &TranscriptionPipeline::stateChanged);

        pipeline.setState(TranscriptionPipeline::State::Recording);

        const QByteArray dummyWav("RIFFdummyWAVdata");
        QMetaObject::invokeMethod(&pipeline, "onRecordingFinished", Q_ARG(QByteArray, dummyWav));

        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Transcribing);
        QCOMPARE(pipeline.isBusy(), true);
        QCOMPARE(groqClient.m_transcribeCallCount, 1);
        QCOMPARE(groqClient.m_lastReceivedWav, dummyWav);

        groqClient.simulateSuccess(u"Hello world dictation"_s);

        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Idle);
        QCOMPARE(pipeline.isBusy(), false);
        QCOMPARE(pipeline.lastTranscription(), u"Hello world dictation"_s);
        QCOMPARE(finishedSpy.count(), 1);
        QCOMPARE(finishedSpy.at(0).at(0).toString(), u"Hello world dictation"_s);
    }

    void testErrorAndRetryFlow() {
        TranscriptionPipeline pipeline;
        FakeSttClient groqClient;
        pipeline.registerBackend(TranscriptionPipeline::Backend::Groq, &groqClient);
        pipeline.setActiveBackend(TranscriptionPipeline::Backend::Groq);
        pipeline.initialize();

        QSignalSpy errorSpy(&pipeline, &TranscriptionPipeline::errorOccurred);

        pipeline.setState(TranscriptionPipeline::State::Recording);

        const QByteArray dummyWav("RIFFdummyWAVdata");
        QMetaObject::invokeMethod(&pipeline, "onRecordingFinished", Q_ARG(QByteArray, dummyWav));
        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Transcribing);

        groqClient.simulateError(u"Rate Limit 429"_s);

        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Error);
        QCOMPARE(pipeline.lastError(), u"Rate Limit 429"_s);
        QCOMPARE(errorSpy.count(), 1);

        pipeline.retry();

        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Transcribing);
        QCOMPARE(groqClient.m_transcribeCallCount, 2);
        QCOMPARE(groqClient.m_lastReceivedWav, dummyWav);

        groqClient.simulateSuccess(u"Retried text"_s);
        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Idle);
        QCOMPARE(pipeline.lastTranscription(), u"Retried text"_s);
    }

    void testCancelFlow() {
        TranscriptionPipeline pipeline;
        FakeSttClient groqClient;
        pipeline.registerBackend(TranscriptionPipeline::Backend::Groq, &groqClient);
        pipeline.setActiveBackend(TranscriptionPipeline::Backend::Groq);
        pipeline.initialize();

        pipeline.setState(TranscriptionPipeline::State::Recording);

        pipeline.cancel();
        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Idle);
        QCOMPARE(groqClient.m_transcribeCallCount, 0);
        QVERIFY(groqClient.m_lastReceivedWav.isEmpty());
        QCOMPARE(groqClient.m_cancelled, true);

        groqClient.m_cancelled = false;
        pipeline.setState(TranscriptionPipeline::State::Recording);
        const QByteArray dummyWav("RIFFdummyWAVdata");
        QMetaObject::invokeMethod(&pipeline, "onRecordingFinished", Q_ARG(QByteArray, dummyWav));
        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Transcribing);

        pipeline.cancel();
        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Idle);
        QCOMPARE(groqClient.m_cancelled, true);
    }

    void testIgnoreRecordingFinishedWhenNotRecording() {
        TranscriptionPipeline pipeline;
        FakeSttClient groqClient;
        pipeline.registerBackend(TranscriptionPipeline::Backend::Groq, &groqClient);
        pipeline.setActiveBackend(TranscriptionPipeline::Backend::Groq);
        pipeline.initialize();

        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Idle);

        const QByteArray dummyWav("RIFFdummyWAVdata");
        QMetaObject::invokeMethod(&pipeline, "onRecordingFinished", Q_ARG(QByteArray, dummyWav));

        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Idle);
        QCOMPARE(groqClient.m_transcribeCallCount, 0);
    }

    void testClientReadinessAndCanRecord() {
        TranscriptionPipeline pipeline;
        FakeSttClient groqClient;
        groqClient.setReady(false);
        pipeline.registerBackend(TranscriptionPipeline::Backend::Groq, &groqClient);
        pipeline.setActiveBackend(TranscriptionPipeline::Backend::Groq);

        QCOMPARE(pipeline.canRecord(), false);

        groqClient.setReady(true);
        QCOMPARE(groqClient.isReady(), true);
    }

    void testLlmEnhancementTriggeredForGroq() {
        TranscriptionPipeline pipeline;
        FakeSttClient groqClient;
        GroqApiClient apiClient;
        apiClient.setApiKey(u"gsk_test_dummy_key_12345"_s);

        GroqLlmClient llmClient;
        llmClient.setApiClient(&apiClient);
        llmClient.setEnabled(true);

        pipeline.registerBackend(TranscriptionPipeline::Backend::Groq, &groqClient);
        pipeline.setActiveBackend(TranscriptionPipeline::Backend::Groq);
        pipeline.setLlmClient(&llmClient);
        pipeline.initialize();

        pipeline.setState(TranscriptionPipeline::State::Recording);
        const QByteArray dummyWav("RIFFdummyWAVdata");
        QMetaObject::invokeMethod(&pipeline, "onRecordingFinished", Q_ARG(QByteArray, dummyWav));
        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Transcribing);

        groqClient.simulateSuccess(u"Raw transcribed speech"_s);

        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Enhancing);
        QCOMPARE(pipeline.isBusy(), true);

        emit llmClient.enhancementReady(u"Polished speech text."_s);

        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Idle);
        QCOMPARE(pipeline.isBusy(), false);
        QCOMPARE(pipeline.lastTranscription(), u"Polished speech text."_s);
    }

    void testLlmEnhancementBypassedForWhisperCpp() {
        TranscriptionPipeline pipeline;
        FakeSttClient whisperClient;
        GroqLlmClient llmClient;
        llmClient.setEnabled(true);

        pipeline.registerBackend(TranscriptionPipeline::Backend::WhisperCpp, &whisperClient);
        pipeline.setActiveBackend(TranscriptionPipeline::Backend::WhisperCpp);
        pipeline.setLlmClient(&llmClient);
        pipeline.initialize();

        pipeline.setState(TranscriptionPipeline::State::Recording);
        const QByteArray dummyWav("RIFFdummyWAVdata");
        QMetaObject::invokeMethod(&pipeline, "onRecordingFinished", Q_ARG(QByteArray, dummyWav));
        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Transcribing);

        whisperClient.simulateSuccess(u"Raw local offline text"_s);

        QCOMPARE(pipeline.state(), TranscriptionPipeline::State::Idle);
        QCOMPARE(pipeline.isBusy(), false);
        QCOMPARE(pipeline.lastTranscription(), u"Raw local offline text"_s);
    }

    void testSpeechControllerBackendDefault() {
        SpeechController controller;
        // Default when no settings are set and pipeline is null should be WhisperCpp
        QCOMPARE(controller.activeBackend(), SpeechController::TranscriptionBackend::WhisperCpp);

        TranscriptionPipeline pipeline;
        QSignalSpy backendSpy(&controller, &SpeechController::activeBackendChanged);
        controller.setPipeline(&pipeline);

        QCOMPARE(backendSpy.count(), 1);
        QCOMPARE(controller.activeBackend(), SpeechController::TranscriptionBackend::WhisperCpp);
    }

    void testSpeechControllerBackendSwitching() {
        TranscriptionPipeline pipeline;
        FakeSttClient groqClient;
        FakeSttClient whisperClient;

        pipeline.registerBackend(TranscriptionPipeline::Backend::Groq, &groqClient);
        pipeline.registerBackend(TranscriptionPipeline::Backend::WhisperCpp, &whisperClient);

        SpeechController controller;
        controller.setPipeline(&pipeline);

        QSignalSpy backendSpy(&controller, &SpeechController::activeBackendChanged);

        controller.setActiveBackend(SpeechController::TranscriptionBackend::Groq);
        QCOMPARE(controller.activeBackend(), SpeechController::TranscriptionBackend::Groq);
        QCOMPARE(pipeline.activeBackend(), TranscriptionPipeline::Backend::Groq);
        QCOMPARE(backendSpy.count(), 1);

        controller.setActiveBackend(SpeechController::TranscriptionBackend::WhisperCpp);
        QCOMPARE(controller.activeBackend(), SpeechController::TranscriptionBackend::WhisperCpp);
        QCOMPARE(pipeline.activeBackend(), TranscriptionPipeline::Backend::WhisperCpp);
        QCOMPARE(backendSpy.count(), 2);
    }

    void initTestCase() {
        qputenv("QT_MEDIA_BACKEND", "null");
    }

    void cleanupTestCase() {
        QCoreApplication::processEvents();
    }
};

QTEST_GUILESS_MAIN(TestTranscriptionPipeline)
#include "test_transcription_pipeline.moc"
