#include "brainamp.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <thread>

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#else
// dummy declarations for non-windows OS
const HANDLE INVALID_HANDLE_VALUE = nullptr;
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
	HIGH_PRIORITY_CLASS,
	METHOD_NEITHER
};
inline int GetCurrentProcess() { return 0; }
inline int SetPriorityClass(int, int) { return 0; }
inline int32_t CTL_CODE(int, int32_t ctl, int, int) noexcept { return (ctl << 2); }
inline void CloseHandle(HANDLE) {}
inline bool DeviceIoControl(HANDLE, int, void *, int, void *, int, unsigned long *, void *) {
	return true;
}
inline HANDLE CreateFile(const char *dummy, int, int, void *, int, int, void *) {
	return (void *)(dummy);
}
inline int GetLastError() { return 0; }
inline bool ReadFile(int, void *, int, unsigned long *, void *) { return false; }
#endif

static const char *error_messages[] = {"No error.", "Loss lock.", "Low power.",
	"Can't establish communication at start.", "Synchronisation error"};

const int IMPEDANCE_FREQUENCY = 15; // sinewave generator's frequency [Hz]

// Set impedance test mode, i.e. even in recording mode, the amplifier is still
// in impedance check mode
// Parameter [IN]: long bTestMode, != 0 testmode, 0 normal mode
const auto IOCTL_BA_IMPEDANCE_TESTMODE =
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_BUFFERED, FILE_WRITE_DATA);

// Disable DC offset correction for testing purposes.
// Parameter [IN]: long bTestMode, != 0 DC offset correction off, 0 normal mode
const auto IOCTL_BA_DCOFFSET_TESTMODE =
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80A, METHOD_BUFFERED, FILE_WRITE_DATA);

// Send calibration settings, Parameter [IN]: BA_CALIBRATION_SETTINGS struct.
const auto IOCTL_BA_CALIBRATION_SETTINGS =
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80B, METHOD_BUFFERED, FILE_WRITE_DATA);

const auto IOCTL_BA_ERROR_STATE =
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x809, METHOD_BUFFERED, FILE_READ_DATA);

static const std::map<int, std::string> ioctlNames{
	{0x801, "SETUP"},
	{0x802, "START"},
	{0x803, "STOP"},
	{0x804, "BUFFERFILLING_STATE"},
	{0x805, "BATTERY_VOLTAGE"},
	{0x806, "IMPEDANCE_FREQUENCY"},
	{0x807, "IMPEDANCE_TESTMODE"},
	{0x808, "IMPEDANCE_GROUPRANGE"},
	{0x809, "ERROR_STATE"},
	{0x80A, "DCOFFSET_TESTMODE"},
	{0x80B, "CALIBRATION_SETTINGS"},
	{0x80C, "DIGITALINPUT_PULL_UP"},
	{0x80D, "DIGITALINPUT_VALUE"},
	{0x80E, "DRIVERVERSION"},
	{0x80F, "COMMAND"},
	{0x810, "CONTROLREGISTER"},
	{0x811, "DCOFFSET_COARSE"},
	{0x812, "DCOFFSET_CORRECTION_COARSE"},
	{0x813, "DCOFFSET_FINE"},
	{0x814, "DCOFFSET_CORRECTION_FINE"},
	{0x815, "SET_CAL_IMP_LINES"},
	{0x816, "AMPLIFIER_TYPE"},
	{0x817, "DCOFFSET_CORRECTION"},
	{0x818, "GET_SERIALNUMBER"},
	{0x819, "GET_SUPPORTED_AMPLIFIERS"},
	{0x81a, "PRESTART"},
	{0x81b, "POSTSTART"},
	{0x81c, "DISABLE_BUSMASTERING"},
	{0x81d, "BUFFERMISSING_MS"},
};

void BrainAmpUSB::setPullUp(uint16_t newstate) {
	// Set pullup/pulldown resistors for the digital input (default is pulldown).
	// This can not be done for each bit, but for 2 groups.
	// Parameter [IN]: USHORT bPullup
	// (low byte for bit 0 - 7, high byte for bit 8 - 15)
	// != 0 DC pullup, 0 pulldown
	const auto IOCTL_BA_DIGITALINPUT_PULL_UP =
		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80C, METHOD_BUFFERED, FILE_WRITE_DATA);
	ioCtl<void>(IOCTL_BA_DIGITALINPUT_PULL_UP, newstate, "setting pullup resistors");
}

