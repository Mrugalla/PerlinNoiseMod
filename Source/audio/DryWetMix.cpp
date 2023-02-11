#include "DryWetMix.h"

namespace audio
{
	// DryWetMix

	DryWetMix::DryWetMix() :
		latencyCompensation(),

		buffers(),
#if PPDHasGainIn
		gainInSmooth(0.f),
#endif
		mixSmooth(1.f),
#if PPDHasGainOut
		gainOutSmooth(1.f),
#endif
		dryBuf(),

		mixValue(0.f),
		gainOutValue(0.f),
		mixSmoothing(false),
		gainOutSmoothing(false)
	{}

	void DryWetMix::prepare(float sampleRate, int blockSize, int latency)
	{
		latencyCompensation.prepare(blockSize, latency);

#if PPDHasGainIn
		gainInSmooth.makeFromDecayInMs(20.f, sampleRate);
#endif
		mixSmooth.makeFromDecayInMs(20.f, sampleRate);
#if PPDHasGainOut
		gainOutSmooth.makeFromDecayInMs(20.f, sampleRate);
#endif

		dryBuf.setSize(2, blockSize, false, true, false);

		buffers.setSize(NumBufs, blockSize, false, true, false);
	}

	void DryWetMix::saveDry(float* const* samples, int numChannels, int numSamples,
#if PPDHasGainIn
		float gainInP,
#if PPDHasUnityGain
		float unityGainP,
#endif
#endif
		float mixP
#if PPDHasGainOut
		, float gainP
#if PPDHasPolarity
		, float polarityP
#endif
#endif
	) noexcept
	{
		auto bufs = buffers.getArrayOfWritePointers();

		latencyCompensation
		(
			dryBuf.getArrayOfWritePointers(),
			samples,
			numChannels,
			numSamples
		);

#if PPDHasGainIn
		auto gainInBuf = bufs[GainIn];
		const auto gainInVal = PPDGainInDecibels ? Decibels::decibelsToGain(gainInP) : gainInP;
		const auto gainInSmoothing = gainInSmooth(gainInBuf, gainInVal, numSamples);
		if (gainInSmoothing)
			for (auto ch = 0; ch < numChannels; ++ch)
				for (auto s = 0; s < numSamples; ++s)
					samples[ch][s] *= gainInBuf[s];
		else
			for (auto ch = 0; ch < numChannels; ++ch)
				SIMD::multiply(samples[ch], gainInVal, numSamples);

#if PPDHasUnityGain
		gainP -= gainInP * unityGainP;
#endif
#endif
		auto mixBuf = bufs[Mix];
#if PPD_MixOrGainDry == 0
		mixValue = mixP;
#else
		mixValue = decibelToGain(mixP, -80.f);
#endif
		mixSmoothing = mixSmooth(mixBuf, mixValue, numSamples);
#if PPDHasGainOut
		gainP = PPDGainInDecibels ? Decibels::decibelsToGain(gainP) : gainP;
#if PPDHasPolarity
		gainP *= polarityP;
#endif
		gainOutValue = gainP;
		gainOutSmoothing = gainOutSmooth(bufs[GainOut], gainOutValue, numSamples);
#endif
	}

	void DryWetMix::processBypass(float* const* samples, int numChannels, int numSamples) noexcept
	{
		latencyCompensation
		(
			dryBuf.getArrayOfWritePointers(),
			samples,
			numChannels,
			numSamples
		);

		for (auto ch = 0; ch < numChannels; ++ch)
		{
			const auto dry = dryBuf.getReadPointer(ch);
			auto smpls = samples[ch];

			SIMD::copy(smpls, dry, numSamples);
		}
	}

#if PPDHasGainOut
	void DryWetMix::processOutGain(float* const* samples, int numChannels, int numSamples) const noexcept
	{
		if (gainOutSmoothing)
		{
			auto bufs = buffers.getArrayOfReadPointers();
			const auto gainBuf = bufs[GainOut];

			for (auto ch = 0; ch < numChannels; ++ch)
				SIMD::multiply(samples[ch], gainBuf, numSamples);
			return;
		}
		
		for (auto ch = 0; ch < numChannels; ++ch)
			SIMD::multiply(samples[ch], gainOutValue, numSamples);
	}
#endif

	void DryWetMix::processMix(float* const* samples, int numChannels, int numSamples
#if PPDHasDelta
		, bool deltaP
#endif
		) const noexcept
	{
		if (mixSmoothing)
		{
			auto bufs = buffers.getArrayOfReadPointers();
			const auto mix = bufs[Mix];

			for (auto ch = 0; ch < numChannels; ++ch)
			{
				const auto dry = dryBuf.getReadPointer(ch);
				auto smpls = samples[ch];

				for (auto s = 0; s < numSamples; ++s)
				{
					const auto d = dry[s];
					const auto w = smpls[s];
					const auto m = mix[s];
#if PPD_MixOrGainDry == 0
					smpls[s] = d + m * (w - d);
#else
					smpls[s] = m * d + w;
#endif
				}
			}
		}
		else
		{
			for (auto ch = 0; ch < numChannels; ++ch)
			{
				const auto dry = dryBuf.getReadPointer(ch);
				auto smpls = samples[ch];
				
				for (auto s = 0; s < numSamples; ++s)
				{
					const auto d = dry[s];
					const auto w = smpls[s];
#if PPD_MixOrGainDry == 0
					smpls[s] = d + mixValue * (w - d);
#else
					smpls[s] = mixValue * d + w;
#endif
				}
			}
		}
#if PPDHasDelta
		if(deltaP)
		{
			for (auto ch = 0; ch < numChannels; ++ch)
			{
				const auto dry = dryBuf.getReadPointer(ch);
				auto smpls = samples[ch];

				for (auto s = 0; s < numSamples; ++s)
				{
					const auto d = dry[s];
					const auto w = smpls[s];
					
					smpls[s] = w - d;
				}
			}
		}
#endif
	}
}