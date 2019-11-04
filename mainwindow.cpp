#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QCloseEvent>
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <chrono>
#include <lsl_cpp.h>

#ifdef WIN32
#include <winioctl.h>
#else
// dummy declarations to test compilation / static analysis on Linux/OS X
static HANDLE INVALID_HANDLE_VALUE = nullptr;
enum Dummy {
	FILE_DEVICE_UNKNOWN,
	NORMAL_PRIORITY_CLASS,
	METHOD_BUFFERED,
	FILE_WRITE_DATA,
	FILE_READ_DATA,
	GENERIC_READ,
	GENERIC_WRITE,
	FILE_ATTRIBUTE_NORMAL,
	FILE_FLAG_WRITE_THROUGH,
	OPEN_EXISTING,
	HIGH_PRIORITY_CLASS
};
inline int GetCurrentProcess() { return 0; }
inline int SetPriorityClass(int, int) { return 0; }
inline int CTL_CODE(int, int, int, int) { return 0; }
inline void CloseHandle(HANDLE) {}
inline bool DeviceIoControl(HANDLE, int, void *, int, void *, int, void *, void *) { return true; }
inline HANDLE CreateFileA(const char *, int, int, void *, int, int, void *) {
	return static_cast<void *>(&INVALID_HANDLE_VALUE);
}
inline int32_t GetLastError() { return 0; }
inline bool ReadFile(HANDLE, int16_t *, int, ulong *, void *) { return false; }
using DWORD = unsigned long;
using USHORT = uint16_t;
using ULONG = unsigned long;
using CHAR = signed char;
using UCHAR = unsigned char;
#endif

#include "BrainAmpIoCtl.h"

const double sampling_rate = 5000.0;
static const char *error_messages[] = {"No error.", "Loss lock.", "Low power.",
	"Can't establish communication at start.", "Synchronisation error"};

MainWindow::MainWindow(QWidget *parent, const char *config_file)
	: QMainWindow(parent), ui(new Ui::MainWindow) {
	ui->setupUi(this);

	// make GUI connections
	connect(ui->actionLoad_Configuration, &QAction::triggered, [this]() {
		load_config(QFileDialog::getOpenFileName(
			this, "Load Configuration File", "", "Configuration Files (*.cfg)"));
	});
	connect(ui->actionSave_Configuration, &QAction::triggered, [this]() {
		save_config(QFileDialog::getSaveFileName(
			this, "Save Configuration File", "", "Configuration Files (*.cfg)"));
	});
	connect(ui->actionQuit, &QAction::triggered, this, &MainWindow::close);
	connect(ui->linkButton, &QPushButton::clicked, this, &MainWindow::toggleRecording);

	QString cfgfilepath = find_config_file(config_file);
	load_config(cfgfilepath);
}

void MainWindow::load_config(const QString &filename) {
	QSettings pt(filename, QSettings::IniFormat);

	ui->deviceNumber->setValue(pt.value("settings/devicenumber", 1).toInt());
	ui->channelCount->setValue(pt.value("settings/channelcount", 32).toInt());
	ui->impedanceMode->setCurrentIndex(pt.value("settings/impedancemode", 0).toInt());
	ui->resolution->setCurrentIndex(pt.value("settings/resolution", 0).toInt());
	ui->dcCoupling->setCurrentIndex(pt.value("settings/dccoupling", 0).toInt());
	ui->chunkSize->setValue(pt.value("settings/chunksize", 32).toInt());
	ui->usePolyBox->setChecked(pt.value("settings/usepolybox", false).toBool());
	ui->sendRawStream->setChecked(pt.value("settings/sendrawstream", false).toBool());
	ui->unsampledMarkers->setChecked(pt.value("settings/unsampledmarkers", false).toBool());
	ui->sampledMarkers->setChecked(pt.value("settings/sampledmarkers", true).toBool());
	ui->sampledMarkersEEG->setChecked(pt.value("settings/sampledmarkersEEG", false).toBool());
	ui->channelLabels->setPlainText(pt.value("channels/labels").toStringList().join('\n'));
}

