#include "DspWorker.h"
#include "BinaryStructures.h"

#include <QThread>
#include <QFileInfo>
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <liquid/liquid.h>

// --- BEGIN:LUT for INT32 MATH without liquiddsp ---
#define LUT_BITS 12
#define LUT_SIZE (1 << LUT_BITS)

static int16_t sine_lut[LUT_SIZE];
static int16_t cos_lut[LUT_SIZE];
static bool luts_initialized = false;
// --- END:LUT for INT32 MATH without liquiddsp ---


void DspWorker::process() {
    running = true;
    QString currentFile = filePath;
    
    // HARDWARE-KONFIGURATION (Port 5000)
    // Wenn NICHT die IQ-Engine genutzt wird, setzen wir die Hardware vorab
    // Die IQ-Engine steuert die Hardware dynamisch anhand der Metadaten aus der WAV-Datei!
    if (!checkIQEngine) {
        set_dac_width(targetIP.toStdString(), targetBits);
        set_dac_rate(targetIP.toStdString(), targetRate);
        QThread::msleep(50);
    }

    // DATEN-VERBINDUNG (z.B. Port 1234)
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in srv = { .sin_family = AF_INET, .sin_port = htons(targetPort) };
    inet_pton(AF_INET, targetIP.toStdString().c_str(), &srv.sin_addr);

    if (::connect(sock, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        emit finished();
        return;
    }

    // TCP_NODELAY aktivieren, damit kleine Pakete sofort rausgehen
    int flag = 1;
    ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

    while (!currentFile.isEmpty() && running) {
        // --- Auswahl der Engine ---
        if (checkIQEngine) {
            currentFile = run_IQ_engine(currentFile, sock);  // simples I/Q Streaming, DUC auf SDR in SW oder FPGA realisiert
        } else if (checkDSP32) {
            currentFile = run_dsp_engine_32INT(currentFile, sock);  // COHIRADIAStreamer interer DUC, optimierter für langsamere Rechner
        } else {
            currentFile = run_dsp_engine(currentFile, sock); // COHIRADIAStreamer interner DUC mit liquidDSP, höherer CPU-Verbrauch aber sauberes Spektrum
        }
        
        if (!currentFile.isEmpty()) {
            QFileInfo info(filePath);
            currentFile = info.absolutePath() + "/" + currentFile;
            if (!QFile::exists(currentFile)) break;
        }
    }
    ::close(sock);
    emit finished();
}

// Steuerungsfunktion für Port 5000
bool DspWorker::set_dac_width(const std::string& ip, int bits) {
    int ctrl_sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (ctrl_sock < 0) return false;

    // Timeout für Connect und Senden setzen
    struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 }; // 500ms
    setsockopt(ctrl_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in serv_addr = { .sin_family = AF_INET, .sin_port = htons(5000) };
    
    // IP-Validierung inkl. Socket-Schutz
    if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
        ::close(ctrl_sock);
        return false;
    }

    bool success = false;
    if (::connect(ctrl_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0) {
        int hardwareBits = (bits == 8) ? 8 : 16;
        std::string cmd = "width " + std::to_string(hardwareBits) + "\n";

        ssize_t sent = ::send(ctrl_sock, cmd.c_str(), cmd.length(), 0);
        if (sent == (ssize_t)cmd.length()) {
            success = true;
        }
    }

    ::close(ctrl_sock);
    return success;
}

// Steuerungsfunktion für Port 5000
bool DspWorker::set_dac_rate(const std::string& ip, float rate) {
    int ctrl_sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (ctrl_sock < 0) return false;

    struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 };
    setsockopt(ctrl_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in serv_addr = { .sin_family = AF_INET, .sin_port = htons(5000) };
    if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
        ::close(ctrl_sock);
        return false;
    }

    bool success = false;
    if (::connect(ctrl_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0) {
        // Konvertierung von Hz in MHz (z.B. 12500000 -> 12.5)
        float rate_mhz = rate / 1000000.0f;
        
        // Command erstellen, z.B. "rate 12.5\n"
        // %.1f sorgt für eine Dezimalstelle
        std::string cmd = "rate " + std::to_string(rate_mhz);
        
        // Falls das Backend exakt "5.0" statt "5" braucht, nutzen wir snprintf:
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "rate %.1f\n", rate_mhz);
        std::string finalCmd(buffer);

        ssize_t sent = ::send(ctrl_sock, finalCmd.c_str(), finalCmd.length(), 0);
        if (sent == (ssize_t)finalCmd.length()) {
            success = true;
        }
    }

    ::close(ctrl_sock);
    return success;
}

