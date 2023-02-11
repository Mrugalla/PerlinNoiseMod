#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include "WHead.h"



namespace audio
{
	struct NullSynth
	{
		using File = juce::File;
		using UniqueStream = std::unique_ptr<juce::FileInputStream>;
		using SpecLoc = File::SpecialLocationType;
		
		NullSynth() :
			noise()
		{
			noise.reserve(1 << 17);
			UniqueStream inputStream(File::getSpecialLocation(SpecLoc::currentApplicationFile).createInputStream());
			auto& stream = *inputStream;

			while (!stream.isExhausted())
			{
				auto smpl = stream.readFloat();
				if (!std::isnan(smpl) && !std::isinf(smpl) && smpl != 0.f)
				{
					while (smpl < -1.f || smpl > 1.f)
						smpl *= .5f;
					noise.push_back(smpl);
				}
			}
		}

		void prepare(int blockSize)
		{
			wHead.prepare(blockSize, static_cast<int>(noise.size()));
		}

		void operator()(float** samples, int numChannels, int numSamples) noexcept
		{
			wHead(numSamples);

			for (auto ch = 0; ch < numChannels; ++ch)
			{
				auto smpls = samples[ch];

				for (auto s = 0; s < numSamples; ++s)
				{
					const auto w = wHead[s];

					smpls[s] = noise[w];
				}
			}
		}
		
		WHead wHead;
		std::vector<float> noise;
	};
}

/*

this synth makes crappy noise from data that is used in a wrong way.
it's a fun side project. contributions are welcome

todo: save and load buffer indexes after first opened plugin

*/