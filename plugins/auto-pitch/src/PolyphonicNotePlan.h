#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace sattari::poly
{
// Search the intersection of scale and user mask from the actual input pitch,
// not from an already-rounded target. An empty intersection means leave it alone.
inline bool nearestAllowedMidi (double midi, int allowedMask, int& target) noexcept
{
    if (! std::isfinite (midi) || allowedMask == 0) return false;
    const int centre = (int) std::floor (midi);
    double best = std::numeric_limits<double>::max();
    for (int candidate = centre - 12; candidate <= centre + 12; ++candidate)
    {
        const int pc = ((candidate % 12) + 12) % 12;
        const double distance = std::abs ((double) candidate - midi);
        if (((allowedMask >> pc) & 1) != 0 && distance < best)
        {
            best = distance;
            target = candidate;
        }
    }
    return best < std::numeric_limits<double>::max();
}

// Conservative, bounded peak identity and harmonic support. This is NOT source
// separation: unresolved partials are still ambiguous. Unlike a broad 3% comb,
// an isolated octave is not enough evidence to take ownership of another note.
// All storage is reserved during prepare(), never on the audio thread.
class PeakTracker
{
public:
    void prepare (int maxPeaks)
    {
        previous.reserve ((size_t) maxPeaks);
        current.reserve ((size_t) maxPeaks);
        support.reserve ((size_t) maxPeaks);
        reset();
    }

    void reset() { previous.clear(); current.clear(); support.clear(); }

    void update (const std::vector<int>& bins, const std::vector<float>& frequencies)
    {
        previous.swap (current);
        current.clear();
        for (int bin : bins)
        {
            const float hz = frequencies[(size_t) bin];
            int age = 1;
            // Adjacent-bin matching permits a moving partial without confusing
            // a newly started note with an old peak at a different frequency.
            const auto begin = std::lower_bound (previous.begin(), previous.end(), bin - 1,
                [] (const Track& t, int b) { return t.bin < b; });
            for (auto it = begin; it != previous.end() && it->bin <= bin + 1; ++it)
                if (hz > 0.0f && std::abs (hz - it->hz) < hz * 0.007f)
                    age = std::max (age, std::min (8, it->age + 1));
            current.push_back ({ bin, hz, age });
        }

        support.assign (current.size(), 0);
        for (size_t root = 0; root < current.size(); ++root)
            if (current[root].age >= 3)
                for (size_t partial = root + 1; partial < current.size(); ++partial)
                {
                    const int h = harmonic (partial, root);
                    if (h != 0) support[root] |= 1 << h;
                }
    }

    int owner (int peak, const std::vector<int>& owners) const noexcept
    {
        for (int root = peak - 1; root >= 0; --root)
        {
            if (owners[(size_t) root] != root) continue;
            const int evidence = support[(size_t) root];
            // At least two distinct upper partials, persistent over three
            // frames, must corroborate this fundamental's harmonic comb.
            if ((evidence & (evidence - 1)) != 0 && harmonic ((size_t) peak, (size_t) root) != 0)
                return root;
        }
        return peak;
    }

private:
    struct Track { int bin; float hz; int age; };
    std::vector<Track> previous, current;
    std::vector<int> support;

    int harmonic (size_t partial, size_t root) const noexcept
    {
        const auto& a = current[partial];
        const auto& b = current[root];
        if (a.age < 3 || b.age < 3 || b.hz <= 0.0f || a.hz <= 0.0f) return 0;
        const double ratio = (double) a.hz / b.hz;
        const int h = (int) std::lround (ratio);
        // Six cents rather than ~51 cents: independent +/-10 or +/-20 cent
        // octave notes must not inherit one another's correction. Support and
        // temporal identity above keep this from being only a threshold tweak.
        constexpr double sixCents = 0.003459737172132;
        return h >= 2 && h <= 12 && std::abs (ratio / h - 1.0) <= sixCents ? h : 0;
    }
};
} // namespace sattari::poly
