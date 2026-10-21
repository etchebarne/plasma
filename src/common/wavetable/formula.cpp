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

#include "formula.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace {
  constexpr double kPi = 3.14159265358979323846;
  constexpr int kMaxDepth = 200;

  enum Variable { kX, kT, kY, kPiConstant, kTau, kE };

  enum Operator {
    kAdd, kSubtract, kMultiply, kDivide, kModulo, kPower,
    kLess, kLessEqual, kGreater, kGreaterEqual, kEqual, kNotEqual, kAnd, kOr,
    kNegate, kNot
  };

  enum Function {
    kSin, kCos, kTan, kAsin, kAcos, kAtan, kAtan2, kSinh, kCosh, kTanh, kExp, kLog, kLog2, kLog10,
    kSqrt, kAbs, kSign, kFloor, kCeil, kRound, kFrac, kMin, kMax, kClamp, kPow, kMod, kLerp,
    kStep, kSmoothstep, kRand, kSaw, kTri, kSquare, kPulse
  };

  struct FunctionInfo {
    const char* name;
    Function function;
    int num_args;
  };

  const FunctionInfo kFunctions[] = {
    { "sin", kSin, 1 }, { "cos", kCos, 1 }, { "tan", kTan, 1 }, { "asin", kAsin, 1 },
    { "acos", kAcos, 1 }, { "atan", kAtan, 1 }, { "atan2", kAtan2, 2 }, { "sinh", kSinh, 1 },
    { "cosh", kCosh, 1 }, { "tanh", kTanh, 1 }, { "exp", kExp, 1 }, { "log", kLog, 1 },
    { "log2", kLog2, 1 }, { "log10", kLog10, 1 }, { "sqrt", kSqrt, 1 }, { "abs", kAbs, 1 },
    { "sign", kSign, 1 }, { "floor", kFloor, 1 }, { "ceil", kCeil, 1 }, { "round", kRound, 1 },
    { "frac", kFrac, 1 }, { "min", kMin, 2 }, { "max", kMax, 2 }, { "clamp", kClamp, 3 },
    { "pow", kPow, 2 }, { "mod", kMod, 2 }, { "lerp", kLerp, 3 }, { "step", kStep, 2 },
    { "smoothstep", kSmoothstep, 3 }, { "rand", kRand, 0 }, { "saw", kSaw, 1 }, { "tri", kTri, 1 },
    { "square", kSquare, 1 }, { "pulse", kPulse, 2 }
  };

  double frac(double value) {
    return value - std::floor(value);
  }

  // Floored modulo, so mod(-0.25, 1) is 0.75 like in most shader languages.
  double floorMod(double a, double b) {
    if (b == 0.0)
      return 0.0;
    return a - b * std::floor(a / b);
  }

  // Stateless noise from the sample and frame, so a formula renders the same every time.
  double hashNoise(unsigned int sample, unsigned int frame) {
    uint32_t h = sample * 0x9E3779B1u ^ (frame + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return (h / 4294967295.0) * 2.0 - 1.0;
  }
}

class FormulaParser {
  public:
    FormulaParser(const std::string& text, Formula& formula) :
        text_(text), formula_(formula), position_(0), depth_(0) { }

    bool parse(std::string& error) {
      skipSpace();
      if (atEnd())
        return fail("Enter a formula", error);

      int root = parseConditional();
      if (root >= 0) {
        skipSpace();
        if (!atEnd())
          error_ = "Unexpected '" + std::string(1, text_[position_]) + "'";
      }
      if (!error_.empty()) {
        error = error_ + " at position " + std::to_string(position_ + 1);
        return false;
      }
      formula_.root_ = root;
      return true;
    }

  private:
    bool fail(const std::string& message, std::string& error) {
      error = message;
      return false;
    }

    int setError(const std::string& message) {
      if (error_.empty())
        error_ = message;
      return -1;
    }

    bool atEnd() const { return position_ >= text_.size(); }

    void skipSpace() {
      while (!atEnd() && std::isspace((unsigned char)text_[position_]))
        ++position_;
    }

