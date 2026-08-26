#include "SpeechController.h"

#include "GroqApiClient.h"
#include "LoggingCategories.h"
#include "TextInjectorClient.h"
#include "TranscriptionModel.h"

#include "GlobalShortcutManager.h"
#include "TranscriptionPipeline.h"

#include <QClipboard>
#include <QDebug>
#include <QGuiApplication>
#include <QSettings>
#include <QSoundEffect>
#include <QStringTokenizer>
#include <QUrl>

using namespace Qt::StringLiterals;

SpeechController::SpeechController(QObject* parent)
    : QObject(parent)
    , m_startChime(new QSoundEffect(this))
    , m_stopChime(new QSoundEffect(this)) {
    m_startChime->setSource(QUrl(u"qrc:/qt/qml/QTranscribe/assets/chime_start.wav"_s));
    m_startChime->setVolume(0.8f);

    m_stopChime->setSource(QUrl(u"qrc:/qt/qml/QTranscribe/assets/chime_stop.wav"_s));
    m_stopChime->setVolume(0.8f);

    QSettings settings;
    m_soundEnabled = settings.value(u"Audio/SoundEnabled"_s, true).toBool();

    qCDebug(lcSpeech) << "SpeechController presentation coordinator constructed.";
}

void SpeechController::setPipeline(TranscriptionPipeline* pipeline) {
    if (m_pipeline == pipeline) {
        return;
    }

    if (m_pipeline) {
        disconnect(m_pipeline, nullptr, this, nullptr);
    }

    m_pipeline = pipeline;

    if (m_pipeline) {
        connect(m_pipeline, &TranscriptionPipeline::transcriptionFinished, this,
                &SpeechController::onPipelineTranscriptionFinished);
        connect(m_pipeline, &TranscriptionPipeline::stateChanged, this, &SpeechController::onPipelineStateChanged);
        connect(m_pipeline, &TranscriptionPipeline::activeBackendChanged, this,
                &SpeechController::activeBackendChanged);
        connect(m_pipeline, &TranscriptionPipeline::canRecordChanged, this, &SpeechController::canRecordChanged);
        connect(m_pipeline, &TranscriptionPipeline::audioLevelChanged, this, &SpeechController::audioLevelChanged);
        connect(m_pipeline, &TranscriptionPipeline::statusMessageChanged, this,
                &SpeechController::statusMessageChanged);
        connect(m_pipeline, &TranscriptionPipeline::lastErrorChanged, this, &SpeechController::lastErrorChanged);
        connect(m_pipeline, &TranscriptionPipeline::lastTranscriptionChanged, this,
                &SpeechController::lastTranscriptionChanged);
        connect(m_pipeline, &TranscriptionPipeline::maxDurationWarningTriggered, this,
                &SpeechController::maxDurationWarningTriggered);
        connect(m_pipeline, &TranscriptionPipeline::llmFallbackWarningTriggered, this,
                &SpeechController::llmFallbackWarningTriggered);
    }

    emit activeBackendChanged();
    emit dictationStateChanged();
    emit recordingChanged();
    emit transcribingChanged();
    emit enhancingChanged();
    emit lastTranscriptionChanged();
    emit statusMessageChanged();
    emit lastErrorChanged();
    updatePresenterState();
}

TranscriptionPipeline* SpeechController::pipeline() const {
    return m_pipeline;
}

void SpeechController::setApiClient(GroqApiClient* api) {
    if (m_apiClient == api) {
        return;
    }
    if (m_apiClient) {
        disconnect(m_apiClient, &GroqApiClient::apiKeySetChanged, this, &SpeechController::updatePresenterState);
    }
    m_apiClient = api;
    if (m_apiClient) {
        connect(m_apiClient, &GroqApiClient::apiKeySetChanged, this, &SpeechController::updatePresenterState);
    }
    updatePresenterState();
}

void SpeechController::setShortcutManager(GlobalShortcutManager* mgr) {
    if (m_shortcutMgr == mgr) {
        return;
    }
    if (m_shortcutMgr) {
        disconnect(m_shortcutMgr, &GlobalShortcutManager::shortcutActivated, this,
                   &SpeechController::onShortcutActivated);
        disconnect(m_shortcutMgr, &GlobalShortcutManager::availableChanged, this,
                   &SpeechController::updatePresenterState);
        disconnect(m_shortcutMgr, &GlobalShortcutManager::supportedChanged, this,
                   &SpeechController::updatePresenterState);
        disconnect(m_shortcutMgr, &GlobalShortcutManager::statusMessageChanged, this,
                   &SpeechController::updatePresenterState);
    }
    m_shortcutMgr = mgr;
    if (m_shortcutMgr) {
        connect(m_shortcutMgr, &GlobalShortcutManager::shortcutActivated, this, &SpeechController::onShortcutActivated);
        connect(m_shortcutMgr, &GlobalShortcutManager::availableChanged, this, &SpeechController::updatePresenterState);
        connect(m_shortcutMgr, &GlobalShortcutManager::supportedChanged, this, &SpeechController::updatePresenterState);
        connect(m_shortcutMgr, &GlobalShortcutManager::statusMessageChanged, this,
                &SpeechController::updatePresenterState);
    }
    updatePresenterState();
}

