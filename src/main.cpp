#include "AudioRecorder.h"
#include "DBusService.h"
#include "GroqApiClient.h"
#include "GroqLlmClient.h"
#include "GroqSttClient.h"
#include "GroqUsageTracker.h"
#include "SpeechController.h"
#include "TextInjectorClient.h"
#include "TranscriptionModel.h"

#include "GlobalShortcutManager.h"
#include "StatusNotifierService.h"
#include "TranscriptionPipeline.h"
#include "WhisperModelManager.h"
#include "WhisperSttClient.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>
#include <QtQml/QQmlExtensionPlugin>

#include <array>

Q_IMPORT_QML_PLUGIN(QTranscribePlugin)

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

#ifndef QTRANSCRIBE_VERSION
#define QTRANSCRIBE_VERSION "0.0.0-dev"
#endif

int main(int argc, char* argv[]) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "wayland");
    }

    QGuiApplication app(argc, argv);
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(u"Fusion"_s);
        QQuickStyle::setFallbackStyle(u"Fusion"_s);
    }
    app.setApplicationName(u"QTranscribe"_s);
    app.setApplicationVersion(u"" QTRANSCRIBE_VERSION ""_s);
    app.setOrganizationName(u"QTranscribe"_s);
    app.setOrganizationDomain(u"io.github.qtranscribe"_s);
    app.setDesktopFileName(u"io.github.qtranscribe"_s);
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(u"QTranscribe — Wayland native Speech-to-Text application"_s);
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption toggleOption({u"t"_s, u"toggle"_s}, u"Toggle speech-to-text recording"_s);
    QCommandLineOption startOption(u"start"_s, u"Start speech-to-text recording"_s);
    QCommandLineOption stopOption(u"stop"_s, u"Stop speech-to-text recording"_s);
    QCommandLineOption showOption({u"s"_s, u"show"_s}, u"Show and activate the main window"_s);
    QCommandLineOption quitOption({u"q"_s, u"quit"_s}, u"Quit the running application"_s);

    parser.addOption(toggleOption);
    parser.addOption(startOption);
    parser.addOption(stopOption);
    parser.addOption(showOption);
    parser.addOption(quitOption);

    parser.process(app);

    const auto command = [&]() -> QString {
        if (parser.isSet(toggleOption))
            return u"toggleRecording"_s;
        if (parser.isSet(startOption))
            return u"startRecording"_s;
        if (parser.isSet(stopOption))
            return u"stopRecording"_s;
        if (parser.isSet(quitOption))
            return u"quitApp"_s;
        return u"showWindow"_s;
    }();
    if (DBusService::sendRemoteCommand(command)) {
        return 0;
    }

    QIcon appIcon;
    constexpr auto kIconSizes = std::to_array({16, 24, 32, 64, 128, 256, 512});
    for (int sz : kIconSizes) {
        appIcon.addFile(QString(u":/qt/qml/QTranscribe/assets/speech-to-text-%1.png"_s).arg(sz), QSize(sz, sz));
    }
    app.setWindowIcon(appIcon);

    QQmlApplicationEngine engine;

    QObject::connect(&engine, &QQmlEngine::quit, []() { QCoreApplication::exit(0); });

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    auto* api = engine.singletonInstance<GroqApiClient*>("QTranscribe", "GroqApiClient");
    auto* stt = engine.singletonInstance<GroqSttClient*>("QTranscribe", "GroqSttClient");
    auto* whisperStt = engine.singletonInstance<WhisperSttClient*>("QTranscribe", "WhisperSttClient");
    auto* whisperModels = engine.singletonInstance<WhisperModelManager*>("QTranscribe", "WhisperModelManager");
    auto* llm = engine.singletonInstance<GroqLlmClient*>("QTranscribe", "GroqLlmClient");
    auto* tracker = engine.singletonInstance<GroqUsageTracker*>("QTranscribe", "GroqUsageTracker");
    auto* recorder = engine.singletonInstance<AudioRecorder*>("QTranscribe", "AudioRecorder");
    auto* shortcut = engine.singletonInstance<GlobalShortcutManager*>("QTranscribe", "GlobalShortcutManager");
    auto* injector = engine.singletonInstance<TextInjectorClient*>("QTranscribe", "TextInjectorClient");
    auto* history = engine.singletonInstance<TranscriptionModel*>("QTranscribe", "TranscriptionModel");
    auto* pipeline = new TranscriptionPipeline(&app);
    auto* controller = engine.singletonInstance<SpeechController*>("QTranscribe", "SpeechController");

    if (stt && api)
        stt->setApiClient(api);
    if (whisperStt && whisperModels)
        whisperStt->setModelManager(whisperModels);
    if (llm && api)
        llm->setApiClient(api);
    if (tracker && api)
        tracker->setApiClient(api);

    if (pipeline) {
        pipeline->setAudioRecorder(recorder);
        pipeline->registerBackend(TranscriptionPipeline::Backend::Groq, stt);
        pipeline->registerBackend(TranscriptionPipeline::Backend::WhisperCpp, whisperStt);
        pipeline->setLlmClient(llm);
    }

    if (controller) {
        if (pipeline) {
            controller->setPipeline(pipeline);
        }
        controller->setApiClient(api);
        controller->setShortcutManager(shortcut);
        controller->setTextInjector(injector);
        controller->setHistoryModel(history);
        controller->initialize();
        QObject::connect(controller, &SpeechController::requestQuitApp, []() { QCoreApplication::exit(0); });

        auto* dbus = new DBusService(&app);
        dbus->registerController(controller);

        auto* sni = new StatusNotifierService(&app);
        sni->registerController(controller);
    }

    engine.loadFromModule("QTranscribe", "Main");

    if (parser.isSet(toggleOption) || parser.isSet(startOption)) {
        QTimer::singleShot(200ms, [controller]() {
            if (controller) {
                controller->startRecording();
            }
        });
    }

    if (parser.isSet(showOption)) {
        QTimer::singleShot(100ms, [controller]() {
            if (controller) {
                controller->showWindow();
            }
        });
    }

    return app.exec();
}
