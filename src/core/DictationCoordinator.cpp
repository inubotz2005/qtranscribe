#include "DictationCoordinator.h"

#include "AudioRecorder.h"
#include "GroqApiClient.h"
#include "GroqLlmClient.h"
#include "LoggingCategories.h"
#include "TextInjectorClient.h"
#include "TranscriptionModel.h"

#include "AudioFeedbackPlayer.h"
#include "DictationPadModel.h"
#include "GlobalShortcutManager.h"
#include "SystemHealthMonitor.h"

#include <QDebug>
#include <QGuiApplication>
#include <QSettings>

using namespace Qt::StringLiterals;

DictationCoordinator::DictationCoordinator(QObject* parent)
    : QObject(parent)
    , m_padModel(new DictationPadModel(this))
    , m_feedbackPlayer(new AudioFeedbackPlayer(this))
    , m_healthMonitor(new SystemHealthMonitor(this)) {
    connect(m_padModel, &DictationPadModel::textChanged, this, &DictationCoordinator::dictationPadTextChanged);
    connect(m_feedbackPlayer, &AudioFeedbackPlayer::soundEnabledChanged, this,
            &DictationCoordinator::soundEnabledChanged);
    connect(m_healthMonitor, &SystemHealthMonitor::systemHealthChanged, this,
            &DictationCoordinator::systemHealthChanged);
    connect(m_healthMonitor, &SystemHealthMonitor::canRecordChanged, this, &DictationCoordinator::canRecordChanged);

    QSettings settings;
    const QString modeStr = settings.value(u"Speech/RecordingMode"_s, u"Toggle"_s).toString();
    if (modeStr == u"PushToTalk"_s) {
        m_recordingMode = RecordingMode::PushToTalk;
    } else {
        m_recordingMode = RecordingMode::Toggle;
    }

    const QString backendStr = settings.value(u"Speech/Backend"_s, u"WhisperCpp"_s).toString();
    if (backendStr == u"Groq"_s) {
        m_activeBackend = TranscriptionBackend::Groq;
    } else {
        m_activeBackend = TranscriptionBackend::WhisperCpp;
    }

    setStatusMessage(tr("Ready"));
    qCDebug(lcSpeech) << "DictationCoordinator deep module constructed. Initial backend:"
                      << (m_activeBackend == TranscriptionBackend::WhisperCpp ? "WhisperCpp" : "Groq")
                      << "mode:" << (m_recordingMode == RecordingMode::PushToTalk ? "PushToTalk" : "Toggle");
}

void DictationCoordinator::setAudioRecorder(AudioRecorder* recorder) {
    if (m_recorder == recorder) {
        return;
    }

    if (m_recorder) {
        disconnect(m_recorder, &AudioRecorder::recordingFinished, this, &DictationCoordinator::onRecordingFinished);
        disconnect(m_recorder, &AudioRecorder::maxDurationReached, this, &DictationCoordinator::onMaxDurationReached);
        disconnect(m_recorder, &AudioRecorder::audioLevelChanged, this, nullptr);
    }

    m_recorder = recorder;
    m_healthMonitor->setAudioRecorder(m_recorder);

    if (m_recorder) {
        connect(m_recorder, &AudioRecorder::recordingFinished, this, &DictationCoordinator::onRecordingFinished);
        connect(m_recorder, &AudioRecorder::maxDurationReached, this, &DictationCoordinator::onMaxDurationReached);
        connect(m_recorder, &AudioRecorder::audioLevelChanged, this, [this]() {
            if (m_recorder) {
                emit audioLevelChanged(m_recorder->audioLevel());
            }
        });
    }

    updateCoordinatorHealth();
}

