#pragma once
#include <array>
#include <map>
#include <memory>
#include <vector>

class DigitalFilter
{
	using Vec = std::vector<double>;
private:
	// coefficients and filter state
	Vec m_pdA;
	Vec m_pdB;
	Vec m_pdZ;
	// local state variables for direct form II
	double m_dXi;
	double m_dYi;

	// filter data
	int m_nFilterOrder;
	int m_nFilterBlockLen; // this is how many samples to process at a time

public:
	template<class T>
	void Filter(T* ptIn, T* ptOut)
	{
		for (int i = 0; i < m_nFilterBlockLen; i++)
		{
			m_dXi = ptIn[i];
			m_dYi = m_pdB[0] * m_dXi + m_pdZ[0];
			for (int j = 1; j < m_nFilterOrder + 1; j++)
				m_pdZ[j - 1] = m_pdB[j] * m_dXi + m_pdZ[j] - m_pdA[j] * m_dYi;
			m_pdZ[m_nFilterOrder - 1] = m_pdB[m_nFilterOrder] * m_dXi - m_pdA[m_nFilterOrder] * m_dYi;
			ptOut[i] = (T)(m_pdA[0] * m_dYi);
		}
	}

	DigitalFilter(int nOrder, int nBlockLen, double *pdB, double *pdA, double *pdZ)
		: m_nFilterOrder(nOrder), m_nFilterBlockLen(nBlockLen), m_pdB(pdB, pdB + nOrder + 1),
		  m_pdA(pdA != nullptr ? Vec(pdA, pdA + nOrder + 1) : Vec(nOrder + 1, 0.0)),
		  m_pdZ(pdZ != nullptr ? Vec(pdZ, pdZ + nOrder) : Vec(nOrder + 1, 0.0)) {
		if (pdA == nullptr) m_pdA[0] = 1;
		m_pdZ.resize(m_nFilterOrder + 1, 0.);
	}
};

struct Coeffs {
	std::array<double, 3> pdB, pdA;
	Coeffs(std::array<double, 3> b, std::array<double, 3> a)
		: pdB(b), pdA(a) {}

	Coeffs(const Coeffs&) = default;
	Coeffs(Coeffs&&) = default;
	Coeffs& operator=(const Coeffs&) = default;
	Coeffs& operator=(Coeffs&&) = default;
};

const std::map<int, Coeffs> pdCoeffs {
	{2, Coeffs({0.292893218813452, 0.585786437626905, 0.292893218813452},
			{1., -0., 0.171572875253810})},
	{5, Coeffs({0.067455273889072, 0.134910547778144, 0.067455273889072},
			{1.000000000000000, -1.142980502539901, 0.412801598096189})},
	{10, Coeffs({0.020083365564211, 0.040166731128423, 0.020083365564211},
			 {1., -1.561018075800718, 0.641351538057563})},
	{20, Coeffs({0.005542717210281, 0.011085434420561, 0.005542717210281},
			 {1., -1.778631777824585, 0.800802646665708})},
	{25, Coeffs({0.003621681514929, 0.007243363029857, 0.003621681514929},
			 {1., -1.822694925196308, 0.837181651256023})},
	{50, Coeffs({0.020083365564211, 0.040166731128423, 0.020083365564211},
			 {1., -1.561018075800718, 0.641351538057563})}};

template<class T>
class Downsampler
{
private:
	std::shared_ptr<DigitalFilter> m_pDigitalFilter;
	int m_nDownsamplingFactor;
	int m_nChunkLen;
	bool m_bFilterSignal;
	std::vector<T> m_ptFilteredSignal;
	void SetupMemory()
	{
		m_ptFilteredSignal.reserve(m_nChunkLen * m_nDownsamplingFactor);
		m_ptDataOut.reserve(m_nChunkLen);
		m_ptFilteredSignal.resize(m_nChunkLen * m_nDownsamplingFactor, 0);
		m_ptDataOut.resize(m_nChunkLen,0);

	}
public:
	std::vector<T> m_ptDataOut;
	Downsampler(int nDownsamplingFactor, int nChunkLen, bool bFilterSignal = true)
	{
		m_nDownsamplingFactor = nDownsamplingFactor;
		m_nChunkLen = nChunkLen;
		m_bFilterSignal = bFilterSignal;
		auto coeffs = pdCoeffs.at(nDownsamplingFactor);
		auto pdBCoeffs = coeffs.pdB.data();
		auto pdACoeffs = coeffs.pdA.data();

		m_pDigitalFilter.reset(new DigitalFilter(2, nChunkLen * nDownsamplingFactor, pdBCoeffs, pdACoeffs, nullptr));
		SetupMemory();
	}

	Downsampler(const Downsampler& obj)
	{
		m_nDownsamplingFactor = obj.m_nDownsamplingFactor;
		m_nChunkLen = obj.m_nChunkLen;
		m_bFilterSignal = obj.m_bFilterSignal;
		m_ptDataOut = obj.m_ptDataOut;
		m_ptFilteredSignal = obj.m_ptFilteredSignal;
		m_pDigitalFilter.reset(new DigitalFilter(*(obj.m_pDigitalFilter.get())));
		SetupMemory();
	}
	void Downsample(T* ptDataIn)
	{
		size_t sz = sizeof(T);
		if (m_bFilterSignal)
		{
			m_ptDataOut.clear();
			m_pDigitalFilter->Filter(ptDataIn, &m_ptFilteredSignal[0]);
			for (int i = 0; i < m_nChunkLen; i++)
				m_ptDataOut.push_back(m_ptFilteredSignal[i * m_nDownsamplingFactor]);
		}
		else
			m_ptDataOut.assign(ptDataIn, ptDataIn+(m_nChunkLen+m_nDownsamplingFactor));
		
	}
	~Downsampler() = default;
};
