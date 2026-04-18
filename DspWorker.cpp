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

void DspWorker::process() {
    running = true;
    QString currentFile = filePath;
    
    // 1. HARDWARE-KONFIGURATION (Port 5000)
    set_dac_width(targetIP.toStdString(), targetBits);
    set_dac_rate(targetIP.toStdString(), targetRate);
    QThread::msleep(50);

    // 2. DATEN-VERBINDUNG (z.B. Port 1234)
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
        currentFile = run_dsp_engine(currentFile, sock);
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
