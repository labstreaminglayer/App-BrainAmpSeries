#include "mainwindow.h"
#include <QApplication>
#include <string>
#include "Downsampler.h"
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>


void testFilter()
{
	int nDownsamplingFactor = 2;
	int nChunkLen = 10;
	std::vector<Downsampler<double>*> downsamplers;
	downsamplers.push_back(new Downsampler<double>(nDownsamplingFactor, nChunkLen));
	double* pdDataIn = (double*)malloc(sizeof(double) * nChunkLen * nDownsamplingFactor);
	double* pdDataOut = (double*)malloc(sizeof(double) * nChunkLen);
	std::ifstream ifDataIn;
	std::ofstream ofDataOut;
	std::string str;
	int idx = 0;
	ifDataIn.open("C:\\Users\\david.medine\\matlab\\random_noise.txt");
	ofDataOut.open("C:\\Users\\david.medine\\matlab\\filtered.txt");
	while (std::getline(ifDataIn, str))
	{
		pdDataIn[idx++] = std::stod(str);
		if (idx >= nDownsamplingFactor * nChunkLen)
		{
			downsamplers[0]->Downsample(pdDataIn);
			for (int i = 0; i < nChunkLen; i++)
				ofDataOut << downsamplers[0]->m_ptDataOut[i] << "\n";
			idx = 0;
		}
	}
	free(pdDataIn);
	free(pdDataOut);
	delete downsamplers[0];
}

void testDownsampler()
{
	int nDownsamplingFactor = 5;
	int nChunkLen = 10;
	std::vector<Downsampler<double>*> downsamplers;
	downsamplers.push_back(new Downsampler<double>(nDownsamplingFactor, nChunkLen));
	double* pdDataIn = (double*)malloc(sizeof(double) * nChunkLen * nDownsamplingFactor);
	double* pdDataOut = (double*)malloc(sizeof(double) * nChunkLen);
	std::ifstream ifDataIn;
	std::ofstream ofDataOut;
	std::string str;
	int idx = 0;
	ifDataIn.open("C:\\Users\\david.medine\\matlab\\additive.txt");
	ofDataOut.open("C:\\Users\\david.medine\\matlab\\downsampled.txt");
	while(std::getline(ifDataIn, str))
	{
		pdDataIn[idx++] = std::stod(str);
		if (idx >= nDownsamplingFactor * nChunkLen)
		{
			downsamplers[0]->Downsample(pdDataIn);
			for (int i = 0; i < nChunkLen; i++)
				ofDataOut << downsamplers[0]->m_ptDataOut[i] << "\n";
			idx = 0;
		}
	}
	free(pdDataIn);
	free(pdDataOut);
	delete downsamplers[0];

}

int main(int argc, char* argv[]) {

	//testDownsampler();
	//testFilter();
	//return 0;
	// determine the startup config file...
	const char* config_file = "BrainAmpSeries.cfg";
	for (int k = 1; k < argc; k++)
		if (std::string(argv[k]) == "-c" || std::string(argv[k]) == "--config")
			config_file = argv[k + 1];

	QApplication a(argc, argv);
	MainWindow w(nullptr, config_file);
	w.show();
	return a.exec();
}