void DictationCoordinator::registerBackend(TranscriptionBackend backend, AbstractSttClient* client) {
    if (m_sttClients.value(backend) == client) {
        return;
    }

    if (m_sttClients.contains(backend)) {
        auto* old = m_sttClients[backend];
        disconnect(old, &AbstractSttClient::transcriptionReady, this, &DictationCoordinator::onSttTranscriptionReady);
        disconnect(old, &AbstractSttClient::errorOccurred, this, &DictationCoordinator::onSttError);
    }

    if (client) {
        m_sttClients.insert(backend, client);
        connect(client, &AbstractSttClient::transcriptionReady, this, &DictationCoordinator::onSttTranscriptionReady);
        connect(client, &AbstractSttClient::errorOccurred, this, &DictationCoordinator::onSttError);

        if (backend == m_activeBackend && m_initialized) {
            client->activate();
        }
    } else {
        m_sttClients.remove(backend);
    }

    m_healthMonitor->setActiveSttClient(activeSttClient());
    updateCoordinatorHealth();
}

void DictationCoordinator::setLlmClient(GroqLlmClient* llmClient) {
    if (m_llmClient == llmClient) {
        return;
    }

    if (m_llmClient) {
        disconnect(m_llmClient, &GroqLlmClient::enhancementReady, this, &DictationCoordinator::onLlmEnhancementReady);
        disconnect(m_llmClient, &GroqLlmClient::errorOccurred, this, &DictationCoordinator::onLlmError);
    }

    m_llmClient = llmClient;

    if (m_llmClient) {
        connect(m_llmClient, &GroqLlmClient::enhancementReady, this, &DictationCoordinator::onLlmEnhancementReady);
        connect(m_llmClient, &GroqLlmClient::errorOccurred, this, &DictationCoordinator::onLlmError);
    }
}

void DictationCoordinator::setApiClient(GroqApiClient* api) {
    if (m_apiClient == api) {
        return;
    }
    if (m_apiClient) {
        disconnect(m_apiClient, &GroqApiClient::apiKeySetChanged, this, &DictationCoordinator::updateCoordinatorHealth);
    }
    m_apiClient = api;
    if (m_apiClient) {
        connect(m_apiClient, &GroqApiClient::apiKeySetChanged, this, &DictationCoordinator::updateCoordinatorHealth);
    }
    updateCoordinatorHealth();
}

void DictationCoordinator::setShortcutManager(GlobalShortcutManager* mgr) {
    if (m_shortcutMgr == mgr) {
        return;
    }
    if (m_shortcutMgr) {
        disconnect(m_shortcutMgr, &GlobalShortcutManager::shortcutActivated, this,
                   &DictationCoordinator::onShortcutActivated);
        disconnect(m_shortcutMgr, &GlobalShortcutManager::shortcutDeactivated, this,
                   &DictationCoordinator::onShortcutDeactivated);
    }
    m_shortcutMgr = mgr;
    m_healthMonitor->setShortcutManager(m_shortcutMgr);

    if (m_shortcutMgr) {
        connect(m_shortcutMgr, &GlobalShortcutManager::shortcutActivated, this,
                &DictationCoordinator::onShortcutActivated);
        connect(m_shortcutMgr, &GlobalShortcutManager::shortcutDeactivated, this,
                &DictationCoordinator::onShortcutDeactivated);
    }
    updateCoordinatorHealth();
}

void DictationCoordinator::setTextInjector(TextInjectorClient* injector) {
    if (m_injector == injector) {
        return;
    }
    m_injector = injector;
    m_healthMonitor->setTextInjector(m_injector);
    updateCoordinatorHealth();
}

void DictationCoordinator::setHistoryModel(TranscriptionModel* model) {
    m_historyModel = model;
}

DictationCoordinator::TranscriptionBackend DictationCoordinator::activeBackend() const {
    return m_activeBackend;
}

void DictationCoordinator::setActiveBackend(TranscriptionBackend backend) {
    if (m_activeBackend != backend) {
        if (isBusy()) {
            cancelDictation();
        }
        if (auto* oldClient = activeSttClient()) {
            oldClient->deactivate();
        }

        m_activeBackend = backend;
        QSettings settings;
        settings.setValue(u"Speech/Backend"_s,
                          m_activeBackend == TranscriptionBackend::WhisperCpp ? u"WhisperCpp"_s : u"Groq"_s);

        if (auto* newClient = activeSttClient()) {
            newClient->activate();
        }

        m_healthMonitor->setActiveSttClient(activeSttClient());
        emit activeBackendChanged();
        updateCoordinatorHealth();
    }
}