    bool match(const char* token) {
      skipSpace();
      size_t length = std::char_traits<char>::length(token);
      if (text_.compare(position_, length, token) == 0) {
        position_ += length;
        return true;
      }
      return false;
    }

    // Matches a one-character operator that isn't the start of a longer one (e.g. '<' but not "<=").
    bool matchSingle(char c, const char* not_followed_by = "") {
      skipSpace();
      if (atEnd() || text_[position_] != c)
        return false;
      if (position_ + 1 < text_.size()) {
        for (const char* f = not_followed_by; *f; ++f) {
          if (text_[position_ + 1] == *f)
            return false;
        }
      }
      ++position_;
      return true;
    }

    int add(Formula::Node node) {
      formula_.nodes_.push_back(node);
      return (int)formula_.nodes_.size() - 1;
    }

    int binary(int op, int left, int right) {
      if (left < 0 || right < 0)
        return -1;
      Formula::Node node;
      node.type = Formula::Node::kBinary;
      node.op = op;
      node.children[0] = left;
      node.children[1] = right;
      node.num_children = 2;
      return add(node);
    }

    int unary(int op, int child) {
      if (child < 0)
        return -1;
      Formula::Node node;
      node.type = Formula::Node::kUnary;
      node.op = op;
      node.children[0] = child;
      node.num_children = 1;
      return add(node);
    }

    struct DepthGuard {
      DepthGuard(int& depth) : depth_(depth) { ++depth_; }
      ~DepthGuard() { --depth_; }
      int& depth_;
    };

    int parseConditional() {
      DepthGuard guard(depth_);
      if (depth_ > kMaxDepth)
        return setError("Formula is nested too deeply");

      int condition = parseOr();
      if (condition < 0 || !match("?"))
        return condition;

      int when_true = parseConditional();
      if (when_true < 0)
        return -1;
      if (!match(":"))
        return setError("Expected ':' to finish '? :'");
      int when_false = parseConditional();
      if (when_false < 0)
        return -1;

      Formula::Node node;
      node.type = Formula::Node::kConditional;
      node.children[0] = condition;
      node.children[1] = when_true;
      node.children[2] = when_false;
      node.num_children = 3;
      return add(node);
    }

    int parseOr() {
      int left = parseAnd();
      while (left >= 0 && match("||"))
        left = binary(kOr, left, parseAnd());
      return left;
    }

    int parseAnd() {
      int left = parseComparison();
      while (left >= 0 && match("&&"))
        left = binary(kAnd, left, parseComparison());
      return left;
    }

    int parseComparison() {
      int left = parseAdditive();
      while (left >= 0) {
        if (match("<="))
          left = binary(kLessEqual, left, parseAdditive());
        else if (match(">="))
          left = binary(kGreaterEqual, left, parseAdditive());
        else if (match("=="))
          left = binary(kEqual, left, parseAdditive());
        else if (match("!="))
          left = binary(kNotEqual, left, parseAdditive());
        else if (matchSingle('<'))
          left = binary(kLess, left, parseAdditive());
        else if (matchSingle('>'))
          left = binary(kGreater, left, parseAdditive());
        else
          break;
      }
      return left;
    }

    int parseAdditive() {
      int left = parseMultiplicative();
      while (left >= 0) {
        if (matchSingle('+'))
          left = binary(kAdd, left, parseMultiplicative());
        else if (matchSingle('-'))
          left = binary(kSubtract, left, parseMultiplicative());
        else
          break;
      }
      return left;
    }

    int parseMultiplicative() {
      int left = parseUnary();
      while (left >= 0) {
        if (matchSingle('*'))
          left = binary(kMultiply, left, parseUnary());
        else if (matchSingle('/'))
          left = binary(kDivide, left, parseUnary());
        else if (matchSingle('%'))
          left = binary(kModulo, left, parseUnary());
        else
          break;
      }
      return left;
    }

    int parseUnary() {
      DepthGuard guard(depth_);
      if (depth_ > kMaxDepth)
        return setError("Formula is nested too deeply");

      if (matchSingle('-'))
        return unary(kNegate, parseUnary());
      if (matchSingle('+'))
        return parseUnary();
      if (matchSingle('!', "="))
        return unary(kNot, parseUnary());
      return parsePower();
    }

