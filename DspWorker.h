#ifndef DSPWORKER_H
#define DSPWORKER_H

#include <QObject>
#include <QString>
#include <atomic>
#include <string>

// --- DSP WORKER ---
class DspWorker : public QObject {
    Q_OBJECT
public:
    std::atomic<bool> running{false};
    QString filePath;
    QString targetIP;
    int targetPort;
    int targetBits;
    float targetRate;
    float manualShiftFreq;
    std::atomic<bool> useAGC{true};
    std::atomic<float> manualGainValue{0.65f};
    std::atomic<bool> useOffset{false};
    std::atomic<bool> checkDSP32{false};

signals:
    void progressUpdated(float percent);
    void levelUpdated(int level);
    void finished();

public slots:
    void process();

private:
    bool set_dac_width(const std::string& ip, int bits);
    bool set_dac_rate(const std::string& ip, float rate);
    QString run_dsp_engine(QString fullPath, int sock);
    QString run_dsp_engine_32INT(QString fullPath, int sock);
};

#endif // DSPWORKER_H