DictationCoordinator::RecordingMode DictationCoordinator::recordingMode() const {
    return m_recordingMode;
}

void DictationCoordinator::setRecordingMode(RecordingMode mode) {
    if (mode == RecordingMode::PushToTalk && !pushToTalkSupported()) {
        qCDebug(lcSpeech)
            << "Push-to-Talk requested but not supported on current desktop environment; falling back to Toggle";
        mode = RecordingMode::Toggle;
    }

    if (m_recordingMode != mode) {
        m_recordingMode = mode;
        QSettings settings;
        settings.setValue(u"Speech/RecordingMode"_s,
                          m_recordingMode == RecordingMode::PushToTalk ? u"PushToTalk"_s : u"Toggle"_s);
        emit recordingModeChanged();
    }
}

bool DictationCoordinator::pushToTalkSupported() const {
    return m_healthMonitor ? m_healthMonitor->pushToTalkSupported() : false;
}

void DictationCoordinator::initialize() {
    if (m_initialized) {
        return;
    }
    m_initialized = true;
    if (auto* client = activeSttClient()) {
        client->activate();
    }
    updateCoordinatorHealth();
    qCDebug(lcSpeech) << "DictationCoordinator: initialized and ready for dictation";
}

void DictationCoordinator::updateCoordinatorHealth() {
    if (m_recordingMode == RecordingMode::PushToTalk && !pushToTalkSupported()) {
        qCDebug(lcSpeech) << "Push-to-talk not supported by portal; reverting to Toggle mode";
        m_recordingMode = RecordingMode::Toggle;
        QSettings settings;
        settings.setValue(u"Speech/RecordingMode"_s, u"Toggle"_s);
        emit recordingModeChanged();
    }
    emit canRecordChanged();
    emit systemHealthChanged();
}

DictationCoordinator::DictationState DictationCoordinator::dictationState() const {
    return m_state;
}

bool DictationCoordinator::isBusy() const {
    return m_state == DictationState::Recording || m_state == DictationState::Transcribing ||
           m_state == DictationState::Enhancing;
}

bool DictationCoordinator::canRecord() const {
    const bool notProcessing = (m_state != DictationState::Transcribing && m_state != DictationState::Enhancing);
    return m_healthMonitor ? m_healthMonitor->canRecord(notProcessing) : false;
}

qreal DictationCoordinator::audioLevel() const {
    return m_recorder ? m_recorder->audioLevel() : 0.0;
}

QString DictationCoordinator::statusMessage() const {
    return m_statusMessage;
}

QString DictationCoordinator::lastError() const {
    return m_lastError;
}

bool DictationCoordinator::recording() const {
    return m_state == DictationState::Recording;
}

bool DictationCoordinator::transcribing() const {
    return m_state == DictationState::Transcribing;
}

bool DictationCoordinator::enhancing() const {
    return m_state == DictationState::Enhancing;
}

QString DictationCoordinator::lastTranscription() const {
    return m_lastTranscription;
}

bool DictationCoordinator::soundEnabled() const {
    return m_feedbackPlayer ? m_feedbackPlayer->soundEnabled() : true;
}

void DictationCoordinator::setSoundEnabled(bool enabled) {
    if (m_feedbackPlayer) {
        m_feedbackPlayer->setSoundEnabled(enabled);
    }
}

bool DictationCoordinator::systemShortcutHasIssue() const {
    return m_healthMonitor ? m_healthMonitor->systemShortcutHasIssue() : false;
}

bool DictationCoordinator::systemShortcutSupported() const {
    return m_healthMonitor ? m_healthMonitor->systemShortcutSupported() : false;
}

QString DictationCoordinator::systemShortcutStatus() const {
    return m_healthMonitor ? m_healthMonitor->systemShortcutStatus() : QString();
}