void SpeechController::setTextInjector(TextInjectorClient* injector) {
    if (m_injector == injector) {
        return;
    }
    if (m_injector) {
        disconnect(m_injector, &TextInjectorClient::connectedChanged, this, &SpeechController::updatePresenterState);
        disconnect(m_injector, &TextInjectorClient::hasFatalErrorChanged, this,
                   &SpeechController::updatePresenterState);
        disconnect(m_injector, &TextInjectorClient::fatalErrorMessageChanged, this,
                   &SpeechController::updatePresenterState);
    }
    m_injector = injector;
    if (m_injector) {
        connect(m_injector, &TextInjectorClient::connectedChanged, this, &SpeechController::updatePresenterState);
        connect(m_injector, &TextInjectorClient::hasFatalErrorChanged, this, &SpeechController::updatePresenterState);
        connect(m_injector, &TextInjectorClient::fatalErrorMessageChanged, this,
                &SpeechController::updatePresenterState);
    }
    updatePresenterState();
}

void SpeechController::setHistoryModel(TranscriptionModel* model) {
    m_historyModel = model;
}

SpeechController::TranscriptionBackend SpeechController::activeBackend() const {
    if (!m_pipeline) {
        QSettings settings;
        const QString backendStr = settings.value(u"Speech/Backend"_s, u"WhisperCpp"_s).toString();
        if (backendStr == u"Groq"_s) {
            return TranscriptionBackend::Groq;
        }
        return TranscriptionBackend::WhisperCpp;
    }
    return static_cast<TranscriptionBackend>(m_pipeline->activeBackend());
}

void SpeechController::setActiveBackend(TranscriptionBackend backend) {
    if (m_pipeline) {
        m_pipeline->setActiveBackend(static_cast<TranscriptionPipeline::Backend>(backend));
    } else {
        QSettings settings;
        settings.setValue(u"Speech/Backend"_s,
                          backend == TranscriptionBackend::WhisperCpp ? u"WhisperCpp"_s : u"Groq"_s);
        emit activeBackendChanged();
    }
}

void SpeechController::initialize() {
    if (m_initialized) {
        return;
    }
    m_initialized = true;
    if (m_pipeline) {
        m_pipeline->initialize();
    }
    updatePresenterState();
    qCDebug(lcSpeech) << "SpeechController: initialized and connected to TranscriptionPipeline";
}

void SpeechController::updatePresenterState() {
    emit canRecordChanged();
    emit systemHealthChanged();
}

SpeechController::DictationState SpeechController::dictationState() const {
    if (!m_pipeline) {
        return DictationState::Idle;
    }
    return static_cast<DictationState>(m_pipeline->state());
}

bool SpeechController::isBusy() const {
    return m_pipeline && m_pipeline->isBusy();
}

bool SpeechController::canRecord() const {
    return m_pipeline && m_pipeline->canRecord();
}

qreal SpeechController::audioLevel() const {
    return m_pipeline ? m_pipeline->audioLevel() : 0.0;
}

QString SpeechController::statusMessage() const {
    return m_pipeline ? m_pipeline->statusMessage() : QString();
}

QString SpeechController::lastError() const {
    return m_pipeline ? m_pipeline->lastError() : QString();
}

bool SpeechController::recording() const {
    return m_pipeline && m_pipeline->state() == TranscriptionPipeline::State::Recording;
}

bool SpeechController::transcribing() const {
    return m_pipeline && m_pipeline->state() == TranscriptionPipeline::State::Transcribing;
}

bool SpeechController::enhancing() const {
    return m_pipeline && m_pipeline->state() == TranscriptionPipeline::State::Enhancing;
}

QString SpeechController::lastTranscription() const {
    return m_pipeline ? m_pipeline->lastTranscription() : QString();
}

bool SpeechController::soundEnabled() const {
    return m_soundEnabled;
}

void SpeechController::setSoundEnabled(bool enabled) {
    if (m_soundEnabled != enabled) {
        m_soundEnabled = enabled;
        QSettings settings;
        settings.setValue(u"Audio/SoundEnabled"_s, m_soundEnabled);
        emit soundEnabledChanged();
    }
}

bool SpeechController::systemShortcutHasIssue() const {
    return m_shortcutMgr && !m_shortcutMgr->isAvailable();
}

bool SpeechController::systemShortcutSupported() const {
    return m_shortcutMgr && m_shortcutMgr->isSupported();
}

QString SpeechController::systemShortcutStatus() const {
    return m_shortcutMgr ? m_shortcutMgr->statusMessage() : QString();
}

bool SpeechController::directTypingHasIssue() const {
    return m_injector && (!m_injector->isConnected() || m_injector->hasFatalError());
}

