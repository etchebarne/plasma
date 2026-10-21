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

#include "arpeggiator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
  constexpr double kBeatEpsilon = 1e-9;
  constexpr double kResyncTolerance = 1e-3;
  constexpr int kAllSoundOff = 120;
  constexpr int kAllNotesOff = 123;
  constexpr int kOutputReserve = 2048;
}

Arpeggiator::Arpeggiator() : random_(0x504c534d) {
  output_.ensureSize(kOutputReserve);
  reset();
}

void Arpeggiator::reset() {
  was_on_ = false;
  running_ = false;
  immediate_step_ = false;
  host_playing_ = false;
  beat_ = 0.0;
  beats_per_sample_ = 0.0;
  step_beats_ = kRateBeats[settings_.rate];
  next_step_ = 0;
  pattern_position_ = 0;
  last_rate_ = -1;
  press_counter_ = 0;
  num_sounding_ = 0;
  clearHeld();
}

void Arpeggiator::clearHeld() {
  for (int i = 0; i < kNumMidiNotes; ++i) {
    physical_[i] = false;
    latched_[i] = false;
    velocities_[i] = 0;
    channels_[i] = 1;
    press_order_[i] = 0;
  }
}

double Arpeggiator::stepBeat(long long step) const {
  double swing_offset = (step & 1) ? 0.5 * settings_.swing * step_beats_ : 0.0;
  return step * step_beats_ + swing_offset;
}

long long Arpeggiator::firstStepAtOrAfter(double beat) const {
  long long step = (long long)std::floor(beat / step_beats_) - 1;
  while (stepBeat(step) < beat - kBeatEpsilon)
    ++step;
  return step;
}

long long Arpeggiator::firstStepAfter(double beat) const {
  long long step = (long long)std::floor(beat / step_beats_) - 1;
  while (stepBeat(step) <= beat + kBeatEpsilon)
    ++step;
  return step;
}

int Arpeggiator::samplesUntil(double beat) const {
  double samples = (beat - beat_) / beats_per_sample_;
  if (samples <= 0.0)
    return 0;
  if (samples > std::numeric_limits<int>::max() / 2)
    return std::numeric_limits<int>::max() / 2;
  return (int)std::ceil(samples - kBeatEpsilon);
}

bool Arpeggiator::anyActive() const {
  const bool* notes = settings_.latch ? latched_ : physical_;
  for (int i = 0; i < kNumMidiNotes; ++i) {
    if (notes[i])
      return true;
  }
  return false;
}

bool Arpeggiator::anyPhysical() const {
  for (int i = 0; i < kNumMidiNotes; ++i) {
    if (physical_[i])
      return true;
  }
  return false;
}

void Arpeggiator::process(MidiBuffer& midi, int num_samples, double sample_rate, double bpm,
                          bool host_playing, double host_ppq, const Settings& settings) {
  bool latch_released = settings_.latch && !settings.latch;
  settings_ = settings;
  settings_.mode = jlimit(0, kNumModes - 1, settings_.mode);
  settings_.rate = jlimit(0, kNumRates - 1, settings_.rate);
  settings_.octaves = jlimit(1, kMaxOctaves, settings_.octaves);
  settings_.gate = jlimit(0.05f, 1.0f, settings_.gate);
  settings_.swing = jlimit(0.0f, 1.0f, settings_.swing);

  beats_per_sample_ = std::max(1.0, bpm) / (60.0 * std::max(1.0, sample_rate));
  step_beats_ = kRateBeats[settings_.rate];
  output_.clear();

  if (!settings_.on) {
    // Pass MIDI through untouched, tracking held keys so turning the arp on mid-hold works.
    if (was_on_) {
      releaseAll(0);
      running_ = false;
      for (int i = 0; i < kNumMidiNotes; ++i)
        latched_[i] = false;
    }
    was_on_ = false;
    for (const MidiMessageMetadata metadata : midi) {
      MidiMessage message = metadata.getMessage();
      if (message.isNoteOn())
        physical_[message.getNoteNumber()] = true;
      else if (message.isNoteOff())
        physical_[message.getNoteNumber()] = false;
      else if (message.isAllNotesOff() || message.isAllSoundOff())
        clearHeld();
      output_.addEvent(message, metadata.samplePosition);
    }
    midi.swapWith(output_);
    return;
  }

  if (!was_on_) {
    // The engine already holds these keys from pass-through; release them and arpeggiate instead.
    for (int i = 0; i < kNumMidiNotes; ++i) {
      if (physical_[i])
        output_.addEvent(MidiMessage::noteOff(channels_[i], i), 0);
    }
    running_ = false;
    was_on_ = true;
  }

  if (latch_released) {
    for (int i = 0; i < kNumMidiNotes; ++i)
      latched_[i] = false;
  }

  host_playing_ = host_playing;
  if (host_playing_ && std::abs(host_ppq - beat_) > kResyncTolerance) {
    beat_ = host_ppq;
    if (running_)
      next_step_ = firstStepAtOrAfter(beat_);
  }
  if (settings_.rate != last_rate_ && running_)
    next_step_ = firstStepAfter(beat_);
  last_rate_ = settings_.rate;

  if (running_ && !anyActive())
    running_ = false;
  else if (!running_ && anyActive())
    start();

  MidiBufferIterator event = midi.begin();
  int sample = 0;
  while (true) {
    int next_event = event != midi.end() ? jlimit(sample, num_samples, (*event).samplePosition) : num_samples;

    int next_off = num_samples;
    for (int i = 0; i < num_sounding_; ++i)
      next_off = std::min(next_off, sample + samplesUntil(sounding_[i].off_beat));

    int next_step = num_samples;
    if (running_)
      next_step = immediate_step_ ? sample : sample + samplesUntil(stepBeat(next_step_));
    int time = std::min(next_event, std::min(next_off, next_step));
    if (time >= num_samples) {
      beat_ += (num_samples - sample) * beats_per_sample_;
      break;
    }

    beat_ += (time - sample) * beats_per_sample_;
    sample = time;

    if (next_off == time)
      releaseDueNotes(time);
    else if (next_event == time) {
      handleMessage((*event).getMessage(), time);
      ++event;
    }
    else if (immediate_step_) {
      immediate_step_ = false;
      fireStep(time);
    }
    else {
      fireStep(time);
      ++next_step_;
    }
  }

  midi.swapWith(output_);
}