bool DictationCoordinator::directTypingHasIssue() const {
    return m_healthMonitor ? m_healthMonitor->directTypingHasIssue() : false;
}

bool DictationCoordinator::directTypingConnected() const {
    return m_healthMonitor ? m_healthMonitor->directTypingConnected() : false;
}

bool DictationCoordinator::directTypingFatalError() const {
    return m_healthMonitor ? m_healthMonitor->directTypingFatalError() : false;
}

QString DictationCoordinator::directTypingStatus() const {
    return m_healthMonitor ? m_healthMonitor->directTypingStatus() : QString();
}

QString DictationCoordinator::dictationPadText() const {
    return m_padModel ? m_padModel->text() : QString();
}

void DictationCoordinator::setDictationPadText(const QString& text) {
    if (m_padModel) {
        m_padModel->setText(text);
    }
}

int DictationCoordinator::dictationWordCount() const {
    return m_padModel ? m_padModel->wordCount() : 0;
}

int DictationCoordinator::dictationCharCount() const {
    return m_padModel ? m_padModel->charCount() : 0;
}

void DictationCoordinator::appendDictationPadText(const QString& text) {
    if (m_padModel) {
        m_padModel->append(text);
    }
}

void DictationCoordinator::clearDictationPad() {
    if (m_padModel) {
        m_padModel->clear();
    }
}

void DictationCoordinator::copyDictationPad() {
    if (m_padModel) {
        m_padModel->copyToClipboard();
    }
}

void DictationCoordinator::copyToClipboard(const QString& text) {
    DictationPadModel::copyTextToClipboard(text);
}

AbstractSttClient* DictationCoordinator::activeSttClient() const {
    return m_sttClients.value(m_activeBackend, nullptr);
}

DictationPadModel* DictationCoordinator::dictationPadModel() const {
    return m_padModel;
}

AudioFeedbackPlayer* DictationCoordinator::audioFeedbackPlayer() const {
    return m_feedbackPlayer;
}

SystemHealthMonitor* DictationCoordinator::systemHealthMonitor() const {
    return m_healthMonitor;
}

void DictationCoordinator::showWindow() {
    qCDebug(lcSpeech) << "DictationCoordinator: showWindow requested via IPC/D-Bus";
    emit requestShowWindow();
}

void DictationCoordinator::quitApp() {
    qCDebug(lcSpeech) << "DictationCoordinator: quitApp requested via IPC/D-Bus";
    emit requestQuitApp();
}

void DictationCoordinator::startRecording() {
    if (isBusy()) {
        qCDebug(lcSpeech) << "DictationCoordinator: startRecording ignored — already busy";
        return;
    }

    if (m_injector) {
        m_injector->cancelPendingInjection();
    }

    if (!m_recorder || !m_recorder->hasAudioInputDevice()) {
        qWarning() << "DictationCoordinator: No microphone input device available";
        setLastError(tr("No microphone found. Please connect a microphone."));
        setStatusMessage(tr("No microphone found"));
        setState(DictationState::Error);
        return;
    }

    if (m_feedbackPlayer) {
        m_feedbackPlayer->playStartSound();
    }

    setLastError({});
    setState(DictationState::Recording);
    setStatusMessage(tr("Listening…"));
    m_recorder->startRecording();
}

void DictationCoordinator::stopRecording() {
    if (m_state != DictationState::Recording) {
        qCDebug(lcSpeech) << "DictationCoordinator: stopRecording ignored — not currently recording";
        return;
    }

    if (m_feedbackPlayer) {
        m_feedbackPlayer->playStopSound();
    }

    if (m_recorder) {
        m_recorder->stopRecording();
    }
}

void DictationCoordinator::toggleRecording() {
    if (m_state == DictationState::Recording) {
        stopRecording();
    } else if (m_state == DictationState::Idle || m_state == DictationState::Error) {
        startRecording();
    } else {
        qCDebug(lcSpeech) << "DictationCoordinator: Toggle ignored while busy (state:" << static_cast<int>(m_state)
                          << ")";
    }
}

