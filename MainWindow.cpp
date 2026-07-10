#include "MainWindow.h"
#include "DspWorker.h"
#include "BinaryStructures.h"

#include <QTreeView>
#include <QFileSystemModel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QThread>
#include <QHeaderView>
#include <QCheckBox>
#include <fstream>

MainWindow::MainWindow() {
    QWidget *central = new QWidget();
    QHBoxLayout *mainLayout = new QHBoxLayout(central);
    
    fileModel = new QFileSystemModel(this);
    fileModel->setRootPath("");
    fileModel->setNameFilters(QStringList() << "*.wav");
    fileModel->setNameFilterDisables(false);

    treeView = new QTreeView();
    treeView->setModel(fileModel);
    treeView->setColumnHidden(1, true); 
    treeView->setColumnHidden(2, true); 
    treeView->setColumnHidden(3, true);
    mainLayout->addWidget(treeView, 1);

    QVBoxLayout *rightLayout = new QVBoxLayout();
    infoBox = new QLabel("choose COHIRADIA WAV file...");
    infoBox->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    infoBox->setMinimumHeight(80);
    rightLayout->addWidget(new QLabel("<b>Fileinfo:</b>"));
    rightLayout->addWidget(infoBox);

    settingsContainer = new QWidget();
    QFormLayout *settingsForm = new QFormLayout(settingsContainer);

    editIP = new QLineEdit("127.0.0.1");
    editPort = new QLineEdit("1234");

    comboRate = new QComboBox();
    comboRate->addItem("5 MSPS (SMISDR, parlioSDR)", 5000000.0f);
    comboRate->addItem("10.0 MSPS (fl2k, parlioSDR)", 10000000.0f);
    comboRate->addItem("12.5 MSPS (SMISDR)", 12500000.0f);
    comboRate->addItem("15.625 MSPS (SMISDR)", 15625000.0f);
    comboRate->addItem("25 MSPS (SMISDR)", 25000000.0f);
    
    editShift = new QLineEdit("0");
    
    comboBits = new QComboBox();
    comboBits->addItem("8 Bit (Native)", 8);
    comboBits->addItem("8 Bit (in 16 Bit)", 816);
    comboBits->addItem("10 Bit", 10);
    comboBits->addItem("12 Bit", 12);
    comboBits->addItem("14 Bit", 14);
    comboBits->addItem("16 Bit", 16);
    comboBits->setCurrentIndex(2);

    checkAGC = new QCheckBox("AGC Aktiv");
    checkAGC->setChecked(true);
    editManualGain = new QLineEdit("65");
    editManualGain->setEnabled(false);
    connect(checkAGC, &QCheckBox::toggled, editManualGain, &QLineEdit::setDisabled);

    settingsForm->addRow("IP:", editIP);
    settingsForm->addRow("Port:", editPort);
    settingsForm->addRow("Samplerate:", comboRate);
    settingsForm->addRow("Shift (Hz):", editShift);
    settingsForm->addRow("DAC Bits:", comboBits);
    
    QHBoxLayout *agcLayout = new QHBoxLayout();
    agcLayout->addWidget(checkAGC);
    agcLayout->addWidget(new QLabel("Gain %:"));
    agcLayout->addWidget(editManualGain);
    settingsForm->addRow("Amp:", agcLayout);

    checkOffset = new QCheckBox("DC Offset");
    checkOffset->setChecked(false); //  default on SMI: off
    settingsForm->addRow("Options:", checkOffset);

    checkDSP32 = new QCheckBox("use INT32-DSP instead liquiddsp [for slower CPUs]");
    checkDSP32->setChecked(false); //  default off
    settingsForm->addRow("DSP-Machine:",checkDSP32);

    //Checkbox für das FPGA In-Band Signaling
    checkIQEngine = new QCheckBox("SW or FPGA DUC (16 Bit with In-Band Signaling)");
    checkIQEngine->setChecked(false); //  default off
    settingsForm->addRow("I/Q Mode:", checkIQEngine);
    
    rightLayout->addWidget(settingsContainer);

    statusLabel = new QLabel("Bandwidth Check");
    statusLabel->setWordWrap(true);
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setStyleSheet("font-weight: bold; padding: 5px; border: 1px solid gray;");

    rightLayout->addWidget(new QLabel("<b>Network Compatibility:</b>"));
    rightLayout->addWidget(statusLabel);

    levelBar = new QProgressBar();
    levelBar->setRange(0, 100);
    levelBar->setTextVisible(false);
    levelBar->setFixedHeight(15);
    rightLayout->addWidget(new QLabel("<b>AGC Level:</b>"));
    rightLayout->addWidget(levelBar);

    progress = new QProgressBar();
    rightLayout->addWidget(new QLabel("<b>File progress:</b>"));
    rightLayout->addWidget(progress);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnPlay = new QPushButton("PLAY"); btnStop = new QPushButton("STOP");
    btnStop->setEnabled(false);
    btnLayout->addWidget(btnPlay); btnLayout->addWidget(btnStop);
    rightLayout->addLayout(btnLayout);

    mainLayout->addLayout(rightLayout, 1);
    setCentralWidget(central);
    resize(950, 550);
    
    // Wenn die INT32-Engine gewählt wird, darf In-Band Signaling nicht aktiv sein
    connect(checkDSP32, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            checkIQEngine->setChecked(false);
        }
        updateInfrastrukturCheck();
    });

    // Wenn die FPGA In-Band Engine gewählt wird, deaktivieren wir die INT32-Engine
    // Zudem grauen wir Samplerate und Bits aus, da diese nativ aus der Datei bestimmt werden!
    connect(checkIQEngine, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            checkDSP32->setChecked(false);
            comboRate->setEnabled(false);
            comboBits->setEnabled(false);
        } else {
            comboRate->setEnabled(true);
            comboBits->setEnabled(true);
        }
        updateInfrastrukturCheck();
    });


    connect(treeView, &QTreeView::clicked, this, &MainWindow::onFileSelected);
    connect(btnPlay, &QPushButton::clicked, this, &MainWindow::startStreaming);
    connect(btnStop, &QPushButton::clicked, this, &MainWindow::stopStreaming);

    connect(checkAGC, &QCheckBox::toggled, this, &MainWindow::onAgcToggled);
    connect(editManualGain, &QLineEdit::textChanged, this, &MainWindow::onGainChanged);

    connect(checkOffset, &QCheckBox::toggled, this, &MainWindow::onOffsetToggled);
 
    connect(comboRate, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::updateInfrastrukturCheck);
    connect(comboBits, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::updateInfrastrukturCheck);
    updateInfrastrukturCheck();
}

