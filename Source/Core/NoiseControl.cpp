#include "NoiseControl.h"

#include <algorithm>
#include <thread>
#include <utility>
#include <QMetaObject>

#include "../Logger.h"

namespace Core::NoiseControl {

bool Supports(AirPods::Model model, Mode mode)
{
    using AirPods::Model;
    switch (model) {
    case Model::AirPods_4_ANC:
    case Model::AirPods_5:
    case Model::AirPods_Pro_2:
    case Model::AirPods_Pro_2_USB_C:
    case Model::AirPods_Pro_3:
        return true;
    case Model::AirPods_Pro:
    case Model::AirPods_Max:
    case Model::AirPods_Max_USB_C:
        return mode != Mode::Adaptive;
    default:
        return false;
    }
}

std::vector<uint8_t> Command(Mode mode)
{
    return {0x04, 0x00, 0x04, 0x00, 0x09, 0x00, 0x0d, static_cast<uint8_t>(mode), 0, 0, 0};
}

std::optional<Mode> ParseMode(const uint8_t *data, size_t size)
{
    static constexpr uint8_t prefix[]{0x04, 0x00, 0x04, 0x00, 0x09, 0x00, 0x0d};
    if (size < 8 || !std::equal(std::begin(prefix), std::end(prefix), data) || data[7] < 1 ||
        data[7] > 4) {
        return std::nullopt;
    }
    return static_cast<Mode>(data[7]);
}

void Confirmation::Sent(Request request, Clock::time_point now)
{
    if (_pending) {
        LOG(Trace, "Noise control: sent request {} supersedes pending request {}", request.serial,
            _pending->serial);
    }
    _pending = request;
    _deadline = now + std::chrono::seconds{8};
    LOG(Trace, "Noise control: sent mode {} request {}", static_cast<unsigned>(request.mode),
        request.serial);
}

void Confirmation::Observe(Mode mode, Clock::time_point now)
{
    if (!_pending) {
        LOG(Trace, "Noise control: received mode {} with no pending request", static_cast<unsigned>(mode));
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - (_deadline - std::chrono::seconds{8})).count();
    const bool matches = _pending->mode == mode;
    LOG(Trace, "Noise control: received mode {} with pending request {} (mode {}, elapsed {} ms, {})",
        static_cast<unsigned>(mode), _pending->serial, static_cast<unsigned>(_pending->mode), elapsed,
        matches ? "matching" : "nonmatching");
    if (matches) {
        LOG(Trace, "Noise control: request {} confirmed by matching report", _pending->serial);
        _pending.reset();
    }
}

std::optional<Request> Confirmation::Expire(Clock::time_point now)
{
    if (!_pending || now < _deadline) return std::nullopt;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - (_deadline - std::chrono::seconds{8})).count();
    LOG(Trace, "Noise control: request {} (mode {}) timed out after {} ms, after draining reports",
        _pending->serial, static_cast<unsigned>(_pending->mode), elapsed);
    return std::exchange(_pending, std::nullopt);
}

CycleResult RunCycle(Confirmation &confirmation, std::optional<Request> command, const CycleIO &io)
{
    auto drain = [&]() {
        constexpr size_t limit = 32;
        for (size_t count = 0; ; ++count) {
            if (io.cancelled()) return CycleResult::Cancelled;
            std::vector<uint8_t> packet;
            const auto result = io.receive(packet);
            if (result == ReceiveResult::Empty) return CycleResult::Ok;
            if (result == ReceiveResult::Closed) return CycleResult::Closed;
            if (result == ReceiveResult::Error) return CycleResult::ReadError;
            if (count == limit) return CycleResult::Flood;
            if (result == ReceiveResult::Packet) {
                if (auto mode = ParseMode(packet.data(), packet.size())) {
                    confirmation.Observe(*mode, io.now());
                    io.onMode(*mode);
                } else {
                    // Unknown packet contents and lengths may contain identifying information.
                    LOG(Trace, "Noise control: received unrecognized packet");
                }
            } else if (result == ReceiveResult::Truncated) {
                LOG(Trace, "Noise control: received truncated packet");
            }
        }
    };
    auto result = drain();
    if (result != CycleResult::Ok) return result;
    if (command) {
        if (!io.waitWritable()) return io.cancelled() ? CycleResult::Cancelled : CycleResult::WriteError;
        // Reports can arrive while waiting for writability; consume them before arming the new
        // request. A later report still cannot be causally attributed: AAP supplies no request ID.
        result = drain();
        if (result != CycleResult::Ok) return result;
    }
    if (auto expired = confirmation.Expire(io.now())) io.onTimeout(*expired);
    if (io.cancelled()) return CycleResult::Cancelled;
    if (command) {
        if (!io.send(*command)) {
            LOG(Trace, "Noise control: failed to send mode {} request {}",
                static_cast<unsigned>(command->mode), command->serial);
            return CycleResult::WriteError;
        }
        confirmation.Sent(*command, io.now());
    }
    return CycleResult::Ok;
}

Controller::Controller(std::shared_ptr<Backend> backend, QObject *parent, int retryBaseMs,
                       int disconnectGraceMs)
    : QObject{parent}, _backend{std::move(backend)}, _retryBaseMs{std::max(1, retryBaseMs)},
      _disconnectGraceMs{std::max(1, disconnectGraceMs)}
{
    _link->controller = this;
    _retryTimer.setSingleShot(true);
    connect(&_retryTimer, &QTimer::timeout, this, &Controller::Restart);
    _disconnectGraceTimer.setSingleShot(true);
    connect(&_disconnectGraceTimer, &QTimer::timeout, this, &Controller::FlushPendingError);
}