// INTERNER DUC mit liquidDSP, bestes Spektrum, höhere CPU Last
QString DspWorker::run_dsp_engine(QString fullPath, int sock) {
    std::ifstream file(fullPath.toStdString(), std::ios::binary);
    if (!file) return "";

    RiffHeader riff;
    file.read(reinterpret_cast<char*>(&riff), sizeof(RiffHeader));

    uint32_t sampleRate = 0;
    ChunkHeader chunk;
    QString nextFileFound = "";
    
    // Effektive Bits für die Skalierung (816 Sonderfall abfangen)
    int effectiveBits = (targetBits == 816) ? 8 : targetBits;
    float bitScale = powf(2.0f, effectiveBits - 1) - 1.0f;
    float current_gain = bitScale * 0.65f;
    float peak_hold = 0.1f;

    while (file.read(reinterpret_cast<char*>(&chunk), sizeof(ChunkHeader))) {
        if (!running) break;
        std::string tag(chunk.id, 4);

        if (tag == "fmt ") {
            FmtStruct fmt;
            file.read(reinterpret_cast<char*>(&fmt), sizeof(FmtStruct));
            sampleRate = fmt.sampleRate;
            file.seekg(chunk.size - sizeof(FmtStruct), std::ios::cur);
        }
        else if (tag == "auxi") {
            AuxiContent aux;
            file.read(reinterpret_cast<char*>(&aux), sizeof(AuxiContent));
            std::string rawName(aux.filename, 96);
            size_t last = rawName.find_last_not_of(" \t\n\r\0\x01", std::string::npos, 6);
            if (last != std::string::npos) nextFileFound = QString::fromStdString(rawName.substr(0, last + 1));
            file.seekg(chunk.size - sizeof(AuxiContent), std::ios::cur);
        }
        else if (tag == "data") {
            float upRate = targetRate / (float)sampleRate;
            msresamp_crcf resamp = msresamp_crcf_create(upRate, 60.0f);
            nco_crcf vco = nco_crcf_create(LIQUID_VCO);
            
            float freq_rad = 2.0f * M_PI * (manualShiftFreq / targetRate);
            nco_crcf_set_frequency(vco, freq_rad);

            const size_t blockSize = 1024;
            std::vector<int16_t> readBuf(blockSize * 2);
            size_t outSize = (size_t)(blockSize * upRate) + 512;
            std::vector<liquid_float_complex> x(blockSize), y(outSize);
            std::vector<int16_t> netBuf(outSize);
            std::vector<int8_t> nb8(outSize);

            uint32_t dataSize = chunk.size;
            uint32_t bytesRead = 0;

            while (file.read(reinterpret_cast<char*>(readBuf.data()), blockSize * 4) && running) {
                bytesRead += blockSize * 4;
                float block_peak = 0.0001f;
                for (int i = 0; i < blockSize; i++) {
                    x[i] = { (float)readBuf[2*i] / 32768.0f, (float)readBuf[2*i+1] / 32768.0f };
                    float mag = sqrtf(x[i].real*x[i].real + x[i].imag*x[i].imag);
                    if (mag > block_peak) block_peak = mag;
                }
                
                peak_hold = 0.95f * peak_hold + 0.05f * block_peak;
                if (useAGC) current_gain = 0.98f * current_gain + 0.02f * ((bitScale * 0.65f) / (peak_hold + 0.0001f));
                else current_gain = bitScale * manualGainValue;

                emit progressUpdated((float)bytesRead / dataSize * 100.0f);
                emit levelUpdated(qBound(0, (int)(peak_hold * 100.0f), 100));

                unsigned int nw;
                msresamp_crcf_execute(resamp, x.data(), blockSize, y.data(), &nw);

                for (unsigned int j = 0; j < nw; j++) {
                    float c = nco_crcf_cos(vco), s = nco_crcf_sin(vco); nco_crcf_step(vco);
                    float hf = (y[j].real * c - y[j].imag * s) * (current_gain * 0.24f);
                    if (hf > bitScale) hf = bitScale; else if (hf < -bitScale) hf = -bitScale;
                    netBuf[j] = (int16_t)hf;
                }

                ssize_t sentBytes = 0;
                if (targetBits == 8) {
                    for(unsigned int k=0; k<nw; k++) 
                        if (useOffset) {
                            //nb8[k] = (uint8_t)(netBuf[k] + 128);
                            nb8[k] = static_cast<uint8_t>(static_cast<int8_t>(netBuf[k]) + 128);
                        } else {
                            //nb8[k] = (int8_t)netBuf[k];
                            nb8[k] = static_cast<int8_t>(netBuf[k]);
                        }
                    sentBytes = ::send(sock, nb8.data(), nw, MSG_NOSIGNAL);
                } else {
                    sentBytes = ::send(sock, netBuf.data(), nw * 2, MSG_NOSIGNAL);
                }

                if (sentBytes <= 0) {
                    running = false; // Beendet die Schleife im run_dsp_engine
                    break;           // Springt aus der aktuellen data-Schleife
                }
            }
            msresamp_crcf_destroy(resamp);
            nco_crcf_destroy(vco);
            break;
        } else file.seekg(chunk.size, std::ios::cur);
        if (chunk.size % 2 != 0) file.seekg(1, std::ios::cur);
    }
    return nextFileFound;
}

