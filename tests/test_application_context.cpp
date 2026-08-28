#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include "ApplicationContext.h"
#include "AudioRecorder.h"
#include "DictationCoordinator.h"
#include "GlobalShortcutManager.h"
#include "GroqApiClient.h"
#include "GroqLlmClient.h"
#include "GroqSttClient.h"
#include "GroqUsageTracker.h"
#include "TextInjectorClient.h"
#include "TranscriptionModel.h"
#include "WhisperModelManager.h"
#include "WhisperSttClient.h"

using namespace Qt::StringLiterals;

class TestApplicationContext : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testHeadlessInitialization();
    void testSubsystemWiringIntegrity();
    void testRepeatedInitializeIsNoOp();
};

void TestApplicationContext::initTestCase() {
    QCoreApplication::setOrganizationName(u"QTranscribeTestOrg"_s);
    QCoreApplication::setApplicationName(u"QTranscribeTestApp"_s);
}

void TestApplicationContext::testHeadlessInitialization() {
    ApplicationContext context;
    QVERIFY(!context.isInitialized());
    QVERIFY(context.dictationCoordinator() == nullptr);

    context.initializeHeadless();
    QVERIFY(context.isInitialized());

    QVERIFY(context.dictationCoordinator() != nullptr);
    QVERIFY(context.groqApiClient() != nullptr);
    QVERIFY(context.apiKeyStore() != nullptr);
    QVERIFY(context.groqSttClient() != nullptr);
    QVERIFY(context.whisperSttClient() != nullptr);
    QVERIFY(context.whisperModelManager() != nullptr);
    QVERIFY(context.groqLlmClient() != nullptr);
    QVERIFY(context.groqUsageTracker() != nullptr);
    QVERIFY(context.audioRecorder() != nullptr);
    QVERIFY(context.shortcutManager() != nullptr);
    QVERIFY(context.textInjector() != nullptr);
    QVERIFY(context.historyModel() != nullptr);
}

void TestApplicationContext::testSubsystemWiringIntegrity() {
    ApplicationContext context;
    context.initializeHeadless();

    QCOMPARE(context.groqSttClient()->apiClient(), context.groqApiClient());
    QCOMPARE(context.groqLlmClient()->apiClient(), context.groqApiClient());
    QCOMPARE(context.groqUsageTracker()->apiClient(), context.groqApiClient());
    QCOMPARE(context.whisperSttClient()->modelManager(), context.whisperModelManager());
    QVERIFY(context.dictationCoordinator() != nullptr);
}

void TestApplicationContext::testRepeatedInitializeIsNoOp() {
    ApplicationContext context;
    context.initializeHeadless();
    auto* initialCoordinator = context.dictationCoordinator();

    context.initializeHeadless();
    QCOMPARE(context.dictationCoordinator(), initialCoordinator);
}

QTEST_MAIN(TestApplicationContext)
#include "test_application_context.moc"
