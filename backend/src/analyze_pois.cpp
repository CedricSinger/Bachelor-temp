// Analyse-Tool: POI-Verteilung nach bekannten Keys vs. Rest.
//
// Gibt bekannte POI-Keys (amenity, shop, ...) und alle anderen key=value
// Kombinationen als HTML-Tabelle aus.
//
// Aufruf: analyze_pois <file.osm.pbf> [out.html]

#include <osmium/io/pbf_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr uint64_t OTHER_THRESHOLD = 50'000;

const std::unordered_set<std::string> KNOWN_KEYS = {
    "amenity", "shop", "tourism", "leisure",
    "healthcare", "historic", "office", "craft"
};

bool is_freetext_key(std::string_view k) {
    static const std::array<std::string_view, 10> prefixes = {
        "name", "addr:", "ref", "source", "note",
        "website", "url", "contact:", "phone", "email"
    };
    for (const auto& p : prefixes)
        if (k.size() >= p.size() && k.compare(0, p.size(), p) == 0) return true;
    return false;
}

struct POIStats : public osmium::handler::Handler {
    std::unordered_map<std::string, uint64_t> known_counts;
    std::unordered_map<std::string, uint64_t> unknown_counts;
    uint64_t total_nodes = 0;

    void node(const osmium::Node& node) {
        ++total_nodes;
        for (const auto& key : KNOWN_KEYS) {
            if (node.tags()[key.c_str()]) {
                ++known_counts[key];
                return;
            }
        }
        for (const auto& tag : node.tags()) {
            std::string_view k = tag.key();
            std::string_view v = tag.value();
            if (is_freetext_key(k) || v.size() > 40) continue;
            std::string kv;
            kv.reserve(k.size() + v.size() + 1);
            kv.append(k).append("=").append(v);
            ++unknown_counts[std::move(kv)];
        }
    }
};

template <typename Map>
std::vector<std::pair<std::string, uint64_t>> sorted_desc(const Map& m) {
    std::vector<std::pair<std::string, uint64_t>> v(m.begin(), m.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    return v;
}

std::string fmt_int(uint64_t n) {
    std::string s = std::to_string(n);
    int pos = static_cast<int>(s.size()) - 3;
    while (pos > 0) { s.insert(static_cast<size_t>(pos), "."); pos -= 3; }
    return s;
}

std::string fmt_f(double v, int prec) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(prec) << v;
    std::string s = ss.str();
    for (char& c : s) if (c == '.') c = ',';
    return s;
}

std::string hesc(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;";  break;
            case '>': o += "&gt;";  break;
            default:  o += c;
        }
    }
    return o;
}

