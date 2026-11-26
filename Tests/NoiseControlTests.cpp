#include <QtTest>
#include <condition_variable>
#include <deque>
#include <sstream>
#include <spdlog/sinks/ostream_sink.h>
#include "Source/Core/NoiseControl.h"

using namespace Core::NoiseControl;
using Core::AirPods::Model;

// An intentionally slow-to-close fake makes overlap and GUI-thread waiting observable.
class FakeBackend final : public Backend
{
public:
    struct Call {
        uint64_t address;
        std::shared_ptr<Session> session;
        std::function<void(std::optional<Mode>, QString, std::optional<Request>)> report;
        bool finish{};
    };
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::shared_ptr<Call>> calls;
    int active{};
    int maxActive{};

    void Run(uint64_t address, std::shared_ptr<Session> session,
             std::function<void(std::optional<Mode>, QString, std::optional<Request>)> report) override
    {
        std::unique_lock lock{mutex};
        auto call = std::make_shared<Call>(address, session, std::move(report), false);
        calls.push_back(call);
        maxActive = std::max(maxActive, ++active);
        changed.wait(lock, [&] { return call->finish; });
        --active;
    }
    size_t Count() { std::lock_guard lock{mutex}; return calls.size(); }
    bool Stopped(size_t index) { std::lock_guard lock{mutex}; return calls.at(index)->session->stopped; }
    int MaximumActive() { std::lock_guard lock{mutex}; return maxActive; }
    void Finish(size_t index)
    {
        std::lock_guard lock{mutex};
        calls.at(index)->finish = true;
        changed.notify_all();
    }
    Request LastRequest(size_t index)
    {
        std::shared_ptr<Session> session;
        { std::lock_guard lock{mutex}; session = calls.at(index)->session; }
        std::lock_guard lock{session->mutex};
        return session->command.value();
    }
    void Timeout(size_t index, Request request)
    {
        std::function<void(std::optional<Mode>, QString, std::optional<Request>)> callback;
        { std::lock_guard lock{mutex}; callback = calls.at(index)->report; }
        callback({}, {}, request);
    }
    void Report(size_t index, std::optional<Mode> mode, QString error = {})
    {
        std::function<void(std::optional<Mode>, QString, std::optional<Request>)> callback;
        { std::lock_guard lock{mutex}; callback = calls.at(index)->report; }
        callback(mode, error, {});
    }
};

