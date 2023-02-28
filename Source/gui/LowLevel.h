#pragma once
#include "Knob.h"
#include "EnvelopeGenerator.h"

namespace gui
{
    struct LowLevel :
        public Comp,
		public Timer
    {
        LowLevel(Utils& u) :
            Comp(u, "", CursorType::Default),
			Timer(),
            rateHz(u),
			rateBeats(u),
            oct(u),
            width(u),
            phase(u),
            smooth(u),
            rateType(u),
            seed(u, "Generate a new random seed for the procedural perlin noise mod.")
        {
            makeParameter(rateHz, PID::RateHz, "Rate");
            addAndMakeVisible(rateHz);

			makeParameter(rateBeats, PID::RateBeats, "Rate");
			addAndMakeVisible(rateBeats);

			makeParameter(oct, PID::Octaves, "Oct");
			addAndMakeVisible(oct);

			makeParameter(width, PID::Width, "Width");
			addAndMakeVisible(width);

			makeParameter(phase, PID::RatePhase, "Phase");
			addAndMakeVisible(phase);

			makeParameter(smooth, PID::Smooth, "Smooth");
			addAndMakeVisible(smooth);
            
			makeParameter(rateType, PID::RateType, ButtonSymbol::TempoSync);
			addAndMakeVisible(rateType);
            
            makeTextButton(seed, "Seed", false);
            addAndMakeVisible(seed);

            seed.onClick.push_back([](Button& btn, const Mouse&)
            {
                auto& u = btn.utils;
                Random rand;
                u.audioProcessor.perlin.setSeed(rand.nextInt());
            });

            layout.init
            (
                { 1, 3, 8, 5, 5, 5, 5, 1 },
                { 1, 8, 1 }
            );

            startTimerHz(24);
        }

        void paint(Graphics&) override
        {
        }

        void resized() override
        {
            layout.resized();

            layout.place(rateHz, 2, 1, 1, 1, false);
            layout.place(rateBeats, 2, 1, 1, 1, false);
			layout.place(oct, 3, 1, 1, 1, false);
			layout.place(width, 4, 1, 1, 1, false);
			layout.place(phase, 5, 1, 1, 1, false);
			layout.place(smooth, 6, 1, 1, 1, false);
			
            layout.place(seed, 1, 1.f, 1, .333f, true);
            layout.place(rateType, 1, 1.333f, 1, .333f, true);
        }

        void timerCallback() override
        {
            bool isTempoSync = utils.getParam(PID::RateType)->getValMod() > .5f;
            rateBeats.setVisible(isTempoSync);
			rateHz.setVisible(!isTempoSync);
        }

    protected:
		Knob rateHz, rateBeats, oct, width, phase, smooth;
        Button rateType, seed;
    };
}