Controller::~Controller()
{
    { std::lock_guard lock{_link->mutex}; _link->controller = nullptr; }
    Stop();
}

void Controller::Stop()
{
    ++_generation;
    _retryTimer.stop();
    _retryAttempt = 0;
    if (_session) _session->stopped = true;
    _session.reset();
    _available = false;
    _mode.reset();
    CancelPendingError();
}

void Controller::CancelPendingError()
{
    if (!_pendingError) return;
    LOG(Trace, "Noise control: dropping held error '{}' after a disconnect or binding change",
        _pendingError->toStdString());
    _pendingError.reset();
    _disconnectGraceTimer.stop();
}

void Controller::FlushPendingError()
{
    if (!_pendingError) return;
    auto message = std::exchange(_pendingError, std::nullopt);
    if (_errorReported) return;
    LOG(Trace, "Noise control: device remained connected; reporting held error '{}'",
        message->toStdString());
    _errorReported = true;
    emit Error(*message);
}

void Controller::Bind(uint64_t address, uint64_t generation)
{
    Stop();
    _address = address;
    _bindingGeneration = generation;
    _errorReported = false;
    _connected = false;
    _model = AirPods::Model::Unknown;
    emit Changed();
}

void Controller::SetConnected(bool connected)
{
    if (_connected == connected) return;
    _connected = connected;
    if (!connected) {
        Stop();
        emit Changed();
    } else {
        Restart();
    }
}

void Controller::SetBoundModel(uint64_t address, AirPods::Model model)
{
    if (address != _address || model == AirPods::Model::Unknown || _model == model) return;
    _model = model;
    Stop();
    Restart();
}

void Controller::SetBoundDetails(uint64_t address, uint64_t generation, AirPods::Model model, bool connected)
{
    if (address != _address || generation != _bindingGeneration) return;
    if (!connected) SetConnected(false);
    SetBoundModel(address, model);
    if (connected) SetConnected(true);
}

void Controller::Restart()
{
    emit Changed();
    // The previous backend owns its socket until Run returns. Its completion will call Restart
    // again if the desired binding changed, so the GUI never waits and sockets never overlap.
    if (_workerRunning || !_backend || !_address || !_connected || !Supports(_model, Mode::Anc)) return;
    auto session = std::make_shared<Session>();
    _session = session;
    _workerRunning = true;
    const auto generation = _generation;
    std::thread{[backend = _backend, link = _link, session, address = _address, generation] {
        backend->Run(address, session, [link, generation](std::optional<Mode> mode, QString error,
                                                            std::optional<Core::NoiseControl::Request> timedOut) {
            std::lock_guard lock{link->mutex};
            if (!link->controller) return;
            QMetaObject::invokeMethod(link->controller, [self = link->controller, generation, mode, error, timedOut] {
                if (generation != self->_generation) return;
                if (timedOut) {
                    const bool current = timedOut->serial == self->_requestSerial;
                    LOG(Trace, "Noise control: timeout for request {} {} by controller", timedOut->serial,
                        current ? "delivered" : "discarded as superseded");
                    if (current) emit self->RequestTimedOut(timedOut->mode);
                    return;
                }
                if (!error.isEmpty()) {
                    self->_available = false;
                    self->_mode.reset();
                    emit self->Changed();
                    // The AAP socket can close before BlueZ reports the bound-device disconnect.
                    // Hold the notification briefly so that a coinciding disconnect stays silent
                    // while a failure with the device still connected still surfaces once.
                    if (!self->_errorReported && !self->_pendingError) {
                        LOG(Trace, "Noise control: holding error '{}' for {} ms pending a disconnect",
                            error.toStdString(), self->_disconnectGraceMs);
                        self->_pendingError = error;
                        self->_disconnectGraceTimer.start(self->_disconnectGraceMs);
                    }
                } else {
                    self->_available = true;
                    if (mode && Supports(self->_model, *mode)) {
                        self->_mode = mode;
                        self->_retryAttempt = 0;
                        self->_errorReported = false;
                    }
                    emit self->Changed();
                }
            }, Qt::QueuedConnection);
        });
        std::lock_guard lock{link->mutex};
        if (!link->controller) return;
        QMetaObject::invokeMethod(link->controller, [self = link->controller, generation] {
            self->_workerRunning = false;
            if (generation != self->_generation) {
                self->Restart();
                return;
            }
            ++self->_generation; // Reject late reports from the completed session.
            self->_session.reset();
            self->_available = false;
            self->_mode.reset();
            emit self->Changed();
            if (self->_address && self->_connected && Supports(self->_model, Mode::Anc)) {
                const auto shift = std::min(self->_retryAttempt++, 5u);
                const int delay = static_cast<int>(std::min<int64_t>(30000, int64_t{self->_retryBaseMs} << shift));
                self->_retryTimer.start(delay);
            }
        }, Qt::QueuedConnection);
    }}.detach();
}

void Controller::Request(Mode mode)
{
    if (!_available || !_session || !Supports(_model, mode)) return;
    _errorReported = false;
    std::lock_guard lock{_session->mutex};
    _session->command = Core::NoiseControl::Request{mode, ++_requestSerial};
    LOG(Trace, "Noise control: queued mode {} request {}", static_cast<unsigned>(mode), _requestSerial);
}
} // namespace Core::NoiseControl