class NoiseControlTests : public QObject
{
    Q_OBJECT
private slots:
    void Protocol()
    {
        QCOMPARE(Command(Mode::Anc), (std::vector<uint8_t>{4,0,4,0,9,0,13,2,0,0,0}));
        auto packet = Command(Mode::Transparency);
        QVERIFY(ParseMode(packet.data(), packet.size()) == Mode::Transparency);
        QVERIFY(!ParseMode(packet.data(), 7));
        packet[0] = 0; QVERIFY(!ParseMode(packet.data(), packet.size()));
        packet[0] = 4; packet[7] = 5; QVERIFY(!ParseMode(packet.data(), packet.size()));
        QVERIFY(!Supports(Model::AirPods_4, Mode::Anc));
        QVERIFY(!Supports(Model::AirPods_Pro, Mode::Adaptive));
        QVERIFY(Supports(Model::AirPods_Pro_2, Mode::Adaptive));
    }
    void TraceDiagnosticsAreSanitized()
    {
        using namespace std::chrono_literals;
        std::ostringstream output;
        auto previous = spdlog::default_logger();
        struct RestoreLogger {
            std::shared_ptr<spdlog::logger> logger;
            ~RestoreLogger() { spdlog::set_default_logger(logger); }
        } restore{previous};
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(output);
        auto logger = std::make_shared<spdlog::logger>("noise-test", sink);
        logger->set_level(spdlog::level::trace);
        logger->set_pattern("%v");
        spdlog::set_default_logger(logger);

        std::deque<std::vector<uint8_t>> packets;
        auto now = Confirmation::Clock::time_point{};
        CycleIO io{
            .receive = [&](std::vector<uint8_t> &packet) {
                if (packets.empty()) return ReceiveResult::Empty;
                packet = std::move(packets.front()); packets.pop_front();
                return ReceiveResult::Packet;
            },
            .onMode = [](Mode) {},
            .onTimeout = [](Request) {},
            .waitWritable = [] { return true; },
            .send = [](Request) { return true; },
            .cancelled = [] { return false; },
            .now = [&] { return now; },
        };
        Confirmation confirmation;
        QCOMPARE(RunCycle(confirmation, Request{Mode::Adaptive, 42}, io), CycleResult::Ok);
        now += 3s;
        packets.push_back({'S', 'E', 'C', 'R', 'E', 'T', '-', 'A', 'D', 'D', 'R'});
        packets.push_back(Command(Mode::Anc));
        QCOMPARE(RunCycle(confirmation, {}, io), CycleResult::Ok);
        now += 5s;
        QCOMPARE(RunCycle(confirmation, {}, io), CycleResult::Ok);
        packets.push_back(Command(Mode::Adaptive));
        QCOMPARE(RunCycle(confirmation, {}, io), CycleResult::Ok);
        QCOMPARE(RunCycle(confirmation, Request{Mode::Adaptive, 43}, io), CycleResult::Ok);
        now += 1s;
        packets.push_back(Command(Mode::Adaptive));
        QCOMPARE(RunCycle(confirmation, {}, io), CycleResult::Ok);
        const auto log = output.str();
        QVERIFY(log.find("sent mode 4 request 42") != std::string::npos);
        QVERIFY(log.find("received unrecognized packet") != std::string::npos);
        QVERIFY(log.find("pending request 42 (mode 4, elapsed 3000 ms, nonmatching)") != std::string::npos);
        QVERIFY(log.find("request 42 (mode 4) timed out after 8000 ms") != std::string::npos);
        QVERIFY(log.find("received mode 4 with no pending request") != std::string::npos);
        QVERIFY(log.find("request 43 confirmed by matching report") != std::string::npos);
        QVERIFY(log.find("SECRET") == std::string::npos);
        QVERIFY(log.find("ADDR") == std::string::npos);
    }
    void ReceiveOrderingAndBounds()
    {
        using namespace std::chrono_literals;
        struct Item { ReceiveResult result; std::vector<uint8_t> bytes; };
        std::deque<Item> queue;
        std::vector<Mode> modes;
        std::vector<Request> timeouts;
        bool cancelled = false;
        bool addWhileWaiting = false;
        auto now = Confirmation::Clock::time_point{};
        CycleIO io{
            .receive = [&](std::vector<uint8_t> &packet) {
                if (queue.empty()) return ReceiveResult::Empty;
                auto item = std::move(queue.front()); queue.pop_front();
                packet = std::move(item.bytes); return item.result;
            },
            .onMode = [&](Mode mode) { modes.push_back(mode); },
            .onTimeout = [&](Request request) { timeouts.push_back(request); },
            .waitWritable = [&] {
                if (addWhileWaiting) queue.push_back({ReceiveResult::Packet, Command(Mode::Off)});
                return true;
            },
            .send = [](Request) { return true; },
            .cancelled = [&] { return cancelled; },
            .now = [&] { return now; },
        };
        Confirmation confirmation;
        confirmation.Sent({Mode::Anc, 1}, now);
        now += 8s;
        queue.push_back({ReceiveResult::Packet, Command(Mode::Anc)});
        QCOMPARE(RunCycle(confirmation, {}, io), CycleResult::Ok);
        QVERIFY(timeouts.empty()); // queued confirmation wins over deadline
        queue.push_back({ReceiveResult::Packet, Command(Mode::Off)});
        addWhileWaiting = true;
        QCOMPARE(RunCycle(confirmation, Request{Mode::Off, 2}, io), CycleResult::Ok);
        addWhileWaiting = false;
        now += 8s;
        QCOMPARE(RunCycle(confirmation, {}, io), CycleResult::Ok);
        QCOMPARE(timeouts.size(), size_t{1});
        QCOMPARE(timeouts[0].serial, uint64_t{2}); // pre-send reports did not confirm it
        queue.push_back({ReceiveResult::Packet, Command(Mode::Off)});
        QCOMPARE(RunCycle(confirmation, {}, io), CycleResult::Ok);
        QCOMPARE(modes.back(), Mode::Off); // late reports still update actual state
        auto count = modes.size();
        queue.push_back({ReceiveResult::Truncated, Command(Mode::Anc)});
        QCOMPARE(RunCycle(confirmation, {}, io), CycleResult::Ok);
        QCOMPARE(modes.size(), count); // a truncated prefix is not a valid report
        for (int i = 0; i < 33; ++i) queue.push_back({ReceiveResult::Truncated, {}});
        QCOMPARE(RunCycle(confirmation, Request{Mode::Off, 3}, io), CycleResult::Flood);
        cancelled = true;
        QCOMPARE(RunCycle(confirmation, {}, io), CycleResult::Cancelled);
        cancelled = false;
        queue.clear();
        queue.push_back({ReceiveResult::Closed, {}});
        QCOMPARE(RunCycle(confirmation, {}, io), CycleResult::Closed);
    }
    void ConfirmationTimeoutPreservesSession()
    {
        using namespace std::chrono_literals;
        Confirmation tracker;
        const auto start = Confirmation::Clock::time_point{};
        tracker.Sent(Request{Mode::Off, 1}, start);
        QVERIFY(!tracker.Expire(start + 7999ms));
        tracker.Observe(Mode::Anc); // unrelated report is not confirmation
        auto expired = tracker.Expire(start + 8s);
        QVERIFY(expired && expired->mode == Mode::Off && expired->serial == 1);
        QVERIFY(!tracker.Expire(start + 9s));
        tracker.Observe(Mode::Off); // late report does not recreate a pending request
        tracker.Sent(Request{Mode::Transparency, 2}, start + 9s);
        tracker.Observe(Mode::Transparency);
        QVERIFY(!tracker.Expire(start + 20s));

        auto backend = std::make_shared<FakeBackend>();
        Controller controller{backend, nullptr, 20};
        std::vector<Mode> timeouts;
        connect(&controller, &Controller::RequestTimedOut, &controller,
                [&](Mode mode) { timeouts.push_back(mode); });
        controller.Bind(123, 1);
        controller.SetBoundDetails(123, 1, Model::AirPods_Pro_2, true);
        QTRY_COMPARE(backend->Count(), size_t{1});
        backend->Report(0, Mode::Anc);
        QTRY_VERIFY(controller.ReportedMode() == Mode::Anc);
        controller.Request(Mode::Off);
        const auto off = backend->LastRequest(0);
        backend->Timeout(0, off);
        QTRY_COMPARE(timeouts.size(), size_t{1});
        QCOMPARE(timeouts.front(), Mode::Off);
        QVERIFY(controller.Available());
        QVERIFY(controller.ReportedMode() == Mode::Anc);
        QCOMPARE(backend->Count(), size_t{1});
        controller.Request(Mode::Transparency);
        const auto next = backend->LastRequest(0);
        backend->Timeout(0, off); // superseded timeout cannot report against a newer request
        QCoreApplication::processEvents();
        QCOMPARE(timeouts.size(), size_t{1});
        backend->Report(0, Mode::Off); // late report reflects the actual device
        QTRY_VERIFY(controller.ReportedMode() == Mode::Off);
        backend->Timeout(0, next);
        QTRY_COMPARE(timeouts.size(), size_t{2});
        QVERIFY(controller.Available());
        backend->Report(0, Mode::Transparency);
        QTRY_VERIFY(controller.ReportedMode() == Mode::Transparency);
        controller.SetConnected(false);
        backend->Finish(0);
    }
    void RetryAndSerializedRebind()
    {
        auto backend = std::make_shared<FakeBackend>();
        Controller controller{backend, nullptr, 20};
        constexpr uint64_t first = 0x112233445566;
        constexpr uint64_t second = 0xaabbccddeeff;
        controller.Bind(first);
        controller.SetBoundDetails(first, 0, Model::AirPods_Pro_2, true);
        QTRY_COMPARE(backend->Count(), size_t{1});
        backend->Report(0, {});
        QTRY_VERIFY(controller.Available());
        controller.Request(Mode::Anc);
        QVERIFY(!controller.ReportedMode());
        backend->Report(0, Mode::Transparency);
        QTRY_VERIFY(controller.ReportedMode() == Mode::Transparency);
        backend->Report(0, {}, QStringLiteral("transient failure"));
        QTRY_VERIFY(!controller.Available());
        backend->Finish(0);
        QTRY_COMPARE(backend->Count(), size_t{2});
        backend->Report(1, Mode::Anc);
        QTRY_VERIFY(controller.ReportedMode() == Mode::Anc);
        controller.Bind(second);
        controller.SetBoundDetails(first, 0, Model::AirPods_Pro_2, true); // stale address
        controller.SetBoundDetails(second, 0, Model::AirPods_Pro, true);
        QVERIFY(backend->Stopped(1));
        backend->Report(1, Mode::Transparency); // stale generation
        QTest::qWait(60);
        QCOMPARE(backend->Count(), size_t{2}); // closing is asynchronous, replacement waits
        QVERIFY(!controller.ReportedMode());
        backend->Finish(1);
        QTRY_COMPARE(backend->Count(), size_t{3});
        QCOMPARE(backend->MaximumActive(), 1);
        backend->Report(2, Mode::Anc);
        QTRY_VERIFY(controller.Available());
        controller.SetBoundDetails(second, 0, Model::AirPods_Pro, false);
        QVERIFY(backend->Stopped(2));
        controller.SetConnected(true); // resolved model survives disconnect, no advertisement
        QCOMPARE(controller.Model(), Model::AirPods_Pro);
        backend->Finish(2);
        QTRY_COMPARE(backend->Count(), size_t{4});
        backend->Report(4 - 1, Mode::Transparency);
        QTRY_VERIFY(controller.ReportedMode() == Mode::Transparency);
        controller.SetConnected(false);
        backend->Finish(3);
    }
    void UnknownClassicModelAndGeneration()
    {
        auto backend = std::make_shared<FakeBackend>();
        Controller controller{backend, nullptr, 20};
        constexpr uint64_t address = 123;
        controller.Bind(address, 1);
        controller.SetBoundDetails(address, 1, Model::Unknown, true);
        QTest::qWait(30);
        QCOMPARE(backend->Count(), size_t{0});
        QVERIFY(!controller.Available());
        controller.Bind(address, 2);
        controller.SetBoundDetails(address, 1, Model::AirPods_Pro_2, true);
        QCOMPARE(controller.Model(), Model::Unknown); // same-address stale generation rejected
        QCOMPARE(backend->Count(), size_t{0});
        controller.SetBoundDetails(address, 2, Model::AirPods_Pro_2, true);
        QTRY_COMPARE(backend->Count(), size_t{1});
        controller.SetBoundDetails(address, 2, Model::Unknown, false);
        QCOMPARE(controller.Model(), Model::AirPods_Pro_2);
        controller.SetBoundDetails(address, 2, Model::Unknown, true);
        backend->Finish(0);
        QTRY_COMPARE(backend->Count(), size_t{2});
        controller.SetConnected(false);
        backend->Finish(1);
    }
    void AutomaticFailuresAreDeduplicated()
    {
        auto backend = std::make_shared<FakeBackend>();
        Controller controller{backend, nullptr, 10, 10};
        QSignalSpy errors{&controller, &Controller::Error};
        controller.Bind(123, 1);
        controller.SetBoundDetails(123, 1, Model::AirPods_Pro_2, true);
        QTRY_COMPARE(backend->Count(), size_t{1});
        backend->Report(0, {}, QStringLiteral("first"));
        QTRY_COMPARE(errors.count(), 1);
        backend->Finish(0);
        QTRY_COMPARE(backend->Count(), size_t{2});
        backend->Report(1, {}, QStringLiteral("again"));
        QTest::qWait(20);
        QCOMPARE(errors.count(), 1);
        backend->Finish(1);
        QTRY_COMPARE(backend->Count(), size_t{3});
        backend->Report(2, Mode::Anc);
        QTRY_VERIFY(controller.Available());
        backend->Report(2, {}, QStringLiteral("after recovery"));
        QTRY_COMPARE(errors.count(), 2);
        backend->Report(2, {}); // ready alone does not mark recovery
        QTRY_VERIFY(controller.Available());
        controller.Request(Mode::Anc); // explicit action opens a new episode
        backend->Report(2, {}, QStringLiteral("after request"));
        QTRY_COMPARE(errors.count(), 3);
        controller.Bind(123, 2);
        controller.SetBoundDetails(123, 2, Model::AirPods_Pro, true);
        backend->Finish(2);
        QTRY_COMPARE(backend->Count(), size_t{4});
        backend->Report(3, {}, QStringLiteral("new binding"));
        QTRY_COMPARE(errors.count(), 4);
        controller.SetConnected(false);
        backend->Finish(3);
    }
    // The AAP socket usually notices a disconnect before BlueZ delivers Connected=false. The
    // closure report must clear availability/mode without surfacing an error notification.
    void ClosureBeforeDisconnectIsSilent()
    {
        auto backend = std::make_shared<FakeBackend>();
        Controller controller{backend, nullptr, 20, 30};
        QSignalSpy errors{&controller, &Controller::Error};
        controller.Bind(123, 1);
        controller.SetBoundDetails(123, 1, Model::AirPods_Pro_2, true);
        QTRY_COMPARE(backend->Count(), size_t{1});
        backend->Report(0, Mode::Anc);
        QTRY_VERIFY(controller.Available());

        backend->Report(0, {}, QStringLiteral("AAP connection closed")); // closure wins the race
        QCoreApplication::processEvents();
        QVERIFY(!controller.Available());
        QVERIFY(!controller.ReportedMode());
        controller.SetConnected(false); // BlueZ disconnect arrives inside the grace window
        QTest::qWait(80);
        QCOMPARE(errors.count(), 0);

        backend->Finish(0);
        QTest::qWait(30);
        // Reconnecting restarts the session.
        controller.SetConnected(true);
        QTRY_COMPARE(backend->Count(), size_t{2});
        backend->Report(1, Mode::Off);
        QTRY_VERIFY(controller.Available());
        QVERIFY(controller.ReportedMode() == Mode::Off);
        controller.SetConnected(false);
        backend->Finish(1);
    }
    // A failure with the device still connected (no disconnect follows) must still notify once.
    void HeldFailureNotifiesWhenStillConnected()
    {
        auto backend = std::make_shared<FakeBackend>();
        Controller controller{backend, nullptr, 20, 30};
        QSignalSpy errors{&controller, &Controller::Error};
        controller.Bind(123, 1);
        controller.SetBoundDetails(123, 1, Model::AirPods_Pro_2, true);
        QTRY_COMPARE(backend->Count(), size_t{1});

        backend->Report(0, {}, QStringLiteral("AAP connection closed"));
        QTRY_VERIFY(!controller.Available());
        QTRY_COMPARE(errors.count(), 1);
        QCOMPARE(errors.front().at(0).toString(), QStringLiteral("AAP connection closed"));

        // Retained dedup: another failure in the same episode stays silent.
        backend->Report(0, {}, QStringLiteral("AAP connection closed again"));
        QTest::qWait(80);
        QCOMPARE(errors.count(), 1);
        backend->Finish(0);
        controller.SetConnected(false);
    }
    // A genuine connect failure also notifies once; the grace window must not swallow it.
    void GenuineConnectFailureStillNotifies()
    {
        auto backend = std::make_shared<FakeBackend>();
        Controller controller{backend, nullptr, 20, 30};
        QSignalSpy errors{&controller, &Controller::Error};
        controller.Bind(123, 1);
        controller.SetBoundDetails(123, 1, Model::AirPods_Pro_2, true);
        QTRY_COMPARE(backend->Count(), size_t{1});

        backend->Report(0, {}, QStringLiteral("AAP connection failed"));
        QTRY_VERIFY(!controller.Available());
        QTRY_COMPARE(errors.count(), 1);
        QCOMPARE(errors.front().at(0).toString(), QStringLiteral("AAP connection failed"));

        backend->Report(0, {}, QStringLiteral("AAP connection failed again"));
        QTest::qWait(80);
        QCOMPARE(errors.count(), 1);
        backend->Finish(0);
        controller.SetConnected(false);
    }
    void ShutdownIgnoresLateCallbacks()
    {
        auto backend = std::make_shared<FakeBackend>();
        auto controller = std::make_unique<Controller>(backend, nullptr, 20);
        controller->Bind(123);
        controller->SetBoundDetails(123, 0, Model::AirPods_Pro_2, true);
        QTRY_COMPARE(backend->Count(), size_t{1});
        controller.reset(); // must not wait for the deliberately blocked worker
        QVERIFY(backend->Stopped(0));
        backend->Report(0, Mode::Anc);
        backend->Finish(0);
        QTest::qWait(30);
        QCOMPARE(backend->Count(), size_t{1});
    }
};
QTEST_GUILESS_MAIN(NoiseControlTests)
#include "NoiseControlTests.moc"