// Send Commands during recording
// Parameter [IN]:
//		USHORT nCommand (1 = DC Correction),
//		USHORT nChannel (-1 = all channels)
const auto IOCTL_BA_COMMAND =
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80F, METHOD_BUFFERED, FILE_WRITE_DATA);

// Set Control register directly (WRCON)
// Parameter [IN]: USHORT nAddress, ulong[4] channel mask
// Control registers:
// 0x01: 250Hz lowpass
// 0x02: DC coupling
// 0x03: resolution==1
// 0x04: AC coupling?
// 0x06: resolution==2
// 0x05: 
const auto IOCTL_BA_CONTROLREGISTER =
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x810, METHOD_BUFFERED, FILE_WRITE_DATA);

// Set DC offset value for potentiometer A (coarse)
// Parameter [IN]: USHORT nChannel, UCHAR nValue
const auto IOCTL_BA_DCOFFSET_COARSE =
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x811, METHOD_BUFFERED, FILE_WRITE_DATA);

// Do DC Offset correction for potentiometer A (coarse)
const auto IOCTL_BA_DCOFFSET_CORRECTION_COARSE =
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x812, METHOD_NEITHER, FILE_WRITE_DATA);

// Set DC offset value for potentiometer B (fine)
// Parameter [IN]: USHORT nChannel, UCHAR nValue
const auto IOCTL_BA_DCOFFSET_FINE =
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x813, METHOD_BUFFERED, FILE_WRITE_DATA);

// Do DC Offset correction for potentiometer B (fine)
const auto IOCTL_BA_DCOFFSET_CORRECTION_FINE =
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x814, METHOD_NEITHER, FILE_WRITE_DATA);

// Set Cal, Imp1, and Imp2 lines (testing only!)
// Parameter [IN]: BYTE Cal, BYTE, Imp1, Byte Imp2
const auto IOCTL_BA_SET_CAL_IMP_LINES =
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x815, METHOD_BUFFERED, FILE_WRITE_DATA);

// Do DC Offset correction, no parameters
const auto IOCTL_BA_DCOFFSET_CORRECTION =
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x817, METHOD_NEITHER, FILE_WRITE_DATA);

// Prestart, used only if more than device is involved.
const auto IOCTL_BA_PRESTART =
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x81a, METHOD_BUFFERED, FILE_WRITE_DATA);

// Poststart, used only if more than device is involved.
const auto IOCTL_BA_POSTSTART =
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x81b, METHOD_BUFFERED, FILE_WRITE_DATA);

// Disable busmastering
const auto IOCTL_BA_DISABLE_BUSMASTERING =
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x81c, METHOD_BUFFERED, FILE_WRITE_DATA);

// DBID 1440
// Get missing buffer size in ms, Parameter [OUT]: long nSize,
//  0 = no overflow detected
//  -1 = Device/FW Overflow,
//  1 - n = missing buffer size in ms.
const auto IOCTL_BA_BUFFERMISSING_MS =
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x81d, METHOD_BUFFERED, FILE_READ_DATA);

/*enum ImpedanceGroupRange {
	Data_100k = 0, // 0 = 100kOhm Data (IMP1 | CAL),
	Data_10k = 1, // 1 = 10 kOhm Data (IMP1),
	Ref_100k = 2, // 2 = 100 kOhm Reference (IMP2 | CAL),
	Ref_10k = 3, // 3 = 10 kOhm Reference (IMP2)
	Ground = 4 // 4 = Ground (IMP2)
};*/
enum class ImpedanceGroupRange { HighData = 0, LowData = 1, HighRef = 2, LowRef = 3, Ground = 4 };

