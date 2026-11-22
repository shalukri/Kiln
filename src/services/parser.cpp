#include "kiln/services/parser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace kiln {
namespace {

struct Problem {
    int line;
    std::string text;
};

bool validName(const std::string& s) {
    if (s.empty() || s.size() > 200) return false;
    for (unsigned char c : s) {
        bool ok = std::isalnum(c) || c == '_' || c == '.' || c == '-' || c == '/' || c == '+';
        if (!ok) return false;
    }
    return true;
}

std::vector<std::string> words(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> out;
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

bool parseCost(const std::string& s, long long& out) {
    if (s.empty() || s.size() > 10) return false;   // 10 digits is enough to be over the limit
    long long v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    if (v > kMaxCost) return false;
    out = v;
    return true;
}

}  // namespace

ParseResult parseKilnText(const std::string& text) {
    ParseResult res;
    std::vector<Problem> problems;
    auto problem = [&](int line, const std::string& msg) { problems.push_back({line, msg}); };

    struct PendingDep {
        int line;
        int task;
        std::string dep;
    };
    std::vector<PendingDep> pending;              // resolved after the whole file is read
    std::unordered_map<std::string, int> definedOn;
    std::unordered_set<std::string> broken;       // names on lines we could not use

    std::istringstream in(text);
    std::string raw;
    int lineNo = 0;
    while (std::getline(in, raw)) {
        lineNo++;
        if (lineNo == 1 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) raw.erase(0, 3);   // BOM from Notepad
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();                     // Windows line ends
        std::size_t hash = raw.find('#');
        if (hash != std::string::npos) raw.erase(hash);
        if (words(raw).empty()) continue;

        std::size_t colon = raw.find(':');
        std::string left = colon == std::string::npos ? raw : raw.substr(0, colon);
        std::string right = colon == std::string::npos ? "" : raw.substr(colon + 1);
        std::vector<std::string> head = words(left);
        if (!head.empty()) broken.insert(head[0]);   // removed again below if the line is fine

        if (right.find(':') != std::string::npos) {
            problem(lineNo, "more than one ':' on this line");
            continue;
        }
        if (head.size() != 2) {
            problem(lineNo, "expected '<name> <cost> : <dependencies>'");
            continue;
        }
        if (!validName(head[0])) {
            problem(lineNo, "'" + head[0] + "' is not a valid target name (letters, digits and _ . - / + only)");
            continue;
        }
        long long cost = 0;
        if (!parseCost(head[1], cost)) {
            problem(lineNo, "cost of '" + head[0] + "' must be a whole number from 0 to " + std::to_string(kMaxCost));
            continue;
        }
        auto prev = definedOn.find(head[0]);
        if (prev != definedOn.end()) {
            problem(lineNo, "'" + head[0] + "' is already defined on line " + std::to_string(prev->second));
            continue;
        }

        broken.erase(head[0]);
        int id = res.graph.addTask(head[0], cost);
        definedOn[head[0]] = lineNo;

        std::unordered_set<std::string> seenOnLine;   // "a: b b" should count b once
        for (const std::string& d : words(right)) {
            if (!validName(d)) {
                problem(lineNo, "'" + d + "' is not a valid target name");
                continue;
            }
            if (seenOnLine.insert(d).second) pending.push_back({lineNo, id, d});
        }
    }

    for (const PendingDep& p : pending) {
        int dep = res.graph.find(p.dep);
        if (dep >= 0) {
            res.graph.addEdge(p.task, dep);
        } else if (!broken.count(p.dep)) {
            problem(p.line, "'" + res.graph.name(p.task) + "' depends on '" + p.dep + "', which is never defined");
        }
    }

    std::stable_sort(problems.begin(), problems.end(),
                     [](const Problem& a, const Problem& b) { return a.line < b.line; });
    for (const Problem& p : problems) res.errors.push_back("line " + std::to_string(p.line) + ": " + p.text);
    return res;
}

ParseResult parseKilnFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        ParseResult res;
        res.errors.push_back("cannot open '" + path + "'");
        return res;
    }
    std::stringstream buf;
    buf << f.rdbuf();
    return parseKilnText(buf.str());
}

}  // namespace kiln