    // Right associative, and binds tighter than unary minus on its left: -x^2 is -(x^2).
    int parsePower() {
      int base = parsePrimary();
      if (base >= 0 && matchSingle('^'))
        return binary(kPower, base, parseUnary());
      return base;
    }

    int parseNumber() {
      const char* start = text_.c_str() + position_;
      char* end = nullptr;
      double value = std::strtod(start, &end);
      if (end == start)
        return setError("Expected a number");
      position_ += end - start;

      Formula::Node node;
      node.type = Formula::Node::kNumber;
      node.value = value;
      return add(node);
    }

    int parsePrimary() {
      skipSpace();
      if (atEnd())
        return setError("Unexpected end of formula");

      char c = text_[position_];
      if (std::isdigit((unsigned char)c) || c == '.')
        return parseNumber();

      if (c == '(') {
        ++position_;
        int inner = parseConditional();
        if (inner < 0)
          return -1;
        if (!match(")"))
          return setError("Expected ')'");
        return inner;
      }

      if (std::isalpha((unsigned char)c) || c == '_') {
        size_t start = position_;
        while (!atEnd() && (std::isalnum((unsigned char)text_[position_]) || text_[position_] == '_'))
          ++position_;
        std::string name = text_.substr(start, position_ - start);
        for (char& ch : name)
          ch = (char)std::tolower((unsigned char)ch);
        return parseIdentifier(name, start);
      }

      return setError("Unexpected '" + std::string(1, c) + "'");
    }

    int parseIdentifier(const std::string& name, size_t start) {
      struct VariableInfo { const char* name; Variable variable; };
      static const VariableInfo kVariables[] = {
        { "x", kX }, { "t", kT }, { "y", kY }, { "pi", kPiConstant }, { "tau", kTau }, { "e", kE }
      };

      skipSpace();
      bool is_call = !atEnd() && text_[position_] == '(';
      if (!is_call) {
        for (const VariableInfo& info : kVariables) {
          if (name == info.name) {
            if (info.variable == kY)
              formula_.uses_y_ = true;
            Formula::Node node;
            node.type = Formula::Node::kVariable;
            node.op = info.variable;
            return add(node);
          }
        }
      }

      for (const FunctionInfo& info : kFunctions) {
        if (name != info.name)
          continue;
        if (!is_call) {
          position_ = start;
          return setError("'" + name + "' needs ( )");
        }
        ++position_;

        Formula::Node node;
        node.type = Formula::Node::kFunction;
        node.op = info.function;
        int num_args = 0;
        if (!match(")")) {
          while (true) {
            if (num_args >= 3)
              return setError("Too many arguments to '" + name + "'");
            int arg = parseConditional();
            if (arg < 0)
              return -1;
            node.children[num_args++] = arg;
            if (match(")"))
              break;
            if (!match(","))
              return setError("Expected ',' or ')'");
          }
        }
        if (num_args != info.num_args) {
          return setError("'" + name + "' takes " + std::to_string(info.num_args) + " argument" +
                          (info.num_args == 1 ? "" : "s"));
        }
        node.num_children = num_args;
        return add(node);
      }

      position_ = start;
      return setError("Unknown name '" + name + "'");
    }

    const std::string& text_;
    Formula& formula_;
    size_t position_;
    int depth_;
    std::string error_;
};

std::unique_ptr<Formula> Formula::parse(const std::string& text, std::string& error) {
  std::unique_ptr<Formula> formula(new Formula());
  FormulaParser parser(text, *formula);
  if (!parser.parse(error))
    return nullptr;
  return formula;
}

double Formula::evaluate(const Variables& variables) const {
  if (root_ < 0)
    return 0.0;
  double value = evaluate(root_, variables);
  return std::isfinite(value) ? value : 0.0;
}