void MainWindow::updateLevelBar(int val) {
    levelBar->setValue(val);
    if (val > 90) levelBar->setStyleSheet("QProgressBar::chunk { background-color: red; }");
    else if (val > 70) levelBar->setStyleSheet("QProgressBar::chunk { background-color: yellow; }");
    else levelBar->setStyleSheet("QProgressBar::chunk { background-color: #00FF00; }");
}

void MainWindow::updateInfrastrukturCheck() {
    float rate = comboRate->currentData().toFloat();
    QString rateText = comboRate->currentText();

    // Auch bei 10, 12, 14 Bit oder "8 in 16" wird ein 16-Bit Slot (2 Bytes) übertragen.
    // Nur im "echten" 8-Bit Modus (Native) wird tatsächlich nur 1 Byte übertragen.   
    int transmissionBits = 16; 
    int selectedBits = comboBits->currentData().toInt();
    
    if (selectedBits == 8) {
        transmissionBits = 8; // Nur hier halbiert sich die Rate auf dem Kabel
    }

        if (checkIQEngine && checkIQEngine->isChecked()) {
        rate = static_cast<float>(currentFileSampleRate);
        rate = 2*rate; //  I und Q zu je 16 Bit, also interleaved
        transmissionBits = 16; // In-Band Signaling läuft mit 16 Bit
    }

    // Berechnung: Samplerate * Bits pro Sample / 1 Million = MBit/s
    float mbit = (rate * transmissionBits) / 1000000.0f;

    auto getSpan = [](const QString& name, bool green, bool yellow) {
        QString color = "red";
        if (green) color = "#00FF00";
        else if (yellow) color = "yellow";
        return QString("<span style='color:%1;'>%2</span>").arg(color, name);
    };

    // Grenzwerte (angepasst an realen TCP/IP Overhead)
    QString eth = getSpan("Ethernet (100)", (mbit < 85), (mbit >= 85 && mbit < 96));
    QString usb = getSpan("USB2", (mbit < 280), (mbit >= 280 && mbit < 420));
    QString gbe = getSpan("Gigabit / USB3", (mbit < 850), (mbit >= 850 && mbit < 960));
    QString fib = getSpan("Fiber", true, false);

    statusLabel->setText(QString("%1  -  %2  -  %3  -  %4<br>"
                                 "<small>Required Throughput: <b>%5 MBit/s</b></small>")
                         .arg(eth, usb, gbe, fib).arg(mbit, 0, 'f', 1));
}