void Arpeggiator::handleMessage(const MidiMessage& message, int sample) {
  if (message.isNoteOn()) {
    int note = message.getNoteNumber();
    if (settings_.latch && !anyPhysical()) {
      for (int i = 0; i < kNumMidiNotes; ++i)
        latched_[i] = false;
    }
    physical_[note] = true;
    latched_[note] = true;
    velocities_[note] = message.getVelocity();
    channels_[note] = message.getChannel();
    press_order_[note] = ++press_counter_;
    if (!running_)
      start();
    return;
  }

  if (message.isNoteOff()) {
    physical_[message.getNoteNumber()] = false;
    if (!anyActive())
      running_ = false;
    return;
  }

  if (message.isController() &&
      (message.getControllerNumber() == kAllNotesOff || message.getControllerNumber() == kAllSoundOff)) {
    clearHeld();
    releaseAll(sample);
    running_ = false;
  }

  output_.addEvent(message, sample);
}

void Arpeggiator::start() {
  running_ = true;
  pattern_position_ = 0;
  if (!host_playing_)
    beat_ = 0.0;

  // Play the first step on the key press, then continue on the grid.
  next_step_ = firstStepAfter(beat_);
  immediate_step_ = true;
}

int Arpeggiator::buildSequence() {
  const bool* active = settings_.latch ? latched_ : physical_;
  int notes[kNumMidiNotes];
  int num_notes = 0;
  for (int i = 0; i < kNumMidiNotes; ++i) {
    if (active[i])
      notes[num_notes++] = i;
  }

  if (settings_.mode == kAsPlayed) {
    std::sort(notes, notes + num_notes, [this](int a, int b) { return press_order_[a] < press_order_[b]; });
  }

  int up_size = 0;
  for (int octave = 0; octave < settings_.octaves; ++octave) {
    for (int i = 0; i < num_notes; ++i) {
      int note = notes[i] + 12 * octave;
      if (note < kNumMidiNotes)
        sequence_[up_size++] = note;
    }
  }

  if (settings_.mode == kDown || settings_.mode == kDownUp)
    std::reverse(sequence_, sequence_ + up_size);

  if ((settings_.mode == kUpDown || settings_.mode == kDownUp) && up_size > 2) {
    // Bounce back without repeating the turnaround notes.
    int size = up_size;
    for (int i = up_size - 2; i > 0; --i)
      sequence_[size++] = sequence_[i];
    return size;
  }
  return up_size;
}

void Arpeggiator::fireStep(int sample) {
  int size = buildSequence();
  if (size == 0) {
    running_ = false;
    return;
  }

  if (settings_.mode == kChord) {
    const bool* active = settings_.latch ? latched_ : physical_;
    int octave = pattern_position_ % settings_.octaves;
    for (int note = 0; note < kNumMidiNotes; ++note) {
      int played = note + 12 * octave;
      if (active[note] && played < kNumMidiNotes)
        playNote(played, velocities_[note], channels_[note], sample);
    }
  }
  else {
    int index = 0;
    if (settings_.mode == kRandom)
      index = std::uniform_int_distribution<int>(0, size - 1)(random_);
    else
      index = pattern_position_ % size;

    // Velocity and channel come from the held key the note was transposed from.
    const bool* active = settings_.latch ? latched_ : physical_;
    int note = sequence_[index];
    int source_note = note;
    while (source_note >= 0 && !active[source_note])
      source_note -= 12;
    if (source_note >= 0)
      playNote(note, velocities_[source_note], channels_[source_note], sample);
  }
  pattern_position_++;
}

void Arpeggiator::playNote(int note, int velocity, int channel, int sample) {
  for (int i = 0; i < num_sounding_; ++i) {
    if (sounding_[i].note == note) {
      output_.addEvent(MidiMessage::noteOff(sounding_[i].channel, note), sample);
      sounding_[i] = sounding_[--num_sounding_];
      break;
    }
  }

  output_.addEvent(MidiMessage::noteOn(channel, note, (uint8)jlimit(1, 127, velocity)), sample);
  if (num_sounding_ < kNumMidiNotes)
    sounding_[num_sounding_++] = { note, channel, beat_ + settings_.gate * step_beats_ };
}

void Arpeggiator::releaseDueNotes(int sample) {
  for (int i = 0; i < num_sounding_;) {
    if (samplesUntil(sounding_[i].off_beat) == 0) {
      output_.addEvent(MidiMessage::noteOff(sounding_[i].channel, sounding_[i].note), sample);
      sounding_[i] = sounding_[--num_sounding_];
    }
    else
      ++i;
  }
}

void Arpeggiator::releaseAll(int sample) {
  for (int i = 0; i < num_sounding_; ++i)
    output_.addEvent(MidiMessage::noteOff(sounding_[i].channel, sounding_[i].note), sample);
  num_sounding_ = 0;
}
