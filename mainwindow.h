#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QMainWindow>
#include <atomic>
#include <thread>

struct ReaderConfig {
	int deviceNumber;
	enum Resolution : uint8_t { V_100nV = 0, V_500nV = 1, V_10microV = 2, V_152microV = 3 } resolution;
	bool dcCoupling, usePolyBox, lowImpedanceMode;
	unsigned int chunkSize, channelCount, serialNumber;
	std::vector<std::string> channelLabels;
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
	// background data reader thread
	template <typename T>
	void read_thread(const ReaderConfig config);

	// raw config file IO
	void load_config(const QString &filename);
	void save_config(const QString &filename);
	std::unique_ptr<std::thread> reader{nullptr};
	std::unique_ptr<class BrainAmpUSB> brainamp;

	bool g_unsampledMarkers{false};
	bool g_sampledMarkers{true};
	bool g_sampledMarkersEEG{false};

	bool pullUpHiBits;
	bool pullUpLowBits;
	uint16_t g_pull_dir;

	Ui::MainWindow *ui;
	std::atomic<bool> shutdown{false}; // flag indicating whether the recording thread should quit
};

#endif // MAINWINDOW_H