void DictationCoordinator::cancelDictation() {
    if (m_state == DictationState::Idle) {
        return;
    }

    qCDebug(lcSpeech) << "DictationCoordinator: Cancelling operation in state:" << static_cast<int>(m_state);

    if (m_injector) {
        m_injector->cancelPendingInjection();
    }

    if (m_state == DictationState::Recording && m_recorder) {
        m_recorder->cancelRecording();
    }

    if (auto* stt = activeSttClient()) {
        stt->cancel();
    }

    if (m_llmClient) {
        m_llmClient->cancel();
    }

    m_lastWavData.clear();
    setState(DictationState::Idle);
    setStatusMessage(tr("Ready"));
}

void DictationCoordinator::retryTranscription() {
    if (m_state != DictationState::Error || m_lastWavData.isEmpty()) {
        qCDebug(lcSpeech) << "DictationCoordinator: Cannot retry — not in Error state or no audio cached";
        return;
    }

    setLastError({});
    setState(DictationState::Transcribing);
    setStatusMessage(tr("Retrying transcription…"));

    auto* stt = activeSttClient();
    if (stt) {
        stt->transcribe(m_lastWavData);
    } else {
        setLastError(tr("Speech-to-text service is unavailable"));
        setState(DictationState::Error);
    }
}

void DictationCoordinator::clearLastTranscription() {
    if (!m_lastTranscription.isEmpty()) {
        m_lastTranscription.clear();
        emit lastTranscriptionChanged();
    }
}

void DictationCoordinator::clearError() {
    if (m_state == DictationState::Error) {
        setLastError({});
        setState(DictationState::Idle);
        setStatusMessage(tr("Ready"));
    }
}

void DictationCoordinator::playStartSound() {
    if (m_feedbackPlayer) {
        m_feedbackPlayer->playStartSound();
    }
}

void DictationCoordinator::playStopSound() {
    if (m_feedbackPlayer) {
        m_feedbackPlayer->playStopSound();
    }
}

void DictationCoordinator::onRecordingFinished(const QByteArray& wavData) {
    if (m_state != DictationState::Recording) {
        qCDebug(lcSpeech) << "DictationCoordinator: Ignoring recordingFinished — not in Recording state";
        return;
    }

    if (wavData.isEmpty()) {
        qCDebug(lcSpeech) << "DictationCoordinator: Empty audio payload, resetting to Idle";
        setState(DictationState::Idle);
        setStatusMessage(tr("Ready"));
        return;
    }

    m_lastWavData = wavData;
    setState(DictationState::Transcribing);
    setStatusMessage(tr("Transcribing audio…"));

    auto* stt = activeSttClient();
    if (stt) {
        stt->transcribe(wavData);
    } else {
        setLastError(tr("Speech-to-text service is unavailable"));
        setState(DictationState::Error);
    }
}

void DictationCoordinator::onMaxDurationReached() {
    qWarning() << "DictationCoordinator: Maximum recording duration safety limit reached";
    emit maxDurationWarningTriggered();
    stopRecording();
}

void DictationCoordinator::onSttTranscriptionReady(const QString& text) {
    if (m_state != DictationState::Transcribing || (sender() && sender() != activeSttClient())) {
        qCDebug(lcSpeech) << "DictationCoordinator: Ignoring transcriptionReady: not in Transcribing state or sender "
                             "is not active client";
        return;
    }

    qCDebug(lcSpeech) << "DictationCoordinator: STT transcription received (" << text.size() << "chars)";

    if (text.trimmed().isEmpty()) {
        setState(DictationState::Idle);
        setStatusMessage(tr("Ready"));
        updateCoordinatorHealth();
        return;
    }

    if (m_activeBackend == TranscriptionBackend::Groq && m_llmClient && m_llmClient->enabled()) {
        qCDebug(lcSpeech) << "DictationCoordinator: Passing transcription to LLM post-processing...";
        setState(DictationState::Enhancing);
        setStatusMessage(tr("Enhancing text with AI…"));
        m_llmClient->processText(text);
        updateCoordinatorHealth();
        return;
    }

    completeTranscription(text);
}

