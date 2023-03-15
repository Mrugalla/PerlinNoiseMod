#pragma once
#include "../audio/Oscilloscope.h"
#include "Button.h"

namespace gui
{
	struct Oscilloscope :
		public Comp,
		public Timer
	{
		using Oscope = audio::Oscilloscope;
		static constexpr int FPS = 24;

		Oscilloscope(Utils& u, String&& _tooltip, const Oscope& _oscope) :
			Comp(u, _tooltip, CursorType::Default),
			Timer(),
			lineCID(ColourID::Txt),
			oscope(_oscope),
			curve(),
			bipolar(true)
		{
			startTimerHz(FPS);
		}

		void resized() override
		{
			const auto thicc = utils.thicc;
			bounds = getLocalBounds().toFloat().reduced(thicc);

			curve = Path();
			curve.preallocateSpace(static_cast<int>(bounds.getWidth() / thicc) + 1);
		}

		void paint(Graphics& g) override
		{
			curve.clear();
			const auto thicc = utils.thicc;
			
			const auto data = oscope.data();
			const auto size = oscope.windowLength();
			const auto sizeF = static_cast<float>(size);
			const auto beatLength = oscope.getBeatLength();
			const auto w = bounds.getWidth();
			const auto h = bounds.getHeight();
			const auto xScale = w / std::min(beatLength, sizeF);
			const auto xScaleInv = 1.f / xScale;
			const auto xOff = bounds.getX();
			
			if (!bipolar)
			{
				auto value = data[0];
				auto y = h - value * h;
				curve.startNewSubPath(xOff, y);
				for (auto i = 1.f; i <= w; i += thicc)
				{
					const auto x = xOff + i;
					const auto idx = static_cast<int>(i * xScaleInv);
					value = data[idx];
					y = h - value * h;
					curve.lineTo(x, y);
				}
			}
			else
			{
				const auto heightHalf = h * .5f;
				const auto centreY = bounds.getY() + heightHalf;
				
				{
					g.setColour(Colours::c(ColourID::Hover));
					const auto y = static_cast<int>(centreY);
					const auto inc = static_cast<int>(thicc * 3.f);
					for (auto x = static_cast<int>(bounds.getX()); x < bounds.getRight(); x += inc)
						g.fillRect(x,y,1,1);
				}

				auto value = data[0];
				auto y = centreY - value * heightHalf;
				curve.startNewSubPath(xOff, y);
				for (auto i = 1.f; i <= w; i += thicc)
				{
					const auto x = xOff + i;
					const auto idx = static_cast<int>(i * xScaleInv);
					value = data[idx];
					y = centreY - value * heightHalf;
					curve.lineTo(x, y);
				}
			}
			
			Stroke stroke(thicc, Stroke::JointStyle::beveled, Stroke::EndCapStyle::rounded);
			g.setColour(Colours::c(lineCID));
			g.strokePath(curve, stroke);
		}

		void timerCallback() override
		{
			repaint();
		}

		ColourID lineCID;
	protected:
		const Oscope& oscope;
		BoundsF bounds;
		Path curve;
	public:
		bool bipolar;
	};
}

/*

todo:

performance: only repaint part if the interface that was changed

*/