# Scream

This repo continues the work previously made by Speechrezz [here](https://github.com/Speechrezz/Scream-Filter).

Many ideas used by our design came from reading the paper [The Art of VA Filter Design](https://archive.org/details/the-art-of-va-filter-design-rev.-2.1.2) (See Ch. 6 Nonlinearities)

### Comparisons to the original

The parameters used by this plugin do not, and cannot match 1:1 with the parameters used by the original synths filter. We explain exactly why below.

Instead we aim to capture a slightly larger parameter range, so as thgat similar, if not the same sounds can still be created only with slightly highler or lower parameter settings.

#### Gain

In the original synth, the oscillator had an "AMP" parameter controling volume, which was usually kept at 100%, and all the wavetables were (approximately) matched in loudness. In the original synth, when using the scream filter parameter configuration: 100%, 50%, 100% (Cutoff, scream, resonance respectively) with lower oscillator AMP (< 100), the character of the sound changes drastically. The lower the AMP parameter, the more resonant and feedbacky the signal sounds around the filter cutoff, and it sounds less saturated/overdriven. Keeping the AMP at 100% causes the filter to often sound overdriven when the "Resonance" is set to higher values (>50%).

In most cases people are expecting to get saturated sound with high Resonance levels. In order to acheive this, the input gain must be hitting approximately 0dB, which is a similar level to the what a synths filter would be processing.

Effect plugins cannot guarantee inputs stay consistent at any level. Users will constantly change the order of effect plugins, bypassing some plugins, duplicating them, removing them etc. All of this affects the gain that hits our internal distortion and wildly affects the resulting sound, just like any other distortion effect.

We could have added an autogain feature to boost incoming quiet signal, but this would likely ruin any signal with a long attack / swell, and capturing fast attacks in drum hits for example would be difficult, and likely ruin them too.

We decided to solve this problem by adding an input gain parameter to give users manual contol.

#### Keytracking

In the original synth, the internal filters cutoff and Q were keytracked. When applying the original filter on the synths oscillator, one would hear wildly different sounds depending on weather they played a low MIDI note (dull sound due to lower cutoff of internal lowpass filter, less Q), or pitched the oscillator down several octaves and played a high note (brighter sound due to lowpass)

This plugin is not a synth and cannot reliably know the pitch of the incoming signal. It is only possible to match the parameters 1:1 with the original if the pitch is known at all times.

We could have added pitch detection algorithms, but fast and low latency ones may lack accuracy and not capture the attack of signals very well, which could ruin the intended sound. More accurate pitch detectors may introduce an undesirable amount of latency and CPU usage, and still fail on complicated input signals such as percussion.

We could have the plugin accept MIDI input, but its not clear how the filter should handle chords, long release times, delays, pitch automation etc. In theory this solution may work very well on signals that are monophonic with no pitch automation, no delays, and with an instant release time. Requiring signals fit into this small category greatly restricts how the filter gets used, and we would like this filter to be used on anything. Additionally, routing MIDI to FX is very cumbersome in DAWs and most users wouldn't use the feature even if we offered it.

We decided it was best not to use any keytracking and instead offer the widest possble frequency range. This means our cutoffs will go much further than in the original synth.

The parameter "Resonance" in the orignal plugin is suspected to control both a boost to the gain used by an internal distortion as well as apply a boost to the Q used by internal lowpasses and highpasses. If this assumption is correct, matching the "Resonance" parameters by ear in both the original plugin and ours will be challenging.

The maximum drive that can be applied in our plugins Resonance parameter is approximately the same as the original, as well as the maximum possible Q values used by the internal LP and HP filters.

### Recommendations for comparisons

When doing comparisons, it is recommended to follow these steps in our plugin:

1. Play back the desired input audio and boost the input gain parameter and until the meter averages close to 0dB.
2. Set the Scream paramter to 0% and the Resonance parameter to 75%. This is only temporary.
3. Set the Cutoff parameter higher or lower depending on the MIDI input you are sending to the original synth. For example in the original synth, if the cutoff is at 100% and you play a low E1 midi note, the internal lowpass will be set to approxiamtely 5kHz. If you play E2, the LP cutoff will be at 10kHz, E3 will 20kHz, E0 2.5 kHz, and so on. This pattern can be observed with any spectrum analyser. We recommend you use one to match the LP cutoff in both plugins.
4. Tune the frequency of the Scream parameter by ear, or with the help of a frequency analyser
5. Tune the level of the Resonance parameter by ear, until you start hearing a similar tone. Very small adjustments to the input gain may help you match a slower sound

### Other noteworthy quirks

- The original synths ADSR cannot set its internal release times to 0. The GUI may tell you the paramters release value is 0, but internally its not. When you release a MIDI note, the audio signal will exponentially decay in volume and will be virtually inaudible for about 100ms, but a distortion will boost that level much higher. This is a real nuisance when you start applying loads of distorions and/or compression after the synth, because this results in a nasty squeely distorted release sound, possibly followed by a click. The synth has its own distoriton effects, however they are sit in the signal chain before ADSR, resulting in smooth attacks and decays (assuming no 3rd party FX are applied afterwards).

- The original filter suffered from self oscillation problems. In our filter, we employ clever gating tricks to prevent this.

### Room for improvement

If all parameters are correctly matched, one may notice very subtle characteristics to the distoriton applied in the original filter that we do not match exactly. These differences are observable when profiled by an oscilloscope, however the shape of the audio is still very similar and most importantly sounds "close enough". So far, no one who has beta tested the plugin has voiced this particular criticim.

Future contributions to the plugins distortion are welcome if they bring us even closer to the original. It's unlikely the averge user will benefit from a contribution like this, but it may help to satiate some purist out there.

It's worth noting that "the original" or "old" is not necessarily always better. Future filter designs can take inspiration from this design and change things like filter types (LP/HP/BP/AP/SVF), distoriton types, or change the whole signal chain, all to create cool new unique sounds, some of which may sound better to your ear.

### About the GUI code

This is all a tech demo of work in progress GUI libraries I'm working on. The goal has been to find a straight forward way of writing GUIs at a low enough level that the code can be highly organised, highly tweakable, ergonomic to write, fast to compile and run, and look really good. All other GUI systems I know of compromise on one or more of those points. I want to have my cake and eat it too!

I'm using a novel way write/use an immediate mode (IM) library. There are probably other small projects that use similar techniques to what I do, but I have not come across any. Let's coin the term "treeless immediate mode library" or "TIM" to discern this style of library.

Update: Here are 2 YouTube streams I just found of people implementing what I have, but for mouse down/up events only

1. https://www.youtube.com/watch?v=go-h9c_h6Uw
2. https://www.youtube.com/watch?v=8e56I1z9cn4

Most IM libraries build a big retained mode (RM) like tree structure under the hood and use complicated diffs between tree structures from previous frames while offering a simple declarative IM interface.

TIM calculates hit tests at runtime and stores ids that succeed on a first come first served (served = stores the id) basis. This has the advantage of skipping the enormous amount of state with the bugs and restrictions that come with developing and maintaining a big tree structure, while suffering from a few unique probelems. Here are some examples:

- TIM forces a front to back widget event handling style, which the the reverse of how GUIs usually want to be rendered (back to front).
- Certain events like key presses must respond "event consumed" or "event not consumed" immediately as they're received and cannot be handled at draw time. This plugin attemps to correctly handle this stuff by setting some flags, but the IM lib I'm hoping to use doesn't have any way of solving that problem at all.
- Trying to drag a file out of your window on macOS is something that can only be done from the beginning of the mouse down event sent to your NSView. It cannot be done for example at the end of a frame like it can on Windows.

It's still not certain how well the TIM technique can scale in a large app with lots of dynamic content, popup windows, text input widgets, file drag in and drag out functionality and still be ergonomic to use. The library needs more work and more testing, and may even prove to be not worth it, or may need major rewrites and fundamental design changes. For small apps like this one, none the aforementioned problems are bad enough that the tree based IM system or an RM system would offer better value. I think the lightweight and flexible TIM system is much better for small apps like this one even in its unfinished state. TIM was the right choice for this app and switching to a conservative RM would make this app worse.

My philosophy of making the TIM lib so far has been "run into a tough problem a few times before adding solutions for it to the library".

On the rendering side, I'm running a fork of `nanovg` with using `sokol_gfx` backend, but I hope to incrementally drop parts of it. It's all still a work in progress. For basic shapes and text/images I'd like to replace with SDFs rendered using a single ubershader. I'd like to replace font stash with my own wrapper over `kb_text_shaping` & `freetype2`, but possibly also add options for `stb_truetype`, `CoreText` and `DirectWrite`. I will probably keep nanovgs tessellation for all other complex stroked/filled polygons, unless I find a way to integrate somebody elses eg. `vello` or `pathkit`. I've already added some basic support for creating frame buffers, blurring frame buffers with optional bloom, and running custom shaders

sokol_gfx of course uses global state, so I've had to fork it to make it usable by multi instance plugins and change the API to require setting / unsetting a global state pointer.

### Building

Note that I'm not interested in supporting a wide range of compilers, namely MSVC, or mingw or any of that nonsense. This codebase is written in C99 and probably uses several clang and GCC extensions, many of which aren't supported by MSVC. A clever programmer could probably compile all this as C++ 20/23 with MSVC, but why torture youself? Mingw from my limited experience doesn't come with system headers for Windows MIDI apis rendering it unusable for audio work. Clang is mostly consistent across platforms which IMO is better suited to this kind of development.

Requirements:

- [CMake](https://cmake.org/)
- [Ninja](https://ninja-build.org/)
- [Clang 17+](https://releases.llvm.org/download.html), or something reasonably modern. Older versions likely work
- [sokol-shdc](https://github.com/floooh/sokol-tools)
- a macOS or Windows device

Build:

```
git clone https://github.com/Cure-Audio/Scream
cd Scream
git submodule update --init
#windows
.\shaders.bat
#macos
sh ./shaders.sh
cmake -B build -G Ninja .
cmake --build build
```