void MainWindow::save_config(const QString &filename) {
	QSettings pt(filename, QSettings::IniFormat);

	// transfer UI content into property tree
	pt.beginGroup("settings");
	pt.setValue("devicenumber", ui->deviceNumber->value());
	pt.setValue("devicenumber", ui->deviceNumber->value());
	pt.setValue("channelcount", ui->channelCount->value());
	pt.setValue("impedancemode", ui->impedanceMode->currentIndex());
	pt.setValue("resolution", ui->resolution->currentIndex());
	pt.setValue("dccoupling", ui->dcCoupling->currentIndex());
	pt.setValue("chunksize", ui->chunkSize->value());
	pt.setValue("usepolybox", ui->usePolyBox->isChecked());
	pt.setValue("sendrawstream", ui->sendRawStream->isChecked());
	pt.setValue("unsampledmarkers", ui->unsampledMarkers->isChecked());
	pt.setValue("sampledmarkers", ui->sampledMarkers->isChecked());
	pt.setValue("sampledmarkersEEG", ui->sampledMarkersEEG->isChecked());
	pt.endGroup();

	pt.beginGroup("channels");
	pt.setValue("labels", ui->channelLabels->toPlainText().split('\n'));
	pt.endGroup();
}

void MainWindow::closeEvent(QCloseEvent *ev) {
	if (reader) {
		QMessageBox::warning(this, "Recording still running", "Can't quit while recording");
		ev->ignore();
	}
}

