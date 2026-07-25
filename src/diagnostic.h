#pragma once

#include <iosfwd>

namespace Mockingbird {

// Processes diagnostic commands until quit or end of input.
int run_diagnostic(
  std::istream& input,
  std::ostream& output,
  std::ostream& errors);

}  // namespace Mockingbird
