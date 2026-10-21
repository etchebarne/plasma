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

#include "json/json.h"

#include <string>

using json = nlohmann::json;

namespace formula_wavetable {
  // A formula that uses y renders this many keyframes, evenly spread across the table.
  constexpr int kTableKeyframes = 65;

  struct Example {
    const char* name;
    const char* formula;
  };

  // Shown in the oscillator's formula menu.
  extern const Example kExamples[];
  extern const int kNumExamples;

  // Renders a formula into wavetable JSON in the .vitaltable format, ready to load into an oscillator.
  // Returns an empty object and fills error when the formula doesn't parse.
  json createWavetableJson(const std::string& formula_text, std::string& error);
}