double Formula::evaluate(int index, const Variables& v) const {
  const Node& node = nodes_[index];
  switch (node.type) {
    case Node::kNumber:
      return node.value;
    case Node::kVariable:
      switch (node.op) {
        case kX: return v.x;
        case kT: return v.t;
        case kY: return v.y;
        case kPiConstant: return kPi;
        case kTau: return 2.0 * kPi;
        default: return std::exp(1.0);
      }
    case Node::kUnary: {
      double a = evaluate(node.children[0], v);
      return node.op == kNegate ? -a : (a == 0.0 ? 1.0 : 0.0);
    }
    case Node::kConditional:
      if (evaluate(node.children[0], v) != 0.0)
        return evaluate(node.children[1], v);
      return evaluate(node.children[2], v);
    case Node::kBinary: {
      double a = evaluate(node.children[0], v);
      // Short circuit, so a guarded branch is never evaluated.
      if (node.op == kAnd)
        return a != 0.0 && evaluate(node.children[1], v) != 0.0 ? 1.0 : 0.0;
      if (node.op == kOr)
        return a != 0.0 || evaluate(node.children[1], v) != 0.0 ? 1.0 : 0.0;

      double b = evaluate(node.children[1], v);
      switch (node.op) {
        case kAdd: return a + b;
        case kSubtract: return a - b;
        case kMultiply: return a * b;
        case kDivide: return b == 0.0 ? 0.0 : a / b;
        case kModulo: return floorMod(a, b);
        case kPower: return std::pow(a, b);
        case kLess: return a < b ? 1.0 : 0.0;
        case kLessEqual: return a <= b ? 1.0 : 0.0;
        case kGreater: return a > b ? 1.0 : 0.0;
        case kGreaterEqual: return a >= b ? 1.0 : 0.0;
        case kEqual: return a == b ? 1.0 : 0.0;
        default: return a != b ? 1.0 : 0.0;
      }
    }
    case Node::kFunction: {
      double a = node.num_children > 0 ? evaluate(node.children[0], v) : 0.0;
      double b = node.num_children > 1 ? evaluate(node.children[1], v) : 0.0;
      double c = node.num_children > 2 ? evaluate(node.children[2], v) : 0.0;
      switch (node.op) {
        case kSin: return std::sin(a);
        case kCos: return std::cos(a);
        case kTan: return std::tan(a);
        case kAsin: return std::asin(std::max(-1.0, std::min(1.0, a)));
        case kAcos: return std::acos(std::max(-1.0, std::min(1.0, a)));
        case kAtan: return std::atan(a);
        case kAtan2: return std::atan2(a, b);
        case kSinh: return std::sinh(a);
        case kCosh: return std::cosh(a);
        case kTanh: return std::tanh(a);
        case kExp: return std::exp(a);
        case kLog: return a > 0.0 ? std::log(a) : 0.0;
        case kLog2: return a > 0.0 ? std::log2(a) : 0.0;
        case kLog10: return a > 0.0 ? std::log10(a) : 0.0;
        case kSqrt: return a > 0.0 ? std::sqrt(a) : 0.0;
        case kAbs: return std::abs(a);
        case kSign: return (a > 0.0) - (a < 0.0);
        case kFloor: return std::floor(a);
        case kCeil: return std::ceil(a);
        case kRound: return std::round(a);
        case kFrac: return frac(a);
        case kMin: return std::min(a, b);
        case kMax: return std::max(a, b);
        case kClamp: return std::max(b, std::min(c, a));
        case kPow: return std::pow(a, b);
        case kMod: return floorMod(a, b);
        case kLerp: return a + (b - a) * c;
        case kStep: return b >= a ? 1.0 : 0.0;
        case kSmoothstep: {
          double range = b - a;
          double s = range == 0.0 ? (c >= b ? 1.0 : 0.0) : std::max(0.0, std::min(1.0, (c - a) / range));
          return s * s * (3.0 - 2.0 * s);
        }
        case kRand: return hashNoise(v.sample, v.frame);
        case kSaw: return 2.0 * frac(a) - 1.0;
        case kTri: return 1.0 - std::abs(4.0 * frac(a + 0.25) - 2.0);
        case kSquare: return frac(a) < 0.5 ? 1.0 : -1.0;
        default: return frac(a) < b ? 1.0 : -1.0;
      }
    }
  }
  return 0.0;
}