HANDLE openBA(int deviceIndex) {
	// CreateFileA?
	HANDLE file = CreateFile((R"(\\.\BrainAmpUSB)" + std::to_string(deviceIndex)).c_str(),
		GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		throw std::runtime_error("Error opening BA" + std::to_string(deviceIndex) + ' ' +
								 std::to_string(::GetLastError()));
	return file;
}

BrainAmpUSB::BrainAmpUSB(int deviceIndex) : _setup(0), hDevice(openBA(deviceIndex)) {
	// get serial number
	std::cout << "Opened BrainAmp " << getSerialNumber() << std::endl;
}

void BrainAmpUSB::setupAmp(BrainAmpSettings setup) {
	// Send setup, Parameter [IN]: BA_SETUP struct.
	const auto IOCTL_BA_SETUP =
		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_DATA);

	std::cout << "Calling setup. You should've created a valid _setup before calling this."
			  << std::endl;
	// set up device parameters
	ioCtl(IOCTL_BA_SETUP, setup, "setting the BrainAmp up");
	_setup = setup;
	state = BrainAmpUSB::CaptureType::Stopped;
}

BrainAmpUSB::~BrainAmpUSB() {
	std::cout << "Closing BA" << std::endl;
	if (state != CaptureType::Stopped && state != CaptureType::Uninitialized) stopCapture();
	CloseHandle(hDevice);
}

void BrainAmpUSB::_setImpedanceGroupRange(ImpedanceElectrodes impel, bool highImpedance) {
	// Set impedance group and range
	// Parameter [IN]: long nGroupRange,
	// 0 = 100kOhm Data (IMP1 | CAL),
	// 1 = 10 kOhm Data (IMP1),
	// 2 = 100 kOhm Reference (IMP2 | CAL),
	// 3 = 10 kOhm Reference (IMP2)
	// 4 = Ground (IMP2)
	if (state != CaptureType::ImpedanceCheck)
		throw std::logic_error("Calling setImpedanceGroupRange while not in impedance mode");

	const auto IOCTL_BA_IMPEDANCE_GROUPRANGE =
		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_WRITE_DATA);

	if (highImpedance && impel == ImpedanceElectrodes::Ground)
		throw std::logic_error("Can't measure high impedance with ground electrode");
	int32_t imprange = static_cast<int>(impel) * 2 + highImpedance;

	std::cout << "Setting Impedance mode " << imprange << std::endl;
	ioCtl(IOCTL_BA_IMPEDANCE_GROUPRANGE, imprange, "Error on group / range selection");
	impedanceGroup = impel;
	_highImpedance = highImpedance;
}

void BrainAmpUSB::setImpedanceElectrodes(BrainAmpUSB::ImpedanceElectrodes impgroup) {
	_setImpedanceGroupRange(impgroup, _highImpedance);
}

void BrainAmpUSB::setImpedanceRange(bool highImpedance) {
	_setImpedanceGroupRange(impedanceGroup, highImpedance);
}

uint32_t BrainAmpUSB::readChunk(int16_t *buffer) const {
	if (state == CaptureType::Stopped || state == CaptureType::Uninitialized)
		throw std::logic_error("Read called before starting capture");

	const auto size = _setup.nPoints * _setup.channelCountWithDummy() * 2;

#ifdef WIN32
	unsigned long bytes_read = 0;
	while (bytes_read == 0) {
		if (!ReadFile(hDevice, static_cast<void *>(buffer), size, &bytes_read, nullptr))
			throw std::runtime_error("Read failed: " + std::to_string(::GetLastError()));
		if (bytes_read <= 0) {
			Sleep(1);
			continue;
		}
		if (bytes_read != size) {
			// check for errors
			auto error_code = ioCtl(IOCTL_BA_ERROR_STATE, nullptr, "getting error state");
			if (error_code)
				throw std::runtime_error(
					((error_code & 0xFFFF) >= 0 && (error_code & 0xFFFF) <= 4)
						? error_messages[error_code & 0xFFFF]
						: "Unknown error " + std::to_string(error_code));
			continue;
		}
	}
	return bytes_read;
#else
	// Dummy code to test on other platforms
	static int phase;
	int16_t *out = buffer;
	const int16_t *lastptr = out + size / 2;
	const auto nchan = _setup.channelCount();

	const double phaseDifference = .01;
	std::vector<int> amplitude(nchan);
	for(auto i=0u; i< nchan; ++i)
		amplitude[i] = static_cast<int>((_highImpedance ? 3277 : 32767) * sqrt(i/(double) nchan));
	while (out < lastptr) {
		phase++;
		const double cphase = 15 * 2 * 3.14159 / 5000 * phase;
		for (auto i = 0u; i < nchan; i++) *(out++) = sin(cphase + phaseDifference * i) * amplitude[i];
		if (state == CaptureType::DataAcquisition)
			*(out++) = (phase % 40000) == 0 ? 1 : 0;
		else // acquisition / calibration data, last channel is marker channel
			*(out++) = sin(cphase) * 650;
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(5000000 / _setup.nPoints));
	return size;
#endif
}

