#include "keyinjectord/device_interface.h"
#include "keyinjectord/ipc_server.h"
#include "keyinjectord/launcher_auth.h"
#include "keyinjectord/protocol.h"

#include <QCoreApplication>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTest>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std::chrono_literals;

class MockDevice : public keyinjectord::IDevice {
public:
    bool sendCtrlV() override {
        ++ctrlVCalledCount;
        return true;
    }

    std::atomic<int> ctrlVCalledCount {0};
};

struct ServerRunner {
    keyinjectord::IpcServer& server;
    std::thread thread;

    explicit ServerRunner(keyinjectord::IpcServer& s)
        : server(s)
        , thread([&s]() { s.run(); }) { }

    ~ServerRunner() {
        server.stop();
        if (thread.joinable()) {
            thread.join();
        }
    }
};

class TestIpcServer : public QObject {
    Q_OBJECT

private slots:
    void testValidPasteCommand() {
        int sv[2];
        QCOMPARE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv), 0);

        MockDevice mockDevice;
        keyinjectord::IpcServer server(sv[0], mockDevice);
        ServerRunner runner(server);

        QLocalSocket client;
        QVERIFY(client.setSocketDescriptor(sv[1], QLocalSocket::ConnectedState, QIODevice::ReadWrite));

        const char cmdByte = static_cast<char>(keyinjectord::Opcode::Paste);
        client.write(&cmdByte, 1);
        client.flush();

        QVERIFY(client.waitForReadyRead(2000));
        QByteArray response = client.readAll();
        QCOMPARE(response.size(), 1);
        QCOMPARE(static_cast<uint8_t>(response[0]), static_cast<uint8_t>(keyinjectord::ResponseStatus::Ok));

        QCOMPARE(mockDevice.ctrlVCalledCount.load(), 1);

        client.close();
    }

    void testPingCommand() {
        int sv[2];
        QCOMPARE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv), 0);

        MockDevice mockDevice;
        keyinjectord::IpcServer server(sv[0], mockDevice);
        ServerRunner runner(server);

        QLocalSocket client;
        QVERIFY(client.setSocketDescriptor(sv[1], QLocalSocket::ConnectedState, QIODevice::ReadWrite));

        const char cmdByte = static_cast<char>(keyinjectord::Opcode::Ping);
        client.write(&cmdByte, 1);
        client.flush();

        QVERIFY(client.waitForReadyRead(2000));
        QByteArray response = client.readAll();
        QCOMPARE(response.size(), 1);
        QCOMPARE(static_cast<uint8_t>(response[0]), static_cast<uint8_t>(keyinjectord::ResponseStatus::Ok));

        QCOMPARE(mockDevice.ctrlVCalledCount.load(), 0);

        client.close();
    }

    void testDeviceErrorResponse() {
        int sv[2];
        QCOMPARE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv), 0);

        class FailingMockDevice : public keyinjectord::IDevice {
        public:
            bool sendCtrlV() override { return false; }
        } failingDevice;

        keyinjectord::IpcServer server(sv[0], failingDevice);
        ServerRunner runner(server);

        QLocalSocket client;
        QVERIFY(client.setSocketDescriptor(sv[1], QLocalSocket::ConnectedState, QIODevice::ReadWrite));

        const char cmdByte = static_cast<char>(keyinjectord::Opcode::Paste);
        client.write(&cmdByte, 1);
        client.flush();

        QVERIFY(client.waitForReadyRead(2000));
        QByteArray response = client.readAll();
        QCOMPARE(response.size(), 1);
        QCOMPARE(static_cast<uint8_t>(response[0]), static_cast<uint8_t>(keyinjectord::ResponseStatus::DeviceError));

        client.close();
    }

    void testUnknownOpcodeDisconnect() {
        int sv[2];
        QCOMPARE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv), 0);

        MockDevice mockDevice;
        keyinjectord::IpcServer server(sv[0], mockDevice);
        ServerRunner runner(server);

        QLocalSocket client;
        QVERIFY(client.setSocketDescriptor(sv[1], QLocalSocket::ConnectedState, QIODevice::ReadWrite));

        const char invalidCmd = static_cast<char>(0xFF);
        client.write(&invalidCmd, 1);
        client.flush();

        QVERIFY(client.waitForReadyRead(2000));
        QByteArray response = client.readAll();
        QCOMPARE(response.size(), 1);
        QCOMPARE(static_cast<uint8_t>(response[0]), static_cast<uint8_t>(keyinjectord::ResponseStatus::UnknownCmd));

        QVERIFY(client.waitForDisconnected(2000) || client.state() == QLocalSocket::UnconnectedState);
        QCOMPARE(mockDevice.ctrlVCalledCount.load(), 0);
    }

    void testPeerDisconnectShutsDownServer() {
        int sv[2];
        QCOMPARE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv), 0);

        MockDevice mockDevice;
        keyinjectord::IpcServer server(sv[0], mockDevice);

        std::thread serverThread([&server]() { server.run(); });

        ::close(sv[1]);

        serverThread.join();
        QVERIFY(true);
    }

    void testInvalidDescriptorThrows() {
        MockDevice mockDevice;
        QVERIFY_EXCEPTION_THROWN(keyinjectord::IpcServer server(-1, mockDevice), std::invalid_argument);
    }

    void testAuthorizeLauncherSuccess() {
        int sv[2];
        QCOMPARE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv), 0);

        keyinjectord::AuthResult res = keyinjectord::AuthResult::InvalidFd;
        bool ok = keyinjectord::authorizeLauncher(sv[0], &res);
        ::close(sv[0]);
        ::close(sv[1]);

        QCOMPARE(res, keyinjectord::AuthResult::Success);
        QVERIFY(ok);
    }

    void testAuthorizeLauncherInvalidFd() {
        keyinjectord::AuthResult res = keyinjectord::AuthResult::Success;
        bool ok = keyinjectord::authorizeLauncher(-1, &res);
        QCOMPARE(res, keyinjectord::AuthResult::InvalidFd);
        QVERIFY(!ok);
    }

    void testAuthorizeLauncherNonSocketFd() {
        keyinjectord::AuthResult res = keyinjectord::AuthResult::Success;
        bool ok = keyinjectord::authorizeLauncher(1, &res);
        QCOMPARE(res, keyinjectord::AuthResult::NotASocket);
        QVERIFY(!ok);
    }

    std::filesystem::path currentExePath() const {
        std::array<char, PATH_MAX> selfExeBuf{};
        ssize_t len = readlink("/proc/self/exe", selfExeBuf.data(), selfExeBuf.size() - 1);
        if (len <= 0) return {};
        selfExeBuf[len] = '\0';
        return std::filesystem::path(selfExeBuf.data());
    }

    void testValidateExecutableTopologyRejectsUntrustedPrefixes() {
        const std::filesystem::path selfPath = currentExePath();
        QVERIFY(!selfPath.empty());

        keyinjectord::AuthResult res = keyinjectord::AuthResult::Success;

        QVERIFY(!keyinjectord::validateExecutableTopology("/tmp/qtranscribe", selfPath, &res));
        QCOMPARE(res, keyinjectord::AuthResult::UntrustedLocation);

        QVERIFY(!keyinjectord::validateExecutableTopology("/var/tmp/qtranscribe", selfPath, &res));
        QCOMPARE(res, keyinjectord::AuthResult::UntrustedLocation);

        QVERIFY(!keyinjectord::validateExecutableTopology("/dev/shm/qtranscribe", selfPath, &res));
        QCOMPARE(res, keyinjectord::AuthResult::UntrustedLocation);

        QVERIFY(!keyinjectord::validateExecutableTopology("/run/user/1000/qtranscribe", selfPath, &res));
        QCOMPARE(res, keyinjectord::AuthResult::UntrustedLocation);
    }

    void testValidateExecutableTopologyRejectsDeletedExecutable() {
        const std::filesystem::path selfPath = currentExePath();
        QVERIFY(!selfPath.empty());

        keyinjectord::AuthResult res = keyinjectord::AuthResult::Success;
        std::filesystem::path deletedParent = "/usr/bin/qtranscribe (deleted)";
        QVERIFY(!keyinjectord::validateExecutableTopology(deletedParent, selfPath, &res));
        QCOMPARE(res, keyinjectord::AuthResult::DeletedExecutable);

        std::filesystem::path deletedSelf = selfPath.string() + " (deleted)";
        QVERIFY(!keyinjectord::validateExecutableTopology(selfPath, deletedSelf, &res));
        QCOMPARE(res, keyinjectord::AuthResult::DeletedExecutable);
    }

    void testValidateExecutableTopologyRejectsUnauthorizedName() {
        const std::filesystem::path selfPath = currentExePath();
        QVERIFY(!selfPath.empty());

        keyinjectord::AuthResult res = keyinjectord::AuthResult::Success;
        std::filesystem::path badName = selfPath.parent_path() / "unauthorized_binary";
        QVERIFY(!keyinjectord::validateExecutableTopology(badName, selfPath, &res));
        QCOMPARE(res, keyinjectord::AuthResult::UnauthorizedExecutable);
    }

    void testValidateExecutableTopologyAcceptsSelfInvocation() {
        const std::filesystem::path selfPath = currentExePath();
        QVERIFY(!selfPath.empty());

        keyinjectord::AuthResult res = keyinjectord::AuthResult::InvalidFd;
        QVERIFY(keyinjectord::validateExecutableTopology(selfPath, selfPath, &res));
        QCOMPARE(res, keyinjectord::AuthResult::Success);
    }

    void testValidateExecutableTopologyRejectsNonColocated() {
        const std::filesystem::path selfPath = currentExePath();
        QVERIFY(!selfPath.empty());

        std::filesystem::path nestedDir = selfPath.parent_path() / "test_nested_dir";
        std::filesystem::create_directories(nestedDir);
        std::filesystem::path nestedBinary = nestedDir / "qtranscribe";

        FILE* f = fopen(nestedBinary.c_str(), "w");
        QVERIFY(f != nullptr);
        fclose(f);

        keyinjectord::AuthResult res = keyinjectord::AuthResult::Success;

        bool ok = keyinjectord::validateExecutableTopology(nestedBinary, selfPath, &res);
        QVERIFY(!ok);
        QCOMPARE(res, keyinjectord::AuthResult::UntrustedLocation);

        std::filesystem::remove(nestedBinary);
        std::filesystem::remove(nestedDir);
    }

    void testValidateExecutableTopologyRejectsWorldWritable() {
        const std::filesystem::path selfPath = currentExePath();
        QVERIFY(!selfPath.empty());

        std::filesystem::path testDir = selfPath.parent_path() / "test_isolated_world_writable_dir";
        std::filesystem::create_directories(testDir);

        std::filesystem::path mockSelf = testDir / "test_ipc_server";
        FILE* fSelf = fopen(mockSelf.c_str(), "w");
        QVERIFY(fSelf != nullptr);
        fclose(fSelf);

        std::filesystem::path mockParent = testDir / "qtranscribe";
        FILE* fParent = fopen(mockParent.c_str(), "w");
        QVERIFY(fParent != nullptr);
        fclose(fParent);

        QCOMPARE(::chmod(mockParent.c_str(), 0777), 0);

        keyinjectord::AuthResult res = keyinjectord::AuthResult::Success;
        bool ok = keyinjectord::validateExecutableTopology(mockParent, mockSelf, &res);
        QVERIFY(!ok);
        QCOMPARE(res, keyinjectord::AuthResult::WorldWritable);

        std::error_code ec;
        std::filesystem::remove_all(testDir, ec);
    }

    void testAuthResultToString() {
        QCOMPARE(QString(keyinjectord::authResultToString(keyinjectord::AuthResult::Success)), QString("Success"));
        QCOMPARE(QString(keyinjectord::authResultToString(keyinjectord::AuthResult::WorldWritable)),
                 QString("Parent executable or directory is world-writable"));
        QCOMPARE(QString(keyinjectord::authResultToString(keyinjectord::AuthResult::GroupWritable)),
                 QString("Parent executable or directory is group-writable"));
        QCOMPARE(QString(keyinjectord::authResultToString(keyinjectord::AuthResult::NonRootOwner)),
                 QString("Production helper executable, parent binary, or directory is not owned by root (UID 0)"));
    }
};

QTEST_GUILESS_MAIN(TestIpcServer)
#include "test_ipc_server.moc"
