#pragma once
#include <array>
#include <random>
#include "Phasor.h"
#include "PRM.h"
#include "../arch/Interpolation.h"

namespace audio
{
	inline void generateProceduralNoise(float* noise, int size, unsigned int seed)
	{
		std::random_device rd;
		std::mt19937 mt(rd());
		std::uniform_real_distribution<float> dist(-.8f, .8f); // compensate spline overshoot

		for (auto s = 0; s < size; ++s, ++seed)
		{
			mt.seed(seed);
			noise[s] = dist(mt);
		}
	}

	struct Perlin
	{
		using PlayHeadPos = juce::AudioPlayHead::CurrentPositionInfo;

		static constexpr int NumOctaves = 7;
		static constexpr int NoiseOvershoot = 4;
	
		static constexpr int NoiseSize = 1 << NumOctaves;
		static constexpr int NoiseSizeMax = NoiseSize - 1;

		using NoiseArray = std::array<float, NoiseSize + NoiseOvershoot>;
		using GainBuffer = std::array<float, NumOctaves + 2>;

		Perlin() :
			// misc
			sampleRateInv(1.),
			fs(1.f),
			// phase
			phasor(),
			phaseBuffer(),
			noiseIdx(0),
			// parameters
			rateHz(0.),
			rateBeatsInv(1.),
			octaves(1.f),
			width(0.f),
			phs(0.f),
			rateType(false)
		{
		}

		/* sampleRate, blockSize */
		void prepare(float _sampleRate, int blockSize)
		{
			fs = _sampleRate;
			const auto fsInv = 1.f / fs;
			sampleRateInv = static_cast<double>(fsInv);
			phaseBuffer.resize(blockSize);
		}

		/* rateHz, rateBeats, octaves, width, phase, rateType */
		void setParameters(double _rateHz, double _rateBeats, float _octaves, float _width, float _phs, bool _rateType) noexcept
		{
			rateHz = _rateHz;
			rateBeatsInv = .25 / _rateBeats;
			octaves = _octaves;
			width = _width;
			phs = _phs;
			rateType = _rateType;
		}

		/* samples, noise, numChannels, numSamples, playHeadPos */
		void operator()(float* const* samples, const float* noise, const float* gainBuffer,
			const float* octavesBuf, const float* phsBuf, const float* widthBuf, int numChannels, int numSamples,
			const PlayHeadPos& playHeadPos, bool octavesSmoothing, bool phsSmoothing, bool widthSmoothing) noexcept
		{
			synthesizePhasor(phsBuf, numSamples, playHeadPos, phsSmoothing);
			
			processOctaves(samples[0], octavesBuf, noise, gainBuffer, numSamples, octavesSmoothing);
			
			if(numChannels == 2)
				processWidth(samples, octavesBuf, widthBuf, noise, gainBuffer, numSamples, octavesSmoothing, widthSmoothing);
		}

		// misc
		double sampleRateInv;
		float fs;
		
		// phase
		Phasor<double> phasor;
		std::vector<float> phaseBuffer;
		int noiseIdx;

		// parameters
		double rateHz, rateBeatsInv;
		float octaves, width, phs;
		bool rateType;

	protected:
		void synthesizePhasor(const float* phsBuf, int numSamples, const PlayHeadPos& playHeadPos,
			bool phaseSmoothing) noexcept
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

			if(!phaseSmoothing)
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

		void processOctaves(float* smpls, const float* octavesBuf,
			const float* noise, const float* gainBuffer, int numSamples,
			bool octavesSmoothing) noexcept
		{
			if (!octavesSmoothing)
			{
				const auto octFloor = std::floor(octaves);

				for (auto s = 0; s < numSamples; ++s)
				{
					auto sample = 0.f;
					for (auto o = 0; o < octFloor; ++o)
					{
						const auto phase = getPhaseOctaved(phaseBuffer[s], o);
						sample += interpolate::cubicHermiteSpline(noise, phase) * gainBuffer[o];
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
						const auto sample = interpolate::cubicHermiteSpline(noise, phase) * gainBuffer[octFloorInt];
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
						sample += interpolate::cubicHermiteSpline(noise, phase) * gainBuffer[o];
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
						sample = interpolate::cubicHermiteSpline(noise, phase) * gainBuffer[octFloorInt];
						smpls[s] += octFrac * sample;

						gain += octFrac * gainBuffer[octFloorInt];
					}

					smpls[s] /= std::sqrt(gain);
				}
			}
		}
		
