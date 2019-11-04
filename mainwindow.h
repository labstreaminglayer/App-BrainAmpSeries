#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QMainWindow>
#include <atomic>
#include <thread>

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
using HANDLE = void *;
using ULONG = unsigned long;
#endif

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
	void read_thread(int deviceNumber, ULONG serialNumber, int impedanceMode, int resolution,
		int dcCoupling, unsigned int chunkSize, unsigned int channelCount,
		std::vector<std::string> channelLabels);

	// raw config file IO
	void load_config(const QString &filename);
	void save_config(const QString &filename);
	std::unique_ptr<std::thread> reader{nullptr};
	HANDLE hDevice{nullptr};

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