void MainWindow::onFileSelected(const QModelIndex &index) {
    if (btnStop->isEnabled()) return; // Während Play keine neue Datei wählen
    QString path = fileModel->filePath(index);
    if (!path.endsWith(".wav")) return;
    std::ifstream file(path.toStdString(), std::ios::binary);
    RiffHeader riff; file.read((char*)&riff, sizeof(RiffHeader));
    uint32_t sRate = 0; QString nextFile = "Keine"; ChunkHeader chunk;
    while(file.read((char*)&chunk, sizeof(ChunkHeader))) {
        std::string tag(chunk.id, 4);
        if(tag == "fmt ") {
            FmtStruct fmt; file.read((char*)&fmt, sizeof(FmtStruct));
            sRate = fmt.sampleRate; file.seekg(chunk.size - sizeof(FmtStruct), std::ios::cur);
        } else if(tag == "auxi") {
            AuxiContent aux; file.read((char*)&aux, sizeof(AuxiContent));
            nextFile = QString::fromLatin1(aux.filename).trimmed();
            file.seekg(chunk.size - sizeof(AuxiContent), std::ios::cur);
        } else file.seekg(chunk.size, std::ios::cur);
    }

    currentFileSampleRate = sRate; //Samplerate für spätere Berechnungen in der Klasse merken
    float cf = 0;
    size_t khzPos = path.toStdString().find("kHz");
    if (khzPos != std::string::npos) {
        size_t start = path.toStdString().find_last_of("_ ", khzPos);
        start = (start == std::string::npos) ? 0 : start + 1;
        cf = std::stof(path.toStdString().substr(start, khzPos - start)) * 1000.0f;
    }
    infoBox->setText(QString("Rate: %1 Hz | Shift: %2 Hz\nNext: %3").arg(sRate).arg(cf).arg(nextFile));
    editShift->setText(QString::number(cf));
    selectedFile = path;
   
    updateInfrastrukturCheck();
}

void MainWindow::startStreaming() {
    if (selectedFile.isEmpty()) return;
    
    // UI Sperren
    treeView->setEnabled(false);
    btnPlay->setEnabled(false);
    btnStop->setEnabled(true);
    editIP->setEnabled(false);
    editPort->setEnabled(false);
    comboRate->setEnabled(false);
    comboBits->setEnabled(false);
    editShift->setEnabled(false);
    checkDSP32->setEnabled(false);
    checkIQEngine->setEnabled(false);

    // AGC und Gain bleiben ENABLED!
    checkAGC->setEnabled(true); 
    editManualGain->setEnabled(true);

    QThread *thread = new QThread();
    DspWorker *worker = new DspWorker();

    // Parameter an den Worker übergeben
    worker->filePath = selectedFile;
    worker->targetIP = editIP->text();
    worker->targetPort = editPort->text().toInt();
    worker->targetBits = comboBits->currentData().toInt();
    worker->manualShiftFreq = editShift->text().toFloat();
    worker->useAGC = checkAGC->isChecked();
    worker->useOffset = checkOffset->isChecked();
    worker->checkDSP32 = checkDSP32->isChecked();
    worker->checkIQEngine = checkIQEngine->isChecked(); // NEU: Aktiviert run_IQ_engine
    worker->manualGainValue = editManualGain->text().toFloat() / 100.0f;
    

    QString rateText = comboRate->currentText();
    worker->targetRate = comboRate->currentData().toFloat();

    // Worker in den Thread verschieben
    worker->moveToThread(thread);

    // Signal-Slot Verbindungen für Thread-Steuerung und Telemetrie
    connect(thread, &QThread::started, worker, &DspWorker::process);
    connect(worker, &DspWorker::finished, thread, &QThread::quit);
    connect(worker, &DspWorker::finished, worker, &DspWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    connect(worker, &DspWorker::progressUpdated, progress, &QProgressBar::setValue);
    connect(worker, &DspWorker::levelUpdated, this, &MainWindow::updateLevelBar);

    // Clean-Up beim Beenden des Streamings
    connect(worker, &DspWorker::finished, this, [this](){ 

       // UI wieder freigeben
       editIP->setEnabled(true);
       editPort->setEnabled(true);
       comboRate->setEnabled(true);
       comboBits->setEnabled(true);
       editShift->setEnabled(true);
       checkDSP32->setEnabled(true);
       checkIQEngine->setEnabled(true);
       // Ausgegrauter Zustand der Comboboxen nach Stop wiederherstellen, 
       // falls In-Band Engine weiterhin selektiert ist
       if (checkIQEngine->isChecked()) {
          comboRate->setEnabled(false);
          comboBits->setEnabled(false);
       }

       // UI-Elemente
       treeView->setEnabled(true);
       btnPlay->setEnabled(true); 
       btnStop->setEnabled(false); 

       levelBar->setValue(0);
       progress->setValue(0); // Fortschrittsbalken auch zurücksetzen
    });

    currentWorker = worker;
    thread->start();
}

void MainWindow::stopStreaming() { 
    if (currentWorker) {
        currentWorker->running = false; 
    }
}

void MainWindow::onOffsetToggled(bool checked) {
    if (currentWorker) {
        currentWorker->useOffset = checked;
        currentWorker->checkDSP32 = checked;
    }
}

void MainWindow::onAgcToggled(bool checked) {
     if (currentWorker) currentWorker->useAGC = checked;
}

void MainWindow::onGainChanged(const QString &text) {
     if (currentWorker) {
         float val = text.toFloat() / 100.0f;
         currentWorker->manualGainValue = qBound(0.0f, val, 1.0f);
     }
}
