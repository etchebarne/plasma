// Checks the arpeggiator's timing and patterns sample by sample, then runs it through the plugin.
#include "JuceHeader.h"
#include "arpeggiator.h"

#include <cstdio>
#include <vector>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {
  constexpr double kSampleRate = 48000.0;
  constexpr double kBpm = 120.0;
  constexpr int kBlockSize = 512;
  // 1/16 at 120 BPM: a quarter beat, 6000 samples.
  constexpr int kSixteenth = 6000;

  struct Event {
    long long sample;
    bool on;
    int note;
  };

  struct Input {
    long long sample;
    juce::MidiMessage message;
  };

  int failures = 0;

  void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "  ok  " : "  FAIL", what);
    if (!condition)
      ++failures;
  }

  // Runs the arp over num_blocks blocks, feeding inputs at absolute sample positions.
  std::vector<Event> run(Arpeggiator::Settings settings, std::vector<Input> inputs, int num_blocks,
                         bool host_playing = false, double start_ppq = 0.0) {
    Arpeggiator arp;
    std::vector<Event> events;
    for (int b = 0; b < num_blocks; ++b) {
      long long block_start = (long long)b * kBlockSize;
      juce::MidiBuffer midi;
      for (auto& input : inputs) {
        if (input.sample >= block_start && input.sample < block_start + kBlockSize)
          midi.addEvent(input.message, (int)(input.sample - block_start));
      }
      double ppq = start_ppq + block_start * kBpm / (60.0 * kSampleRate);
      arp.process(midi, kBlockSize, kSampleRate, kBpm, host_playing, ppq, settings);
      for (const auto metadata : midi) {
        auto message = metadata.getMessage();
        if (message.isNoteOn())
          events.push_back({ block_start + metadata.samplePosition, true, message.getNoteNumber() });
        else if (message.isNoteOff())
          events.push_back({ block_start + metadata.samplePosition, false, message.getNoteNumber() });
      }
    }
    return events;
  }

  std::vector<Event> ons(const std::vector<Event>& events) {
    std::vector<Event> result;
    for (auto& e : events) {
      if (e.on)
        result.push_back(e);
    }
    if (getenv("ARP_TEST_VERBOSE")) {
      for (size_t i = 0; i < result.size() && i < 8; ++i)
        std::printf("    %lld:%d", result[i].sample, result[i].note);
      std::printf("  (%zu)\n", result.size());
    }
    return result;
  }

  std::vector<Input> chord(std::vector<int> notes, long long on, long long off = -1) {
    std::vector<Input> inputs;
    for (int note : notes) {
      inputs.push_back({ on, juce::MidiMessage::noteOn(1, note, (juce::uint8)100) });
      if (off >= 0)
        inputs.push_back({ off, juce::MidiMessage::noteOff(1, note) });
    }
    return inputs;
  }

  bool notesAre(const std::vector<Event>& events, std::vector<int> expected) {
    if (events.size() < expected.size())
      return false;
    for (size_t i = 0; i < expected.size(); ++i) {
      if (events[i].note != expected[i])
        return false;
    }
    return true;
  }

  void testArpeggiator() {
    Arpeggiator::Settings up;
    up.on = true;

    auto events = run(up, chord({ 60, 64, 67 }, 0), 60);
    auto on = ons(events);
    check(notesAre(on, { 60, 64, 67, 60, 64 }), "up pattern C E G C E");
    check(on.size() > 3 && on[0].sample == 0 && on[1].sample == kSixteenth && on[2].sample == 2 * kSixteenth,
          "free clock: first step on key press, then every 1/16");
    bool gate_ok = false;
    for (auto& e : events) {
      if (!e.on && e.note == 60) {
        gate_ok = e.sample == kSixteenth / 2;
        break;
      }
    }
    check(gate_ok, "50% gate releases halfway through the step");

    Arpeggiator::Settings up_down = up;
    up_down.mode = Arpeggiator::kUpDown;
    up_down.octaves = 2;
    on = ons(run(up_down, chord({ 60, 64, 67 }, 0), 140));
    check(notesAre(on, { 60, 64, 67, 72, 76, 79, 76, 72, 67, 64, 60 }), "up/down over 2 octaves bounces");

    Arpeggiator::Settings down = up;
    down.mode = Arpeggiator::kDown;
    check(notesAre(ons(run(down, chord({ 60, 64, 67 }, 0), 40)), { 67, 64, 60 }), "down pattern");

    Arpeggiator::Settings played = up;
    played.mode = Arpeggiator::kAsPlayed;
    std::vector<Input> order = { { 0, juce::MidiMessage::noteOn(1, 67, (juce::uint8)100) },
                                 { 10, juce::MidiMessage::noteOn(1, 60, (juce::uint8)100) },
                                 { 20, juce::MidiMessage::noteOn(1, 64, (juce::uint8)100) } };
    check(notesAre(ons(run(played, order, 60)), { 67, 60, 64, 67 }), "as played keeps press order");

    Arpeggiator::Settings chord_mode = up;
    chord_mode.mode = Arpeggiator::kChord;
    on = ons(run(chord_mode, chord({ 60, 64 }, 0), 20));
    check(on.size() >= 4 && on[0].sample == 0 && on[1].sample == 0 && on[2].sample == kSixteenth,
          "chord mode plays every note each step");

    on = ons(run(up, chord({ 60, 64 }, 0, 2 * kSixteenth + 100), 80));
    check(on.size() == 3, "releasing the keys stops the arp");

    Arpeggiator::Settings latched = up;
    latched.latch = true;
    on = ons(run(latched, chord({ 60, 64 }, 0, 2 * kSixteenth + 100), 80));
    check(on.size() > 6, "latch keeps playing after release");

    Arpeggiator::Settings swung = up;
    swung.swing = 1.0f;
    on = ons(run(swung, chord({ 60, 64, 67 }, 0), 40));
    check(on.size() >= 3 && on[1].sample == kSixteenth + kSixteenth / 2 && on[2].sample == 2 * kSixteenth,
          "full swing delays odd steps by half a step");

    // Host playing from beat 0.1: the first note plays on the key press, the next one on beat 0.25.
    on = ons(run(up, chord({ 60, 64 }, 0), 40, true, 0.1));
    check(on.size() >= 2 && on[0].sample == 0 && on[1].sample == (long long)(0.15 * 24000.0 + 0.5),
          "host sync locks steps to the beat grid");

    Arpeggiator::Settings off;
    std::vector<Input> pass = chord({ 60 }, 100);
    pass.push_back({ 200, juce::MidiMessage::controllerEvent(1, 1, 64) });
    Arpeggiator bypass;
    juce::MidiBuffer midi;
    for (auto& input : pass)
      midi.addEvent(input.message, (int)input.sample);
    bypass.process(midi, kBlockSize, kSampleRate, kBpm, false, 0.0, off);
    check(midi.getNumEvents() == 2, "arp off passes MIDI through");
  }

  void setParam(juce::AudioProcessor& processor, const juce::String& name, float normalized) {
    for (auto* param : processor.getParameters()) {
      if (param->getName(100) == name) {
        param->setValue(normalized);
        return;
      }
    }
    check(false, ("parameter " + name).toRawUTF8());
  }

  void testPlugin() {
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    processor->setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor->prepareToPlay(kSampleRate, kBlockSize);
    setParam(*processor, "Arp Switch", 1.0f);
    setParam(*processor, "Arp Gate", 0.3f);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    // RMS of each 1/16 step: with a short gate every step should start loud and fall quiet.
    std::vector<double> step_energy(16, 0.0);
    bool finite = true;
    for (int b = 0; b < 16 * kSixteenth / kBlockSize; ++b) {
      juce::MidiBuffer midi;
      if (b == 0) {
        midi.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8)100), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 55, (juce::uint8)100), 0);
      }
      buffer.clear();
      processor->processBlock(buffer, midi);
      for (int s = 0; s < kBlockSize; ++s) {
        float sample = buffer.getSample(0, s);
        finite = finite && std::isfinite(sample);
        long long position = (long long)b * kBlockSize + s;
        step_energy[(position / kSixteenth) % 16] += sample * sample;
      }
    }
    processor->releaseResources();
    int sounding_steps = 0;
    for (double energy : step_energy)
      sounding_steps += energy > 1e-3;
    std::printf("  plugin: %d of 16 steps produced sound\n", sounding_steps);
    check(finite && sounding_steps >= 14, "plugin renders the arp pattern with the arp switched on");
  }
}

int main() {
  juce::ScopedJuceInitialiser_GUI juce_init;
  testArpeggiator();
  testPlugin();
  std::printf(failures ? "FAIL\n" : "PASS\n");
  return failures ? 1 : 0;
}
