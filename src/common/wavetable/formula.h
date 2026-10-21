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

#include <memory>
#include <string>
#include <vector>

// A small math expression language for drawing waveforms, in the style of Serum's formula parser.
//
// Variables:  x  position in the cycle, -1 to 1      t  position in the cycle, 0 to 1
//             y  position in the wavetable, 0 to 1  (a formula using y renders a full table)
// Constants:  pi, tau, e
// Operators:  + - * / % ^ (power), comparisons (< <= > >= == !=, giving 0 or 1),
//             && || !, and the conditional  cond ? a : b
// Functions:  sin cos tan asin acos atan atan2 sinh cosh tanh exp log log2 log10 sqrt abs sign
//             floor ceil round frac min max clamp pow mod lerp step smoothstep
//             rand  (repeatable noise for each sample and frame)
//             saw tri square pulse  (waveforms of a 0-1 phase, in phase with sin; pulse(p, width))
class Formula {
  public:
    struct Variables {
      double x = 0.0;
      double t = 0.0;
      double y = 0.0;
      unsigned int sample = 0;
      unsigned int frame = 0;
    };

    // Returns nullptr and fills error (with the character position) when the text doesn't parse.
    static std::unique_ptr<Formula> parse(const std::string& text, std::string& error);

    double evaluate(const Variables& variables) const;
    bool usesTablePosition() const { return uses_y_; }

    struct Node;

  private:
    Formula() : root_(-1), uses_y_(false) { }
    double evaluate(int node, const Variables& variables) const;

    std::vector<Node> nodes_;
    int root_;
    bool uses_y_;

    friend class FormulaParser;
};

struct Formula::Node {
  enum Type {
    kNumber,
    kVariable,
    kUnary,
    kBinary,
    kConditional,
    kFunction
  };

  Type type;
  double value = 0.0;
  int op = 0;
  int children[3] = { -1, -1, -1 };
  int num_children = 0;
};
