// Linux Apple Accessory Protocol noise control.
// Protocol details were consulted in olegoonline/airpods-pro-linux and
// superninjv/airpods-helper; see Docs/NoiseControl-Licenses.txt.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <vector>

#include <QObject>
#include <QString>
#include <QTimer>

#include "Base.h"

namespace Core::NoiseControl {

enum class Mode : uint8_t { Off = 1, Anc = 2, Transparency = 3, Adaptive = 4 };

bool Supports(AirPods::Model model, Mode mode);
std::vector<uint8_t> Command(Mode mode);
std::optional<Mode> ParseMode(const uint8_t *data, size_t size);

struct Request { Mode mode; uint64_t serial; };

// Tracks only confirmation of the latest sent request; reports remain authoritative even late.
class Confirmation
{
public:
    using Clock = std::chrono::steady_clock;
    void Sent(Request request, Clock::time_point now);
    void Observe(Mode mode, Clock::time_point now = Clock::now());
    std::optional<Request> Expire(Clock::time_point now);
private:
    std::optional<Request> _pending;
    Clock::time_point _deadline{};
};

// A bounded receive pump shared by the socket backend and hardware-free tests.
enum class ReceiveResult { Packet, Truncated, Empty, Closed, Error };
enum class CycleResult { Ok, Cancelled, Closed, ReadError, Flood, WriteError };
struct CycleIO {
    std::function<ReceiveResult(std::vector<uint8_t> &)> receive;
    std::function<void(Mode)> onMode;
    std::function<void(Request)> onTimeout;
    std::function<bool()> waitWritable;
    std::function<bool(Request)> send;
    std::function<bool()> cancelled;
    std::function<Confirmation::Clock::time_point()> now;
};
CycleResult RunCycle(Confirmation &confirmation, std::optional<Request> command, const CycleIO &io);

struct Session {
    std::mutex mutex;
    std::optional<Request> command;
    std::atomic_bool stopped{false};
};

class Backend
{
public:
    virtual ~Backend() = default;
    // Runs only on a worker. Must check session->stopped and return promptly.
    virtual void Run(uint64_t address, std::shared_ptr<Session> session,
                     std::function<void(std::optional<Mode>, QString, std::optional<Request>)> report) = 0;
};

std::shared_ptr<Backend> CreateLinuxBackend();

class Controller : public QObject
{
    Q_OBJECT
public:
    // Errors are cleared from the presentation immediately but their notification is held for
    // disconnectGraceMs, because the AAP socket usually notices a bound-device disconnect before
    // BlueZ reports it. If the device disconnects (or the binding changes) inside that window the
    // notification is dropped; otherwise it surfaces once as before.
    explicit Controller(std::shared_ptr<Backend> backend, QObject *parent = nullptr,
                        int retryBaseMs = 1000, int disconnectGraceMs = 2000);
    ~Controller() override;
    void Bind(uint64_t address, uint64_t generation = 0);
    void SetConnected(bool connected);
    void SetBoundDetails(uint64_t address, uint64_t generation, AirPods::Model model, bool connected);
    void Request(Mode mode);
    bool HasBinding() const { return _address != 0; }
    bool Available() const { return _available; }
    AirPods::Model Model() const { return _model; }
    std::optional<Mode> ReportedMode() const { return _mode; }

signals:
    void Changed();
    void Error(QString message);
    void RequestTimedOut(Core::NoiseControl::Mode mode);

private:
    struct Link { std::mutex mutex; Controller *controller{}; };
    void SetBoundModel(uint64_t address, AirPods::Model model);
    void Restart();
    void Stop();
    void CancelPendingError();
    void FlushPendingError();
    std::shared_ptr<Backend> _backend;
    std::shared_ptr<Link> _link{std::make_shared<Link>()};
    std::shared_ptr<Session> _session;
    uint64_t _address{};
    uint64_t _bindingGeneration{};
    uint64_t _generation{};
    uint64_t _requestSerial{};
    bool _errorReported{};
    bool _connected{};
    bool _available{};
    bool _workerRunning{};
    int _retryBaseMs;
    int _disconnectGraceMs;
    unsigned _retryAttempt{};
    QTimer _retryTimer{this};
    QTimer _disconnectGraceTimer{this};
    std::optional<QString> _pendingError;
    AirPods::Model _model{AirPods::Model::Unknown};
    std::optional<Mode> _mode;
};
} // namespace Core::NoiseControl