// INTERNER DUC optimiert nur auf INT32, für ältere Rechner besser geeignet als liquidDSP Version
QString DspWorker::run_dsp_engine_32INT(QString fullPath, int sock) {
    std::ifstream file(fullPath.toStdString(), std::ios::binary);
    if (!file) return "";

    // 1. Look-Up-Tables für Sinus/Cosinus (falls noch nicht geschehen)
    if (!luts_initialized) {
        for(int i = 0; i < LUT_SIZE; i++) {
            double angle = 2.0 * M_PI * i / LUT_SIZE;
            sine_lut[i] = (int16_t)(round(sin(angle) * 32767.0));
            cos_lut[i]  = (int16_t)(round(cos(angle) * 32767.0));
        }
        luts_initialized = true;
    }

    RiffHeader riff;
    file.read(reinterpret_cast<char*>(&riff), sizeof(RiffHeader));

    uint32_t sampleRate = 0;
    ChunkHeader chunk;
    QString nextFileFound = "";

    while (file.read(reinterpret_cast<char*>(&chunk), sizeof(ChunkHeader))) {
        if (!running) break;
        std::string tag(chunk.id, 4);

        if (tag == "fmt ") {
            FmtStruct fmt;
            file.read(reinterpret_cast<char*>(&fmt), sizeof(FmtStruct));
            sampleRate = fmt.sampleRate;
            file.seekg(chunk.size - sizeof(FmtStruct), std::ios::cur);
        }
        else if (tag == "auxi") {
            AuxiContent aux;
            file.read(reinterpret_cast<char*>(&aux), sizeof(AuxiContent));
            std::string rawName(aux.filename, 96);
            size_t last = rawName.find_last_not_of(" \t\n\r\0\x01", std::string::npos, 6);
            if (last != std::string::npos) nextFileFound = QString::fromStdString(rawName.substr(0, last + 1));
            file.seekg(chunk.size - sizeof(AuxiContent), std::ios::cur);
        }
        else if (tag == "data") {
            // --- TIMING & PHASEN PARAMETER ---
            // Wie viele RF-Samples entsprechen einem IQ-Sample? (z.B. 12.5M / 250k = 50.0)
            double samples_per_iq_wav = (double)targetRate / (double)sampleRate;
            double iq_wav_phase = 0.0; 

            // NCO Increment bezogen auf die Zielrate (für exakte Frequenzlage)
            uint32_t phase_inc_nco = (uint32_t)((manualShiftFreq / targetRate) * 4294967296.0);
            uint32_t phase_nco = 0;

            // --- FILTER & GAIN ---
            int32_t lastI = 0, lastQ = 0;
            // Koeffizienten für einen LP (Summe = 128)
            const int32_t coeffs[7] = { 4, 16, 26, 36, 26, 16, 4 }; 
            int32_t i_history[7] = {0, 0, 0, 0, 0, 0, 0};
            int32_t q_history[7] = {0, 0, 0, 0, 0, 0, 0};

            int effectiveBits = (targetBits == 816) ? 8 : targetBits;
            float bitScale = powf(2.0f, effectiveBits - 1) - 1.0f;
            float current_gain = 1.0f;
            float peak_hold = 0.1f;

            // --- PUFFER ---
            const size_t blockSize = 1024; // Samples aus der WAV
            std::vector<int16_t> readBuf(blockSize * 2);
            
            // Maximale Ausgabegröße berechnen (mit Puffer für Rundungen)
            size_t outSamplesMax = (size_t)(blockSize * samples_per_iq_wav) + 256;
            std::vector<int16_t> netBuf16(outSamplesMax);
            std::vector<uint8_t> netBuf8(outSamplesMax);

            uint32_t dataSize = chunk.size;
            uint32_t totalBytesRead = 0;
            int gui_throttle = 0;

            while (file.read(reinterpret_cast<char*>(readBuf.data()), blockSize * 4) && running) {
                totalBytesRead += blockSize * 4;
                uint32_t out_idx = 0;

                // 1. AGC Peak-Bestimmung (Block-basiert)
                float b_peak = 0.0001f;
                for (size_t i = 0; i < blockSize; i++) {
                    float m = sqrtf(powf(readBuf[2*i]/32768.0f, 2) + powf(readBuf[2*i+1]/32768.0f, 2));
                    if (m > b_peak) b_peak = m;
                }
                peak_hold = 0.95f * peak_hold + 0.05f * b_peak;

                // 2. GUI Update Signale (gedrosselt auf ~50Hz Update-Rate)
                if (gui_throttle++ % 20 == 0) {
                    emit progressUpdated((float)totalBytesRead / (float)dataSize * 100.0f);
                    emit levelUpdated(std::max(0, std::min(100, (int)(peak_hold * 100.0f))));
                }

                // 3. Gain-Berechnung (AGC an/aus)
                if (useAGC) {
                    float target_g = (bitScale * 0.70f) / (peak_hold + 0.001f);
                    current_gain = 0.98f * current_gain + 0.02f * target_g;
                } else {
                    current_gain = bitScale * manualGainValue;
                }
                int32_t g_int = (int32_t)current_gain;

                // 4. CORE DSP SCHLEIFE (Upsampling + NCO Mix + FIR)
                for (size_t i = 0; i < blockSize; i++) {
                    int32_t nextI = readBuf[2*i];
                    int32_t nextQ = readBuf[2*i+1];

                    // Wir füllen den zeitlichen Raum eines IQ-Samples mit RF-Samples
                    while (iq_wav_phase < samples_per_iq_wav) {
                        
                        // Interpolationsfaktor mu (0.0 bis 1.0)
                        float mu = (float)(iq_wav_phase / samples_per_iq_wav);
                        // Cosinus-Glättung des Basisbands
                        float mu2 = (1.0f - cosf(mu * M_PI)) / 2.0f;
                        
                        int32_t currI = (int32_t)(lastI * (1.0f - mu2) + nextI * mu2);
                        int32_t currQ = (int32_t)(lastQ * (1.0f - mu2) + nextQ * mu2);


                        // FIR Glättungs-Filter auf das BASISBAND (I und Q separat)/
                        for (int k=0; k<6; k++) {
                            i_history[k] = i_history[k+1];
                            q_history[k] = q_history[k+1];
                        }
                        i_history[6] = currI;
                        q_history[6] = currQ;

                        int32_t filt_I = (i_history[0]*coeffs[0] + i_history[1]*coeffs[1] + 
                                          i_history[2]*coeffs[2] + i_history[3]*coeffs[3] + 
                                          i_history[4]*coeffs[4] + i_history[5]*coeffs[5] + 
                                          i_history[6]*coeffs[6]) >> 7;

                        int32_t filt_Q = (q_history[0]*coeffs[0] + q_history[1]*coeffs[1] + 
                                          q_history[2]*coeffs[2] + q_history[3]*coeffs[3] + 
                                          q_history[4]*coeffs[4] + q_history[5]*coeffs[5] + 
                                          q_history[6]*coeffs[6]) >> 7;

                        // NCO Mischer (Phase rückt pro Ausgangs-Sample vor)
                        phase_nco += phase_inc_nco;
                        uint32_t lut_idx = phase_nco >> (32 - LUT_BITS);
                        int32_t c = cos_lut[lut_idx];
                        int32_t s = sine_lut[lut_idx];

                        // Komplexer Frequenz-Shift: I*cos - Q*sin mit gefiltertem I und Q
                        int32_t mixed = ((filt_I * c) - (filt_Q * s)) >> 15;
                        int32_t smoothed = (mixed * g_int) >> 15;

                        // Clipping & Format-Wandlung
                        if (smoothed > bitScale) smoothed = bitScale;
                        else if (smoothed < -bitScale) smoothed = -bitScale;

                        if (targetBits == 8) {
                            netBuf8[out_idx++] = useOffset ? (uint8_t)(smoothed + 128) : (int8_t)smoothed;
                        } else {
                            netBuf16[out_idx++] = (int16_t)smoothed;
                        }

                        iq_wav_phase += 1.0; // Ein RF-Schritt weiter
                    }

                    // Phase für das nächste WAV-Sample korrigieren (Übertrag behalten)
                    iq_wav_phase -= samples_per_iq_wav;
                    lastI = nextI;
                    lastQ = nextQ;
                }

                // 5. Senden über Socket
                if (out_idx > 0) {
                    ssize_t s_ret;
                    if (targetBits == 8) s_ret = ::send(sock, netBuf8.data(), out_idx, MSG_NOSIGNAL);
                    else                 s_ret = ::send(sock, netBuf16.data(), out_idx * 2, MSG_NOSIGNAL);
                    
                    if (s_ret <= 0) { running = false; break; }
                }
            }
            break;
        } else {
            file.seekg(chunk.size, std::ios::cur);
        }
    }
    return nextFileFound;
}


