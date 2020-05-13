#include "mainwindow.h"
#include "firdn.hpp"
#include "ui_mainwindow.h"
#include <QCloseEvent>
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <chrono>
#include <lsl_cpp.h>
#include <memory>

#include "brainamp.h"

const double sampling_rate = 5000.0;

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
	ui->input_channelOffset->setValue(pt.value("settings/channelOffset", 0).toInt());
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
	ui->inp_downsampling->setText(pt.value("settings/downsampling", "").toString());
}

void MainWindow::save_config(const QString &filename) {
	QSettings pt(filename, QSettings::IniFormat);

	// transfer UI content into property tree
	pt.beginGroup("settings");
	pt.setValue("devicenumber", ui->deviceNumber->value());
	pt.setValue("devicenumber", ui->deviceNumber->value());
	pt.setValue("channelOffset", ui->input_channelOffset->value());
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
	pt.setValue("downsampling", ui->inp_downsampling->text());
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

template<typename In, typename Out=double, typename Coef=double> std::unique_ptr<firdn::Chain<In>> chain_from_str(QString dsconf, unsigned int nchan) {
	if(dsconf.isEmpty()) return nullptr;
	auto splitstep = [](QString part){
		auto i = part.indexOf(':');
		if(i==-1) throw std::runtime_error("No ':' found in downsampler config part");
		return std::make_pair(part.left(i).toUInt(), firdn::coefs_from_file<Coef>(part.mid(i+1).toStdString()));
	};
	auto steps = dsconf.split(';');
	auto stepconf{splitstep(steps.first())};
	auto res = std::make_unique<firdn::Chain<In>>(firdn::make_downsampler<In, Coef>(
		stepconf.first, nchan, stepconf.second.data(), stepconf.second.size()));
	steps.removeFirst();
	for(auto pt: steps) {
		stepconf = splitstep(pt);
		res->add_downsampler(stepconf.first, stepconf.second.data(), stepconf.second.size());
	}
	return res;
}

// start/stop the BrainAmpSeries connection
void MainWindow::toggleRecording() {
	if (reader) {
		// === perform unlink action ===
		try {
			shutdown = true;
			reader->join();
			reader.reset();
			brainamp->stopCapture();
			brainamp = nullptr;
		} catch (std::exception &e) {
			if (brainamp)
				QMessageBox::critical(this, "Error", QString::fromStdString(brainamp->getErrorState()), QMessageBox::Ok);
			QMessageBox::critical(this, "Error",
				QStringLiteral("Could not stop the background processing: ") + e.what(), QMessageBox::Ok);
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
			conf.channelOffset = ui->input_channelOffset->value();
			conf.lowImpedanceMode = ui->impedanceMode->currentIndex() == 1;
			conf.resolution = static_cast<ReaderConfig::Resolution>(ui->resolution->currentIndex());
			conf.dcCoupling = static_cast<unsigned char>(ui->dcCoupling->currentIndex());
			conf.chunkSize = (unsigned int) ui->chunkSize->value();
			conf.usePolyBox = ui->usePolyBox->checkState() == Qt::Checked;
			conf.downsampler = chain_from_str<uint16_t, double>(ui->inp_downsampling->text(), conf.channelCount);
			bool sendRawStream = ui->sendRawStream->isChecked();

			conf.g_unsampledMarkers = ui->unsampledMarkers->checkState() == Qt::Checked;
			conf.g_sampledMarkers = ui->sampledMarkers->checkState() == Qt::Checked;
			conf.g_sampledMarkersEEG = ui->sampledMarkersEEG->checkState() == Qt::Checked;

			for (auto &label : ui->channelLabels->toPlainText().split('\n'))
				conf.channelLabels.push_back(label.toStdString());
			if (conf.channelLabels.size() != conf.channelCount)
				throw std::runtime_error("The number of channels labels does not match the channel "
										 "count device setting.");

			// try to open the device
			brainamp = std::make_shared<BrainAmpUSB>(conf.deviceNumber);

			// get serial number
			// auto serialNumber = brainamp->getSerialNumber();

			// set up device parameters
			BrainAmpSettings setup(conf.channelCount, conf.usePolyBox, conf.channelOffset);
			setup.setChunkSize(conf.chunkSize);
			setup.nHoldValue = 0;
			setup.setResolution(static_cast<BrainAmpSettings::Resolution>(conf.resolution));
			setup.setDCCoupling(conf.dcCoupling);
			setup.setLowImpedance(conf.lowImpedanceMode);

			/*g_pull_dir = (pullUpLowBits ? 0xff : 0) | (pullUpHiBits ? 0xff00 : 0);
			if (!DeviceIoControl(hDevice, IOCTL_BA_DIGITALINPUT_PULL_UP, &g_pull_dir,
					sizeof(g_pull_dir), nullptr, 0, &bytes_returned, nullptr))
				throw std::runtime_error("Could not apply pull up/down parameter.");*/

			brainamp->setupAmp(setup);
			brainamp->startCapture();

			// start reader thread
			shutdown = false;
			auto function_handle =
				sendRawStream ? &read_thread<int16_t> : &read_thread<float>;
			reader = std::make_unique<std::thread>(function_handle, conf, brainamp, std::ref(shutdown));
		}

		catch (std::exception &e) {
			std::string msg{e.what()};
			if (brainamp) msg += brainamp->getErrorState();

			 try to decode the error message
			/*
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
			}*/
			QMessageBox::critical(this, "Error",
				("Could not initialize the BrainAmpSeries interface: " + msg).c_str(),
				QMessageBox::Ok);
			return;
		}

		// done, all successful
		ui->linkButton->setText("Unlink");
	}
}

// background data reader thread
template <typename T> void read_thread(const ReaderConfig conf, std::shared_ptr<BrainAmpUSB> brainamp, std::atomic<bool>& shutdown) {
	const float unit_scales[] = {0.1f, 0.5f, 10.f, 152.6f};
	const char *unit_strings[] = {"100 nV", "500 nV", "10 muV", "152.6 muV"};
	const bool sendRawStream = std::is_same<T, int16_t>::value;
	// reserve buffers to receive and send data
	unsigned int chunk_words = conf.chunkSize * (conf.channelCount + 1);
	std::vector<int16_t> recv_buffer(chunk_words, 0);
	unsigned int outbufferChannelCount = conf.channelCount + (conf.g_sampledMarkersEEG ? 1 : 0);
	std::vector<T> send_buffer(conf.chunkSize * outbufferChannelCount, 0);

	std::vector<std::string> marker_buffer(conf.chunkSize, std::string());
	std::string s_mrkr;
	std::vector<uint16_t> trigger_buffer(conf.chunkSize);
	const std::string streamprefix = "BrainAmpSeries-" + std::to_string(conf.deviceNumber);

	// for keeping track of sampled marker stream data
	uint16_t mrkr = 0, prev_mrkr = 0;

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
		if (conf.g_sampledMarkersEEG) {
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
			.append_child_value("dc_coupling", conf.dcCoupling ? "DC" : "AC")
			.append_child_value("channel_offset", std::to_string(conf.channelOffset));
		data_info.desc()
			.append_child("acquisition")
			.append_child_value("manufacturer", "Brain Products")
			.append_child_value("serial_number", std::to_string(conf.serialNumber));
		// make a data outlet
		lsl::stream_outlet data_outlet(data_info);

		//// create marker streaminfo and outlet
		// create unsampled marker streaminfo and outlet

		if (conf.g_unsampledMarkers) {
			lsl::stream_info marker_info(streamprefix + "-Markers", "Markers", 1, 0, lsl::cf_string,
				streamprefix + '_' + std::to_string(conf.serialNumber) + "_markers");
			marker_outlet.reset(new lsl::stream_outlet(marker_info));
		}

		// create sampled marker streaminfo and outlet
		if (conf.g_sampledMarkers) {
			lsl::stream_info marker_info(streamprefix + "-Sampled-Markers", "sampledMarkers", 1,
				sampling_rate, lsl::cf_string,
				streamprefix + '_' + std::to_string(conf.serialNumber) + "_sampled_markers");
			s_marker_outlet.reset(new lsl::stream_outlet(marker_info));
		}

		// enter transmission loop
		const T scale = std::is_same<T, float>::value ? unit_scales[conf.resolution] : 1;

		while (!shutdown) {
			brainamp->readChunk(recv_buffer.begin(), recv_buffer.end());

			double now = lsl::local_clock();

			auto recvbuf_it = recv_buffer.cbegin();
			auto sendbuf_it = send_buffer.begin();
			// reformat into send_buffer
			for (unsigned int s = 0; s < conf.chunkSize; s++) {
				for (unsigned int c = 0; c < conf.channelCount; c++)
					*sendbuf_it++ = *recvbuf_it++ * scale;

				mrkr = (uint16_t)*recvbuf_it++;
				mrkr ^= conf.g_pull_dir;
				trigger_buffer[s] = mrkr;

				if (conf.g_sampledMarkersEEG)
					*sendbuf_it++ = (mrkr == prev_mrkr ? 0.0 : static_cast<T>(mrkr));

				if (conf.g_sampledMarkers || conf.g_unsampledMarkers) {
					s_mrkr = mrkr == prev_mrkr ? "" : std::to_string(mrkr);
					if (mrkr != prev_mrkr && conf.g_unsampledMarkers)
						marker_outlet->push_sample(
							&s_mrkr, now + (s + 1 - conf.chunkSize) / sampling_rate);
					marker_buffer.at(s) = s_mrkr;
				}
				prev_mrkr = mrkr;
			}

			// push data chunk into the outlet
			data_outlet.push_chunk_multiplexed(send_buffer, now);

			if (conf.g_sampledMarkers) s_marker_outlet->push_chunk_multiplexed(marker_buffer, now);
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
