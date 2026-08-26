#include "GroqApiClient.h"
#include "GroqSttClient.h"

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

class TestGroqApiClient : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(u"QTranscribeTest"_s);
        QCoreApplication::setApplicationName(u"QTranscribeTest"_s);
    }

    void cleanup() {
        QSettings settings;
        settings.remove(u"GroqTest/ApiKey"_s);
        settings.sync();
    }

    void testDeferredLoadingOnStartup() {
        QSettings settings;
        settings.setValue(u"GroqTest/ApiKey"_s, u"gsk_test_deferred_key"_s);
        settings.sync();

        GroqApiClient client;
        client.setStorageKeys(u"QTranscribeTestService"_s, u"test_groq_api_key"_s, u"GroqTest/ApiKey"_s);

        // Verify key is NOT loaded upon instantiation
        QCOMPARE(client.apiKey(), QString());
        QCOMPARE(client.apiKeySet(), false);

        QSignalSpy spyKeyChanged(&client, &GroqApiClient::apiKeyChanged);
        QSignalSpy spyKeySetChanged(&client, &GroqApiClient::apiKeySetChanged);

        client.ensureApiKeyLoaded();

        // Wait for async keychain query / settings fallback
        QTRY_COMPARE(client.apiKey(), u"gsk_test_deferred_key"_s);
        QCOMPARE(client.apiKeySet(), true);
        QVERIFY(spyKeyChanged.count() > 0);
        QVERIFY(spyKeySetChanged.count() > 0);

        // Ensure idempotency: calling ensureApiKeyLoaded again shouldn't reload or duplicate
        const int prevKeyChangedCount = spyKeyChanged.count();
        client.ensureApiKeyLoaded();
        QCOMPARE(spyKeyChanged.count(), prevKeyChangedCount);
    }

    void testSetApiKeyUpdatesAuthAndMemory() {
        GroqApiClient client;
        client.setStorageKeys(u"QTranscribeTestService"_s, u"test_groq_api_key"_s, u"GroqTest/ApiKey"_s);
        QCOMPARE(client.apiKeySet(), false);

        QSignalSpy spyKeyChanged(&client, &GroqApiClient::apiKeyChanged);
        QSignalSpy spyKeySetChanged(&client, &GroqApiClient::apiKeySetChanged);

        client.setApiKey(u"  gsk_new_active_key_12345  "_s);

        QCOMPARE(client.apiKey(), u"gsk_new_active_key_12345"_s);
        QCOMPARE(client.apiKeySet(), true);
        QCOMPARE(spyKeyChanged.count(), 1);
        QCOMPARE(spyKeySetChanged.count(), 1);

        QNetworkRequest req = client.createApiRequest(u"models"_s);
        QCOMPARE(req.header(QNetworkRequest::KnownHeaders::UserAgentHeader).toString(), GroqApiClient::userAgent());
    }

    void testClearApiKeyDeletesFallbackSettings() {
        QSettings settings;
        settings.setValue(u"GroqTest/ApiKey"_s, u"gsk_temp_key_to_delete"_s);
        settings.sync();

        GroqApiClient client;
        client.setStorageKeys(u"QTranscribeTestService"_s, u"test_groq_api_key"_s, u"GroqTest/ApiKey"_s);
        client.ensureApiKeyLoaded();
        QTRY_COMPARE(client.apiKey(), u"gsk_temp_key_to_delete"_s);

        client.setApiKey(QString());

        QCOMPARE(client.apiKey(), QString());
        QCOMPARE(client.apiKeySet(), false);

        QVERIFY(settings.value(u"GroqTest/ApiKey"_s).toString().isEmpty());
    }

    void testGroqSttClientActivateTriggersLazyLoad() {
        QSettings settings;
        settings.setValue(u"GroqTest/ApiKey"_s, u"gsk_stt_integration_key"_s);
        settings.sync();

        GroqApiClient api;
        api.setStorageKeys(u"QTranscribeTestService"_s, u"test_groq_api_key"_s, u"GroqTest/ApiKey"_s);
        GroqSttClient stt;

        QCOMPARE(api.apiKeySet(), false);
        QCOMPARE(stt.isReady(), false);

        stt.setApiClient(&api);

        // Setting api client should not load key yet
        QCOMPARE(api.apiKeySet(), false);
        QCOMPARE(stt.isReady(), false);

        // Activating GroqSttClient triggers lazy load
        stt.activate();

        QTRY_VERIFY(stt.isReady());
        QCOMPARE(api.apiKey(), u"gsk_stt_integration_key"_s);
    }
};

QTEST_GUILESS_MAIN(TestGroqApiClient)
#include "test_groq_api_client.moc"