		void processWidth(float* const* samples, const float* octavesBuf,
			const float* widthBuf, const float* noise, const float* gainBuffer, int numSamples,
			bool octavesSmoothing, bool widthSmoothing) noexcept
		{
			if (!widthSmoothing)
				if (width == 0.f)
					return SIMD::copy(samples[1], samples[0], numSamples);
				else
					SIMD::add(phaseBuffer.data(), width, numSamples);
			else
				SIMD::add(phaseBuffer.data(), widthBuf, numSamples);

			processOctaves(samples[1], octavesBuf, noise, gainBuffer, numSamples, octavesSmoothing);
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

	struct Perlin2
	{
		using AudioBuffer = juce::AudioBuffer<float>;

		Perlin2() :
			// misc
			sampleRateInv(1.),
			// noise
			noise(),
			gainBuffer(),
			// perlin
			prevBuffer(),
			perlins(),
			perlinIndex(0),
			// parameters
			octavesPRM(1.f),
			widthPRM(0.f),
			phsPRM(0.f),
			rateHz(0.),
			rateBeats(-1.),
			octaves(1.f),
			width(0.f),
			phs(0.f),
			// crossfade
			xFadeBuffer(),
			xPhase(0.f),
			xInc(0.f),
			crossfading(false)
		{
			generateProceduralNoise(noise.data(), Perlin::NoiseSize, 420 * 69 / 666 * 42);
			
			for (auto s = 0; s < Perlin::NoiseOvershoot; ++s)
				noise[Perlin::NoiseSize + s] = noise[s];

			for (auto o = 0; o < gainBuffer.size(); ++o)
				gainBuffer[o] = 1.f / static_cast<float>(1 << o);
		}

		void prepare(float fs, int blockSize)
		{
			const auto fsInv = 1.f / fs;
			sampleRateInv = static_cast<double>(fsInv);

			prevBuffer.setSize(2, blockSize, false, false, false);
			for (auto& perlin : perlins)
				perlin.prepare(fs, blockSize);
			xInc = msInInc(420.f, fs);
			xFadeBuffer.resize(blockSize);
			octavesPRM.prepare(fs, blockSize, 10.f);
			widthPRM.prepare(fs, blockSize, 20.f);
			phsPRM.prepare(fs, blockSize, 20.f);
		}

		void setParameters(double _rateHz, double _rateBeats, float _octaves, float _width, float _phs, bool _rateType) noexcept
		{
			rateHz = _rateHz * sampleRateInv;
			octaves = _octaves;
			width = _width;
			phs = _phs;
			
			if (rateBeats != _rateBeats && !crossfading)
			{
				perlins[perlinIndex].setParameters(rateHz, rateBeats, octaves, width, phs, _rateType);
				rateBeats = _rateBeats;
				perlinIndex = 1 - perlinIndex;
				perlins[perlinIndex].setParameters(rateHz, rateBeats, octaves, width, phs, _rateType);
				crossfading = true;
				xPhase = 0.;
			}
			else
				perlins[perlinIndex].setParameters(rateHz, rateBeats, octaves, width, phs, _rateType);
		}

		void operator()(float* const* samples, int numChannels, int numSamples,
			const PlayHeadPos& playHeadPos) noexcept
		{
			const auto octavesBuf = octavesPRM(octaves, numSamples);
			const auto phsBuf = phsPRM(phs, numSamples);
			const auto widthBuf = widthPRM(width, numSamples);
			
			perlins[perlinIndex]
			(
				samples,
				noise.data(),
				gainBuffer.data(),
				octavesBuf,
				phsBuf,
				widthBuf,
				numChannels,
				numSamples,
				playHeadPos,
				octavesPRM.smoothing,
				phsPRM.smoothing,
				widthPRM.smoothing
			);

			if (crossfading)
			{
				auto prevSamples = prevBuffer.getArrayOfWritePointers();
				perlins[1 - perlinIndex]
				(
					prevSamples,
					noise.data(),
					gainBuffer.data(),
					octavesBuf,
					phsBuf,
					widthBuf,
					numChannels,
					numSamples,
					playHeadPos,
					octavesPRM.smoothing,
					phsPRM.smoothing,
					widthPRM.smoothing
				);
				
				for (auto s = 0; s < numSamples; ++s)
				{
					xFadeBuffer[s] = xPhase;
					xPhase += xInc;
					if (xPhase > 1.f)
					{
						crossfading = false;
						xPhase = 1.f;
					}
				}
				
				for (auto ch = 0; ch < numChannels; ++ch)
				{
					auto smpls = samples[ch];
					const auto prevSmpls = prevSamples[ch];
					
					for (auto s = 0; s < numSamples; ++s)
					{
						const auto prev = prevSmpls[s];
						const auto cur = smpls[s];
						
						const auto xFade = xFadeBuffer[s];
						const auto xPi = xFade * Pi;
						const auto xPrev = std::cos(xPi) + 1.f;
						const auto xCur = std::cos(xPi + Pi) + 1.f;

						smpls[s] = (prev * xPrev + cur * xCur) * .5f;
					}
				}
			}
		}
		
		// misc
		double sampleRateInv;
		// noise
		Perlin::NoiseArray noise;
		Perlin::GainBuffer gainBuffer;
		// perlin
		AudioBuffer prevBuffer;
		std::array<Perlin, 2> perlins;
		int perlinIndex;
		// parameters
		PRM octavesPRM, widthPRM, phsPRM;
		double rateHz, rateBeats;
		float octaves, width, phs;
		// crossfade
		std::vector<float> xFadeBuffer;
		float xPhase, xInc;
		bool crossfading;
	};
}

/*

todo features:
	-

optimize:
	handle rateType from Perlin2
	memory used for crossfade reduced
	fixed blocksize

bugs:
	jumps in project position at temposync rate = discontinuity

*/