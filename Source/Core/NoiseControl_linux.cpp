#include "NoiseControl.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>

#include "../Logger.h"

namespace Core::NoiseControl {
namespace {
using Clock = std::chrono::steady_clock;

class LinuxBackend final : public Backend
{
public:
    void Run(uint64_t address, std::shared_ptr<Session> session,
             std::function<void(std::optional<Mode>, QString, std::optional<Request>)> report) override
    {
        LOG(Trace, "Noise control: opening AAP connection");
        const int fd = socket(AF_BLUETOOTH, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
                              BTPROTO_L2CAP);
        if (fd < 0) {
            const int error = errno;
            LOG(Trace, "Noise control: AAP socket creation failed");
            report({}, QString::fromLocal8Bit(strerror(error)), {}); return;
        }
        struct Close { int fd; ~Close() { close(fd); } } closeFd{fd};
        auto fail = [&](const QString &message) { if (!session->stopped) report({}, message, {}); };
        auto wait = [&](short events, Clock::time_point deadline) {
            while (!session->stopped && Clock::now() < deadline) {
                pollfd p{fd, events, 0};
                const int result = poll(&p, 1, 100);
                if (result < 0 && errno == EINTR) continue;
                if (result < 0 || (result > 0 && (p.revents & (POLLERR | POLLHUP | POLLNVAL)))) return false;
                if (result > 0 && (p.revents & events)) return true;
            }
            return false;
        };
        sockaddr_l2 target{};
        target.l2_family = AF_BLUETOOTH;
        target.l2_psm = htobs(0x1001);
        target.l2_bdaddr_type = BDADDR_BREDR;
        for (int i = 0; i < 6; ++i) target.l2_bdaddr.b[i] = (address >> (8 * i)) & 0xff;
        if (connect(fd, reinterpret_cast<sockaddr *>(&target), sizeof(target)) < 0) {
            if (errno != EINPROGRESS) {
                const int error = errno;
                LOG(Trace, "Noise control: AAP connect failed immediately");
                fail(QString::fromLocal8Bit(strerror(error))); return;
            }
            if (!wait(POLLOUT, Clock::now() + std::chrono::seconds{5})) {
                LOG(Trace, "Noise control: AAP connect wait failed, timed out, or was cancelled");
                fail(QStringLiteral("AAP connection failed or timed out")); return;
            }
            int error = 0; socklen_t length = sizeof(error);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error) {
                const int failure = error ? error : errno;
                LOG(Trace, "Noise control: AAP connect completion failed");
                fail(QString::fromLocal8Bit(strerror(failure))); return;
            }
        }
        LOG(Trace, "Noise control: AAP connection established");
        auto sendPacket = [&](const uint8_t *data, size_t size) {
            if (!wait(POLLOUT, Clock::now() + std::chrono::seconds{2})) return false;
            return send(fd, data, size, MSG_NOSIGNAL) == static_cast<ssize_t>(size);
        };
        // Existing implementations disagree on acknowledgement/framing. This sequence follows
        // olegoonline's observed packets; successful writes are not treated as a mode report.
        static constexpr uint8_t handshake[]{0,0,4,0,1,0,2,0,0,0,0,0,0,0,0,0};
        static constexpr uint8_t features[]{4,0,4,0,0x4d,0,0xd7,0,0,0,0,0,0,0};
        static constexpr uint8_t subscribe[]{4,0,4,0,0x0f,0,0xff,0xff,0xff,0xff,0xff};
        auto pause = [&] { poll(nullptr, 0, 50); return !session->stopped; };
        if (!sendPacket(handshake, sizeof(handshake)) || !pause() ||
            !sendPacket(features, sizeof(features)) || !pause() ||
            !sendPacket(subscribe, sizeof(subscribe))) {
            LOG(Trace, "Noise control: AAP initialization failed or was cancelled");
            fail(QStringLiteral("AAP initialization failed")); return;
        }
        if (session->stopped) return;
        LOG(Trace, "Noise control: AAP initialization complete");
        report({}, {}, {});
        Confirmation confirmation;
        const CycleIO io{
            .receive = [&](std::vector<uint8_t> &packet) {
                std::array<uint8_t, 1024> buffer{};
                iovec vector{buffer.data(), buffer.size()};
                msghdr message{};
                message.msg_iov = &vector;
                message.msg_iovlen = 1;
                ssize_t size;
                do { size = recvmsg(fd, &message, MSG_DONTWAIT); }
                while (size < 0 && errno == EINTR && !session->stopped);
                if (size < 0) {
                    return errno == EAGAIN || errno == EWOULDBLOCK ? ReceiveResult::Empty
                                                                    : ReceiveResult::Error;
                }
                if (size == 0) return ReceiveResult::Closed;
                if (message.msg_flags & MSG_TRUNC) return ReceiveResult::Truncated;
                packet.assign(buffer.begin(), buffer.begin() + size);
                return ReceiveResult::Packet;
            },
            .onMode = [&](Mode mode) { report(mode, {}, {}); },
            .onTimeout = [&](Request expired) { report({}, {}, expired); },
            .waitWritable = [&] { return wait(POLLOUT, Clock::now() + std::chrono::seconds{2}); },
            .send = [&](Request request) {
                auto bytes = Command(request.mode);
                return send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL) ==
                       static_cast<ssize_t>(bytes.size());
            },
            .cancelled = [&] { return session->stopped.load(); },
            .now = [] { return Clock::now(); },
        };
        while (!session->stopped) {
            std::optional<Request> command;
            { std::lock_guard lock{session->mutex}; command = session->command; session->command.reset(); }
            const auto cycle = RunCycle(confirmation, command, io);
            if (cycle != CycleResult::Ok) {
                LOG(Trace, "Noise control: AAP receive cycle ended with result {}",
                    static_cast<unsigned>(cycle));
                if (cycle != CycleResult::Cancelled) fail(QStringLiteral("AAP receive or write failed"));
                return;
            }
            pollfd p{fd, POLLIN, 0};
            int result = poll(&p, 1, 100);
            if (result < 0 && errno == EINTR) continue;
            // Drain readable packets even when hangup is also signalled.
            if (result > 0 && (p.revents & POLLIN)) continue;
            if (result < 0 || (result > 0 && (p.revents & (POLLERR | POLLHUP | POLLNVAL)))) {
                LOG(Trace, "Noise control: AAP connection closed or poll failed");
                fail(QStringLiteral("AAP connection closed")); return;
            }
        }
    }
};
} // namespace
std::shared_ptr<Backend> CreateLinuxBackend() { return std::make_shared<LinuxBackend>(); }
} // namespace Core::NoiseControl
