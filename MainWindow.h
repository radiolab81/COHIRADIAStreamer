#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QModelIndex>
#include <QString>

// Vorwärtsdeklarationen
class QFileSystemModel;
class QTreeView;
class QLabel;
class QWidget;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QProgressBar;
class QPushButton;
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

private:
    QFileSystemModel *fileModel; QTreeView *treeView; QLabel *infoBox;
    QWidget *settingsContainer;
    QLineEdit *editIP, *editPort, *editShift, *editManualGain; 
    QComboBox *comboRate, *comboBits; QCheckBox *checkAGC;
    QProgressBar *progress, *levelBar; QPushButton *btnPlay, *btnStop;
    QString selectedFile; DspWorker *currentWorker = nullptr;
    QLabel *statusLabel;
    QCheckBox *checkOffset, *checkDSP32;
};

#endif // MAINWINDOW_H