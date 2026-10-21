// Renders a held note through the plugin with Hyper/Dimension off and on, checking the output stays
// finite and that each stage widens the stereo image the way it should.
#include "JuceHeader.h"
#include "json/json.h"
#include "synth_constants.h"
#include "utils.h"

#include <cmath>
#include <cstdio>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {
  constexpr double kSampleRate = 48000.0;
  constexpr int kBlockSize = 256;
  constexpr int kNumBlocks = 400;

  struct Stats {
    double rms_left = 0.0, rms_right = 0.0, correlation = 0.0, mono_rms = 0.0;
    bool finite = true;
  };

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

  Stats render(std::vector<std::pair<juce::String, float>> params) {
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    processor->setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor->prepareToPlay(kSampleRate, kBlockSize);
    for (auto& p : params)
      setParam(*processor, p.first, p.second);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    double sum_ll = 0.0, sum_rr = 0.0, sum_lr = 0.0, sum_mono = 0.0;
    long count = 0;
    Stats stats;
    for (int b = 0; b < kNumBlocks; ++b) {
      juce::MidiBuffer midi;
      if (b == 0)
        midi.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8)100), 0);
      buffer.clear();
      processor->processBlock(buffer, midi);
      if (b < kNumBlocks / 4)
        continue;
      for (int s = 0; s < kBlockSize; ++s) {
        double l = buffer.getSample(0, s), r = buffer.getSample(1, s);
        if (!std::isfinite(l) || !std::isfinite(r))
          stats.finite = false;
        sum_ll += l * l; sum_rr += r * r; sum_lr += l * r;
        sum_mono += 0.25 * (l + r) * (l + r);
        ++count;
      }
    }
    processor->releaseResources();
    stats.rms_left = std::sqrt(sum_ll / count);
    stats.rms_right = std::sqrt(sum_rr / count);
    stats.correlation = sum_lr / std::sqrt(sum_ll * sum_rr + 1e-30);
    stats.mono_rms = std::sqrt(sum_mono / count);
    return stats;
  }

  // Loads a state shaped like a pre-Hyper preset (9 effect order, no hyper_* keys) and checks the
  // order is kept with Hyper appended at the end.
  bool checkOldPresetOrder() {
    using json = nlohmann::json;
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    juce::MemoryBlock block;
    processor->getStateInformation(block);
    json state = json::parse(block.toString().toStdString());

    int old_order[9] = { 8, 3, 5, 0, 7, 1, 6, 2, 4 };
    json& settings = state["settings"];
    for (auto it = settings.begin(); it != settings.end();) {
      if (it.key().rfind("hyper_", 0) == 0)
        it = settings.erase(it);
      else
        ++it;
    }
    settings["effect_chain_order"] = vital::utils::encodeOrderToFloat(old_order, 9);
    std::string data = state.dump();
    juce::MemoryOutputStream stream;
    stream.writeString(data);
    processor->setStateInformation(stream.getData(), (int)stream.getDataSize());

    juce::MemoryBlock reloaded;
    processor->getStateInformation(reloaded);
    json new_state = json::parse(reloaded.toString().toStdString());
    int new_order[vital::constants::kNumEffects];
    vital::utils::decodeFloatToOrder(new_order, new_state["settings"]["effect_chain_order"],
                                     vital::constants::kNumEffects);
    bool ok = new_order[9] == vital::constants::kHyper;
    for (int i = 0; i < 9; ++i)
      ok = ok && new_order[i] == old_order[i];
    std::printf("old preset order kept: %d\n", ok);
    return ok;
  }

  void print(const char* label, const Stats& s) {
    std::printf("%-22s rmsL %.4f rmsR %.4f mono %.4f corr %.3f finite %d\n",
                label, s.rms_left, s.rms_right, s.mono_rms, s.correlation, s.finite);
  }
}

int main() {
  juce::ScopedJuceInitialiser_GUI juce_init;

  Stats off = render({});
  Stats hyper = render({ { "Hyper Switch", 1.0f }, { "Hyper Mix", 1.0f }, { "Hyper Detune", 0.6f } });
  Stats dimension = render({ { "Hyper Switch", 1.0f }, { "Hyper Mix", 0.0f },
                             { "Dimension Mix", 1.0f }, { "Dimension Size", 0.7f } });
  Stats extreme = render({ { "Hyper Switch", 1.0f }, { "Hyper Mix", 1.0f }, { "Hyper Detune", 1.0f },
                           { "Hyper Rate", 1.0f }, { "Hyper Unison", 1.0f }, { "Dimension Mix", 1.0f },
                           { "Dimension Size", 1.0f } });
  print("off", off);
  print("hyper", hyper);
  print("dimension", dimension);
  print("extreme", extreme);

  bool ok = off.finite && hyper.finite && dimension.finite && extreme.finite;
  ok = ok && off.rms_left > 0.01 && off.correlation > 0.99;
  ok = ok && hyper.correlation < 0.9 && hyper.rms_left > 0.3 * off.rms_left;
  // Dimension only adds side content: the mono sum stays close to the dry signal.
  ok = ok && dimension.correlation < 0.95 && std::abs(dimension.mono_rms - off.mono_rms) < 0.05 * off.mono_rms;
  ok = checkOldPresetOrder() && ok;
  std::printf(ok ? "PASS\n" : "FAIL\n");
  return ok ? 0 : 1;
}
