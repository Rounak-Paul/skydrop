#pragma once

#include <string>
#include <vector>

struct Annotation {
    std::string trackPath;
    float       posSeconds = 0.0f;
    std::string label;
};

// Persistent per-track annotations.  Stored in ~/.skydrop/annotations.tsv.
// All methods are safe to call from the main (UI) thread only.
class Annotations {
public:
    static void Init();      // loads from disk
    static void Shutdown();  // saves to disk

    static void Add(const std::string& trackPath, float posSeconds, const std::string& label);
    static void Remove(const std::string& trackPath, int index); // index within the track's annotation list

    // Returns annotations for one track, sorted by position.
    static std::vector<Annotation> GetForTrack(const std::string& trackPath);

    static void Save();
    static void Load();

private:
    static std::vector<Annotation> _annotations;
};
