#pragma once

#include "GroqResponseParser.h"

#include <QHttpMultiPart>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkRequestFactory>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QUrl>
#include <qt6keychain/keychain.h>

#include <chrono>
#include <functional>

class GroqApiClient : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY apiKeyChanged FINAL)
    Q_PROPERTY(bool apiKeySet READ apiKeySet NOTIFY apiKeySetChanged FINAL)

public:
    using ResponseCallback = std::function<void(const GroqApiResponse&)>;

    explicit GroqApiClient(QObject* parent = nullptr);
    ~GroqApiClient() override = default;

    QString apiKey() const;
    void setApiKey(const QString& key);
    bool apiKeySet() const;

    Q_INVOKABLE void loadApiKey();
    void ensureApiKeyLoaded();

    void setStorageKeys(const QString& keychainService, const QString& keychainKey,
                        const QString& settingsKey = QString());

    QNetworkAccessManager* networkAccessManager();
    const QNetworkRequestFactory& requestFactory() const;

    QNetworkRequest createApiRequest(const QString& relativePath, const QString& contentType = QString()) const;

    QNetworkReply* postJson(const QString& relativePath, const QJsonObject& body, const QString& endpointLabel,
                            const QString& modelName, ResponseCallback callback);
    QNetworkReply* postMultipart(const QString& relativePath, QHttpMultiPart* multiPart, const QString& endpointLabel,
                                 const QString& modelName, ResponseCallback callback);

    static QString userAgent();

    static constexpr auto kDefaultTransferTimeout = std::chrono::seconds(15);
    inline static constexpr QStringView kApiBaseUrl = u"https://api.groq.com/openai/v1";

signals:
    void apiKeyChanged();
    void apiKeySetChanged();
    void responseProcessed(QNetworkReply* reply, const QString& endpointLabel, const QString& modelName,
                           qint64 latencyMs, const QByteArray& responseBody);

private:
    void updateFactoryAuth();
    void loadApiKeyFromKeychain();
    void loadApiKeyFromSettingsFallback();
    void saveApiKeyToKeychain(const QString& key);
    void deleteApiKeyFromKeychain();

    QNetworkAccessManager* m_nam = nullptr;
    QNetworkRequestFactory m_requestFactory;
    QString m_apiKey;
    bool m_apiKeyLoaded = false;
    bool m_isLoadingApiKey = false;

    inline static constexpr QStringView kKeychainService = u"QTranscribe";
    inline static constexpr QStringView kKeychainKey = u"groq_api_key";
    inline static constexpr QStringView kSettingsApiKey = u"Groq/ApiKey";

    QString m_keychainService = kKeychainService.toString();
    QString m_keychainKey = kKeychainKey.toString();
    QString m_settingsKey = kSettingsApiKey.toString();
};
