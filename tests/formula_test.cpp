// Checks the formula language, then renders formula wavetables through an oscillator in the plugin.
#include "JuceHeader.h"
#include "formula.h"
#include "formula_wavetable.h"
#include "synth_base.h"
#include "wavetable_creator.h"

#include <cmath>
#include <cstdio>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {
  constexpr double kSampleRate = 48000.0;
  constexpr int kBlockSize = 256;
  constexpr int kNote = 57;  // A3
  constexpr double kFrequency = 220.0;

  int failures = 0;

  void check(bool condition, const std::string& what) {
    std::printf("%s %s\n", condition ? "  ok  " : "  FAIL", what.c_str());
    if (!condition)
      ++failures;
  }

  double eval(const std::string& text, double x = 0.0, double y = 0.0) {
    std::string error;
    auto formula = Formula::parse(text, error);
    if (formula == nullptr) {
      std::printf("  parse error for '%s': %s\n", text.c_str(), error.c_str());
      return NAN;
    }
    Formula::Variables variables;
    variables.x = x;
    variables.t = (x + 1.0) / 2.0;
    variables.y = y;
    return formula->evaluate(variables);
  }

  bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

  void testLanguage() {
    check(near(eval("1 + 2 * 3"), 7.0), "multiplication binds tighter than addition");
    check(near(eval("2 ^ 3 ^ 2"), 512.0), "power is right associative");
    check(near(eval("-2 ^ 2"), -4.0), "unary minus applies after power");
    check(near(eval("(1 + 2) * 3"), 9.0), "parentheses group");
    check(near(eval("x * 2", 0.25), 0.5) && near(eval("t", 0.0), 0.5) && near(eval("y", 0.0, 0.75), 0.75),
          "variables x, t and y");
    check(near(eval("sin(pi / 2)"), 1.0) && near(eval("max(2, 5)"), 5.0) && near(eval("clamp(7, 0, 1)"), 1.0),
          "functions with one, two and three arguments");
    check(near(eval("x > 0 ? 1 : -1", 0.5), 1.0) && near(eval("x > 0 ? 1 : -1", -0.5), -1.0),
          "conditional operator");
    check(near(eval("x > 0 && x < 1", 0.5), 1.0) && near(eval("!(x > 0) || 0", 0.5), 0.0), "logic operators");
    check(near(eval("mod(-0.25, 1)"), 0.75) && near(eval("-0.25 % 1"), 0.75), "modulo wraps negatives");
    check(near(eval("1 / 0"), 0.0) && near(eval("sqrt(-1)"), 0.0) && near(eval("log(0)"), 0.0),
          "undefined maths gives 0 instead of NaN");
    check(near(eval("SIN(PI * X)", 0.5), 1.0), "names are case-insensitive");
    check(near(eval("pulse(0.1, 0.25)"), 1.0) && near(eval("pulse(0.5, 0.25)"), -1.0) &&
          near(eval("saw(0.25)"), -0.5) && near(eval("tri(0.25)"), 1.0), "waveform helpers");
    check(eval("rand()") == eval("rand()"), "rand is repeatable");

    struct BadFormula { const char* text; const char* expected; };
    const BadFormula bad[] = {
      { "", "Enter a formula" },
      { "sin(x", "Expected ',' or ')'" },
      { "foo(x)", "Unknown name 'foo'" },
      { "1 +", "Unexpected end" },
      { "max(1)", "'max' takes 2 arguments" },
      { "x > 0 ? 1", "Expected ':'" },
      { "2 $ 3", "Unexpected '$'" },
      { "sin", "'sin' needs ( )" },
    };
    for (const BadFormula& b : bad) {
      std::string error;
      auto formula = Formula::parse(b.text, error);
      check(formula == nullptr && error.find(b.expected) != std::string::npos,
            std::string("rejects '") + b.text + "' with: " + error);
    }

    std::string deep(500, '(');
    std::string error;
    check(Formula::parse(deep + "1" + std::string(500, ')'), error) == nullptr, "rejects runaway nesting");
  }

  void testWavetableJson() {
    std::string error;
    json single = formula_wavetable::createWavetableJson("sin(pi * x)", error);
    json table = formula_wavetable::createWavetableJson("lerp(-x, square(t), y)", error);
    int single_frames = single["groups"][0]["components"][0]["keyframes"].size();
    int table_frames = table["groups"][0]["components"][0]["keyframes"].size();
    std::printf("  keyframes: single %d, table %d\n", single_frames, table_frames);
    check(single_frames == 1 && table_frames == formula_wavetable::kTableKeyframes,
          "only formulas using y render a full table");
    int last_position = table["groups"][0]["components"][0]["keyframes"][table_frames - 1]["position"];
    check(last_position == vital::kNumOscillatorWaveFrames - 1, "table keyframes span the whole table");

    json bad = formula_wavetable::createWavetableJson("sin(", error);
    check(bad.is_null() && !error.empty(), "a broken formula returns its error");

    for (int i = 0; i < formula_wavetable::kNumExamples; ++i) {
      std::string example_error;
      formula_wavetable::createWavetableJson(formula_wavetable::kExamples[i].formula, example_error);
      check(example_error.empty(), std::string("example '") + formula_wavetable::kExamples[i].name + "' parses");
    }
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

  void setParam(juce::AudioProcessor& processor, const juce::String& name, float normalized) {
    for (auto* param : processor.getParameters()) {
      if (param->getName(100) == name) {
        param->setValue(normalized);
        return;
      }
    }
    std::printf("missing parameter %s\n", name.toRawUTF8());
    std::exit(1);
  }

  // Loads a formula into oscillator 1, plays a note and returns h2/h1 and h3/h1.
  std::pair<double, double> renderHarmonics(const std::string& formula, float frame_position) {
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    processor->setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor->prepareToPlay(kSampleRate, kBlockSize);

    std::string error;
    json data = formula_wavetable::createWavetableJson(formula, error);
    WavetableCreator* creator = dynamic_cast<SynthBase*>(processor.get())->getWavetableCreator(0);
    creator->jsonToState(data);
    creator->render();
    setParam(*processor, "Oscillator 1 Wave Frame", frame_position);
    // Unison would smear the harmonics; one voice keeps the measurement clean.
    setParam(*processor, "Oscillator 1 Unison Voices", 0.0f);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    std::vector<float> out;
    int num_blocks = 1.0 * kSampleRate / kBlockSize;
    for (int b = 0; b < num_blocks; ++b) {
      juce::MidiBuffer midi;
      if (b == 0)
        midi.addEvent(juce::MidiMessage::noteOn(1, kNote, (juce::uint8)100), 0);
      buffer.clear();
      processor->processBlock(buffer, midi);
      for (int s = 0; s < kBlockSize; ++s)
        out.push_back(buffer.getSample(0, s));
    }
    processor->releaseResources();

    int start = kSampleRate / 2;
    int length = (int)((out.size() - start) * kFrequency / kSampleRate) * kSampleRate / kFrequency;
    std::vector<float> steady(out.begin() + start, out.begin() + start + length);
    double h1 = std::max(magnitudeAt(steady, kFrequency), 1e-9);
    return { magnitudeAt(steady, 2 * kFrequency) / h1, magnitudeAt(steady, 3 * kFrequency) / h1 };
  }

  void testPlugin() {
    auto sine = renderHarmonics("sin(pi * x)", 0.0f);
    std::printf("  sine:            h2 %.3f h3 %.3f\n", sine.first, sine.second);
    check(sine.first < 0.02 && sine.second < 0.02, "sine formula plays a pure tone");

    auto start = renderHarmonics("lerp(sin(pi * x), square(t), y)", 0.0f);
    auto end = renderHarmonics("lerp(sin(pi * x), square(t), y)", 1.0f);
    std::printf("  sine->square y=0: h3 %.3f   y=1: h2 %.3f h3 %.3f\n", start.second, end.first, end.second);
    check(start.second < 0.02 && end.second > 0.28 && end.second < 0.38 && end.first < 0.03,
          "the wavetable frame knob morphs along y");
  }
}

int main() {
  juce::ScopedJuceInitialiser_GUI juce_init;
  testLanguage();
  testWavetableJson();
  testPlugin();
  std::printf(failures ? "FAIL\n" : "PASS\n");
  return failures ? 1 : 0;
}
