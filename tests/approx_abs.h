#pragma once
#include <cmath>
#include <ostream>

struct approx_abs {
  double value, margin;
  approx_abs(double v, double m) : value(v), margin(m) {}
  friend bool operator==(double lhs, const approx_abs& rhs) {
    return std::fabs(lhs - rhs.value) <= rhs.margin;
  }
  friend bool operator==(const approx_abs& lhs, double rhs) { return rhs == lhs; }
  friend bool operator!=(double lhs, const approx_abs& rhs) { return !(lhs == rhs); }
  friend bool operator!=(const approx_abs& lhs, double rhs) { return !(lhs == rhs); }
  friend std::ostream& operator<<(std::ostream& os, const approx_abs& a) {
    return os << a.value << " +/- " << a.margin;
  }
};
