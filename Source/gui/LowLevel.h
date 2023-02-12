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
            rateType(u)
        {
            makeParameter(rateHz, PID::RateHz, "Rate");
            addAndMakeVisible(rateHz);

			makeParameter(rateBeats, PID::RateBeats, "Rate");
			addAndMakeVisible(rateBeats);

			makeParameter(oct, PID::Octaves, "Octaves");
			addAndMakeVisible(oct);

			makeParameter(width, PID::Width, "Width");
			addAndMakeVisible(width);
            
			makeParameter(rateType, PID::RateType, ButtonSymbol::TempoSync);
			addAndMakeVisible(rateType);
            
            layout.init
            (
                { 1, 13, 13, 13, 1 },
                { 1, 13, 2, 1 }
            );

            startTimerHz(24);
        }

        void paint(Graphics&) override
        {
        }

        void resized() override
        {
            layout.resized();

            layout.place(rateHz, 1, 1, 1, 1, false);
            layout.place(rateBeats, 1, 1, 1, 1, false);
			layout.place(oct, 2, 1, 1, 1, false);
			layout.place(width, 3, 1, 1, 1, false);
			
            layout.place(rateType, 1, 2, 1, 1, true);
        }

        void timerCallback() override
        {
            bool isTempoSync = utils.getParam(PID::RateType)->getValMod() > .5f;
            rateBeats.setVisible(isTempoSync);
			rateHz.setVisible(!isTempoSync);
        }

    protected:
		Knob rateHz, rateBeats, oct, width;
        Button rateType;
    };
}