#include "diagnostic.h"

#include <iostream>

int main() {
    return Mockingbird::run_diagnostic(
      std::cin, std::cout, std::cerr);
}
