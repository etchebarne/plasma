/* Copyright 2026 Plasma contributors
 *
 * Plasma is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Plasma is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Plasma.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "JuceHeader.h"

#include <random>

// MIDI-stage arpeggiator. It rewrites a block of incoming MIDI: held notes are consumed and replaced
// with a sample-accurate note pattern, everything else passes through. When the host is playing,
// steps lock to the host's beat grid; otherwise the arp runs on its own clock, started by the first
// key press. It does not allocate on the audio thread.
class Arpeggiator {
  public:
    enum Mode {
      kUp,
      kDown,
      kUpDown,
      kDownUp,
      kAsPlayed,
      kRandom,
      kChord,
      kNumModes
    };

    static constexpr int kNumRates = 16;
    static constexpr int kMaxOctaves = 4;
    static constexpr int kNumMidiNotes = 128;

    // Length of one step in quarter-note beats, indexed by the "arp_rate" parameter.
    static constexpr double kRateBeats[kNumRates] = {
      4.0, 2.0, 3.0, 4.0 / 3.0,              // 1/1, 1/2, 1/2., 1/2T
      1.0, 1.5, 2.0 / 3.0,                   // 1/4, 1/4., 1/4T
      0.5, 0.75, 1.0 / 3.0,                  // 1/8, 1/8., 1/8T
      0.25, 0.375, 1.0 / 6.0,                // 1/16, 1/16., 1/16T
      0.125, 1.0 / 12.0, 0.0625              // 1/32, 1/32T, 1/64
    };

    struct Settings {
      bool on = false;
      int mode = kUp;
      int rate = 10;
      int octaves = 1;
      float gate = 0.5f;
      float swing = 0.0f;
      bool latch = false;
    };

    Arpeggiator();

    void process(MidiBuffer& midi, int num_samples, double sample_rate, double bpm,
                 bool host_playing, double host_ppq, const Settings& settings);
    void reset();

  private:
    struct Sounding {
      int note;
      int channel;
      double off_beat;
    };

    double stepBeat(long long step) const;
    long long firstStepAtOrAfter(double beat) const;
    long long firstStepAfter(double beat) const;
    int samplesUntil(double beat) const;
    bool anyActive() const;
    bool anyPhysical() const;
    void handleMessage(const MidiMessage& message, int sample);
    void start();
    void fireStep(int sample);
    int buildSequence();
    void playNote(int note, int velocity, int channel, int sample);
    void releaseDueNotes(int sample);
    void releaseAll(int sample);
    void clearHeld();

    Settings settings_;
    bool was_on_;
    bool running_;
    // Set on start so the first step plays after every event at the same sample (a whole chord).
    bool immediate_step_;
    bool host_playing_;
    double beat_;
    double beats_per_sample_;
    double step_beats_;
    long long next_step_;
    long long pattern_position_;
    int last_rate_;

    // Physically held keys and, when latched, the notes kept after release.
    bool physical_[kNumMidiNotes];
    bool latched_[kNumMidiNotes];
    int velocities_[kNumMidiNotes];
    int channels_[kNumMidiNotes];
    long long press_order_[kNumMidiNotes];
    long long press_counter_;

    int sequence_[kNumMidiNotes * kMaxOctaves * 2];
    Sounding sounding_[kNumMidiNotes];
    int num_sounding_;

    std::minstd_rand random_;
    MidiBuffer output_;

    JUCE_LEAK_DETECTOR(Arpeggiator)
};
