# PerlinNoiseMod
This is a plugin that synthesizes perlin noise.
The output signal can be used as a modulator in DAWs that allow audio or midi cc signals to be modulators of other plugins parameters, like Bitwig and Reaper.

![perlin noise mod img](https://user-images.githubusercontent.com/54960398/225359341-e5125809-7fa5-44ed-9009-68722fa4ec31.PNG)

Perlin noise was developed by Ken Perlin while working on the original Tron movie in the early 1980s. He used it to create procedural textures for computer-generated effects and won an Academy Award in technical achievement for this work in 1997. Ken Perlin faced the problem that white noise is too random to generate realistic and believable looking landscapes, so he came up with an algorithm that creates random, yet continuous type of noise with a parameter that controls the complexity of the curve.

Quite a lot of plugin developers have already incorporated perlin noise as a modulator in their plugins, because as it turns out natural noise is also useful for music production. You can find a perlin noise modulator in my vibrato, NEL, or in the popular synthesizer, Vital, for example. This plugin generates perlin noise for you as an audio signal or midi cc messages, so you can decide yourself what you wanna use it for in your DAW of choice.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rate: Can be in free units (mhz / hz) or in temposync units (beats). It's the speed of the modulation. It determines how fast new random values get picked.

Oct: This parameters dials in the different octaves of noise. It makes the resulting modulation more complex. Putting oct to 1 essentially turns the perlin noise modulator to a normal randomizer.

Phase: You can shift the current phase of the noise with this parameter. This is useful when using the procedural mode, where the modulation becomes deterministic.

Width: The right channel receives a phase offset, which makes the modulation wide. However the Audiorate-Modulator in Bitwig, which would be used to receive the modulation data, does not pick up both channels individually, so there's no use for this parameter yet. I still decided to keep it there in case someone finds a diffrent usecase for it, and also because I want to implement this modulator in other plugins, like NEL, in the future. You can probably ignore it.

Steppy/Linear/Round: Defines how the plugin interpolates between random values. Steppy shapes make for some nice granular effects, round shapes make it a typical smooth random modulator and linear shapes are a nice middle-ground. Try it on a delay and you get an interesting pitchshifter-like effect for example.

Temposync: If enabled the rate is in temposync values.

Proc: The procedural mode makes the signal deterministic. It means if you pass the same project position over and over again at the same seed, bpm and rate as before, it will be the same noise segment. This is useful for people who wanna use randomization in their music on the one hand, but on the other hand want to be in full control of the sound that gets exported in the end.

+/-: If enabled this outputs bipolar modulation [-1, 1], if disabled it's omni [0, 1]

CC: If enabled this plugin does not only output the signal as an audio signal, but also as a stream of MIDI CC signals.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