// start/stop the BrainAmpSeries connection
void MainWindow::toggleRecording() {
	DWORD bytes_returned;
	if (reader) {
		// === perform unlink action ===
		try {
			shutdown = true;
			reader->join();
			reader.reset();
			SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
			if (hDevice != nullptr) {
				DeviceIoControl(
					hDevice, IOCTL_BA_STOP, nullptr, 0, nullptr, 0, &bytes_returned, nullptr);
				CloseHandle(hDevice);
				hDevice = nullptr;
			}
		} catch (std::exception &e) {
			QMessageBox::critical(this, "Error",
				QString("Could not stop the background processing: ") + e.what(), QMessageBox::Ok);
			return;
		}

		// indicate that we are now successfully unlinked
		ui->linkButton->setText("Link");
	} else {
		// === perform link action ===

		try {
			// get the UI parameters...
			ReaderConfig conf;
			conf.deviceNumber = ui->deviceNumber->value();
			conf.channelCount = static_cast<unsigned int>(ui->channelCount->value());
			conf.lowImpedanceMode = ui->impedanceMode->currentIndex() == 1;
			conf.resolution = static_cast<ReaderConfig::Resolution>(ui->resolution->currentIndex());
			conf.dcCoupling = static_cast<unsigned char>(ui->dcCoupling->currentIndex());
			conf.chunkSize = ui->chunkSize->value();
			conf.usePolyBox = ui->usePolyBox->checkState() == Qt::Checked;
			bool sendRawStream = ui->sendRawStream->isChecked();

			g_unsampledMarkers = ui->unsampledMarkers->checkState() == Qt::Checked;
			g_sampledMarkers = ui->sampledMarkers->checkState() == Qt::Checked;
			g_sampledMarkersEEG = ui->sampledMarkersEEG->checkState() == Qt::Checked;

			std::vector<std::string> channelLabels(conf.channelCount);
			for (auto &label : ui->channelLabels->toPlainText().split('\n'))
				channelLabels.push_back(label.toStdString());
			if (channelLabels.size() != conf.channelCount)
				throw std::runtime_error("The number of channels labels does not match the channel "
										 "count device setting.");

			// try to open the device
			std::string deviceName = R"(\\.\BrainAmpUSB)" + std::to_string(conf.deviceNumber);
			hDevice = CreateFileA(deviceName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
			if (hDevice == INVALID_HANDLE_VALUE)
				throw std::runtime_error(
					"Could not open USB device. Please make sure that the device is plugged in, "
					"turned on, and that the driver is installed correctly.");

			// get serial number
			ULONG serialNumber = 0;
			if (!DeviceIoControl(hDevice, IOCTL_BA_GET_SERIALNUMBER, nullptr, 0, &serialNumber,
					sizeof(serialNumber), &bytes_returned, nullptr))
				qWarning() << "Could not get device serial number.";

			// set up device parameters
			BA_SETUP setup = {0};
			setup.nChannels = conf.channelCount;
			for (unsigned char c = 0; c < conf.channelCount; c++)
				setup.nChannelList[c] = c + (conf.usePolyBox ? -8 : 0);
			setup.nPoints = conf.chunkSize;
			setup.nHoldValue = 0;
			for (UCHAR c = 0; c < conf.channelCount; c++) setup.nResolution[c] = conf.resolution;
			for (UCHAR c = 0; c < conf.channelCount; c++) setup.nDCCoupling[c] = conf.dcCoupling;
			setup.nLowImpedance = conf.lowImpedanceMode;

			pullUpHiBits = true;
			pullUpLowBits = true;
			g_pull_dir = (pullUpLowBits ? 0xff : 0) | (pullUpHiBits ? 0xff00 : 0);
			if (!DeviceIoControl(hDevice, IOCTL_BA_DIGITALINPUT_PULL_UP, &g_pull_dir,
					sizeof(g_pull_dir), nullptr, 0, &bytes_returned, nullptr))
				throw std::runtime_error("Could not apply pull up/down parameter.");

			if (!DeviceIoControl(hDevice, IOCTL_BA_SETUP, &setup, sizeof(setup), nullptr, 0,
					&bytes_returned, nullptr))
				throw std::runtime_error("Could not apply device setup parameters.");

			// start recording
			long acquire_eeg = 1;
			if (!DeviceIoControl(hDevice, IOCTL_BA_START, &acquire_eeg, sizeof(acquire_eeg),
					nullptr, 0, &bytes_returned, nullptr))
				throw std::runtime_error("Could not start recording.");

			// start reader thread
			shutdown = false;
			auto function_handle =
				sendRawStream ? &MainWindow::read_thread<int16_t> : &MainWindow::read_thread<float>;
			reader.reset(new std::thread(function_handle, this, conf));
		}

		catch (std::exception &e) {
			// try to decode the error message
			const char *msg = "Could not open USB device.";
			if (hDevice != nullptr) {
				long error_code = 0;
				if (DeviceIoControl(hDevice, IOCTL_BA_ERROR_STATE, nullptr, 0, &error_code,
						sizeof(error_code), &bytes_returned, nullptr) &&
					bytes_returned)
					msg = ((error_code & 0xFFFF) >= 0 && (error_code & 0xFFFF) <= 4)
							  ? error_messages[error_code & 0xFFFF]
							  : "Unknown error (your driver version might not yet be supported).";
				else
					msg = "Could not retrieve error message because the device is closed";
				CloseHandle(hDevice);
				hDevice = nullptr;
			}
			QMessageBox::critical(this, "Error",
				QString("Could not initialize the BrainAmpSeries interface: ") + e.what() +
					" (driver message: " + msg + ")",
				QMessageBox::Ok);
			return;
		}

		// done, all successful
		ui->linkButton->setText("Unlink");
	}
}

// background data reader thread
template <typename T> void MainWindow::read_thread(const ReaderConfig conf) {
	const float unit_scales[] = {0.1f, 0.5f, 10.f, 152.6f};
	const char *unit_strings[] = {"100 nV", "500 nV", "10 muV", "152.6 muV"};
	const bool sendRawStream = std::is_same<T, int16_t>::value;
	// reserve buffers to receive and send data
	unsigned int chunk_words = conf.chunkSize * (conf.channelCount + 1);
	std::vector<int16_t> recv_buffer(chunk_words, 0);
	unsigned int outbufferChannelCount = conf.channelCount + (g_sampledMarkersEEG ? 1 : 0);
	std::vector<std::vector<T>> send_buffer(conf.chunkSize, std::vector<T>(outbufferChannelCount));

	std::vector<std::vector<std::string>> marker_buffer(
		conf.chunkSize, std::vector<std::string>(1));
	std::vector<std::string> s_mrkr;
	std::vector<uint16_t> trigger_buffer(conf.chunkSize);
	const std::string streamprefix = "BrainAmpSeries-" + std::to_string(conf.deviceNumber);

	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

	// for keeping track of sampled marker stream data
	uint16_t mrkr = 0;
	uint16_t prev_mrkr = 0;

	// for keeping track of unsampled markers
	// uint16_t us_prev_mrkr = 0;

	std::unique_ptr<lsl::stream_outlet> marker_outlet, s_marker_outlet;
	try {
		// create data streaminfo and append some meta-data
		auto stream_format = sendRawStream ? lsl::cf_int16 : lsl::cf_float32;
		lsl::stream_info data_info(streamprefix, "EEG", outbufferChannelCount, sampling_rate,
			stream_format, streamprefix + '_' + std::to_string(conf.serialNumber));
		lsl::xml_element channels = data_info.desc().append_child("channels");
		std::string postprocessing_factor =
			sendRawStream ? std::to_string(unit_scales[conf.resolution]) : "1";
		for (const auto &channelLabel : conf.channelLabels)
			channels.append_child("channel")
				.append_child_value("label", channelLabel)
				.append_child_value("type", "EEG")
				.append_child_value("unit", "microvolts")
				.append_child_value("scaling_factor", postprocessing_factor);
		if (g_sampledMarkersEEG) {
			channels.append_child("channel")
				.append_child_value("label", "triggerStream")
				.append_child_value("type", "EEG")
				.append_child_value("unit", "code");
		}

		data_info.desc()
			.append_child("amplifier")
			.append_child("settings")
			.append_child_value("low_impedance_mode", conf.lowImpedanceMode ? "true" : "false")
			.append_child_value("resolution", unit_strings[conf.resolution])
			.append_child_value("resolutionfactor", std::to_string(unit_scales[conf.resolution]))
			.append_child_value("dc_coupling", conf.dcCoupling ? "DC" : "AC");
		data_info.desc()
			.append_child("acquisition")
			.append_child_value("manufacturer", "Brain Products")
			.append_child_value("serial_number", std::to_string(conf.serialNumber));
		// make a data outlet
		lsl::stream_outlet data_outlet(data_info);

		//// create marker streaminfo and outlet
		// create unsampled marker streaminfo and outlet

		if (g_unsampledMarkers) {
			lsl::stream_info marker_info(streamprefix + "-Markers", "Markers", 1, 0, lsl::cf_string,
				streamprefix + '_' + std::to_string(conf.serialNumber) + "_markers");
			marker_outlet.reset(new lsl::stream_outlet(marker_info));
		}

		// create sampled marker streaminfo and outlet
		if (g_sampledMarkers) {
			lsl::stream_info marker_info(streamprefix + "-Sampled-Markers", "sampledMarkers", 1,
				sampling_rate, lsl::cf_string,
				streamprefix + '_' + std::to_string(conf.serialNumber) + "_sampled_markers");
			s_marker_outlet.reset(new lsl::stream_outlet(marker_info));
		}

		// enter transmission loop
		DWORD bytes_read;


		const T scale = std::is_same<T, float>::value ? unit_scales[conf.resolution] : 1;

		while (!shutdown) {
			// read chunk into recv_buffer
			if (!ReadFile(hDevice, recv_buffer.data(), (int)2 * chunk_words, &bytes_read, nullptr))
				throw std::runtime_error(
					"Could not read data, error code " + std::to_string(GetLastError()));

			if (bytes_read <= 0) {
				// CPU saver, this is ok even at higher sampling rates
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}

			if (bytes_read == 2 * chunk_words) {
				double now = lsl::local_clock();


				// reformat into send_buffer
				for (unsigned int s = 0; s < conf.chunkSize; s++) {

					for (unsigned int c = 0; c < conf.channelCount; c++)
						send_buffer[s][c] = scale * recv_buffer[c + s * (conf.channelCount + 1)];

					// buffer for handling triggers
					// trigger_buffer[s] = recv_buffer[channelCount + s*(channelCount+1)];//???
					mrkr = (uint16_t)recv_buffer[conf.channelCount + s * (conf.channelCount + 1)];
					mrkr ^= g_pull_dir;
					trigger_buffer[s] = mrkr;

					if (g_sampledMarkersEEG)
						send_buffer[s][conf.channelCount] =
							(mrkr == prev_mrkr ? 0.0 : static_cast<T>(mrkr));

					if (g_sampledMarkers || g_unsampledMarkers) {
						s_mrkr.clear();
						s_mrkr.push_back(mrkr == prev_mrkr ? "" : std::to_string(mrkr));
						if (mrkr != prev_mrkr) {
							qInfo() << "s: " << s << " mrkr: " << QString::fromStdString(s_mrkr[0]);
							if (g_unsampledMarkers)
								marker_outlet->push_sample(
									&s_mrkr[0], now + (s + 1 - conf.chunkSize) / sampling_rate);
						}
						marker_buffer.at(s) = s_mrkr;
					}
					prev_mrkr = mrkr;
				}

				// push data chunk into the outlet
				data_outlet.push_chunk(send_buffer, now);

				if (g_sampledMarkers) s_marker_outlet->push_chunk(marker_buffer, now);
			} else {
				// check for errors
				long error_code = 0;
				if (DeviceIoControl(hDevice, IOCTL_BA_ERROR_STATE, nullptr, 0, &error_code,
						sizeof(error_code), &bytes_read, nullptr) &&
					error_code)
					throw std::runtime_error(
						((error_code & 0xFFFF) >= 0 && (error_code & 0xFFFF) <= 4)
							? error_messages[error_code & 0xFFFF]
							: "Unknown error (your driver version might not yet be supported).");
				std::this_thread::yield();
			}
		}

	} catch (std::exception &e) {
		// any other error
		QMessageBox::critical(
			nullptr, "Error", QString("Error during processing: ") + e.what(), QMessageBox::Ok);
	}
}

/**
 * Find a config file to load. This is (in descending order or preference):
 * - a file supplied on the command line
 * - [executablename].cfg in one the the following folders:
 *	- the current working directory
 *	- the default config folder, e.g. '~/Library/Preferences' on OS X
 *	- the executable folder
 * @param filename	Optional file name supplied e.g. as command line parameter
 * @return Path to a found config file
 */
QString MainWindow::find_config_file(const char *filename) {
	if (filename) {
		QString qfilename(filename);
		if (!QFileInfo::exists(qfilename))
			QMessageBox(QMessageBox::Warning, "Config file not found",
				QStringLiteral("The file '%1' doesn't exist").arg(qfilename), QMessageBox::Ok,
				this);
		else
			return qfilename;
	}
	QFileInfo exeInfo(QCoreApplication::applicationFilePath());
	QString defaultCfgFilename(exeInfo.completeBaseName() + ".cfg");
	QStringList cfgpaths;
	cfgpaths << QDir::currentPath()
			 << QStandardPaths::standardLocations(QStandardPaths::ConfigLocation) << exeInfo.path();
	for (auto path : cfgpaths) {
		QString cfgfilepath = path + QDir::separator() + defaultCfgFilename;
		if (QFileInfo::exists(cfgfilepath)) return cfgfilepath;
	}
	QMessageBox(QMessageBox::Warning, "No config file not found",
		QStringLiteral("No default config file could be found"), QMessageBox::Ok, this);
	return "";
}

MainWindow::~MainWindow() noexcept { delete ui; }
