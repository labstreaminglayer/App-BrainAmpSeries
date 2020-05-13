#include "downsampler.hpp"
#include <cassert>
#include <iostream>
#include <vector>

int iceil(int val, int div) {
	return val/div + (val%div)>0;
}

void resample_and_print(int* data, int downrate, int nsample, int nchan) {
	int coef1[] = {1, 1, 0};
	auto r(makeDownsampler<int>(2, nchan, coef1, 3));
	//Downsampler<int, double, int> r(2, nchan, coef1, 3);
	const auto nout = nsample*nchan/downrate;
	auto out = new int[nout], out2 = new int[nout];
	r(data, nsample, out, nsample/downrate);
	for(int i=0; i<nout; i++) std::cout << out[i] << ((i+1)%nchan? ' ' : '\n');
	r.resetState();

	std::cout << "\n\nSplit:\n";
	int processed = r(data, nsample/downrate, out, nsample/downrate/2);
	r(data + nout, nsample/downrate, out + processed, nsample/downrate/2);
	for(int i=0; i<nout; ++i) std::cout << out[i] << ((i+1)%nchan? ' ' : '\n');
	for(int i=0;i<nout; ++i) assert(out[i] == out2[i]);
}

int main(int argc, char **argv) {
	// clang-format off
	int data1[] = {
		1, 13, 25,
		1, 13, 25,
		2, 14, 26,
		2, 14, 26,
		3, 15, 27,
		3, 15, 27
	};
	int data2[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
	// clang-format on
	auto out = new double[9];
	resample_and_print(data1, 2, 6, 3);
	std::cout << "\nData2\n";
	resample_and_print(data2, 2, 12, 1);
	std::cout << std::endl;
}
