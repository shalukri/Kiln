#pragma once
#include <string>
#include <vector>
#include "kiln/models/graph.hpp"

namespace kiln {

// File format, one target per line:
//
//     # comment
//     name cost_in_ms : dep1 dep2 dep3
//
// Dependencies may be defined further down in the file. The colon can be left out when
// there are no dependencies.
struct ParseResult {
    Graph graph;
    std::vector<std::string> errors;   // already formatted as "line 12: ...", sorted by line
    bool ok() const { return errors.empty(); }
};

ParseResult parseKilnText(const std::string& text);
ParseResult parseKilnFile(const std::string& path);

constexpr long long kMaxCost = 1000000000LL;

}  // namespace kiln
