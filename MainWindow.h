#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QModelIndex>
#include <QString>

// Vorwärtsdeklarationen für Qt-Klassen
class QFileSystemModel;
class QTreeView;
class QLabel;
class QWidget;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QProgressBar;
class QPushButton;
class QTimer;
class DspWorker;

// --- MAIN WINDOW ---
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

private slots:
    void updateLevelBar(int val);
    void updateInfrastrukturCheck();
    void onOffsetToggled(bool checked);
    void onFileSelected(const QModelIndex &index);
    void startStreaming();
    void stopStreaming();
    void onAgcToggled(bool checked);
    void onGainChanged(const QString &text);

    // NEU: laeuft im GUI-Thread (Timer wird in MainWindow erzeugt, NICHT
    // als Kind von DspWorker - siehe Kommentar in startStreaming()), liest
    // die atomaren Telemetrie-Werte des Workers periodisch aus und
    // aktualisiert progress/levelBar. Entkoppelt die GUI-Updates
    // vollstaendig vom Timing der DSP/Sende-Schleife.
    void pollWorkerTelemetry();

private:
    QFileSystemModel *fileModel; 
    QTreeView *treeView; 
    QLabel *infoBox;
    QWidget *settingsContainer;
    QLineEdit *editIP, *editPort, *editShift, *editManualGain; 
    QComboBox *comboRate, *comboBits; 
    QCheckBox *checkAGC;
    QProgressBar *progress, *levelBar; 
    QPushButton *btnPlay, *btnStop;
    QString selectedFile; 
    DspWorker *currentWorker = nullptr;
    QLabel *statusLabel;

    // NEU: Poll-Timer fuer Worker-Telemetrie, lebt im GUI-Thread (Parent =
    // this = MainWindow), ist NIEMALS von moveToThread() des Workers
    // betroffen, da er kein Kind von DspWorker ist.
    QTimer *telemetryTimer = nullptr;
    
    // Bestehende Checkboxen
    QCheckBox *checkOffset;
    QCheckBox *checkDSP32;
    
    // Checkbox für die direkte FPGA In-Band Signaling Engine
    QCheckBox *checkIQEngine; 

    uint32_t currentFileSampleRate = 0;
};

#endif // MAINWINDOW_H