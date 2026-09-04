#include "pm/fee_model.hpp"

#include <cmath>
#include <iostream>

int main() {
  const double fee = pm::taker_fee_usd(40.0, 0.5, 0.07);
  if (std::abs(fee - 0.7) > 1e-4) {
    std::cerr << "expected fee 0.7, got " << fee << '\n';
    return 1;
  }

  const double total = pm::total_taker_fee_usd(10.0, 0.3, 0.7, 0.05);
  if (total <= 0.0) {
    std::cerr << "expected positive total fee\n";
    return 1;
  }

  std::cout << "fee tests passed\n";
  return 0;
}