// static std::array<std::string,4> brainAmpNames{"None", "BrainAmp", "BrainAmp MR", "BrainAmp DC"};

void BrainAmpUSB::startCapture(CaptureType type) {
	switch (type) {
	case CaptureType::Stopped: stopCapture(); return;
	case CaptureType::Uninitialized: throw std::logic_error("You can't uninitialize an amplifier.");
	case CaptureType::Calibration:
	case CaptureType::DataAcquisition:
	case CaptureType::ImpedanceCheck: break;
	default: throw std::logic_error("Unhandled state change!");
	}

	// Start acquisition, Parameter [IN]: long nType
	// 0 = Impedance check, 1 = Data aquisition, 2 = calibration
	const auto IOCTL_BA_START =
		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_WRITE_DATA);

	if (type == CaptureType::ImpedanceCheck) {
		// Set impedance check sine wave frequency, Parameter [IN]: LONG nFrequency in
		// Hertz
		std::cout << "Setting mode to ImpedanceCheck" << std::endl;
		const auto IOCTL_BA_IMPEDANCE_FREQUENCY =
			CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_WRITE_DATA);
		ioCtl(
			IOCTL_BA_IMPEDANCE_FREQUENCY, IMPEDANCE_FREQUENCY, "setting Impedance check frequency");
	}
	std::cout << "Requesting capture start" << std::endl;
	ioCtl(IOCTL_BA_START, static_cast<int32_t>(type), "Error starting capture");
	std::vector<int16_t> buffer(_setup.channelCountWithDummy() * _setup.getChunkSize(), 0);

	// Clear buffers,
	// This is done in 2 steps:
	// Wait for the first data
	std::cout << "Clearing buffer..." << std::endl;
	// while (read(buffer) == 0) {}

	// Remove all data
	// while (read(buffer)) {}

	state = type;
}

void BrainAmpUSB::stopCapture() {
	// Stop acquisition
	const auto IOCTL_BA_STOP =
		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_WRITE_DATA);
	ioCtl(IOCTL_BA_STOP, nullptr);
	state = BrainAmpUSB::CaptureType::Stopped;
}

int8_t BrainAmpUSB::getBufferFillState() {
	// Get buffer filling state, Parameter [OUT]: long nState, < 0 = Overflow, 0 -
	// 100 = Filling state in percent.
	const auto IOCTL_BA_BUFFERFILLING_STATE =
		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_READ_DATA);
	return ioCtl<int8_t>(IOCTL_BA_BUFFERFILLING_STATE, nullptr, "getting buffer state");
}

int32_t BrainAmpUSB::getBatteryVoltage(uint16_t unitIndex) {
	// Get battery voltage of one unit, Parameter [IN]: USHORT nUnit, Parameter
	// [OUT]: long nVoltage in millivolts
	const auto IOCTL_BA_BATTERY_VOLTAGE =
		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_WRITE_DATA | FILE_READ_DATA);
	return ioCtl<int32_t>(IOCTL_BA_BATTERY_VOLTAGE, unitIndex);
}

uint16_t BrainAmpUSB::sampleDigitalInput() {
	// Get digital input value, Parameter [OUT]: USHORT nValue
	const auto IOCTL_BA_DIGITALINPUT_VALUE =
		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80D, METHOD_BUFFERED, FILE_READ_DATA);
	return ioCtl(IOCTL_BA_DIGITALINPUT_VALUE, nullptr);
}

uint32_t BrainAmpUSB::getSerialNumber() {
	// Retrieve serial number of device. Parameter [OUT]: ULONG
	const auto IOCTL_BA_GET_SERIALNUMBER =
		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x818, METHOD_BUFFERED, FILE_READ_DATA);
	return ioCtl(IOCTL_BA_GET_SERIALNUMBER, nullptr);
}

