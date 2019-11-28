#ifndef BRAINAMP_H
#define BRAINAMP_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <cassert>

// forward declarations to avoid pulling in all of windows.h
using HANDLE = void *;
using DWORD = unsigned long;

#pragma pack(1) // don't reorder/pad the data members
struct BrainAmpSettings {
	enum Resolution : uint8_t { V_100nV = 0, V_500nV = 1, V_10microV = 2, V_152microV = 3 };

	uint32_t nChannels; // Number of channels
	// Channel lookup table, -1 to -8 means PolyBox channels
	int8_t nChannelList[256];
	uint32_t nPoints = 32;		// Number of points per block
	uint16_t nHoldValue = 0;	// Hold value for digital input
								// low byte = user response,
								// high byte = stimulus input

	// The following tables are based on logical channel positions.
	uint8_t n250Hertz[256] = {0}; // Low pass 250 Hz (0 = 1000Hz)
	Resolution nResolution[256];
	uint8_t nDCCoupling[256] = {0};  // DC coupling (0 = AC)
	uint8_t nLowImpedance = 0; // Low impedance i.e. 10 MOhm, (0 = > 100MOhm)

	// sane defaults
	BrainAmpSettings(uint32_t channels, bool usePolyBox = false) noexcept
		: nChannels(channels) {
		std::fill_n(nResolution, 256, Resolution::V_10microV);
		resize(channels);
	}

	void resize(unsigned int channels, bool usePolyBox = false) noexcept {
		assert(channels <= 128);
		nChannels = channels;
		for (auto i = 0u; i < nChannels; ++i) nChannelList[i] = i + (usePolyBox ? -8 : 0);
		std::fill(nChannelList + nChannels, nChannelList + 256, 0);
	}
	unsigned int channelCount() const { return nChannels; }
	/* When recording an additional channel
	 * (normal recording: markers, impedance: ground impedance) is transmitted */
	unsigned int channelCountWithDummy() const { return nChannels + 1; }

	void set250HzLowPass(bool lowpass) { std::fill_n(n250Hertz, nChannels, lowpass); }
	void set250HzLowPass(const std::vector<bool> &lowpass) {
		if (lowpass.size() != nChannels) throw std::logic_error("lowpass.size()!=nChannels");
		std::copy(lowpass.cbegin(), lowpass.cend(), n250Hertz);
	}
	void setResolution(Resolution resolution) { std::fill_n(nResolution, nChannels, resolution); }
	void setResolution(const std::vector<Resolution> &resolution) {
		if (resolution.size() != nChannels) throw std::logic_error("resolution.size()!=nChannels");
		std::copy(resolution.cbegin(), resolution.cend(), nResolution);
	}
	void setDCCoupling(bool coupleDC) { std::fill_n(nDCCoupling, nChannels, coupleDC); }
	void setDCCoupling(const std::vector<bool> &coupleDC) {
		if (coupleDC.size() != nChannels) throw std::logic_error("coupleDC.size()!=nChannels");
		std::copy(coupleDC.cbegin(), coupleDC.cend(), nDCCoupling);
	}
	void setChunkSize(uint32_t chunkSize) { nPoints = chunkSize; }
	uint32_t getChunkSize() const { return nPoints; }

	void setLowImpedance(bool lowImpedance) { nLowImpedance = lowImpedance; }
	bool getLowImpedance() const { return nLowImpedance; }
};

struct BA_CALIBRATION_SETTINGS
// BrainAmp Calibration settings (default: square waves, 5 Hz).
{
	uint16_t nWaveForm;  // 0 = ramp, 1 = triangle, 2 = square, 3 = sine wave
	uint32_t nFrequency; // Frequency in millihertz.
};
#pragma pack()

class BrainAmpUSB {
public:
	enum ImpedanceElectrodes { Data = 0, Reference = 1, Ground = 2 };
	enum class AmplifierType : uint16_t { NONE = 0, BrainAmp = 1, BrainAmpDC = 2, BrainAmpMR = 3 };
	enum class CaptureType {
		ImpedanceCheck = 0,
		DataAcquisition = 1,
		Calibration = 2,
		Stopped,
		Uninitialized
	};

	BrainAmpSettings _setup;

private:
	// device handle for the BrainAmpUSB connector
	HANDLE hDevice;
	// current state of the amplifier
	CaptureType state = CaptureType::Uninitialized;

	// if in impedance mode: what electrode group is currently being measured?
	ImpedanceElectrodes impedanceGroup = ImpedanceElectrodes::Data;
	// if in impedance mode: which range is set (10MOhm or 100MOhm)?
	bool _highImpedance = false;

	template <typename OutType = int, typename T>
	OutType ioCtl(uint32_t ctl, const std::vector<T> &in, const std::string &msg = "") const;

	template <typename OutType = int, typename T>
	OutType ioCtl(uint32_t ctl, const T &in, const std::string &msg = "") const;

	void _setImpedanceGroupRange(ImpedanceElectrodes impel, bool highImpedance);

public:
	BrainAmpUSB(int deviceIndex);
	BrainAmpUSB(BrainAmpUSB &&other) = default;
	BrainAmpUSB& operator=(BrainAmpUSB &&other) = default;
	BrainAmpUSB &operator=(BrainAmpUSB &) = delete;
	BrainAmpUSB(const BrainAmpUSB &other) = delete;
	~BrainAmpUSB();

	/* Return connected amplifier types */
	std::array<AmplifierType, 4> getConnectedAmplifierInfo(uint16_t unit_index = 0);
	uint32_t getDriverVersion();
	uint32_t getSerialNumber();
	uint16_t getMaxConnectableAmplifiers();

	/**
	 * Current state of the adapter / amplifiers
	 */

	/** Returns the voltage in mV*/
	int32_t getBatteryVoltage(uint16_t unitIndex);
	/** Fill state of the transfer buffer in % */
	int8_t getBufferFillState();
	/** Returns the current error string or an empty string */
	std::string getErrorState();
	CaptureType getRecordingState() const { return state; }

	void startCapture(CaptureType type = CaptureType::DataAcquisition);
	void stopCapture();

	/* Impedance measurement */
	ImpedanceElectrodes getImpedanceElectrodes() const { return impedanceGroup; }
	bool getImpedanceRange() const { return _highImpedance; }
	void setImpedanceElectrodes(ImpedanceElectrodes impgroup);
	void setImpedanceRange(bool highImpedance);

	const BrainAmpSettings &getSetup() { return _setup; }
	void setPullUp(uint16_t state);

	void setupAmp(BrainAmpSettings setup);

	/**
	 * @brief reads a chunk of data
	 * @param buffer a pointer to a preallocated buffer
	 * @return number of bytes read
	 *
	 * readChunk() reads a chunk (size = nPoints * (nChannels+1)) from the EEG.
	 * The data is in row major format, the last channel is either the ground electrode
	 * measurement (if in impedance mode) or the marker channel.
	 */
	uint32_t readChunk(int16_t *buffer) const;
	void readChunk(std::vector<int16_t>::iterator begin, std::vector<int16_t>::iterator end) const {
		if (std::distance(begin, end) !=
			static_cast<std::size_t>(_setup.nPoints) * _setup.channelCountWithDummy())
			throw std::logic_error("Buffer with invalid size supplied");
		readChunk(&*begin);
	}
	/** Returns the digital input state at this very moment.
	 * Don't use this if you need the temporal precision
	 */
	uint16_t sampleDigitalInput();
};

#endif // BRAINAMP_H
