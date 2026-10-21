// Renders the sub oscillator alone through the plugin and checks its pitch, the harmonic makeup of
// each waveform, panning, and that its settings survive a state save and reload.
#include "JuceHeader.h"

#include <cmath>
#include <cstdio>
#include <vector>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {
  constexpr double kSampleRate = 48000.0;
  constexpr int kBlockSize = 256;
  constexpr int kNote = 57;  // A3, 220 Hz; the sub's default -12 transpose puts it at 110 Hz.
  constexpr double kSubFrequency = 110.0;

  int failures = 0;

  void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "  ok  " : "  FAIL", what);
    if (!condition)
      ++failures;
  }

  juce::AudioProcessorParameter* findParam(juce::AudioProcessor& processor, const juce::String& name) {
    for (auto* param : processor.getParameters()) {
      if (param->getName(100) == name)
        return param;
    }
    std::printf("missing parameter '%s'\n", name.toRawUTF8());
    std::exit(1);
  }

  // Sets a parameter from its displayed text, so tests read in the units a user sees.
  void setText(juce::AudioProcessor& processor, const juce::String& name, const juce::String& text) {
    auto* param = findParam(processor, name);
    param->setValue(param->getValueForText(text));
  }

  double magnitudeAt(const std::vector<float>& signal, double frequency) {
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < signal.size(); ++i) {
      double phase = 2.0 * juce::MathConstants<double>::pi * frequency * i / kSampleRate;
      re += signal[i] * std::cos(phase);
      im += signal[i] * std::sin(phase);
    }
    return 2.0 * std::sqrt(re * re + im * im) / signal.size();
  }

  struct Render {
    std::vector<float> left, right;
  };

  Render render(juce::AudioProcessor& processor, double seconds) {
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    Render result;
    int num_blocks = seconds * kSampleRate / kBlockSize;
    for (int b = 0; b < num_blocks; ++b) {
      juce::MidiBuffer midi;
      if (b == 0)
        midi.addEvent(juce::MidiMessage::noteOn(1, kNote, (juce::uint8)100), 0);
      buffer.clear();
      processor.processBlock(buffer, midi);
      for (int s = 0; s < kBlockSize; ++s) {
        result.left.push_back(buffer.getSample(0, s));
        result.right.push_back(buffer.getSample(1, s));
      }
    }
    // Skip the attack; analyse a whole number of sub cycles of the sustain.
    int start = kSampleRate / 2;
    int cycles = (result.left.size() - start) * kSubFrequency / kSampleRate;
    int length = cycles * kSampleRate / kSubFrequency;
    result.left = std::vector<float>(result.left.begin() + start, result.left.begin() + start + length);
    result.right = std::vector<float>(result.right.begin() + start, result.right.begin() + start + length);
    return result;
  }

  std::unique_ptr<juce::AudioProcessor> makeSubOnly(const juce::String& waveform) {
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    processor->setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor->prepareToPlay(kSampleRate, kBlockSize);
    findParam(*processor, "Oscillator 1 Switch")->setValue(0.0f);
    findParam(*processor, "Sub Switch")->setValue(1.0f);
    setText(*processor, "Sub Waveform", waveform);
    return processor;
  }

  void testWaveforms() {
    struct Expected {
      const char* name;
      double min_h2, max_h2, min_h3, max_h3;  // Harmonics relative to the fundamental.
    };
    // Ideal ratios: saw 1/2 and 1/3, square 0 and 1/3, triangle 0 and 1/9, 25% pulse 0.71 and 1/3.
    const Expected expected[] = {
      { "Sine", 0.0, 0.02, 0.0, 0.02 },
      { "Triangle", 0.0, 0.02, 0.09, 0.13 },
      { "Saw", 0.45, 0.55, 0.29, 0.38 },
      { "Square", 0.0, 0.03, 0.29, 0.38 },
      { "Pulse", 0.65, 0.76, 0.29, 0.38 },
      { "Rounded", 0.0, 0.03, 0.1, 0.33 },
    };

    for (const Expected& e : expected) {
      auto processor = makeSubOnly(e.name);
      Render out = render(*processor, 1.2);
      processor->releaseResources();

      bool finite = true;
      for (float s : out.left)
        finite = finite && std::isfinite(s);
      double h1 = magnitudeAt(out.left, kSubFrequency);
      double h2 = magnitudeAt(out.left, 2.0 * kSubFrequency) / std::max(h1, 1e-9);
      double h3 = magnitudeAt(out.left, 3.0 * kSubFrequency) / std::max(h1, 1e-9);
      double octave_up = magnitudeAt(out.left, 2.0 * kSubFrequency * 2.0) / std::max(h1, 1e-9);
      std::printf("  %-9s fundamental %.3f  h2 %.3f  h3 %.3f\n", e.name, h1, h2, h3);

      char label[128];
      std::snprintf(label, sizeof(label), "%s plays at the sub pitch with the expected harmonics", e.name);
      check(finite && h1 > 0.05 && h2 >= e.min_h2 && h2 <= e.max_h2 && h3 >= e.min_h3 && h3 <= e.max_h3 &&
            (std::string(e.name) != "Sine" || octave_up < 0.02), label);
    }
  }

  void testPan() {
    auto processor = makeSubOnly("Sine");
    setText(*processor, "Sub Pan", "-100");
    Render out = render(*processor, 1.0);
    double left = magnitudeAt(out.left, kSubFrequency);
    double right = magnitudeAt(out.right, kSubFrequency);
    std::printf("  pan hard left: L %.3f R %.3f\n", left, right);
    check(left > 0.05 && right < 0.1 * left, "hard left pan silences the right channel");
  }

  void testState() {
    auto processor = makeSubOnly("Saw");
    setText(*processor, "Sub Transpose", "-24");
    juce::MemoryBlock state;
    processor->getStateInformation(state);

    std::unique_ptr<juce::AudioProcessor> reloaded(createPluginFilter());
    reloaded->setStateInformation(state.getData(), (int)state.getSize());
    bool on = findParam(*reloaded, "Sub Switch")->getValue() > 0.5f;
    juce::String waveform = findParam(*reloaded, "Sub Waveform")->getCurrentValueAsText();
    juce::String transpose = findParam(*reloaded, "Sub Transpose")->getCurrentValueAsText();
    std::printf("  reloaded: on %d, waveform %s, transpose %s\n", on, waveform.toRawUTF8(), transpose.toRawUTF8());
    check(on && waveform == "Saw" && transpose == "-24", "sub settings survive save and reload");
  }
}

int main() {
  juce::ScopedJuceInitialiser_GUI juce_init;
  testWaveforms();
  testPan();
  testState();
  std::printf(failures ? "FAIL\n" : "PASS\n");
  return failures ? 1 : 0;
}