uint16_t BrainAmpUSB::getMaxConnectableAmplifiers() {
	// Retrieve number of supported amplifiers for the device. Parameter [OUT]:
	// USHORT
	const auto IOCTL_BA_GET_SUPPORTED_AMPLIFIERS =
		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x819, METHOD_BUFFERED, FILE_READ_DATA);
	return ioCtl(IOCTL_BA_GET_SUPPORTED_AMPLIFIERS, nullptr);
}

std::string BrainAmpUSB::getErrorState() {
	// Get error state, Parameter [OUT]: long nState, Highword (amplifier):
	// Bit 0 - 3: amplifier number(s)
	// Loword:  (error code):	0 = no error, 1 = loss lock, 2 = low power,
	// 3 = can't establish communication at start. 4 = synchronisation error
	static const std::vector<std::string> error_messages{
		"",
		"Loss lock.",
		"Low power.",
		"Can't establish communication at start",
		"Synchronisation error"};

	auto error_code = ioCtl(IOCTL_BA_ERROR_STATE, nullptr, "getting the error state");
	error_code &= 0xFFFF;
	std::cout << error_code << std::endl; 
	if (error_code >= error_messages.size()) throw std::runtime_error("Unsupported error number " + std::to_string(error_code));
	return error_messages[error_code];
}

std::array<BrainAmpUSB::AmplifierType, 4> BrainAmpUSB::getConnectedAmplifierInfo(
	uint16_t unit_index) {
	// Get amplifier type of all 4 units,
	// Parameter [IN]: USHORT nUnit, Parameter [OUT]: USHORT Type[4]: 0 no Amp, 1
	// BrainAmp, 2 MR, 3 DC
	const auto IOCTL_BA_AMPLIFIER_TYPE =
		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x816, METHOD_BUFFERED, FILE_WRITE_DATA | FILE_READ_DATA);
	return ioCtl<std::array<BrainAmpUSB::AmplifierType, 4>>(IOCTL_BA_AMPLIFIER_TYPE, unit_index);
}

uint32_t BrainAmpUSB::getDriverVersion() {
	// Get driver version, Parameter [OUT]: ULONG nValue
	// The version is coded as Major.Minor.DLL.
	// The "DLL" part is used for intermediate versions and contains 4 digits.
	// The minor part contains 2 digits.
	// The number 1010041 for example means version 1.01.0041.
	// If the highest bit (bit 31) is set, it means "test version".
	const auto IOCTL_BA_DRIVERVERSION =
		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80E, METHOD_BUFFERED, FILE_READ_DATA);
	return ioCtl(IOCTL_BA_DRIVERVERSION, nullptr);
}

template <typename Out>
inline Out _ioCtl(HANDLE dev, uint32_t ctl, const void *data, DWORD size, const std::string &msg) {
	std::cout << "CTL " << ' ' << ioctlNames.at((ctl >> 2) & 0xfff) << std::endl;
	unsigned long ret = 0;
	Out out = Out();

	if (!DeviceIoControl(
			dev, ctl, const_cast<void *>(data), size, &out, sizeof(out), &ret, nullptr))
		throw std::runtime_error("Error " + msg + ": " + std::to_string(::GetLastError()));
	return out;
}

template <>
inline void _ioCtl(HANDLE dev, uint32_t ctl, const void *data, DWORD size, const std::string &msg) {
	std::cout << "CTL " << ' ' << ioctlNames.at((ctl >> 2) & 0xfff) << std::endl;
	unsigned long retsize = 0;
	if (!DeviceIoControl(dev, ctl, const_cast<void *>(data), size, nullptr, 0, &retsize, nullptr))
		throw std::runtime_error("Error " + msg + ": " + std::to_string(::GetLastError()));
}

template <typename OutType, typename T>
OutType BrainAmpUSB::ioCtl(uint32_t ctl, const std::vector<T> &in, const std::string &msg) const {
	return _ioCtl<OutType>(
		hDevice, ctl, in.size() ? in.data() : nullptr, in.size() * sizeof(T), msg);
}

template <typename OutType, typename T>
OutType BrainAmpUSB::ioCtl(uint32_t ctl, const T &in, const std::string &msg) const {
	auto outSize = std::is_same_v<std::decay_t<T>, nullptr_t> ? 0 : sizeof(T);
	if(!std::is_same_v<std::decay_t<OutType>, void>)
		return _ioCtl<OutType>(hDevice, ctl, &in, outSize, msg);
}
