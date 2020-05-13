#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QMainWindow>
#include <atomic>
#include <memory>
#include <thread>

#include "firdn.hpp"

struct ReaderConfig {
	int deviceNumber, channelOffset;
	enum Resolution : uint8_t { V_100nV = 0, V_500nV = 1, V_10microV = 2, V_152microV = 3 } resolution;
	bool dcCoupling, usePolyBox, lowImpedanceMode;
	unsigned int chunkSize, channelCount, serialNumber;
	std::vector<std::string> channelLabels;
	std::shared_ptr<firdn::Chain<uint16_t>> downsampler;

	bool g_unsampledMarkers{false};
	bool g_sampledMarkers{true};
	bool g_sampledMarkersEEG{false};

	bool pullUpHiBits{true};
	bool pullUpLowBits{true};
	uint16_t g_pull_dir;
};

namespace Ui {
class MainWindow;
}
class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	explicit MainWindow(QWidget *parent, const char *config_file);
	~MainWindow() noexcept override;

private slots:
	// close event (potentially disabled)
	void closeEvent(QCloseEvent *ev) override;
	// start the BrainAmpSeries connection
	void toggleRecording();

private:
	// function for loading / saving the config file
	QString find_config_file(const char *filename);

	// raw config file IO
	void load_config(const QString &filename);
	void save_config(const QString &filename);
	std::unique_ptr<std::thread> reader{nullptr};
	std::shared_ptr<class BrainAmpUSB> brainamp;

	Ui::MainWindow *ui;
	std::atomic<bool> shutdown{false}; // flag indicating whether the recording thread should quit
};

// background data reader thread
template <typename T>
void read_thread(const ReaderConfig conf, std::shared_ptr<class BrainAmpUSB> brainamp, std::atomic<bool>& shutdown);

#endif // MAINWINDOW_H
