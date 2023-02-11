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
            rate(u),
            oct(u),
            width(u),
            seed(u)
        {
            makeParameter(rate, PID::RateHz, "Rate");
            addAndMakeVisible(rate);

			makeParameter(oct, PID::Octaves, "Octaves");
			addAndMakeVisible(oct);

			makeParameter(width, PID::Width, "Width");
			addAndMakeVisible(width);
            
			makeParameter(seed, PID::Seed, "Seed");
			addAndMakeVisible(seed);
            
            layout.init
            (
                { 1, 13, 13, 13, 13, 1 },
                { 1, 2, 1 }
            );
        }

        void paint(Graphics&) override
        {
        }

        void resized() override
        {
            layout.resized();

            layout.place(rate, 1, 1, 1, 1, false);
			layout.place(oct, 2, 1, 1, 1, false);
			layout.place(width, 3, 1, 1, 1, false);
			layout.place(seed, 4, 1, 1, 1, false);
        }

        void timerCallback() override
        {}

    protected:
		Knob rate, oct, width, seed;
    };
}