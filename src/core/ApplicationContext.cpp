#include "ApplicationContext.h"

#include "AudioRecorder.h"
#include "DBusService.h"
#include "DictationCoordinator.h"
#include "GroqApiClient.h"
#include "GroqLlmClient.h"
#include "GroqSttClient.h"
#include "GroqUsageTracker.h"
#include "LoggingCategories.h"
#include "TextInjectorClient.h"
#include "TranscriptionModel.h"

#include "ApiKeyStore.h"
#include "GlobalShortcutManager.h"
#include "StatusNotifierService.h"
#include "WhisperModelManager.h"
#include "WhisperSttClient.h"

#include <QQmlApplicationEngine>

ApplicationContext::ApplicationContext(QObject* parent)
    : QObject(parent) { }

void ApplicationContext::initialize(QQmlApplicationEngine& engine) {
    if (m_initialized) {
        return;
    }

    m_apiClient = engine.singletonInstance<GroqApiClient*>("QTranscribe", "GroqApiClient");
    m_groqSttClient = engine.singletonInstance<GroqSttClient*>("QTranscribe", "GroqSttClient");
    m_whisperSttClient = engine.singletonInstance<WhisperSttClient*>("QTranscribe", "WhisperSttClient");
    m_whisperModelManager = engine.singletonInstance<WhisperModelManager*>("QTranscribe", "WhisperModelManager");
    m_groqLlmClient = engine.singletonInstance<GroqLlmClient*>("QTranscribe", "GroqLlmClient");
    m_groqUsageTracker = engine.singletonInstance<GroqUsageTracker*>("QTranscribe", "GroqUsageTracker");
    m_audioRecorder = engine.singletonInstance<AudioRecorder*>("QTranscribe", "AudioRecorder");
    m_shortcutManager = engine.singletonInstance<GlobalShortcutManager*>("QTranscribe", "GlobalShortcutManager");
    m_textInjector = engine.singletonInstance<TextInjectorClient*>("QTranscribe", "TextInjectorClient");
    m_historyModel = engine.singletonInstance<TranscriptionModel*>("QTranscribe", "TranscriptionModel");
    m_dictationCoordinator = engine.singletonInstance<DictationCoordinator*>("QTranscribe", "DictationCoordinator");

    wireSubsystems();

    if (m_dictationCoordinator) {
        m_dbusService = new DBusService(this);
        m_dbusService->registerController(m_dictationCoordinator);

        m_statusNotifierService = new StatusNotifierService(this);
        m_statusNotifierService->registerController(m_dictationCoordinator);
    }

    m_initialized = true;
    qCDebug(lcSpeech) << "ApplicationContext: GUI composition root successfully initialized";
}

void ApplicationContext::initializeHeadless() {
    if (m_initialized) {
        return;
    }

    m_apiClient = new GroqApiClient(this);
    m_groqSttClient = new GroqSttClient(this);
    m_whisperModelManager = new WhisperModelManager(this);
    m_whisperSttClient = new WhisperSttClient(this);
    m_groqLlmClient = new GroqLlmClient(this);
    m_groqUsageTracker = new GroqUsageTracker(this);
    m_audioRecorder = new AudioRecorder(this);
    m_shortcutManager = new GlobalShortcutManager(this);
    m_textInjector = new TextInjectorClient(this);
    m_historyModel = new TranscriptionModel(this);
    m_dictationCoordinator = new DictationCoordinator(this);

    wireSubsystems();

    m_initialized = true;
    qCDebug(lcSpeech) << "ApplicationContext: Headless composition root successfully initialized";
}

void ApplicationContext::wireSubsystems() {
    if (m_groqSttClient && m_apiClient) {
        m_groqSttClient->setApiClient(m_apiClient);
    }
    if (m_whisperSttClient && m_whisperModelManager) {
        m_whisperSttClient->setModelManager(m_whisperModelManager);
    }
    if (m_groqLlmClient && m_apiClient) {
        m_groqLlmClient->setApiClient(m_apiClient);
    }
    if (m_groqUsageTracker && m_apiClient) {
        m_groqUsageTracker->setApiClient(m_apiClient);
    }

    if (m_dictationCoordinator) {
        m_dictationCoordinator->setAudioRecorder(m_audioRecorder);
        m_dictationCoordinator->registerBackend(DictationCoordinator::TranscriptionBackend::Groq, m_groqSttClient);
        m_dictationCoordinator->registerBackend(DictationCoordinator::TranscriptionBackend::WhisperCpp,
                                                m_whisperSttClient);
        m_dictationCoordinator->setLlmClient(m_groqLlmClient);
        m_dictationCoordinator->setApiClient(m_apiClient);
        m_dictationCoordinator->setShortcutManager(m_shortcutManager);
        m_dictationCoordinator->setTextInjector(m_textInjector);
        m_dictationCoordinator->setHistoryModel(m_historyModel);
        m_dictationCoordinator->initialize();
    }
}

bool ApplicationContext::isInitialized() const noexcept {
    return m_initialized;
}

DictationCoordinator* ApplicationContext::dictationCoordinator() const noexcept {
    return m_dictationCoordinator;
}

GroqApiClient* ApplicationContext::groqApiClient() const noexcept {
    return m_apiClient;
}

ApiKeyStore* ApplicationContext::apiKeyStore() const noexcept {
    return m_apiClient ? m_apiClient->keyStore() : nullptr;
}

GroqSttClient* ApplicationContext::groqSttClient() const noexcept {
    return m_groqSttClient;
}

WhisperSttClient* ApplicationContext::whisperSttClient() const noexcept {
    return m_whisperSttClient;
}

WhisperModelManager* ApplicationContext::whisperModelManager() const noexcept {
    return m_whisperModelManager;
}

GroqLlmClient* ApplicationContext::groqLlmClient() const noexcept {
    return m_groqLlmClient;
}

GroqUsageTracker* ApplicationContext::groqUsageTracker() const noexcept {
    return m_groqUsageTracker;
}

AudioRecorder* ApplicationContext::audioRecorder() const noexcept {
    return m_audioRecorder;
}

GlobalShortcutManager* ApplicationContext::shortcutManager() const noexcept {
    return m_shortcutManager;
}

TextInjectorClient* ApplicationContext::textInjector() const noexcept {
    return m_textInjector;
}

TranscriptionModel* ApplicationContext::historyModel() const noexcept {
    return m_historyModel;
}

DBusService* ApplicationContext::dbusService() const noexcept {
    return m_dbusService;
}

StatusNotifierService* ApplicationContext::statusNotifierService() const noexcept {
    return m_statusNotifierService;
}
