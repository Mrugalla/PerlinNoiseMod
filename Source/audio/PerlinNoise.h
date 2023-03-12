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
		void processRateFreeInc(double rateHzInv) noexcept
		{
			phasor.inc = rateHzInv;
		}

		/*  playHeadPos, rateHz */
		void processRateFreeProcedural(const PlayHeadPos& playHeadPos, double rateHz) noexcept
		{
			const auto timeInSamples = playHeadPos.timeInSamples;
			const auto timeInSecs = static_cast<double>(timeInSamples) * sampleRateInv;
			const auto timeInHz = timeInSecs * rateHz;
			const auto timeInHzFloor = std::floor(timeInHz);
			
			noiseIdx = static_cast<int>(timeInHzFloor) & NoiseSizeMax;
			phasor.phase.phase = timeInHz - timeInHzFloor;
		}

		/* rateBeatsInv, beatsPerSamples */
		void processRateSyncInc(double rateBeatsInv, double beatsPerSamples) noexcept
		{
			phasor.inc = beatsPerSamples * rateBeatsInv;
		}
		
		/* playHeadPos, rateBeatsInv */
		void processRateSyncProcedural(const PlayHeadPos& playHeadPos, double rateBeatsInv) noexcept
		{
			const auto ppq = playHeadPos.ppqPosition * rateBeatsInv + .5;
			const auto ppqFloor = std::floor(ppq);

			noiseIdx = static_cast<int>(ppqFloor) & NoiseSizeMax;
			phasor.phase.phase = ppq - ppqFloor;
		}

		/* playHeadPos, rateBeatsInv */
		void processRateSyncRandom(const PlayHeadPos& playHeadPos, double rateBeatsInv) noexcept
		{
			const auto ppq = playHeadPos.ppqPosition * rateBeatsInv + .5;
			const auto ppqFloor = std::floor(ppq);

			//noiseIdx = static_cast<int>(ppqFloor) & NoiseSizeMax;
			phasor.phase.phase = ppq - ppqFloor;
		}

		/* samples, noise, gainBuffer, octavesBuffer, phsBuf, widthBuf, shape,
		numChannels, numSamples, octavesSmoothing, phsSmoothing, widthSmoothing */
		void operator()(float* const* samples, const float* noise, const float* gainBuffer,
			const float* octavesBuf, const float* phsBuf, const float* widthBuf, Shape shape,
			int numChannels, int numSamples,
			bool octavesSmoothing, bool phsSmoothing, bool widthSmoothing) noexcept
		{
			synthesizePhasor(phsBuf, numSamples, phsSmoothing);
			
			processOctaves(samples[0], octavesBuf, noise, gainBuffer, shape, numSamples, octavesSmoothing);
			
			if(numChannels == 2)
				processWidth(samples, octavesBuf, widthBuf, noise, gainBuffer, shape, numSamples, octavesSmoothing, widthSmoothing);
		}

		// misc
		std::array<InterpolationFunc, 3> interpolationFuncs;
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
			const float* noise, const float* gainBuffer, Shape shape, int numSamples,
			bool octavesSmoothing) noexcept
		{
			if (!octavesSmoothing)
				processOctavesNotSmoothing(smpls, noise, gainBuffer, shape, numSamples);
			else
				processOctavesSmoothing(smpls, octavesBuf, noise, gainBuffer, shape, numSamples);
		}

		
		float getInterpolatedSample(const float* noise, float phase, Shape shape) noexcept
		{
			return interpolationFuncs[static_cast<int>(shape)](noise, phase);
			
			/*
			switch (shape)
			{
			case Shape::NN: return getInterpolatedNN(noise, phase);
			case Shape::Lerp: return getInterpolatedLerp(noise, phase);
			default: // Spline
				return getInterpolatedSpline(noise, phase);
			}
			*/
		}

		void processOctavesNotSmoothing(float* smpls, const float* noise,
			const float* gainBuffer, Shape shape, int numSamples) noexcept
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

		void processWidth(float* const* samples, const float* octavesBuf,
			const float* widthBuf, const float* noise, const float* gainBuffer, Shape shape,
			int numSamples, bool octavesSmoothing, bool widthSmoothing) noexcept
		{
			if (!widthSmoothing)
				if (width == 0.f)
					return SIMD::copy(samples[1], samples[0], numSamples);
				else
					SIMD::add(phaseBuffer.data(), width, numSamples);
			else
				SIMD::add(phaseBuffer.data(), widthBuf, numSamples);

			processOctaves(samples[1], octavesBuf, noise, gainBuffer, shape, numSamples, octavesSmoothing);
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
			//
			rateHz(0.),
			rateHzInv(1.),
			rateBeats(-1.),
			rateBeatsInv(1.),
			octaves(1.f),
			width(0.f),
			phs(0.f),
			shape(Shape::Spline),
			temposync(false),
			procedural(false),
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

		void setParameters(double _rateHz, double _rateBeats,
			float _octaves, float _width, float _phs,
			Shape _shape, bool _temposync, bool _procedural) noexcept
		{
			octaves = _octaves;
			width = _width;
			phs = _phs;
			shape = _shape;
			temposync = _temposync;
			procedural = _procedural;

			if (rateBeats != _rateBeats)
			{
				perlins[perlinIndex].setParameters(octaves, width, phs);
				
				if (!crossfading)
				{
					rateBeats = _rateBeats;
					rateBeatsInv = .25 / rateBeats;
					initCrossfade();
				}
			}
			if (rateHz != _rateHz)
			{
				if (procedural)
				{
					if(!crossfading)
					{
						rateHz = _rateHz;
						rateHzInv = _rateHz * sampleRateInv;
						initCrossfade();
					}
				}
				else
				{
					rateHz = _rateHz;
					rateHzInv = _rateHz * sampleRateInv;
				}
			}
		}

		void operator()(float* const* samples, int numChannels, int numSamples,
			const PlayHeadPos& playHeadPos) noexcept
		{
			if(temposync)
				processTemposync(playHeadPos, numSamples);
			else
				processFree(playHeadPos, numSamples);

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
		double rateHz, rateHzInv, rateBeats, rateBeatsInv;
		float octaves, width, phs;
		Shape shape;
		bool temposync;
		// crossfade
		std::vector<float> xFadeBuffer;
		float xPhase, xInc;
		bool crossfading, procedural;
		// seed
		std::atomic<int> seed;
		// project position
		__int64 curPosEstimate, curPosInSamples;

		void processTemposync(const PlayHeadPos& playHeadPos, int numSamples) noexcept
		{
			if (playHeadPos.isPlaying)
			{
				if (playHeadJumps(playHeadPos.timeInSamples))
					initCrossfade();

				if (procedural)
					perlins[perlinIndex].processRateSyncProcedural(playHeadPos, rateBeatsInv);
				else
					perlins[perlinIndex].processRateSyncRandom(playHeadPos, rateBeatsInv);

				processCurPosEstimate(numSamples);
			}

			const auto bpMins = playHeadPos.bpm;
			const auto bpSecs = bpMins / 60.;
			const auto bpSamples = bpSecs * sampleRateInv;

			perlins[perlinIndex].processRateSyncInc(rateBeatsInv, bpSamples);
		}

		void processFree(const PlayHeadPos& playHeadPos, int numSamples) noexcept
		{
			if (procedural && playHeadPos.isPlaying)
			{
				if (playHeadJumps(playHeadPos.timeInSamples))
				{
					initCrossfade();
					perlins[perlinIndex].processRateFreeInc(rateHzInv);
				}
				oopsie(playHeadPos.ppqPosition > 1 && crossfading);

				perlins[perlinIndex].processRateFreeProcedural(playHeadPos, rateHz);

				processCurPosEstimate(numSamples);
			}
			else
				perlins[perlinIndex].processRateFreeInc(rateHzInv);
		}
		
		bool playHeadJumps(__int64 timeInSamples) noexcept
		{
			curPosInSamples = timeInSamples;
			const auto distance = std::abs(curPosInSamples - curPosEstimate);
			return distance > 2 && !crossfading;
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
			perlins[perlinIndex].setParameters(octaves, width, phs);
		}

		void processCrossfade(float* const* samples, const float* octavesBuf,
			const float* phsBuf, const float* widthBuf,
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