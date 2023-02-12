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
		using PlayHeadPos = juce::AudioPlayHead::CurrentPositionInfo;

		static constexpr int NumOctaves = 7;
		static constexpr int NoiseOvershoot = 4;
	
		static constexpr int NoiseSize = 1 << NumOctaves;
		static constexpr int NoiseSizeMax = NoiseSize - 1;

		Perlin() :
			// misc
			sampleRateInv(1.),
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
			rateHz(0.),
			rateBeats(-1.),
			rateBeatsInv(1.),
			octavesPRM(1.f),
			widthPRM(0.f),
			phsPRM(0.f),
			octaves(1.f),
			width(0.f),
			phs(0.f),
			rateType(false)
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
				gainBuffer[o] = 1.f / static_cast<float>(1 << o);
		}

		/* sampleRate, blockSize */
		void prepare(float _sampleRate, int blockSize)
		{
			fs = _sampleRate;
			fsInv = 1.f / fs;
			sampleRateInv = static_cast<double>(fsInv);
			octavesPRM.prepare(fs, blockSize, 10.f);
			widthPRM.prepare(fs, blockSize, 20.f);
			phsPRM.prepare(fs, blockSize, 20.f);
			phaseBuffer.resize(blockSize);
		}

		/* rateHz, rateBeats, octaves, width, phase, rateType */
		void setParameters(double _rateHz, double _rateBeats, float _octaves, float _width, float _phs, bool _rateType) noexcept
		{
			rateHz = _rateHz * sampleRateInv;
			if (rateBeats != _rateBeats)
			{
				rateBeats = _rateBeats;
				rateBeatsInv = .25 / rateBeats;
			}
			octaves = _octaves;
			width = _width;
			phs = _phs;
			rateType = _rateType;
		}

		/* samples, numChannels, numSamples, playHeadPos */
		void operator()(float* const* samples, int numChannels, int numSamples,
			const PlayHeadPos& playHeadPos) noexcept
		{
			synthesizePhasor(numSamples, playHeadPos);

			const auto octavesBuf = octavesPRM(octaves, numSamples);
			processOctaves(samples[0], octavesBuf, numSamples);
			
			if(numChannels == 2)
				processWidth(samples, octavesBuf, numSamples);
		}

		// misc
		double sampleRateInv;
		float fs, fsInv;
		
		// noise
		std::array<float, NoiseSize + NoiseOvershoot> noise;
		std::array<float, NumOctaves + 2> gainBuffer;
		
		// phase
		Phasor<double> phasor;
		std::vector<float> phaseBuffer;
		int noiseIdx;

		// parameters
		PRM octavesPRM, widthPRM, phsPRM;
		double rateHz, rateBeats, rateBeatsInv;
		float octaves, width, phs;
		bool rateType;

	protected:
		void synthesizePhasor(int numSamples, const PlayHeadPos& playHeadPos) noexcept
		{
			if (!rateType)
				phasor.inc = rateHz;
			else
			{
				const auto bpMins = playHeadPos.bpm;
				const auto bpSecs = bpMins / 60.;
				const auto bpSamples = bpSecs * sampleRateInv;
					
				phasor.inc = bpSamples * rateBeatsInv;

				if (playHeadPos.isPlaying)
				{
					const auto ppq = playHeadPos.ppqPosition * rateBeatsInv;
					const auto ppqFloor = std::floor(ppq);

					noiseIdx = static_cast<int>(ppqFloor) & NoiseSizeMax;
					phasor.phase.phase = ppq - ppqFloor;
				}
			}

			const auto phsBuf = phsPRM(phs, numSamples);

			if(!phsPRM.smoothing)
				for (auto s = 0; s < numSamples; ++s)
				{
					const auto phaseInfo = phasor();
					if (phaseInfo.retrig)
						noiseIdx = (noiseIdx + 1) & NoiseSizeMax;

					phaseBuffer[s] = static_cast<float>(phaseInfo.phase) + phs + static_cast<float>(noiseIdx);
				}
			else
				for (auto s = 0; s < numSamples; ++s)
				{
					const auto phaseInfo = phasor();
					if (phaseInfo.retrig)
						noiseIdx = (noiseIdx + 1) & NoiseSizeMax;

					phaseBuffer[s] = static_cast<float>(phaseInfo.phase) + phsBuf[s] + static_cast<float>(noiseIdx);
				}
		}

		void processOctaves(float* smpls, const float* octavesBuf, int numSamples) noexcept
		{
			if (!octavesPRM.smoothing)
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
				for (auto o = 0; o < octFloor; ++o)
					gain += gainBuffer[o];

				const auto octFrac = octaves - octFloor;
				if (octFrac != 0.f)
				{
					const auto octFloorInt = static_cast<int>(octFloor);

					for (auto s = 0; s < numSamples; ++s)
					{
						const auto phase = getPhaseOctaved(phaseBuffer[s], octFloorInt);
						const auto sample = interpolate::cubicHermiteSpline(noise.data(), phase) * gainBuffer[octFloorInt];
						smpls[s] += octFrac * sample;
					}

					gain += octFrac * gainBuffer[octFloorInt];
				}
				
				SIMD::multiply(smpls, 1.f / std::sqrt(gain), numSamples);
			}
			else
			{
				for (auto s = 0; s < numSamples; ++s)
				{
					const auto octFloor = std::floor(octavesBuf[s]);

					auto sample = 0.f;
					for (auto o = 0; o < octFloor; ++o)
					{
						const auto phase = getPhaseOctaved(phaseBuffer[s], o);
						sample += interpolate::cubicHermiteSpline(noise.data(), phase) * gainBuffer[o];
					}

					smpls[s] = sample;

					auto gain = 0.f;
					for (auto o = 0; o < octFloor; ++o)
						gain += gainBuffer[o];

					const auto octFrac = octavesBuf[s] - octFloor;
					if (octFrac != 0.f)
					{
						const auto octFloorInt = static_cast<int>(octFloor);
						
						const auto phase = getPhaseOctaved(phaseBuffer[s], octFloorInt);
						sample = interpolate::cubicHermiteSpline(noise.data(), phase) * gainBuffer[octFloorInt];
						smpls[s] += octFrac * sample;

						gain += octFrac * gainBuffer[octFloorInt];
					}

					smpls[s] /= std::sqrt(gain);
				}
			}
		}

		void processWidth(float* const* samples, const float* octavesBuf, int numSamples) noexcept
		{
			const auto widthBuf = widthPRM(width, numSamples);
			if (!widthPRM.smoothing)
				if (width == 0.f)
					return SIMD::copy(samples[1], samples[0], numSamples);
				else
					SIMD::add(phaseBuffer.data(), width, numSamples);
			else
				SIMD::add(phaseBuffer.data(), widthBuf, numSamples);

			processOctaves(samples[1], octavesBuf, numSamples);
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

		void controlRange(float* const* samples, int numChannels, int numSamples) noexcept
		{
			for(auto ch = 0; ch < numChannels; ++ch)
				for(auto s = 0; s < numSamples; ++s)
					if (std::abs(samples[ch][s]) > 1.f)
						DBG(samples[ch][s] << " > 1");
					else if (std::abs(samples[ch][s]) == 1.f)
						DBG(samples[ch][s] << " == 1");
		}
	};
}

/*

todo features:
	-

optimize:
	fixed blocksize

bugs:
	temposync
		smooth rate changes

*/