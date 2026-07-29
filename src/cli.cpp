#include "uci.h"

#include <iostream>

int main() {
    return Mockingbird::run_uci(
      std::cin, std::cout, std::cerr);
}
