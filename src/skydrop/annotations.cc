#include "annotations.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

std::vector<Annotation> Annotations::_annotations;

// ---- Storage path ---------------------------------------------------------

static std::string GetStoragePath() {
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0') home = ".";
    return std::string(home) + "/.skydrop/annotations.tsv";
}

// ---- Escape / unescape field (tab-separated values) ----------------------

static std::string EscapeField(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if      (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else                out += c;
    }
    return out;
}

static std::string UnescapeField(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            switch (s[i]) {
                case '\\': out += '\\'; break;
                case 't':  out += '\t'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                default:   out += '\\'; out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

// ---- Public API -----------------------------------------------------------

void Annotations::Load() {
    _annotations.clear();
    std::ifstream f(GetStoragePath());
    if (!f.is_open()) return;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;

        const size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        const size_t t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;

        Annotation a;
        a.trackPath  = UnescapeField(line.substr(0, t1));
        try { a.posSeconds = std::stof(line.substr(t1 + 1, t2 - t1 - 1)); }
        catch (...) { continue; }
        a.label = UnescapeField(line.substr(t2 + 1));
        _annotations.push_back(std::move(a));
    }
}

void Annotations::Save() {
    const std::string path = GetStoragePath();
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), ec);

    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) return;

    f << "# Skydrop annotations v1 — path\\tpos_seconds\\tlabel\n";
    for (const auto& a : _annotations) {
        f << EscapeField(a.trackPath) << '\t'
          << a.posSeconds             << '\t'
          << EscapeField(a.label)     << '\n';
    }
}

void Annotations::Init()     { Load(); }
void Annotations::Shutdown() { Save(); }

void Annotations::Add(const std::string& trackPath, float posSeconds,
                      const std::string& label) {
    Annotation a;
    a.trackPath  = trackPath;
    a.posSeconds = posSeconds;
    a.label      = label;
    _annotations.push_back(std::move(a));
}

void Annotations::Remove(const std::string& trackPath, int index) {
    int count = 0;
    for (auto it = _annotations.begin(); it != _annotations.end(); ++it) {
        if (it->trackPath == trackPath) {
            if (count == index) { _annotations.erase(it); return; }
            ++count;
        }
    }
}

std::vector<Annotation> Annotations::GetForTrack(const std::string& trackPath) {
    std::vector<Annotation> result;
    for (const auto& a : _annotations)
        if (a.trackPath == trackPath) result.push_back(a);
    std::sort(result.begin(), result.end(),
        [](const Annotation& a, const Annotation& b) {
            return a.posSeconds < b.posSeconds;
        });
    return result;
}
