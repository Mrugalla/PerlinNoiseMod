#pragma once
#include "Knob.h"
#include "EnvelopeGenerator.h"
#include "Oscilloscope.h"

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
            shape(u),
            rateType(u),
            seed(u, "Generate a new random seed for the procedural perlin noise mod."),
            orientation(u),
            randType(u),
			outputType(u),
            scopeL(u, "", u.audioProcessor.scope[0]),
			scopeR(u, "", u.audioProcessor.scope[1])
        {
            addAndMakeVisible(scopeL);
			addAndMakeVisible(scopeR);
            scopeL.bipolar = scopeR.bipolar = true;
            scopeR.lineCID = ColourID::Mod;
            scopeL.lineCID = ColourID::Bias;

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

			makeParameter(shape, PID::Shape, "Shape");
			addAndMakeVisible(shape);
            
			makeParameter(rateType, PID::RateType, ButtonSymbol::TempoSync);
			addAndMakeVisible(rateType);
            
            makeTextButton(seed, "Seed: " + String(u.audioProcessor.perlin.seed.load()), false);
            addAndMakeVisible(seed);

			makeParameter(orientation, PID::Orientation, "Orientation");
			addAndMakeVisible(orientation);

			makeParameter(randType, PID::RandType, "Rand Type");
            addAndMakeVisible(randType);

			makeParameter(outputType, PID::OutputType, "Output Type");
			addAndMakeVisible(outputType);

            seed.onClick.push_back([](Button& btn, const Mouse&)
            {
                auto& u = btn.utils;
                Random rand;
				auto& perlin = u.audioProcessor.perlin;
                perlin.setSeed(rand.nextInt());
                btn.label.setText("Seed: " + String(perlin.seed.load()));
            });

            layout.init
            (
                { 1, 34, 21, 34, 1 }, // margin; xtra stuff; rate; perlin params; margin
                { 5, 3, 3, 13 } // visualizers, radiobuttons; seed+knobs; knobs
            );

            startTimerHz(24);
        }

        void paint(Graphics& g) override
        {
            const auto thicc = utils.thicc;
            g.setColour(Colours::c(ColourID::Darken));
            g.fillRoundedRectangle(scopeL.getBounds().toFloat(), thicc);
        }

        void resized() override
        {
            layout.resized();

            layout.place(rateHz, 2, 1, 1, 3);
            layout.place(rateBeats, 2, 1, 1, 3);
            // perlin params:
			layout.place(scopeL, 3, 1, 1, 1);
			layout.place(scopeR, 3, 1, 1, 1);
            layout.place(shape, 3, 2, 1, 1);
            {
                const auto area = layout(3, 2, 1, 2);
                const auto w = area.getWidth();
                const auto knobW = w / 3.f;
                auto x = area.getX();
				
				oct.setBounds(BoundsF(x, area.getY(), knobW, area.getHeight()).toNearestInt());
				x += knobW;
				phase.setBounds(BoundsF(x, area.getY(), knobW, area.getHeight()).toNearestInt());
				x += knobW;
				width.setBounds(BoundsF(x, area.getY(), knobW, area.getHeight()).toNearestInt());
            }
            layout.place(seed, 1, 1, 1, 1);
            {
                const auto area = layout(1, 2, 1, 2);
                const auto w = area.getWidth();
				const auto h = area.getHeight();
				auto x = area.getX();
				auto y = area.getY();
                
                const auto buttonLengthRel = .8f;
				const auto buttonLength = w * buttonLengthRel;
                
                const auto buttonHeight = h / 3.f;
                orientation.setBounds(BoundsF(x, y, buttonLength, buttonHeight).toNearestInt());
				y += buttonHeight;
				randType.setBounds(BoundsF(x, y, buttonLength, buttonHeight).toNearestInt());
				y += buttonHeight;
				outputType.setBounds(BoundsF(x, y, buttonLength, buttonHeight).toNearestInt());
                x += buttonLength;
                y = area.getY();
				rateType.setBounds(maxQuadIn(BoundsF(x, y, w - buttonLength, h)).toNearestInt());
            }
        }

        void timerCallback() override
        {
            bool isTempoSync = utils.getParam(PID::RateType)->getValMod() > .5f;
            rateBeats.setVisible(isTempoSync);
			rateHz.setVisible(!isTempoSync);
        }

    protected:
		Knob rateHz, rateBeats, oct, width, phase, shape;
        Button rateType, seed;
        Knob orientation, randType, outputType;
        Oscilloscope scopeL, scopeR;
    };
}