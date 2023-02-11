#pragma once
#include <array>
#include <random>
#include "Phasor.h"
#include "PRM.h"
#include "../arch/Interpolation.h"

namespace audio
{
	struct Perlin
	{
		static constexpr int NumOctaves = 7;
		static constexpr int NoiseOvershoot = 4;
	
		static constexpr int NoiseSize = 1 << NumOctaves;
		static constexpr int NoiseSizeMax = NoiseSize - 1;
		
		struct PerlinPhase
		{
			float phase, idx;
		};

		Perlin() :
			// misc
			fs(1.f),
			fsInv(1.f),
			// noise
			noise(),
			gainBuffer(),
			// phase
			phasor(),
			phaseBuffer(),
			noiseIdx(0),
			// parameters
			octavesPRM(1.f),
			widthPRM(0.f),
			rateHz(0.f),
			octaves(1.f),
			width(0.f)
		{
			unsigned int _seed = 420 * 69 / 666 * 42;
			std::random_device rd;
			std::mt19937 mt(rd());
			std::uniform_real_distribution<float> dist(-.8f, .8f); // compensate spline overshoot
			
			for (auto s = 0; s < NoiseSize; ++s, ++_seed)
			{
				mt.seed(_seed);
				noise[s] = dist(mt);
			}

			for (auto s = 0; s < NoiseOvershoot; ++s)
				noise[NoiseSize + s] = noise[s];

			for (auto o = 0; o < gainBuffer.size(); ++o)
			{
				gainBuffer[o] = 1.f / static_cast<float>(1 << o);
			}
		}

		/* sampleRate, blockSize */
		void prepare(float _sampleRate, int blockSize)
		{
			fs = _sampleRate;
			fsInv = 1.f / fs;
			octavesPRM.prepare(fs, blockSize, 10.f);
			widthPRM.prepare(fs, blockSize, 20.f);
			phaseBuffer.resize(blockSize);
		}

		/* rateHz, octaves, width, seed */
		void setParameters(float _rateHz, float _octaves, float _width, float) noexcept
		{
			rateHz = _rateHz;
			octaves = _octaves;
			width = _width * 2.f;
		}

		/* samples, numChannels, numSamples */
		void operator()(float* const* samples, int numChannels, int numSamples) noexcept
		{
			synthesizePhasor(numSamples);

			processOctaves(samples[0], numSamples);
			
			if(numChannels == 2)
				processWidth(samples, numSamples);
		}

		// misc
		float fs, fsInv;
		
		// noise
		std::array<float, NoiseSize + NoiseOvershoot> noise;
		std::array<float, NumOctaves + 1> gainBuffer;
		
		// phase
		Phasor<double> phasor;
		std::vector<float> phaseBuffer;
		int noiseIdx;

		// parameters
		PRM octavesPRM, widthPRM;
		float rateHz, octaves, width;

	protected:
		void synthesizePhasor(int numSamples) noexcept
		{
			phasor.inc = rateHz * fsInv;
			for (auto s = 0; s < numSamples; ++s)
			{
				const auto phaseInfo = phasor();
				if (phaseInfo.retrig)
					noiseIdx = (noiseIdx + 1) & NoiseSizeMax;
				
				phaseBuffer[s] = static_cast<float>(phaseInfo.phase) + static_cast<float>(noiseIdx);
			}
		}

		void processOctaves(float* smpls, int numSamples) noexcept
		{
			const auto octFloor = std::floor(octaves);

			for (auto s = 0; s < numSamples; ++s)
			{
				auto sample = 0.f;
				for (auto o = 0; o < octFloor; ++o)
				{
					const auto phase = getPhaseOctaved(phaseBuffer[s], o);
					sample += interpolate::cubicHermiteSpline(noise.data(), phase) * gainBuffer[o];
				}

				smpls[s] = sample;
			}
			
			auto gain = 0.f;
			{
				for (auto o = 0; o < octFloor; ++o)
					gain += gainBuffer[o];
			}

			const auto octFrac = octaves - octFloor;
			if (octFrac != 0.f)
			{
				const auto octCeil = static_cast<int>(octFloor) + 1;

				for (auto s = 0; s < numSamples; ++s)
				{
					auto sample = 0.f;
					for (auto o = 0; o < octCeil; ++o)
					{
						const auto phase = getPhaseOctaved(phaseBuffer[s], o);
						sample += interpolate::cubicHermiteSpline(noise.data(), phase) * gainBuffer[o];
					}

					smpls[s] += octFrac * (sample - smpls[s]);
				}

				gain += octFrac * gainBuffer[octCeil];
			}
			
			SIMD::multiply(smpls, 1.f / std::sqrt(gain), numSamples);
		}

		void processWidth(float* const* samples, int numSamples) noexcept
		{
			const auto widthBuf = widthPRM(width, numSamples);
			if (!widthPRM.smoothing)
				if (width == 0.f)
					return SIMD::copy(samples[1], samples[0], numSamples);
				else
					SIMD::add(phaseBuffer.data(), width, numSamples);
			else
				SIMD::add(phaseBuffer.data(), widthBuf, numSamples);

			processOctaves(samples[1], numSamples);
		}

		float getPhaseOctaved(float phaseInfo, int o) const noexcept
		{
			const auto ox2 = 1 << o;
			const auto oPhase = phaseInfo * ox2;
			const auto oPhaseFloor = std::floor(oPhase);
			const auto oPhaseInt = static_cast<int>(oPhaseFloor) & NoiseSizeMax;
			return oPhase - oPhaseFloor + static_cast<float>(oPhaseInt);
		}

		// debug:
		void discontinuityJassert(float* smpls, int numSamples, float threshold = .1f)
		{
			auto lastSample = smpls[0];
			for (auto s = 1; s < numSamples; ++s)
			{
				auto curSample = smpls[s];
				jassert(abs(curSample - lastSample) < threshold);
				lastSample = curSample;
			}
		}
	};
}

/*

todo features:
	octaves smoothing
	seed
	temposync rate

optimize:
	-

bugs:
	-

*/