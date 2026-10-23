#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
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
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.contains("--help")) {
        if (qEnvironmentVariableIsSet("FAKE_HELP_HANG")) {
            return app.exec();
        }
        QTextStream(stdout) << "-m -c -ngl -t --host --port --parallel --metrics\n";
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
    QObject::connect(&listener, &QTcpServer::newConnection, &app, [&] {
        while (QTcpSocket* socket = listener.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &started, loadingMs, hangAfterMs] {
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
                if (!request.startsWith("GET /health ")) {
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
        QObject::connect(&timer, &QTimer::timeout, &app, [&timer] {
            static int task = 100;
            const int tokens = 40 + int(QRandomGenerator::global()->bounded(400));
            const double speed = 38.0 + QRandomGenerator::global()->bounded(20.0);
            QTextStream(stderr) << "slot launch_slot_: id  0 | task " << task << " | processing task\n"
                                << "slot      release: id  0 | task " << task << " | stop processing: n_past = " << tokens + 512 << ", truncated = 0\n"
                                << "prompt eval time =     " << QString::number(180.0 + tokens * 0.4, 'f', 2) << " ms /   512 tokens\n"
                                << "       eval time =    " << QString::number(tokens * 1000.0 / speed, 'f', 2) << " ms / " << tokens << " tokens ("
                                << QString::number(speed, 'f', 2) << " tokens per second)\n"
                                << "srv  log_server_r: request: POST /v1/chat/completions 127.0.0.1 200\n"
                                << Qt::flush;
            ++task;
            // Generating tokens costs CPU, so the per-model CPU column has something to show.
            QElapsedTimer busy;
            busy.start();
            const int ms = 700 + int(QRandomGenerator::global()->bounded(1300));
            volatile double sink = 0;
            while (busy.elapsed() < ms) {
                sink = sink + 1.0;
            }
            timer.start(2000 + int(QRandomGenerator::global()->bounded(5000)));
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
