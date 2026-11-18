#include "../src/media/SessionSelection.h"
#include <array>
#include <iostream>
int main() {
    using media::SessionCandidate;
    int failures = 0;
    auto check = [&](auto const& candidates, std::size_t expected) {
        if (media::SelectSession(candidates) != expected) ++failures;
    };
    check(std::array<SessionCandidate, 0>{}, 0);
    check(std::array{SessionCandidate{false,true,false}, SessionCandidate{true,false,false}}, 1);
    check(std::array{SessionCandidate{true,false,true}, SessionCandidate{true,true,false}}, 1);
    check(std::array{SessionCandidate{false,false,false}, SessionCandidate{false,false,true}}, 1);
    check(std::array{SessionCandidate{false,false,true}, SessionCandidate{true,false,false}}, 1);
    check(std::array{SessionCandidate{false,false,false}}, 0);
    std::cout << "selection failures=" << failures << '\n';
    return failures ? 1 : 0;
}