void DictationCoordinator::onLlmEnhancementReady(const QString& enhancedText) {
    qCDebug(lcSpeech) << "DictationCoordinator: LLM enhanced text received (" << enhancedText.size() << "chars)";
    completeTranscription(enhancedText);
}

void DictationCoordinator::onLlmError(const QString& error, const QString& fallbackRawText) {
    qWarning() << "DictationCoordinator: LLM enhancement failed:" << error << "-> Falling back to raw transcription.";
    emit llmFallbackWarningTriggered(error);

    if (!fallbackRawText.isEmpty()) {
        completeTranscription(fallbackRawText);
        setStatusMessage(tr("Ready (Used raw Whisper text)"));
    } else {
        setState(DictationState::Idle);
        setStatusMessage(tr("Ready"));
        updateCoordinatorHealth();
    }
}

void DictationCoordinator::completeTranscription(const QString& text) {
    m_lastTranscription = text;
    emit lastTranscriptionChanged();
    setState(DictationState::Idle);
    setStatusMessage(tr("Ready"));
    updateCoordinatorHealth();
    finishTranscriptionAndInject(text);
    emit transcriptionFinished(text);
}

void DictationCoordinator::finishTranscriptionAndInject(const QString& text) {
    if (text.isEmpty()) {
        return;
    }

    if (m_historyModel) {
        m_historyModel->addRecord(text);
    }

    appendDictationPadText(text);

    // Only inject virtual keystrokes into external target applications.
    // If QTranscribe itself currently has window focus, the text is already
    // displayed in the dictation pad and injecting Ctrl+V would cause duplicate text.
    const bool isAppActive = (QGuiApplication::focusWindow() != nullptr);
    if (m_injector && !isAppActive) {
        m_injector->typeText(text);
    }
}

void DictationCoordinator::onSttError(const QString& error) {
    if (m_state != DictationState::Transcribing || (sender() && sender() != activeSttClient())) {
        qCDebug(lcSpeech)
            << "DictationCoordinator: Ignoring STT error: not in Transcribing state or sender is not active client";
        return;
    }

    qWarning() << "DictationCoordinator: STT Error:" << error;
    setLastError(error);
    setStatusMessage(error);
    setState(DictationState::Error);
    updateCoordinatorHealth();
    emit errorOccurred(error);
}

void DictationCoordinator::onShortcutActivated(const QString& shortcutId) {
    if (shortcutId == u"toggle-recording"_s || shortcutId == u"record"_s) {
        qCDebug(lcSpeech) << "DictationCoordinator: Global shortcut activated (mode:"
                          << (m_recordingMode == RecordingMode::PushToTalk ? "PushToTalk" : "Toggle") << ")";
        if (m_recordingMode == RecordingMode::PushToTalk) {
            if (!recording() && canRecord()) {
                startRecording();
            }
        } else {
            toggleRecording();
        }
    }
}

void DictationCoordinator::onShortcutDeactivated(const QString& shortcutId) {
    if (shortcutId == u"toggle-recording"_s || shortcutId == u"record"_s) {
        qCDebug(lcSpeech) << "DictationCoordinator: Global shortcut deactivated (mode:"
                          << (m_recordingMode == RecordingMode::PushToTalk ? "PushToTalk" : "Toggle") << ")";
        if (m_recordingMode == RecordingMode::PushToTalk) {
            if (recording()) {
                stopRecording();
            }
        }
    }
}

void DictationCoordinator::setState(DictationState state) {
    if (m_state != state) {
        m_state = state;
        emit dictationStateChanged();
        emit recordingChanged();
        emit transcribingChanged();
        emit enhancingChanged();
        emit canRecordChanged();
    }
}

void DictationCoordinator::setStatusMessage(const QString& msg) {
    if (m_statusMessage != msg) {
        m_statusMessage = msg;
        emit statusMessageChanged();
    }
}

void DictationCoordinator::setLastError(const QString& error) {
    if (m_lastError != error) {
        m_lastError = error;
        emit lastErrorChanged();
    }
}
