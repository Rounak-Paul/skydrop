#include "session.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal hand-rolled JSON writer/reader — no external deps.
// Format is a single JSON object with well-known keys.  We only need to
// serialise strings (with basic escape), ints, floats and bool/arrays.
// ---------------------------------------------------------------------------

static std::string GetStorageDir() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.skydrop";
}

std::string Session::GetPath() {
    return GetStorageDir() + "/session.json";
}

// ---- Helpers ---------------------------------------------------------------

static std::string EscapeJsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    out += '"';
    return out;
}

// Very small JSON string value reader — finds key in flat JSON object.
// Returns empty string if not found.
static std::string ReadJsonString(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const size_t k = json.find(needle);
    if (k == std::string::npos) return {};
    size_t colon = json.find(':', k + needle.size());
    if (colon == std::string::npos) return {};
    size_t q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return {};
    size_t q2 = q1 + 1;
    std::string val;
    while (q2 < json.size() && json[q2] != '"') {
        if (json[q2] == '\\' && q2 + 1 < json.size()) {
            ++q2;
            switch (json[q2]) {
                case '"':  val += '"';  break;
                case '\\': val += '\\'; break;
                case 'n':  val += '\n'; break;
                case 'r':  val += '\r'; break;
                case 't':  val += '\t'; break;
                default:   val += json[q2]; break;
            }
        } else {
            val += json[q2];
        }
        ++q2;
    }
    return val;
}

static float ReadJsonFloat(const std::string& json, const std::string& key, float def = 0.0f) {
    const std::string needle = "\"" + key + "\"";
    const size_t k = json.find(needle);
    if (k == std::string::npos) return def;
    size_t colon = json.find(':', k + needle.size());
    if (colon == std::string::npos) return def;
    try { return std::stof(json.substr(colon + 1)); } catch (...) { return def; }
}

static int ReadJsonInt(const std::string& json, const std::string& key, int def = 0) {
    const std::string needle = "\"" + key + "\"";
    const size_t k = json.find(needle);
    if (k == std::string::npos) return def;
    size_t colon = json.find(':', k + needle.size());
    if (colon == std::string::npos) return def;
    try { return std::stoi(json.substr(colon + 1)); } catch (...) { return def; }
}

static bool ReadJsonBool(const std::string& json, const std::string& key, bool def = false) {
    const std::string needle = "\"" + key + "\"";
    const size_t k = json.find(needle);
    if (k == std::string::npos) return def;
    size_t colon = json.find(':', k + needle.size());
    if (colon == std::string::npos) return def;
    size_t v = colon + 1;
    while (v < json.size() && json[v] == ' ') ++v;
    return (v < json.size() && json[v] == 't');
}

// Read a JSON string array ["a","b","c"] under key into out.
static std::vector<std::string> ReadJsonStringArray(const std::string& json,
                                                     const std::string& key) {
    std::vector<std::string> result;
    const std::string needle = "\"" + key + "\"";
    const size_t k = json.find(needle);
    if (k == std::string::npos) return result;
    size_t colon = json.find(':', k + needle.size());
    if (colon == std::string::npos) return result;
    size_t arr = json.find('[', colon + 1);
    if (arr == std::string::npos) return result;
    size_t pos = arr + 1;
    while (pos < json.size()) {
        size_t q1 = json.find('"', pos);
        size_t close = json.find(']', pos);
        if (close != std::string::npos && (q1 == std::string::npos || close < q1)) break;
        if (q1 == std::string::npos) break;
        size_t q2 = q1 + 1;
        std::string val;
        while (q2 < json.size() && json[q2] != '"') {
            if (json[q2] == '\\' && q2 + 1 < json.size()) {
                ++q2;
                switch (json[q2]) {
                    case '"':  val += '"';  break;
                    case '\\': val += '\\'; break;
                    case 'n':  val += '\n'; break;
                    case 'r':  val += '\r'; break;
                    case 't':  val += '\t'; break;
                    default:   val += json[q2]; break;
                }
            } else {
                val += json[q2];
            }
            ++q2;
        }
        result.push_back(std::move(val));
        pos = q2 + 1;
    }
    return result;
}

// ---- Public API -----------------------------------------------------------

void Session::Save(const SessionState& s) {
    std::error_code ec;
    std::filesystem::create_directories(GetStorageDir(), ec);

    std::ofstream f(GetPath(), std::ios::trunc);
    if (!f.is_open()) return;

    f << "{\n";
    f << "  \"currentIndex\": " << s.currentIndex << ",\n";
    f << "  \"posSeconds\": "   << s.posSeconds    << ",\n";
    f << "  \"volume\": "       << s.volume        << ",\n";
    f << "  \"shuffle\": "      << (s.shuffle ? "true" : "false") << ",\n";
    f << "  \"repeatMode\": "   << s.repeatMode    << ",\n";
    f << "  \"queue\": [";
    for (size_t i = 0; i < s.queuePaths.size(); ++i) {
        if (i > 0) f << ", ";
        f << EscapeJsonString(s.queuePaths[i]);
    }
    f << "]\n}\n";
}

bool Session::Load(SessionState& out) {
    std::ifstream f(GetPath());
    if (!f.is_open()) return false;

    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string json = ss.str();

    out.queuePaths  = ReadJsonStringArray(json, "queue");
    out.currentIndex= ReadJsonInt  (json, "currentIndex", -1);
    out.posSeconds  = ReadJsonFloat(json, "posSeconds",  0.0f);
    out.volume      = ReadJsonFloat(json, "volume",      1.0f);
    out.shuffle     = ReadJsonBool (json, "shuffle",     false);
    out.repeatMode  = ReadJsonInt  (json, "repeatMode",  0);

    return !out.queuePaths.empty();
}