void write_html(
    const std::string& infile,
    double secs,
    const std::vector<std::pair<std::string, uint64_t>>& known,
    const std::vector<std::pair<std::string, uint64_t>>& unk_top,
    uint64_t other_types,
    uint64_t other_entries,
    uint64_t total_nodes,
    const std::string& outpath)
{
    uint64_t total_known = 0;
    for (const auto& [k, v] : known) total_known += v;
    uint64_t total_unk = 0;
    for (const auto& [k, v] : unk_top) total_unk += v;

    std::string ds = infile;
    for (char& c : ds) if (c == '\\') c = '/';

    std::time_t now = std::time(nullptr);
    char datebuf[20];
    std::strftime(datebuf, sizeof(datebuf), "%Y-%m-%d", std::localtime(&now));

    std::ofstream f(outpath);
    f << R"(<!DOCTYPE html><html lang="de"><head><meta charset="UTF-8">
<title>POI-Histogramm</title>
<style>
 body{font-family:Georgia,'Times New Roman',serif;max-width:920px;margin:48px auto;padding:0 24px;color:#1a1a1a;line-height:1.5}
 h1{font-size:30px;font-weight:600;margin:0 0 6px}
 h2{font-size:23px;font-weight:600;margin:34px 0 4px}
 p{font-size:19px}
 .meta{font-size:16px;color:#555;margin:0 0 12px}
 table{width:100%;border-collapse:collapse;margin:8px 0}
 th,td{padding:11px 14px;font-size:18px;border-bottom:1px solid #ccc;text-align:left}
 th{border-bottom:2px solid #333}
 td.num,th.num{text-align:right;font-variant-numeric:tabular-nums}
 .note{font-size:16px;color:#444;margin-top:20px;padding:12px 16px;border-left:3px solid #ccc}
 code{font-family:Consolas,monospace;font-size:16px}
</style></head><body>
)";

    f << "<h1>POI-Histogramm</h1>\n"
      << "<p class=\"meta\">" << hesc(ds)
      << " &nbsp;&middot;&nbsp; Scan: " << fmt_f(secs, 1) << " s"
      << " &nbsp;&middot;&nbsp; Erzeugt " << datebuf << "</p>\n"
      << "<p>Knoten gesamt: <strong>" << fmt_int(total_nodes) << "</strong>"
      << " &nbsp;&middot;&nbsp; mit bekanntem POI-Key: <strong>" << fmt_int(total_known) << "</strong>"
      << " &nbsp;&middot;&nbsp; sonstige getaggte Knoten: <strong>"
      << fmt_int(total_unk + other_entries) << "</strong></p>\n";

    // Bekannte POI-Keys
    f << "<h2>Bekannte POI-Keys</h2>\n"
      << "<p class=\"meta\"><code>amenity</code>, <code>shop</code>, <code>tourism</code>, "
      << "<code>leisure</code>, <code>healthcare</code>, <code>historic</code>, "
      << "<code>office</code>, <code>craft</code></p>\n"
      << "<table><thead><tr>"
      << "<th>Key</th><th class=\"num\">Anzahl</th>"
      << "</tr></thead><tbody>\n";
    for (const auto& [k, v] : known) {
        f << "<tr>"
          << "<td><code>" << hesc(k) << "</code></td>"
          << "<td class=\"num\">" << fmt_int(v) << "</td>"
          << "</tr>\n";
    }
    f << "</tbody></table>\n";

    // Häufigste sonstige key=value
    f << "<h2>Häufigste sonstige key=value (Top " << unk_top.size() << ")</h2>\n"
      << "<table><thead><tr>"
      << "<th class=\"num\" style=\"width:5em\">Rang</th>"
      << "<th>key=value</th>"
      << "<th class=\"num\">Anzahl</th>"
      << "</tr></thead><tbody>\n";
    for (size_t i = 0; i < unk_top.size(); ++i) {
        f << "<tr>"
          << "<td class=\"num\">" << (i + 1) << "</td>"
          << "<td><code>" << hesc(unk_top[i].first) << "</code></td>"
          << "<td class=\"num\">" << fmt_int(unk_top[i].second) << "</td>"
          << "</tr>\n";
    }
    f << "</tbody></table>\n"
      << "<p class=\"note\"><strong>Other-Block</strong> (je &lt;&nbsp;" << fmt_int(OTHER_THRESHOLD)
      << " Einträge): <strong>" << fmt_int(other_types) << " verschiedene Types</strong>"
      << " mit zusammen <strong>" << fmt_int(other_entries) << " Einträgen</strong>"
      << " &mdash; zu fragmentiert für sinnvolles Filtern.</p>\n"
      << "</body></html>\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: analyze_pois <file.osm.pbf> [out.html]\n";
        return 1;
    }
    const std::string infile  = argv[1];
    const std::string outfile = (argc >= 3) ? argv[2] : "poi_histogram.html";

    std::cout << "Analyzing POI tags in: " << infile << "\n";
    const auto t0 = std::chrono::high_resolution_clock::now();

    POIStats stats;
    osmium::io::File input{infile};
    osmium::io::Reader reader{input, osmium::osm_entity_bits::node};
    osmium::apply(reader, stats);
    reader.close();

    const double secs = std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - t0).count();

    const auto known   = sorted_desc(stats.known_counts);
    const auto unknown = sorted_desc(stats.unknown_counts);

    auto split = std::partition_point(unknown.begin(), unknown.end(),
        [](const auto& p) { return p.second >= OTHER_THRESHOLD; });

    const std::vector<std::pair<std::string, uint64_t>> unk_top(unknown.begin(), split);
    uint64_t other_types   = static_cast<uint64_t>(std::distance(split, unknown.end()));
    uint64_t other_entries = 0;
    for (auto it = split; it != unknown.end(); ++it) other_entries += it->second;

    std::cout << "\n=== Bekannte POI-Keys ===\n";
    for (const auto& [k, v] : known)
        std::cout << std::setw(16) << k << ": " << v << "\n";
    std::cout << "\n=== Unbekannte Types (Top " << unk_top.size() << ") ===\n";
    for (const auto& [k, v] : unk_top)
        std::cout << std::setw(44) << k << ": " << v << "\n";
    std::cout << "\n=== Other (< " << OTHER_THRESHOLD << ") ===\n"
              << "  Types:   " << other_types   << "\n"
              << "  Entries: " << other_entries << "\n";

    write_html(infile, secs, known, unk_top, other_types, other_entries,
               stats.total_nodes, outfile);
    std::cout << "\nDone in " << std::fixed << std::setprecision(1) << secs
              << " s. HTML -> " << outfile << "\n";
    return 0;
}
