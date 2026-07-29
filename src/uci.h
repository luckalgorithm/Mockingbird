#pragma once

#include <iosfwd>

namespace Mockingbird {

// Processes line-oriented UCI commands until quit or end of input. Search
// commands run on a worker thread so the command stream can deliver stop,
// ponderhit, isready, and quit while search is active.
int run_uci(
  std::istream& input,
  std::ostream& output,
  std::ostream& errors);

}  // namespace Mockingbird
