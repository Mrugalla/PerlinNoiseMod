#pragma once
#include <array>
#include <random>
#include "Phasor.h"
#include "PRM.h"
#include "../arch/Interpolation.h"

#define oopsie(x) jassert(!(x))

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
			octaves(1.f),
			width(0.f),
			phs(0.f)
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

		/* octaves, width, phase */
		void setParameters(float _octaves, float _width, float _phs) noexcept
		{
			octaves = _octaves;
			width = _width;
			phs = _phs;
		}

		/* rateHzInv */
		void processRateFree(double rateHzInv) noexcept
		{
			phasor.inc = rateHzInv;
		}

		/* playHeadPos, rateBeatsInv, beatsPerSamples */
		void processRateSync(const PlayHeadPos& playHeadPos, double rateBeatsInv, double beatsPerSamples) noexcept
		{
			phasor.inc = beatsPerSamples * rateBeatsInv;

			if (playHeadPos.isPlaying)
			{
				const auto ppq = playHeadPos.ppqPosition * rateBeatsInv + .5;
				const auto ppqFloor = std::floor(ppq);

				noiseIdx = static_cast<int>(ppqFloor) & NoiseSizeMax;
				phasor.phase.phase = ppq - ppqFloor;
			}
		}

		/* samples, noise, gainBuffer, octavesBuffer, phsBuf, widthBuf, smoothBuf,
		numChannels, numSamples, octavesSmoothing, phsSmoothing, widthSmoothing */
		void operator()(float* const* samples, const float* noise, const float* gainBuffer,
			const float* octavesBuf, const float* phsBuf, const float* widthBuf, const float* smoothBuf,
			int numChannels, int numSamples,
			bool octavesSmoothing, bool phsSmoothing, bool widthSmoothing) noexcept
		{
			synthesizePhasor(phsBuf, numSamples, phsSmoothing);
			
			processOctaves(samples[0], octavesBuf, noise, gainBuffer, smoothBuf, numSamples, octavesSmoothing);
			
			if(numChannels == 2)
				processWidth(samples, octavesBuf, widthBuf, noise, gainBuffer, smoothBuf, numSamples, octavesSmoothing, widthSmoothing);
		}

		// misc
		double sampleRateInv;
		float fs;
		
		// phase
		Phasor<double> phasor;
		std::vector<float> phaseBuffer;
		int noiseIdx;

		// parameters
		float octaves, width, phs;
		
	protected:
		void synthesizePhasor(const float* phsBuf, int numSamples, bool phaseSmoothing) noexcept
		{
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
			const float* noise, const float* gainBuffer, const float* smoothBuf, int numSamples,
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
						const auto smplLerp = noise[static_cast<int>(std::round(phase)) + 1];
						const auto smplSmooth = interpolate::cubicHermiteSpline(noise, phase);
						sample += smplLerp + smoothBuf[s] * (smplSmooth - smplLerp) * gainBuffer[o];
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
						const auto smplLerp = noise[static_cast<int>(std::round(phase)) + 1];
						const auto smplSmooth = interpolate::cubicHermiteSpline(noise, phase);
						smpls[s] += octFrac * (smplLerp + smoothBuf[s] * (smplSmooth - smplLerp)) * gainBuffer[octFloorInt];
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
						const auto smplLerp = noise[static_cast<int>(std::round(phase)) + 1];
						const auto smplSmooth = interpolate::cubicHermiteSpline(noise, phase);
						sample += (smplLerp + smoothBuf[s] * (smplSmooth - smplLerp)) * gainBuffer[o];
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
						const auto smplLerp = noise[static_cast<int>(std::round(phase)) + 1];
						const auto smplSmooth = interpolate::cubicHermiteSpline(noise, phase);
						smpls[s] += octFrac * (smplLerp + smoothBuf[s] * (smplSmooth - smplLerp)) * gainBuffer[octFloorInt];

						gain += octFrac * gainBuffer[octFloorInt];
					}

					smpls[s] /= std::sqrt(gain);
				}
			}
		}
		
		void processWidth(float* const* samples, const float* octavesBuf,
			const float* widthBuf, const float* noise, const float* gainBuffer, const float* smoothBuf,
			int numSamples, bool octavesSmoothing, bool widthSmoothing) noexcept
		{
			if (!widthSmoothing)
				if (width == 0.f)
					return SIMD::copy(samples[1], samples[0], numSamples);
				else
					SIMD::add(phaseBuffer.data(), width, numSamples);
			else
				SIMD::add(phaseBuffer.data(), widthBuf, numSamples);

			processOctaves(samples[1], octavesBuf, noise, gainBuffer, smoothBuf, numSamples, octavesSmoothing);
		}

		float getPhaseOctaved(float phaseInfo, int o) const noexcept
		{
			const auto ox2 = 1 << o;
			const auto oPhase = phaseInfo * static_cast<float>(ox2);
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
				oopsie(abs(curSample - lastSample) > threshold);
				lastSample = curSample;
			}
		}

		void controlRange(float* const* samples, int numChannels, int numSamples) noexcept
		{
			for(auto ch = 0; ch < numChannels; ++ch)
				for (auto s = 0; s < numSamples; ++s)
				{
					oopsie(std::abs(samples[ch][s]) >= 1.f);
					oopsie(std::abs(samples[ch][s]) <= -1.f);
				}
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
			smoothPRM(1.f),
			//
			rateHz(0.),
			rateBeats(-1.),
			rateBeatsInv(1.),
			octaves(1.f),
			width(0.f),
			phs(0.f),
			temposync(false),
			// crossfade
			xFadeBuffer(),
			xPhase(0.f),
			xInc(0.f),
			crossfading(false),
			seed(),
			// project position
			curPosEstimate(-1)
		{
			setSeed(420 * 69 / 666 * 42);
			
			for (auto s = 0; s < Perlin::NoiseOvershoot; ++s)
				noise[Perlin::NoiseSize + s] = noise[s];

			for (auto o = 0; o < gainBuffer.size(); ++o)
				gainBuffer[o] = 1.f / static_cast<float>(1 << o);
		}

		void setSeed(int _seed)
		{
			seed.store(_seed);
			generateProceduralNoise(noise.data(), Perlin::NoiseSize, static_cast<unsigned int>(_seed));
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
			smoothPRM.prepare(fs, blockSize, 10.f);
		}

		void setParameters(double _rateHz, double _rateBeats, float _octaves, float _width, float _phs, float _smooth, bool _temposync) noexcept
		{
			rateHz = _rateHz * sampleRateInv;
			octaves = _octaves;
			width = _width;
			phs = _phs;
			smooth = _smooth;
			temposync = _temposync;
			
			if (rateBeats != _rateBeats && !crossfading)
			{
				perlins[perlinIndex].setParameters(octaves, width, phs);
				rateBeats = _rateBeats;
				rateBeatsInv = .25 / rateBeats;
				perlinIndex = 1 - perlinIndex;
				perlins[perlinIndex].setParameters(octaves, width, phs);
				crossfading = true;
				xPhase = 0.;
			}
			else
				perlins[perlinIndex].setParameters(octaves, width, phs);
		}

		void operator()(float* const* samples, int numChannels, int numSamples,
			const PlayHeadPos& playHeadPos) noexcept
		{
			const auto octavesBuf = octavesPRM(octaves, numSamples);
			const auto phsBuf = phsPRM(phs, numSamples);
			const auto widthBuf = widthPRM(width, numSamples);
			auto smoothBuf = smoothPRM(smooth, numSamples);
			if(!smoothPRM.smoothing)
				SIMD::fill(smoothBuf, smooth, numSamples);
			
			if (!temposync)
				perlins[perlinIndex].processRateFree(rateHz);
			else
			{
				if (playHeadPos.isPlaying)
				{
					const auto bpMins = playHeadPos.bpm;
					const auto bpSecs = bpMins / 60.;
					const auto bpSamples = bpSecs * sampleRateInv;
					const auto curPosInSamples = playHeadPos.timeInSamples;

					if (std::abs(curPosInSamples - curPosEstimate) > 2)
					{
						crossfading = true;
						perlinIndex = 1 - perlinIndex;
					}

					perlins[perlinIndex].processRateSync(playHeadPos, rateBeatsInv, bpSamples);
					curPosEstimate = curPosInSamples + numSamples;
				}
			}

			perlins[perlinIndex]
			(
				samples,
				noise.data(),
				gainBuffer.data(),
				octavesBuf,
				phsBuf,
				widthBuf,
				smoothBuf,
				numChannels,
				numSamples,
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
					smoothBuf,
					numChannels,
					numSamples,
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
		PRM octavesPRM, widthPRM, phsPRM, smoothPRM;
		double rateHz, rateBeats, rateBeatsInv;
		float octaves, width, phs, smooth;
		bool temposync;
		// crossfade
		std::vector<float> xFadeBuffer;
		float xPhase, xInc;
		bool crossfading;
		// seed
		std::atomic<int> seed;
		// project position
		__int64 curPosEstimate;
	};
}

/*

PERLIN NOISE MOD:

todo features:
	interpolation selection (steppy (next neighbor), lerp (linear), smooth (cubic spline))

optimize:
	make interpolator that returns both nearest neighbour and spline at the same time

bugs:
	octaves is currently grainy as fuck
	jumps in project position in temposync cause discontinuity

PERLIN NOISE PLUGIN:

todo features:
	omnidirectional/bidirectional switch
	midi cc out

optimize:
	fixed blocksize

*/