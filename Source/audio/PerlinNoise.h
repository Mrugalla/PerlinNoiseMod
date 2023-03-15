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

	inline float getInterpolatedNN(const float* noise, float phase) noexcept
	{
		return noise[static_cast<int>(std::round(phase)) + 1];
	}

	inline float getInterpolatedLerp(const float* noise, float phase) noexcept
	{
		return interpolate::lerp(noise, phase + 1.5f);
	}

	inline float getInterpolatedSpline(const float* noise, float phase) noexcept
	{
		return interpolate::cubicHermiteSpline(noise, phase);
	}

	struct Perlin
	{
		enum class Shape
		{
			NN, Lerp, Spline, NumShapes
		};

		using PlayHeadPos = juce::AudioPlayHead::CurrentPositionInfo;
		using InterpolationFunc = float(*)(const float*, float) noexcept;

		static constexpr int NumOctaves = 7;
		static constexpr int NoiseOvershoot = 4;
	
		static constexpr int NoiseSize = 1 << NumOctaves;
		static constexpr int NoiseSizeMax = NoiseSize - 1;

		using NoiseArray = std::array<float, NoiseSize + NoiseOvershoot>;
		using GainBuffer = std::array<float, NumOctaves + 2>;

		Perlin() :
			// misc
			interpolationFuncs{ &getInterpolatedNN, &getInterpolatedLerp, &getInterpolatedSpline },
			sampleRateInv(1.),
			fs(1.f),
			// phase
			phasor(),
			phaseBuffer(),
			noiseIdx(0)
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

		/* rateHzInv */
		void updateSpeed(double rateHzInv) noexcept
		{
			phasor.inc = rateHzInv;
		}

		/*  playHeadPos, rateHz */
		void updatePosition(const PlayHeadPos& playHeadPos, double rateHz) noexcept
		{
			const auto timeInSamples = playHeadPos.timeInSamples;
			const auto timeInSecs = static_cast<double>(timeInSamples) * sampleRateInv;
			const auto timeInHz = timeInSecs * rateHz;
			const auto timeInHzFloor = std::floor(timeInHz);
			
			noiseIdx = static_cast<int>(timeInHzFloor) & NoiseSizeMax;
			phasor.phase.phase = timeInHz - timeInHzFloor;
		}
		
		/* playHeadPos, rateBeatsInv */
		void updatePositionSyncProcedural(const PlayHeadPos& playHeadPos, double rateBeatsInv) noexcept
		{
			const auto ppq = playHeadPos.ppqPosition * rateBeatsInv + .5;
			const auto ppqFloor = std::floor(ppq);

			noiseIdx = static_cast<int>(ppqFloor) & NoiseSizeMax;
			phasor.phase.phase = ppq - ppqFloor;
		}

		/* samples, noise, gainBuffer,
		octavesBuffer, phsBuf, widthBuf, shape,
		octaves, width, phs
		numChannels, numSamples,
		octavesSmoothing, phsSmoothing, widthSmoothing */
		void operator()(float* const* samples, const float* noise, const float* gainBuffer,
			const float* octavesBuf, const float* phsBuf, const float* widthBuf, Shape shape,
			float octaves, float width, float phs,
			int numChannels, int numSamples,
			bool octavesSmoothing, bool phsSmoothing, bool widthSmoothing) noexcept
		{
			synthesizePhasor(phsBuf, phs, numSamples, phsSmoothing);
			
			processOctaves(samples[0], octavesBuf, noise, gainBuffer, octaves, shape, numSamples, octavesSmoothing);
			
			if(numChannels == 2)
				processWidth(samples, octavesBuf, widthBuf, noise, gainBuffer, octaves, width, shape, numSamples, octavesSmoothing, widthSmoothing);
		}

		// misc
		std::array<InterpolationFunc, 3> interpolationFuncs;
		double sampleRateInv;
		float fs;
		
		// phase
		Phasor<double> phasor;
		std::vector<float> phaseBuffer;
		int noiseIdx;
		
	protected:
		/* phsBuf, phs, numSamples, phaseSmoothing */
		void synthesizePhasor(const float* phsBuf, float phs, int numSamples, bool phaseSmoothing) noexcept
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

		/* smpls, octavesBuf, noise, gainBuffer, shape, numSamples, octavesSmoothing */
		void processOctaves(float* smpls, const float* octavesBuf,
			const float* noise, const float* gainBuffer, float octaves, Shape shape, int numSamples,
			bool octavesSmoothing) noexcept
		{
			if (!octavesSmoothing)
				processOctavesNotSmoothing(smpls, noise, gainBuffer, octaves, shape, numSamples);
			else
				processOctavesSmoothing(smpls, octavesBuf, noise, gainBuffer, shape, numSamples);
		}
		
		float getInterpolatedSample(const float* noise, float phase, Shape shape) noexcept
		{
			return interpolationFuncs[static_cast<int>(shape)](noise, phase);
		}

		/* smpls, noise, gainBuffer, octaves, shape, numSamples */
		void processOctavesNotSmoothing(float* smpls, const float* noise,
			const float* gainBuffer, float octaves, Shape shape, int numSamples) noexcept
		{
			const auto octFloor = std::floor(octaves);

			for (auto s = 0; s < numSamples; ++s)
			{
				auto sample = 0.f;
				for (auto o = 0; o < octFloor; ++o)
				{
					const auto phase = getPhaseOctaved(phaseBuffer[s], o);
					const auto smpl = getInterpolatedSample(noise, phase, shape);
					sample += smpl * gainBuffer[o];
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
					const auto smpl = getInterpolatedSample(noise, phase, shape);
					smpls[s] += octFrac * smpl * gainBuffer[octFloorInt];;
				}

				gain += octFrac * gainBuffer[octFloorInt];
			}

			SIMD::multiply(smpls, 1.f / std::sqrt(gain), numSamples);
		}
		
		/* smpls, octavesBuf, noise, gainBuffer, shape, numSamples */
		void processOctavesSmoothing(float* smpls, const float* octavesBuf,
			const float* noise, const float* gainBuffer, Shape shape, int numSamples) noexcept
		{
			for (auto s = 0; s < numSamples; ++s)
			{
				const auto octFloor = std::floor(octavesBuf[s]);

				auto sample = 0.f;
				for (auto o = 0; o < octFloor; ++o)
				{
					const auto phase = getPhaseOctaved(phaseBuffer[s], o);
					const auto smpl = getInterpolatedSample(noise, phase, shape);
					sample += smpl * gainBuffer[o];
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
					const auto smpl = getInterpolatedSample(noise, phase, shape);
					smpls[s] += octFrac * smpl * gainBuffer[octFloorInt];

					gain += octFrac * gainBuffer[octFloorInt];
				}

				smpls[s] /= std::sqrt(gain);
			}
		}

		/* samples, octavesBuf, widthBuf, noise, gainBuffer, octaves, width, shape, numSamples, octavesSmoothing, widthSmoothing */
		void processWidth(float* const* samples, const float* octavesBuf,
			const float* widthBuf, const float* noise, const float* gainBuffer, float octaves, float width,
			Shape shape, int numSamples, bool octavesSmoothing, bool widthSmoothing) noexcept
		{
			if (!widthSmoothing)
				if (width == 0.f)
					return SIMD::copy(samples[1], samples[0], numSamples);
				else
					SIMD::add(phaseBuffer.data(), width, numSamples);
			else
				SIMD::add(phaseBuffer.data(), widthBuf, numSamples);

			processOctaves(samples[1], octavesBuf, noise, gainBuffer, octaves, shape, numSamples, octavesSmoothing);
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
#if JUCE_DEBUG
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
#endif
	};

	struct Perlin2
	{
		using AudioBuffer = juce::AudioBuffer<float>;
		using Shape = Perlin::Shape;

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
			rateBeats(-1.),
			rateHz(-1.),
			rateInv(0.),
			// crossfade
			xFadeBuffer(),
			xPhase(0.f),
			xInc(0.f),
			crossfading(false),
			seed(),
			// project position
			curPosEstimate(-1),
			curPosInSamples(0)
		{
			setSeed(69420);
			
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
		}

		/* samples, numChannels, numSamples, playHeadPos,
		rateHz, rateBeats, octaves, width, phs, shape,
		temposync, procedural */
		void operator()(float* const* samples, int numChannels, int numSamples,
			const PlayHeadPos& playHeadPos,
			double _rateHz, double _rateBeats,
			float octaves, float width, float phs,
			Shape shape, bool temposync, bool procedural) noexcept
		{
			if(temposync)
				processSync(playHeadPos, numSamples, _rateBeats, procedural);
			else
				processFree(playHeadPos, numSamples, _rateHz, procedural);

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
				shape,
				octaves,
				width,
				phs,
				numChannels,
				numSamples,
				octavesPRM.smoothing,
				phsPRM.smoothing,
				widthPRM.smoothing
			);

			processCrossfade
			(
				samples,
				octavesBuf,
				phsBuf,
				widthBuf,
				octaves,
				width,
				phs,
				shape,
				numChannels,
				numSamples
			);
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
		double rateBeats, rateHz;
		double rateInv;
		// crossfade
		std::vector<float> xFadeBuffer;
		float xPhase, xInc;
		bool crossfading;
		// seed
		std::atomic<int> seed;
		// project position
		__int64 curPosEstimate, curPosInSamples;

		// PROCESS FREE
		void processFree(const PlayHeadPos& playHeadPos, int numSamples, double _rateHz, bool procedural) noexcept
		{
			if (procedural && playHeadPos.isPlaying)
				processFreeProcedural(playHeadPos, _rateHz, numSamples);
			else
				processFreeRandom(_rateHz);
		}
		
		void processFreeRandom(double _rateHz) noexcept
		{
			rateHz = _rateHz;
			rateInv = _rateHz * sampleRateInv;

			perlins[perlinIndex].updateSpeed(rateInv);
		}

		void processFreeProcedural(const PlayHeadPos& playHeadPos, double _rateHz, int numSamples) noexcept
		{
			curPosInSamples = playHeadPos.timeInSamples;
			
			if (!crossfading)
			{
				bool shallCrossfade = false;

				if (playHeadJumps())
				{
					shallCrossfade = true;
				}
				else if (rateHz != _rateHz)
				{
					rateHz = _rateHz;
					rateInv = _rateHz * sampleRateInv;
					shallCrossfade = true;
				}

				if (shallCrossfade)
				{
					initCrossfade();
					perlins[perlinIndex].updateSpeed(rateInv);
					
				}
			}

			perlins[perlinIndex].updatePosition(playHeadPos, rateHz);
			
			processCurPosEstimate(numSamples);
		}

		// PROCESS TEMPOSYNC
		void processSync(const PlayHeadPos& playHeadPos, int numSamples, double _rateBeats, bool procedural) noexcept
		{
			if (procedural && playHeadPos.isPlaying)
				processSyncProcedural(playHeadPos, _rateBeats, numSamples);
			else
				processSyncRandom(playHeadPos, _rateBeats);
		}

		void processSyncUpdateSpeed(const PlayHeadPos& playHeadPos) noexcept
		{
			const auto bpMins = playHeadPos.bpm;
			const auto bpSecs = bpMins / 60.;
			const auto bpSamples = bpSecs * sampleRateInv;
			const auto speed = rateInv * bpSamples;

			perlins[perlinIndex].updateSpeed(speed);
		}

		void processSyncRandom(const PlayHeadPos& playHeadPos, double _rateBeats) noexcept
		{
			rateBeats = _rateBeats;
			rateInv = .25 / rateBeats;
			
			processSyncUpdateSpeed(playHeadPos);
		}

		void processSyncProcedural(const PlayHeadPos& playHeadPos, double _rateBeats, int numSamples) noexcept
		{
			curPosInSamples = playHeadPos.timeInSamples;

			if (!crossfading)
			{
				bool shallCrossfade = false;

				if (playHeadJumps())
				{
					shallCrossfade = true;
				}
				else if (rateBeats != _rateBeats)
				{
					rateBeats = _rateBeats;
					rateInv = .25 / rateBeats;
					shallCrossfade = true;
				}

				if (shallCrossfade)
				{
					initCrossfade();
					processSyncUpdateSpeed(playHeadPos);
				}
			}

			perlins[perlinIndex].updatePositionSyncProcedural(playHeadPos, rateInv);

			processCurPosEstimate(numSamples);
		}

		// CROSSFADE FUNCS
		bool playHeadJumps() noexcept
		{
			const auto distance = std::abs(curPosInSamples - curPosEstimate);
			return distance > 2;
		}

		void processCurPosEstimate(int numSamples) noexcept
		{
			curPosEstimate = curPosInSamples + numSamples;
		}

		void initCrossfade() noexcept
		{
			xPhase = 0.f;
			crossfading = true;
			perlinIndex = 1 - perlinIndex;
		}

		/* samples, octavesBuf, phsBuf, widthBuf,
		octaves, width, phs, numChannels, numSamples */
		void processCrossfade(float* const* samples, const float* octavesBuf,
			const float* phsBuf, const float* widthBuf,
			float octaves, float width, float phs, Shape shape,
			int numChannels, int numSamples) noexcept
		{
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
					shape,
					octaves,
					width,
					phs,
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
	};
}

/*

MODULATOR >>>

features:
	-
	
bugs:
	-
	
optimize:
    -

PLUGIN >>>

features:
	-

optimize:
	implement fixed blocksize

*/