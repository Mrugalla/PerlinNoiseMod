#pragma once
#include "../arch/Conversion.h"
#include <cmath>
#include <complex>

namespace audio
{
	struct FilterBandpass
	{
		/* startVal */
		FilterBandpass(float = 0.f);

		void clear() noexcept;

		/* frequency fc [0, .5[, q-factor q [1, 160..] */
		void setFc(float, float) noexcept;
		
		void copy(const FilterBandpass&) noexcept;

		float operator()(float) noexcept;

		float processSample(float) noexcept;

		/* scaledFreq */
		std::complex<float> response(float) const noexcept;
		/* scaledFreq */
		float responseDb(float) const noexcept;

	protected:
		float alpha, cosOmega;
		float a0, a1, a2, b0, b1, b2;
		float     x1, x2,     y1, y2;

		void updateCoefficients() noexcept;
	};

	template<size_t NumFilters>
	struct FilterBandpassSlope
	{
		FilterBandpassSlope();

		void clear() noexcept;

		void setStage(int) noexcept;

		/* frequency fc [0, .5[, q-factor q [1, 160..] */
		void setFc(float fc, float q) noexcept;

		void copy(FilterBandpassSlope<NumFilters>&) noexcept;

		float operator()(float) noexcept;

		/* scaledFreq [0, 22050[ */
		std::complex<float> response(float) const noexcept;

	protected:
		std::array<FilterBandpass, NumFilters> filters;
		int stage;
	};

	//////////////////////////////////////////////////////////////////

	struct IIR
	{
		enum class Type
		{
			LP,
			HP,
			BP,
			BR,
			AP,
			LS,
			HS,
			Notch,
			Bell,
			NumTypes
		};

		/* startVal */
		IIR(float = 0.f);

		void clear() noexcept;

		/* type, frequency fc [0, .5[, q-factor q [1, 160..] */
		void setFc(Type, float, float) noexcept;

		/* frequency fc [0, .5[, q-factor q [1, 160..] */
		void setFcBP(float, float) noexcept;

		/* frequency fc [0, .5[, q-factor q [1, 160..] */ // doesn't work yet
		void setFcLP(float, float) noexcept;
		
		/* frequency fc [0, .5[, q-factor q [1, 160..] */ // doesn't work yet
		void setFcHP(float, float) noexcept;

		void copy(const IIR&) noexcept;

		float operator()(float) noexcept;

		float processSample(float) noexcept;

		/* scaledFreq */
		std::complex<float> response(float) const noexcept;
		/* scaledFreq */
		float responseDb(float) const noexcept;

	protected:
		float alpha, cosOmega;
		float a0, a1, a2, b0, b1, b2;
		float     x1, x2, y1, y2;
	};
}