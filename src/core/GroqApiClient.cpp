#include "GroqApiClient.h"

#include "LoggingCategories.h"

#include "ApiKeyStore.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHttpHeaders>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxyFactory>
#include <QSslConfiguration>
#include <QSslSocket>

#include <memory>

using namespace Qt::StringLiterals;

GroqApiClient::GroqApiClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this)) {
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyPeer);
    sslConfig.setAllowedNextProtocols({QSslConfiguration::ALPNProtocolHTTP2});
    QSslConfiguration::setDefaultConfiguration(sslConfig);

    connect(m_nam, &QNetworkAccessManager::sslErrors, this,
            [](QNetworkReply* /*reply*/, const QList<QSslError>& errors) {
                for (const auto& err : errors) {
                    qWarning() << "GroqApiClient SSL Error:" << err.errorString();
                }
            });

    m_nam->setTransferTimeout(kDefaultTransferTimeout);

    m_requestFactory.setBaseUrl(QUrl(kApiBaseUrl.toString()));
    QHttpHeaders headers;
    headers.append(QHttpHeaders::WellKnownHeader::UserAgent, userAgent());
    m_requestFactory.setCommonHeaders(headers);

    auto* defaultStore = new ApiKeyStore(this);
    setKeyStore(defaultStore);
    m_ownsKeyStore = true;
}

#ifndef QTRANSCRIBE_VERSION
#define QTRANSCRIBE_VERSION "0.0.0-dev"
#endif

QString GroqApiClient::userAgent() {
    const QString appVer = QCoreApplication::applicationVersion().isEmpty() ? u"" QTRANSCRIBE_VERSION ""_s
                                                                            : QCoreApplication::applicationVersion();
    return u"QTranscribe/%1 (Linux; Qt %2)"_s.arg(appVer, QString::fromUtf8(QT_VERSION_STR));
}

void GroqApiClient::setKeyStore(ApiKeyStore* keyStore) {
    if (m_keyStore == keyStore) {
        return;
    }

    if (m_keyStore) {
        disconnect(m_keyStore, &ApiKeyStore::apiKeyChanged, this, nullptr);
        disconnect(m_keyStore, &ApiKeyStore::apiKeySetChanged, this, nullptr);
        if (m_ownsKeyStore && m_keyStore->parent() == this) {
            m_keyStore->deleteLater();
        }
    }

    m_keyStore = keyStore;
    m_ownsKeyStore = false;

    if (m_keyStore) {
        connect(m_keyStore, &ApiKeyStore::apiKeyChanged, this, [this]() {
            updateFactoryAuth();
            emit apiKeyChanged();
        });
        connect(m_keyStore, &ApiKeyStore::apiKeySetChanged, this, &GroqApiClient::apiKeySetChanged);
        updateFactoryAuth();
    }
}

ApiKeyStore* GroqApiClient::keyStore() const {
    return m_keyStore;
}

QString GroqApiClient::apiKey() const {
    return m_keyStore ? m_keyStore->apiKey() : QString();
}

void GroqApiClient::setApiKey(const QString& key) {
    if (m_keyStore) {
        m_keyStore->setApiKey(key);
    }
}

bool GroqApiClient::apiKeySet() const {
    return m_keyStore ? m_keyStore->apiKeySet() : false;
}

void GroqApiClient::loadApiKey() {
    if (m_keyStore) {
        m_keyStore->loadApiKey();
    }
}

void GroqApiClient::ensureApiKeyLoaded() {
    if (m_keyStore) {
        m_keyStore->ensureApiKeyLoaded();
    }
}

void GroqApiClient::setStorageKeys(const QString& keychainService, const QString& keychainKey,
                                   const QString& settingsKey) {
    if (m_keyStore) {
        m_keyStore->setStorageKeys(keychainService, keychainKey, settingsKey);
    }
}

void GroqApiClient::updateFactoryAuth() {
    const QString key = apiKey();
    if (!key.isEmpty()) {
        m_requestFactory.setBearerToken(key.toUtf8());
    } else {
        m_requestFactory.setBearerToken(QByteArray());
    }
}

QNetworkAccessManager* GroqApiClient::networkAccessManager() {
    return m_nam;
}

const QNetworkRequestFactory& GroqApiClient::requestFactory() const {
    return m_requestFactory;
}

QNetworkRequest GroqApiClient::createApiRequest(const QString& relativePath, const QString& contentType) const {
    QNetworkRequest request = m_requestFactory.createRequest(relativePath);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
    request.setTransferTimeout(kDefaultTransferTimeout);
    if (!contentType.isEmpty()) {
        QHttpHeaders headers = request.headers();
        headers.append(QHttpHeaders::WellKnownHeader::ContentType, contentType);
        request.setHeaders(headers);
    }
    return request;
}

QNetworkReply* GroqApiClient::postJson(const QString& relativePath, const QJsonObject& body,
                                       const QString& endpointLabel, const QString& modelName,
                                       ResponseCallback callback) {
    QNetworkRequest request = createApiRequest(relativePath, u"application/json"_s);
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = m_nam->post(request, payload);

    connect(reply, &QNetworkReply::finished, this, [this, reply, timer, endpointLabel, modelName, callback]() {
        reply->deleteLater();
        const qint64 elapsedMs = timer->elapsed();
        const GroqApiResponse response = GroqResponseParser::parseReply(reply, elapsedMs);

        emit responseProcessed(reply, endpointLabel, modelName, elapsedMs, response.rawBody);

        if (callback) {
            callback(response);
        }
    });

    return reply;
}

QNetworkReply* GroqApiClient::postMultipart(const QString& relativePath, QHttpMultiPart* multiPart,
                                            const QString& endpointLabel, const QString& modelName,
                                            ResponseCallback callback) {
    QNetworkRequest request = createApiRequest(relativePath);
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();

    QNetworkReply* reply = m_nam->post(request, multiPart);
    if (multiPart) {
        multiPart->setParent(reply);
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, timer, endpointLabel, modelName, callback]() {
        reply->deleteLater();
        const qint64 elapsedMs = timer->elapsed();
        const GroqApiResponse response = GroqResponseParser::parseReply(reply, elapsedMs);

        emit responseProcessed(reply, endpointLabel, modelName, elapsedMs, response.rawBody);

        if (callback) {
            callback(response);
        }
    });

    return reply;
}

QNetworkReply* GroqApiClient::ping(ResponseCallback callback) {
    QJsonObject rootObj;
    rootObj[u"model"_s] = u"openai/gpt-oss-20b"_s;
    QJsonArray messages;
    QJsonObject userMsg;
    userMsg[u"role"_s] = u"user"_s;
    userMsg[u"content"_s] = u"ping"_s;
    messages.append(userMsg);
    rootObj[u"messages"_s] = messages;
    rootObj[u"max_completion_tokens"_s] = 1;

    return postJson(u"chat/completions"_s, rootObj, u"Quota Check"_s, u"openai/gpt-oss-20b"_s, std::move(callback));
}
