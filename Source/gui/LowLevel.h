#pragma once
#include "Knob.h"
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
            shapeNN(u),
            shapeLin(u),
            shapeRound(u),
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

			makeParameter(phase, PID::Phase, "Phase");
			addAndMakeVisible(phase);

            {
                makeToggleButton(shapeNN, "Steppy");
                addAndMakeVisible(shapeNN);

                makeToggleButton(shapeLin, "Linear");
                addAndMakeVisible(shapeLin);

                makeToggleButton(shapeRound, "Round");
                addAndMakeVisible(shapeRound);
                
				std::array<Button*, 3> buttons = { &shapeNN, &shapeLin, &shapeRound };

                auto param = u.getParam(PID::Shape);
                const auto valDenorm = static_cast<int>(std::round(param->getValueDenorm()));
                
                for (auto& button : buttons)
                    button->toggleState = 0;
				buttons[valDenorm]->toggleState = 1;

                for (auto i = 0; i < buttons.size(); ++i)
                {
                    buttons[i]->onClick[0] = [this, param, buttons, i](Button&, const Mouse&)
                    {
                        const auto& range = param->range;
                        const auto norm = range.convertTo0to1(static_cast<float>(i));
                        param->setValueWithGesture(norm);
                        for (auto& button : buttons)
                            button->toggleState = 0;
                        buttons[i]->toggleState = 1;
                        for (auto& button : buttons)
                            button->repaint();
                    };

					buttons[i]->onTimer.push_back([this, param, buttons, i](Button&)
					{
						const auto valDenorm = static_cast<int>(std::round(param->getValueDenorm()));
                        bool needRepaint = false;
                        if (valDenorm == i && buttons[i]->toggleState == 0)
                        {
							buttons[i]->toggleState = 1;
                            needRepaint = true;
                        }

						if (valDenorm != i && buttons[i]->toggleState == 1)
						{
							buttons[i]->toggleState = 0;
							needRepaint = true;
						}

						if (needRepaint)
                            for (auto& button : buttons)
                                button->repaint();
					});

                    buttons[i]->startTimerHz(12);
                }
                    
            }
            
			makeParameter(rateType, PID::RateType, ButtonSymbol::TempoSync);
			addAndMakeVisible(rateType);
            
            makeTextButton(seed, "Seed: " + String(u.audioProcessor.perlin.seed.load()), false);
            addAndMakeVisible(seed);

			makeParameter(orientation, PID::Orientation, "+/-", true);
			addAndMakeVisible(orientation);

			makeParameter(randType, PID::RandType, "Proc", true);
            addAndMakeVisible(randType);

			makeParameter(outputType, PID::OutputType, "CC", true);
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
                { 1, 13, 21, 34, 1 }, // margin; xtra stuff; rate; perlin params; margin
                { 8, 5, 3, 21 } // visualizers, radiobuttons; seed+knobs; knobs
            );

            startTimerHz(24);
        }

        void paint(Graphics& g) override
        {
            //layout.paint(g);

            const auto thicc = utils.thicc;
            g.setColour(Colours::c(ColourID::Darken));
            g.fillRoundedRectangle(scopeL.getBounds().toFloat(), thicc);
        }

        void resized() override
        {
            layout.resized();

            layout.place(rateHz, 2, 2, 1, 2);
            layout.place(rateBeats, 2, 2, 1, 2);
            // perlin params:
			layout.place(scopeL, 3, 1, 1, 1);
			layout.place(scopeR, 3, 1, 1, 1);
            {
                const auto area = layout(3, 2, 1, 1);
                const auto w = area.getWidth();
                const auto h = area.getHeight();
                auto x = area.getX();
                auto y = area.getY();

                auto buttonWidth = w / 3.f;
                shapeNN.setBounds(BoundsF(x, y, buttonWidth, h).toNearestInt());
				x += buttonWidth;
				shapeLin.setBounds(BoundsF(x, y, buttonWidth, h).toNearestInt());
				x += buttonWidth;
				shapeRound.setBounds(BoundsF(x, y, buttonWidth, h).toNearestInt());
            }
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
                const auto area = layout(1, 2, 1, 1);
                const auto w = area.getWidth();
                const auto h = area.getHeight();
                auto x = area.getX();
                auto y = area.getY();
                
                const auto buttonW = w * .5f;

                rateType.setBounds(maxQuadIn(BoundsF(x, y, w - buttonW, h)).toNearestInt());
                x += buttonW;
                randType.setBounds(maxQuadIn(BoundsF(x, y, w - buttonW, h)).toNearestInt());
            }
            {
                const auto area = layout(1, 3, 1, 1);
                const auto w = area.getWidth();
				const auto h = area.getHeight();
				auto x = area.getX();
				auto y = area.getY();
                
                const auto buttonW = w * .5f;

                orientation.setBounds(maxQuadIn(BoundsF(x, y, buttonW, h)).toNearestInt());
				x += buttonW;
                outputType.setBounds(maxQuadIn(BoundsF(x, y, buttonW, h)).toNearestInt());
            }
        }

        void timerCallback() override
        {
            bool isTempoSync = utils.getParam(PID::RateType)->getValMod() > .5f;
            rateBeats.setVisible(isTempoSync);
			rateHz.setVisible(!isTempoSync);
        }

    protected:
        Knob rateHz, rateBeats, oct, width, phase;
        Button shapeNN, shapeLin, shapeRound;
        Button rateType, seed, orientation, randType, outputType;
        Oscilloscope scopeL, scopeR;
    };
}