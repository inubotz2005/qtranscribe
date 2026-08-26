#include "GroqApiClient.h"

#include "LoggingCategories.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHttpHeaders>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxyFactory>
#include <QSettings>
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
}

#ifndef QTRANSCRIBE_VERSION
#define QTRANSCRIBE_VERSION "0.0.0-dev"
#endif

QString GroqApiClient::userAgent() {
    const QString appVer = QCoreApplication::applicationVersion().isEmpty() ? u"" QTRANSCRIBE_VERSION ""_s
                                                                            : QCoreApplication::applicationVersion();
    return u"QTranscribe/%1 (Linux; Qt %2)"_s.arg(appVer, QString::fromUtf8(QT_VERSION_STR));
}

QString GroqApiClient::apiKey() const {
    return m_apiKey;
}

void GroqApiClient::setApiKey(const QString& key) {
    const QString trimmed = key.trimmed();
    if (m_apiKey != trimmed) {
        const bool wasSet = apiKeySet();
        m_apiKey = trimmed;
        m_apiKeyLoaded = true;
        updateFactoryAuth();
        emit apiKeyChanged();

        if (wasSet != apiKeySet()) {
            emit apiKeySetChanged();
        }

        if (!m_apiKey.isEmpty()) {
            saveApiKeyToKeychain(m_apiKey);
        } else {
            deleteApiKeyFromKeychain();
        }
    }
}

void GroqApiClient::loadApiKey() {
    if (m_isLoadingApiKey) {
        return;
    }
    loadApiKeyFromKeychain();
}

void GroqApiClient::ensureApiKeyLoaded() {
    if (m_apiKeyLoaded || m_isLoadingApiKey) {
        return;
    }
    loadApiKey();
}

void GroqApiClient::setStorageKeys(const QString& keychainService, const QString& keychainKey,
                                   const QString& settingsKey) {
    m_keychainService = keychainService;
    m_keychainKey = keychainKey;
    if (!settingsKey.isEmpty()) {
        m_settingsKey = settingsKey;
    }
}

void GroqApiClient::updateFactoryAuth() {
    if (!m_apiKey.isEmpty()) {
        m_requestFactory.setBearerToken(m_apiKey.toUtf8());
    } else {
        m_requestFactory.setBearerToken(QByteArray());
    }
}

bool GroqApiClient::apiKeySet() const {
    return !m_apiKey.isEmpty();
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

void GroqApiClient::loadApiKeyFromKeychain() {
    m_isLoadingApiKey = true;
    auto* readJob = new QKeychain::ReadPasswordJob(m_keychainService, this);
    readJob->setKey(m_keychainKey);
    readJob->setAutoDelete(true);

    connect(readJob, &QKeychain::ReadPasswordJob::finished, this, [this](QKeychain::Job* job) {
        m_isLoadingApiKey = false;
        m_apiKeyLoaded = true;
        auto* rJob = static_cast<QKeychain::ReadPasswordJob*>(job);
        if (!rJob->error()) {
            const QString key = rJob->textData().trimmed();
            if (!key.isEmpty()) {
                qCDebug(lcNetwork) << "Groq API key loaded successfully from system keychain (key length:" << key.size()
                                   << ")";
                m_apiKey = key;
                updateFactoryAuth();
                emit apiKeyChanged();
                emit apiKeySetChanged();
                return;
            }
        } else if (rJob->error() == QKeychain::EntryNotFound) {
            qCDebug(lcNetwork) << "No Groq API key found in system keychain, checking QSettings fallback";
        } else {
            qWarning("GroqApiClient: Failed to read API key from keychain (%s), checking QSettings fallback",
                     qPrintable(rJob->errorString()));
        }

        loadApiKeyFromSettingsFallback();
    });

    readJob->start();
}

void GroqApiClient::loadApiKeyFromSettingsFallback() {
    QSettings settings;
    const QString key = settings.value(m_settingsKey).toString().trimmed();
    if (!key.isEmpty()) {
        qCDebug(lcNetwork) << "Groq API key loaded from QSettings fallback (key length:" << key.size() << ")";
        m_apiKey = key;
        updateFactoryAuth();
        emit apiKeyChanged();
        emit apiKeySetChanged();
    } else {
        qCDebug(lcNetwork) << "No Groq API key found in QSettings fallback";
    }
}

void GroqApiClient::saveApiKeyToKeychain(const QString& key) {
    auto* writeJob = new QKeychain::WritePasswordJob(m_keychainService, this);
    writeJob->setKey(m_keychainKey);
    writeJob->setTextData(key);
    writeJob->setAutoDelete(true);

    connect(writeJob, &QKeychain::WritePasswordJob::finished, this, [this, key](QKeychain::Job* job) {
        if (job->error()) {
            qWarning("GroqApiClient: Failed to save API key to keychain (%s), saving to QSettings fallback",
                     qPrintable(job->errorString()));
            QSettings settings;
            settings.setValue(m_settingsKey, key);
        } else {
            qCDebug(lcNetwork) << "Groq API key persisted into system keychain";
            QSettings settings;
            settings.remove(m_settingsKey);
        }
    });

    writeJob->start();
}

void GroqApiClient::deleteApiKeyFromKeychain() {
    QSettings settings;
    settings.remove(m_settingsKey);

    auto* deleteJob = new QKeychain::DeletePasswordJob(m_keychainService, this);
    deleteJob->setKey(m_keychainKey);
    deleteJob->setAutoDelete(true);

    connect(deleteJob, &QKeychain::DeletePasswordJob::finished, this, [](QKeychain::Job* job) {
        if (job->error() && job->error() != QKeychain::EntryNotFound) {
            qWarning("GroqApiClient: Failed to delete API key from keychain: %s", qPrintable(job->errorString()));
        } else {
            qCDebug(lcNetwork) << "Groq API key deleted from system keychain";
        }
    });

    deleteJob->start();
}
