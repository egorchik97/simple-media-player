#pragma once
#include <cstddef>
#include <span>

namespace media {
struct SessionCandidate {
    bool playing;
    bool current;
    bool selected;
};
// Prefer playing sessions; use Windows' recommendation, then sticky selection
// to resolve ties. Empty enumeration explicitly means no session.
inline std::size_t SelectSession(std::span<const SessionCandidate> sessions) {
    std::size_t best = sessions.size();
    int bestScore = -1;
    for (std::size_t i = 0; i < sessions.size(); ++i) {
        const auto& s = sessions[i];
        const int score = (s.playing ? 100 : 0) + (s.current ? 10 : 0) + (s.selected ? 1 : 0);
        if (score > bestScore) { best = i; bestScore = score; }
    }
    return best;
}
}