#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>
// Test double for llama-server. Environment switches:
//   FAKE_HELP_HANG            --help never returns
//   FAKE_EXIT                 exit with code 42 after 250 ms
//   FAKE_HEALTH_LOADING_MS    GET /health returns 503 for this long after start, then 200
//   FAKE_HEALTH_HANG_AFTER_MS /health stops answering (connection stays open) after this long
//   FAKE_ONCE_FILE            misbehave (FAKE_EXIT / FAKE_HEALTH_HANG_AFTER_MS) only on the run
//                             that creates this file; later runs behave normally
//   FAKE_LOG_LEVELS           print a few llama.cpp-like warning and error lines at start
//   FAKE_DEMO_LOG             print a llama.cpp-like startup log and request lines instead of
//                             the heartbeat (README screenshots)
//   FAKE_RSS_MB               allocate and touch this many MiB so the process has a realistic size
// GET /v1/models reports --alias (or the -m path) as the model ID; GET /props reports the
// per-slot context (-c divided by --parallel).
//   FAKE_GENERATE_TPS         GET /slots shows slot 0 generating this many tokens per second
//   FAKE_DEMO_TPS             FAKE_DEMO_LOG's base generation speed (default 38-58 tokens per second)
//   FAKE_NO_SLOTS             GET /slots returns 501, like llama-server --no-slots
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.contains("--help")) {
        if (qEnvironmentVariableIsSet("FAKE_HELP_HANG")) {
            return app.exec();
        }
        QTextStream(stdout) << "-m -c -ngl -t --host --port --parallel --metrics -a, --alias\n";
        return 0;
    }
    bool misbehave = true;
    if (qEnvironmentVariableIsSet("FAKE_ONCE_FILE")) {
        QFile marker(qEnvironmentVariable("FAKE_ONCE_FILE"));
        if (marker.exists()) {
            misbehave = false;
        } else if (marker.open(QIODevice::WriteOnly)) {
            marker.close();
        }
    }
    const int loadingMs = qEnvironmentVariableIntValue("FAKE_HEALTH_LOADING_MS");
    const int hangAfterMs = misbehave && qEnvironmentVariableIsSet("FAKE_HEALTH_HANG_AFTER_MS")
        ? qEnvironmentVariableIntValue("FAKE_HEALTH_HANG_AFTER_MS")
        : -1;
    QElapsedTimer started;
    started.start();
    QTcpServer listener;
    const int index = args.indexOf("--port");
    if (index < 0 || !listener.listen(QHostAddress::LocalHost, args.value(index + 1).toUShort())) {
        return 2;
    }
    const auto option = [&args](const QString& name, const QString& fallback) {
        const int i = args.indexOf(name);
        return i >= 0 ? args.value(i + 1) : fallback;
    };
    const QString modelId = option("--alias", option("-m", "model.gguf"));
    const int slotContext = option("-c", "4096").toInt() / std::max(1, option("--parallel", "1").toInt());
    const QByteArray modelsBody = QJsonDocument(QJsonObject { { "object", "list" },
                                                    { "data", QJsonArray { QJsonObject { { "id", modelId }, { "object", "model" }, { "owned_by", "llamacpp" } } } } })
                                      .toJson(QJsonDocument::Compact);
    const QByteArray propsBody = QJsonDocument(QJsonObject { { "default_generation_settings", QJsonObject { { "n_ctx", slotContext } } } })
                                     .toJson(QJsonDocument::Compact);
    const int generateTps = qEnvironmentVariableIntValue("FAKE_GENERATE_TPS");
    const bool noSlots = qEnvironmentVariableIsSet("FAKE_NO_SLOTS");
    // FAKE_DEMO_LOG's simulated request on slot 0: n_decoded grows while it generates.
    struct Demo {
        bool generating = false;
        int task = -1;
        qint64 tokens = 0;
        double speed = 0;
        QElapsedTimer clock;
        qint64 decoded() const { return generating ? std::min<qint64>(tokens, qint64(clock.elapsed() * speed / 1000)) : tokens; }
    } demo;
    const int slotCount = std::max(1, option("--parallel", "1").toInt());
    const auto slotsBody = [&started, &demo, generateTps, slotContext, slotCount] {
        const auto slot = [slotContext](int id, bool processing, int task, qint64 decoded) {
            return QJsonObject { { "id", id }, { "n_ctx", slotContext }, { "speculative", false }, { "is_processing", processing }, { "id_task", task },
                { "next_token", QJsonObject { { "has_next_token", processing }, { "has_new_line", false }, { "n_remain", -1 }, { "n_decoded", decoded } } } };
        };
        QJsonObject first = slot(0, false, -1, 0);
        if (generateTps > 0) {
            first = slot(0, true, 1, started.elapsed() * generateTps / 1000);
        } else if (demo.task >= 0) {
            first = slot(0, demo.generating, demo.task, demo.decoded());
        }
        QJsonArray slotList { first };
        for (int id = 1; id < slotCount; ++id) {
            slotList.append(slot(id, false, -1, 0));
        }
        return QJsonDocument(slotList).toJson(QJsonDocument::Compact);
    };
    QObject::connect(&listener, &QTcpServer::newConnection, &app, [&] {
        while (QTcpSocket* socket = listener.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &started, loadingMs, hangAfterMs, &modelsBody, &propsBody, &slotsBody, noSlots] {
                const auto request = socket->property("buffer").toByteArray() + socket->readAll();
                if (!request.contains("\r\n\r\n")) {
                    socket->setProperty("buffer", request);
                    return;
                }
                socket->setProperty("buffer", QByteArray());
                const auto elapsed = started.elapsed();
                if (hangAfterMs >= 0 && elapsed >= hangAfterMs) {
                    return; // simulate a stuck server
                }
                QByteArray status = "200 OK", body = R"({"status":"ok"})";
                if (request.startsWith("GET /v1/models ")) {
                    body = modelsBody;
                } else if (request.startsWith("GET /props ")) {
                    body = propsBody;
                } else if (request.startsWith("GET /slots ")) {
                    if (elapsed < loadingMs) {
                        status = "503 Service Unavailable";
                        body = R"({"error":{"code":503,"message":"Loading model"}})";
                    } else if (noSlots) {
                        status = "501 Not Implemented";
                        body = R"({"error":{"code":501,"message":"This server does not support slots endpoint."}})";
                    } else {
                        body = slotsBody();
                    }
                } else if (!request.startsWith("GET /health ")) {
                    status = "404 Not Found";
                    body = "{}";
                } else if (elapsed < loadingMs) {
                    status = "503 Service Unavailable";
                    body = R"({"error":{"code":503,"message":"Loading model"}})";
                }
                socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        }
    });
    QTextStream(stdout) << "args=" << QJsonDocument(QJsonArray::fromStringList(args.mid(1))).toJson(QJsonDocument::Compact) << "\n"
                        << "cwd=" << QDir::currentPath() << Qt::endl;
    QTextStream(stderr) << "fake stderr ready\n"
                        << Qt::flush;
    if (qEnvironmentVariableIsSet("FAKE_LOG_LEVELS")) {
        QTextStream(stderr) << "llama_model_loader: loaded meta data with 30 key-value pairs\n"
                            << "W llama_context: n_ctx_per_seq (512) < n_ctx_train (32768) -- the full capacity of the model will not be utilized\n"
                            << "warning: no usable GPU found, --gpu-layers option will be ignored\n"
                            << "E srv  load_model: failed to load LoRA adapter 'missing.gguf'\n"
                            << "main: server is listening on http://127.0.0.1\n"
                            << Qt::flush;
    }
    std::vector<char> resident(size_t(std::max(0, qEnvironmentVariableIntValue("FAKE_RSS_MB"))) * 1024 * 1024);
    for (size_t i = 0; i < resident.size(); i += 4096) {
        resident[i] = 1; // touch every page so it counts as resident memory
    }
    QTimer timer;
    if (qEnvironmentVariableIsSet("FAKE_DEMO_LOG")) {
        QTextStream(stderr) << "build: 6123 (a1b2c3d4) with MSVC 19.51 for x64\n"
                            << "system info: n_threads = 8, n_threads_batch = 8, total_threads = 16\n"
                            << "llama_model_loader: loaded meta data with 32 key-value pairs and 399 tensors\n"
                            << "print_info: file type   = Q4_K - Medium\n"
                            << "load_tensors: offloading 36 repeating layers to GPU\n"
                            << "load_tensors: offloaded 37/37 layers to GPU\n"
                            << "llama_context: n_ctx_per_seq (8192) < n_ctx_train (40960) -- the full capacity of the model will not be utilized\n"
                            << "warning: --metrics is enabled, the /metrics endpoint is not protected by an API key\n"
                            << "srv    load_model: loading model\n"
                            << "main: model loaded\n"
                            << "main: server is listening on http://127.0.0.1:" << args.value(index + 1) << "\n"
                            << "srv  update_slots: all slots are idle\n"
                            << Qt::flush;
        // Alternates between a request that generates for a few seconds and a short idle gap.
        const double baseSpeed = qEnvironmentVariableIsSet("FAKE_DEMO_TPS") ? qEnvironmentVariableIntValue("FAKE_DEMO_TPS") - 4.0 : 38.0;
        const double speedRange = qEnvironmentVariableIsSet("FAKE_DEMO_TPS") ? 8.0 : 20.0;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &app, [&timer, &demo, baseSpeed, speedRange] {
            auto* random = QRandomGenerator::global();
            if (!demo.generating) {
                demo.task = demo.task < 0 ? 100 : demo.task + 1;
                demo.tokens = 120 + random->bounded(400);
                demo.speed = baseSpeed + random->bounded(speedRange);
                demo.generating = true;
                demo.clock.start();
                QTextStream(stderr) << "slot launch_slot_: id  0 | task " << demo.task << " | processing task\n"
                                    << Qt::flush;
                const int ms = int(demo.tokens * 1000 / demo.speed);
                // Generating tokens costs CPU, so the per-model CPU column has something to show.
                std::thread([ms] {
                    QElapsedTimer busy;
                    busy.start();
                    volatile double sink = 0;
                    while (busy.elapsed() < ms) {
                        // About half of one core, like a GPU-offloaded model feeding the GPU.
                        QElapsedTimer slice;
                        slice.start();
                        while (slice.elapsed() < 8) {
                            sink = sink + 1.0;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }).detach();
                timer.start(ms);
                return;
            }
            demo.generating = false;
            QTextStream(stderr) << "slot      release: id  0 | task " << demo.task << " | stop processing: n_past = " << demo.tokens + 512 << ", truncated = 0\n"
                                << "prompt eval time =     " << QString::number(180.0 + demo.tokens * 0.4, 'f', 2) << " ms /   512 tokens\n"
                                << "       eval time =    " << QString::number(demo.tokens * 1000.0 / demo.speed, 'f', 2) << " ms / " << demo.tokens << " tokens ("
                                << QString::number(demo.speed, 'f', 2) << " tokens per second)\n"
                                << "srv  log_server_r: request: POST /v1/chat/completions 127.0.0.1 200\n"
                                << Qt::flush;
            timer.start(1500 + random->bounded(3500));
        });
        timer.start(1500);
    } else {
        QObject::connect(&timer, &QTimer::timeout, &app, [] { QTextStream(stdout) << "heartbeat 模型\n"
                                                                                  << Qt::flush; });
        timer.start(100);
    }
    if (misbehave && qEnvironmentVariableIsSet("FAKE_EXIT")) {
        QTimer::singleShot(250, &app, [] { QCoreApplication::exit(42); });
    }
    return app.exec();
}
