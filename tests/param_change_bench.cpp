// Times processBlock while one parameter changes every block (like dragging a knob), compared with
// leaving it alone. Prints the average and worst block time as a share of the block's real-time budget.
#include "JuceHeader.h"

#include <chrono>
#include <cstdio>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {
  constexpr double kSampleRate = 48000.0;
  constexpr int kBlockSize = 256;
  constexpr int kNumBlocks = 1500;

  juce::AudioProcessorParameter* findParam(juce::AudioProcessor& processor, const juce::String& name) {
    for (auto* param : processor.getParameters()) {
      if (param->getName(100) == name)
        return param;
    }
    std::printf("missing parameter %s\n", name.toRawUTF8());
    std::exit(1);
  }

  void run(const char* label, const juce::String& moving_param) {
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    processor->setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor->prepareToPlay(kSampleRate, kBlockSize);
    findParam(*processor, "Oscillator 1 Unison Voices")->setValue(7.0f / 15.0f);
    juce::AudioProcessorParameter* moving = moving_param.isEmpty() ? nullptr : findParam(*processor, moving_param);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    double total = 0.0, worst = 0.0;
    for (int b = 0; b < kNumBlocks; ++b) {
      juce::MidiBuffer midi;
      if (b == 0) {
        for (int note : { 48, 55, 60, 64 })
          midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)100), 0);
      }
      if (moving)
        moving->setValueNotifyingHost(0.5f + 0.4f * std::sin(b * 0.05f));

      buffer.clear();
      auto start = std::chrono::steady_clock::now();
      processor->processBlock(buffer, midi);
      double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      if (b > 50) {
        total += ms;
        worst = std::max(worst, ms);
      }
    }
    double budget = 1000.0 * kBlockSize / kSampleRate;
    std::printf("%-22s avg %.3f ms (%4.1f%% of budget)  worst %.3f ms (%5.1f%%)\n", label,
                total / (kNumBlocks - 51), 100.0 * total / (kNumBlocks - 51) / budget, worst, 100.0 * worst / budget);
  }
}

int main() {
  juce::ScopedJuceInitialiser_GUI juce_init;
  run("no changes", "");
  run("unison detune moving", "Oscillator 1 Unison Detune");
  run("transpose moving", "Oscillator 1 Transpose");
  run("level moving", "Oscillator 1 Level");
  run("filter cutoff moving", "Filter 1 Cutoff");
  return 0;
}
