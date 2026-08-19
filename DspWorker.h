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
    float manualShiftFreq; // Wird aus dem Dateinamen extrahiert (z.B. "1250kHz")
    
    std::atomic<bool> useAGC{true};
    std::atomic<float> manualGainValue{0.65f};
    std::atomic<bool> useOffset{false};
    std::atomic<bool> checkDSP32{false};
    
    // NEU: Schalter für die direkte In-Band Signaling I/Q Engine
    std::atomic<bool> checkIQEngine{false}; 

    // NEU: lock-freie Telemetrie-Werte statt direktem emit() aus dem
    // DSP/Sende-Thread heraus. Werden dort per store() aktualisiert (billig,
    // kein Cross-Thread-Signal, kein Warten auf den GUI-Event-Loop) und von
    // einem Timer IM GUI-THREAD (siehe MainWindow) periodisch ausgelesen.
    // WICHTIG: Absichtlich NICHT als QTimer/QObject-Kind hier in DspWorker
    // angelegt - ein Timer, der als Kind von DspWorker erzeugt wird, würde
    // durch worker->moveToThread(thread) mit in den DSP-Thread wandern.
    std::atomic<float> progressPct{0.0f};
    std::atomic<int>   levelPct{0};

signals:
    void progressUpdated(float percent);
    void levelUpdated(int level);
    void finished();

public slots:
    void process();

private:
    bool set_dac_width(const std::string& ip, int bits);
    bool set_dac_rate(const std::string& ip, float rate);
    
    // Bestehende Engines
    QString run_dsp_engine(QString fullPath, int sock);
    QString run_dsp_engine_32INT(QString fullPath, int sock);
    
    // NEU: In-Band Signaling Engine (ohne DSP, Tagging im 16-Bit Word)
    QString run_IQ_engine(QString fullPath, int sock);
    
    // NEU: Hilfsfunktionen für das FPGA Protokoll
    uint32_t calculate_ftw(float frequency, float ref_clk = 50000000.0f);
    uint16_t make_cmd_word(uint8_t ctrl, uint8_t index, uint8_t payload);
};

#endif // DSPWORKER_H