// =====================================================================================
// NEU: FPGA IN-BAND SIGNALING HILFSFUNKTIONEN
// =====================================================================================

uint32_t DspWorker::calculate_ftw(float frequency, float ref_clk) {
    // Phase Tuning Word (FTW) Berechnung für den 50 MHz FPGA-Takt
    // Formel: (Frequenz / Takt) * 2^32
    double ftw = (static_cast<double>(frequency) / static_cast<double>(ref_clk)) * 4294967296.0;
    return static_cast<uint32_t>(round(ftw));
}

uint16_t DspWorker::make_cmd_word(uint8_t ctrl, uint8_t index, uint8_t payload) {
    // Protokoll-Aufbau: Bit [15:14]=ctrl, Bit [13:8]=index, Bit [7:0]=payload
    return ((ctrl & 0x03) << 14) | ((index & 0x3F) << 8) | (payload & 0xFF);
}

// =====================================================================================
// NEW: I/Q ENGINE MIT IN-BAND SIGNALING (DSP-Upsampling/Mischung muss im FPGA erfolgen)
// =====================================================================================
QString DspWorker::run_IQ_engine(QString fullPath, int sock) {
    std::ifstream file(fullPath.toStdString(), std::ios::binary);
    if (!file) return "";

    RiffHeader riff;
    file.read(reinterpret_cast<char*>(&riff), sizeof(RiffHeader));

    uint32_t sampleRate = 0;
    ChunkHeader chunk;
    QString nextFileFound = "";
    bool hardwareInitialized = false;

    while (file.read(reinterpret_cast<char*>(&chunk), sizeof(ChunkHeader))) {
        if (!running) break;
        std::string tag(chunk.id, 4);

        if (tag == "fmt ") {
            FmtStruct fmt;
            file.read(reinterpret_cast<char*>(&fmt), sizeof(FmtStruct));
            sampleRate = fmt.sampleRate;
            file.seekg(chunk.size - sizeof(FmtStruct), std::ios::cur);
        }
        else if (tag == "auxi") {
            AuxiContent aux;
            file.read(reinterpret_cast<char*>(&aux), sizeof(AuxiContent));
            std::string rawName(aux.filename, 96);
            size_t last = rawName.find_last_not_of(" \t\n\r\0\x01", std::string::npos, 6);
            if (last != std::string::npos) nextFileFound = QString::fromStdString(rawName.substr(0, last + 1));
            file.seekg(chunk.size - sizeof(AuxiContent), std::ios::cur);
        }
        else if (tag == "data") {
            
            // HARDWARE KONFIGURATION (Control-Port 5000)
            if (!hardwareInitialized) {
                // Das FPGA muss als 16-Bit (Native) konfiguriert werden
                set_dac_width(targetIP.toStdString(), 16);
                
                // WICHTIG: Die Bus-Rate muss DOPPELT so hoch sein wie die IQ_WAV-Samplerate, 
                // da I und Q hintereinander (interleaved) über denselben 16-Bit Bus wandern.
                float busRate = static_cast<float>(sampleRate) * 2.0f;
                set_dac_rate(targetIP.toStdString(), busRate);
                
                QThread::msleep(10); // Kurz warten, bis Backend die Rate übernommen hat
                
                // IN-BAND BEFEHLSSEQUENZ SENDEN (Data-Port 1234)
                uint32_t target_ftw = calculate_ftw(manualShiftFreq); // cf aus der GUI / Dateiname
                uint32_t target_rate = calculate_ftw((float)sampleRate);

                std::vector<uint16_t> init_cmds;
                
                // --- Befehl 1: NCO Frequenz-Shift ('S') ---
                init_cmds.push_back(make_cmd_word(2, 63, 'S')); // INIT Cmd
                init_cmds.push_back(make_cmd_word(2,  0, (target_ftw & 0xFF)));
                init_cmds.push_back(make_cmd_word(2,  1, ((target_ftw >> 8) & 0xFF)));
                init_cmds.push_back(make_cmd_word(2,  2, ((target_ftw >> 16) & 0xFF)));
                init_cmds.push_back(make_cmd_word(2,  3, ((target_ftw >> 24) & 0xFF)));
                init_cmds.push_back(make_cmd_word(3,  0, 'E')); // EXECUTE

                // --- Befehl 2: Eingangs-Samplerate ('R') ---
                init_cmds.push_back(make_cmd_word(2, 63, 'R')); // INIT Cmd
                init_cmds.push_back(make_cmd_word(2,  0, (target_rate & 0xFF)));
                init_cmds.push_back(make_cmd_word(2,  1, ((target_rate >> 8) & 0xFF)));
                init_cmds.push_back(make_cmd_word(2,  2, ((target_rate >> 16) & 0xFF)));
                init_cmds.push_back(make_cmd_word(2,  3, ((target_rate >> 24) & 0xFF)));
                init_cmds.push_back(make_cmd_word(3,  0, 'E')); // EXECUTE

                // Sende Präfix-Befehle
                ::send(sock, init_cmds.data(), init_cmds.size() * sizeof(uint16_t), MSG_NOSIGNAL);
                hardwareInitialized = true;
            }

            // I/Q DATEN STREAMING
            const size_t blockSize = 1024; // Samples aus der WAV
            std::vector<int16_t> readBuf(blockSize * 2); // 16-Bit interleaved von der Disk
            std::vector<uint16_t> netBuf(blockSize * 2); // 16-Bit unsigned für Bit-Maskierung ins Netzwerk

            uint32_t dataSize = chunk.size;
            uint32_t bytesRead = 0;
            int gui_throttle = 0;
            float peak_hold = 0.1f;

            while (file.read(reinterpret_cast<char*>(readBuf.data()), blockSize * 4) && running) {
                bytesRead += blockSize * 4;
                float b_peak = 0.0001f;

                for (size_t i = 0; i < blockSize; i++) {
                    int16_t raw_i = readBuf[2*i];
                    int16_t raw_q = readBuf[2*i+1];

                    // --- Peak-Ermittlung für GUI ---
                    float m = sqrtf(powf(raw_i/32768.0f, 2) + powf(raw_q/32768.0f, 2));
                    if (m > b_peak) b_peak = m;

                    // --- Format-Anpassung ---
                    // 16-Bit Signed durch Arithmetic Right Shift auf 14-Bit reduzieren 
                    // (Das Vorzeichen bleibt durch den Arithmetic Shift erhalten)
                    int16_t scaled_i = raw_i >> 2;
                    int16_t scaled_q = raw_q >> 2;

                    // Untere 14 Bit maskieren (Löscht Einsen im MSB-Bereich, die durch 
                    // Sign-Extension entstanden sein könnten)
                    uint16_t pure_i = (uint16_t)scaled_i & 0x3FFF;
                    uint16_t pure_q = (uint16_t)scaled_q & 0x3FFF;

                    // --- In-Band Tagging ---
                    // I-Sample (Bit 15:14 = 00)
                    netBuf[2*i]   = pure_i | (0x00 << 14);
                    
                    // Q-Sample (Bit 15:14 = 01)
                    netBuf[2*i+1] = pure_q | (0x01 << 14);
                }

                // GUI Updates (Level / Progress)
                peak_hold = 0.95f * peak_hold + 0.05f * b_peak;
                if (gui_throttle++ % 20 == 0) {
                    emit progressUpdated((float)bytesRead / dataSize * 100.0f);
                    emit levelUpdated(qBound(0, (int)(peak_hold * 100.0f), 100));
                }

                // Senden der fertig getaggten 16-Bit I/Q Daten über den Socket
                ssize_t sentBytes = ::send(sock, netBuf.data(), blockSize * 4, MSG_NOSIGNAL);
                if (sentBytes <= 0) {
                    running = false; 
                    break;           
                }
            }
            break;
        } 
        else {
            file.seekg(chunk.size, std::ios::cur);
        }
        
        // Chunk Padding
        if (chunk.size % 2 != 0) file.seekg(1, std::ios::cur);
    }
    return nextFileFound;
}
