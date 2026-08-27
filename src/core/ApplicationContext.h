#pragma once

#include <QObject>

class AudioRecorder;
class DBusService;
class DictationCoordinator;
class GlobalShortcutManager;
class GroqApiClient;
class GroqLlmClient;
class GroqSttClient;
class GroqUsageTracker;
class StatusNotifierService;
class TextInjectorClient;
class TranscriptionModel;
class WhisperModelManager;
class WhisperSttClient;
class QQmlApplicationEngine;

class ApplicationContext : public QObject {
    Q_OBJECT

public:
    explicit ApplicationContext(QObject* parent = nullptr);
    ~ApplicationContext() override = default;

    void initialize(QQmlApplicationEngine& engine);
    void initializeHeadless();
    [[nodiscard]] bool isInitialized() const noexcept;

    [[nodiscard]] DictationCoordinator* dictationCoordinator() const noexcept;
    [[nodiscard]] GroqApiClient* groqApiClient() const noexcept;
    [[nodiscard]] GroqSttClient* groqSttClient() const noexcept;
    [[nodiscard]] WhisperSttClient* whisperSttClient() const noexcept;
    [[nodiscard]] WhisperModelManager* whisperModelManager() const noexcept;
    [[nodiscard]] GroqLlmClient* groqLlmClient() const noexcept;
    [[nodiscard]] GroqUsageTracker* groqUsageTracker() const noexcept;
    [[nodiscard]] AudioRecorder* audioRecorder() const noexcept;
    [[nodiscard]] GlobalShortcutManager* shortcutManager() const noexcept;
    [[nodiscard]] TextInjectorClient* textInjector() const noexcept;
    [[nodiscard]] TranscriptionModel* historyModel() const noexcept;
    [[nodiscard]] DBusService* dbusService() const noexcept;
    [[nodiscard]] StatusNotifierService* statusNotifierService() const noexcept;

private:
    void wireSubsystems();

    GroqApiClient* m_apiClient = nullptr;
    GroqSttClient* m_groqSttClient = nullptr;
    WhisperSttClient* m_whisperSttClient = nullptr;
    WhisperModelManager* m_whisperModelManager = nullptr;
    GroqLlmClient* m_groqLlmClient = nullptr;
    GroqUsageTracker* m_groqUsageTracker = nullptr;
    AudioRecorder* m_audioRecorder = nullptr;
    GlobalShortcutManager* m_shortcutManager = nullptr;
    TextInjectorClient* m_textInjector = nullptr;
    TranscriptionModel* m_historyModel = nullptr;
    DictationCoordinator* m_dictationCoordinator = nullptr;
    DBusService* m_dbusService = nullptr;
    StatusNotifierService* m_statusNotifierService = nullptr;

    bool m_initialized = false;
};