bool SpeechController::directTypingConnected() const {
    return m_injector && m_injector->isConnected();
}

bool SpeechController::directTypingFatalError() const {
    return m_injector && m_injector->hasFatalError();
}

QString SpeechController::directTypingStatus() const {
    if (!m_injector) {
        return QString();
    }
    if (m_injector->hasFatalError()) {
        return m_injector->fatalErrorMessage().isEmpty() ? tr("Direct Typing Error") : m_injector->fatalErrorMessage();
    }
    if (!m_injector->isConnected()) {
        return tr("Clipboard Fallback");
    }
    return tr("Connected");
}

QString SpeechController::dictationPadText() const {
    return m_dictationPadText;
}

void SpeechController::setDictationPadText(const QString& text) {
    if (m_dictationPadText != text) {
        m_dictationPadText = text;
        emit dictationPadTextChanged();
    }
}

int SpeechController::dictationWordCount() const {
    return calculateWordCount(m_dictationPadText);
}

int SpeechController::dictationCharCount() const {
    return m_dictationPadText.length();
}

void SpeechController::appendDictationPadText(const QString& text) {
    if (text.isEmpty()) {
        return;
    }
    if (m_dictationPadText.isEmpty()) {
        m_dictationPadText = text;
    } else {
        m_dictationPadText += u"\n"_s + text;
    }
    emit dictationPadTextChanged();
}

void SpeechController::clearDictationPad() {
    if (!m_dictationPadText.isEmpty()) {
        m_dictationPadText.clear();
        emit dictationPadTextChanged();
    }
}

void SpeechController::copyDictationPad() {
    if (!m_dictationPadText.isEmpty()) {
        copyToClipboard(m_dictationPadText);
    }
}

void SpeechController::copyToClipboard(const QString& text) {
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

int SpeechController::calculateWordCount(const QString& text) {
    int words = 0;
    for (auto token : QStringTokenizer {QStringView(text), u' ', Qt::SkipEmptyParts}) {
        if (!token.trimmed().isEmpty()) {
            ++words;
        }
    }
    return words;
}

void SpeechController::showWindow() {
    qCDebug(lcSpeech) << "SpeechController: showWindow requested via IPC/D-Bus";
    emit requestShowWindow();
}

void SpeechController::quitApp() {
    qCDebug(lcSpeech) << "SpeechController: quitApp requested via IPC/D-Bus";
    emit requestQuitApp();
}

void SpeechController::startRecording() {
    if (isBusy()) {
        qCDebug(lcSpeech) << "SpeechController: startRecording ignored — already busy";
        return;
    }

    if (m_injector) {
        m_injector->cancelPendingInjection();
    }

    if (m_soundEnabled && m_startChime) {
        m_startChime->play();
    }

    if (m_pipeline) {
        m_pipeline->startRecording();
    }
}

void SpeechController::stopRecording() {
    if (!recording()) {
        qCDebug(lcSpeech) << "SpeechController: stopRecording ignored — not currently recording";
        return;
    }

    if (m_soundEnabled && m_stopChime) {
        m_stopChime->play();
    }

    if (m_pipeline) {
        m_pipeline->stopRecording();
    }
}

void SpeechController::toggleRecording() {
    if (recording()) {
        stopRecording();
    } else if (dictationState() == DictationState::Idle || dictationState() == DictationState::Error) {
        startRecording();
    } else {
        qCDebug(lcSpeech) << "SpeechController: Toggle ignored while busy (state:" << static_cast<int>(dictationState())
                          << ")";
    }
}

void SpeechController::cancelDictation() {
    if (m_injector) {
        m_injector->cancelPendingInjection();
    }
    if (m_pipeline) {
        m_pipeline->cancel();
    }
}

void SpeechController::retryTranscription() {
    if (m_pipeline) {
        m_pipeline->retry();
    }
}

void SpeechController::clearLastTranscription() {
    if (m_pipeline) {
        m_pipeline->clearLastTranscription();
    }
}

void SpeechController::clearError() {
    if (m_pipeline) {
        m_pipeline->clearError();
    }
}

void SpeechController::playStartSound() {
    if (m_startChime) {
        m_startChime->play();
    }
}

void SpeechController::playStopSound() {
    if (m_stopChime) {
        m_stopChime->play();
    }
}

void SpeechController::onShortcutActivated(const QString& shortcutId) {
    if (shortcutId == u"toggle-recording"_s) {
        qCDebug(lcSpeech) << "SpeechController: Global shortcut triggered toggleRecording";
        toggleRecording();
    }
}

void SpeechController::onPipelineTranscriptionFinished(const QString& text) {
    finishTranscriptionAndInject(text);
}

void SpeechController::onPipelineStateChanged(TranscriptionPipeline::State /*state*/) {
    emit dictationStateChanged();
    emit recordingChanged();
    emit transcribingChanged();
    emit enhancingChanged();
    updatePresenterState();
}

void SpeechController::finishTranscriptionAndInject(const QString& text) {
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
