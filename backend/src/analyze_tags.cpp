// Eigenständiges Analyse-Tool: zählt die Tag-Verteilung aller Knoten

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
#include <utility>
#include <vector>

namespace {

// Keys mit Freitext / hoher Kardinalität (name, addr:*, ref ...) werden nur
// auf Key-Ebene gezaehlt, NICHT als key=value -- sonst explodiert der Speicher
// durch Millionen einzigartiger Werte (Namen, Hausnummern, URLs).
bool is_freetext_key(std::string_view key) {
    static const std::array<std::string_view, 26> prefixes = {
        "name", "addr:", "ref", "source", "note", "fixme", "description",
        "website", "url", "contact:", "phone", "email", "operator",
        "wikipedia", "wikidata", "image", "opening_hours", "comment",
        "old_name", "alt_name", "loc_name", "official_name", "short_name",
        "height", "ele", "capacity"
    };
    for (const auto& p : prefixes)
        if (key.size() >= p.size() && key.compare(0, p.size(), p) == 0)
            return true;
    return false;
}

struct TagStats : public osmium::handler::Handler {
    std::unordered_map<std::string, uint64_t> kv_counts;
    std::unordered_map<std::string, uint64_t> key_counts;
    uint64_t total_nodes  = 0;
    uint64_t tagged_nodes = 0;

    void node(const osmium::Node& node) {
        ++total_nodes;
        const auto& tags = node.tags();
        if (tags.empty()) return;
        ++tagged_nodes;
        for (const auto& tag : tags) {
            std::string_view key = tag.key();
            key_counts[std::string(key)]++;
            std::string_view value = tag.value();
            if (is_freetext_key(key) || value.size() > 40) continue;
            std::string kv;
            kv.reserve(key.size() + value.size() + 1);
            kv.append(key).append("=").append(value);
            kv_counts[std::move(kv)]++;
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

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: analyze_tags <file.osm.pbf> [out.html]\n";
        return 1;
    }
    const std::string infile  = argv[1];
    const std::string outfile = (argc >= 3) ? argv[2] : "tag_histogram.html";

    std::cout << "Analyzing node tags in: " << infile << "\n";
    const auto t0 = std::chrono::high_resolution_clock::now();

    TagStats stats;
    osmium::io::File input{infile};
    osmium::io::Reader reader{input, osmium::osm_entity_bits::node};
    osmium::apply(reader, stats);
    reader.close();

    const double secs = std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - t0).count();

    const auto kv   = sorted_desc(stats.kv_counts);
    const auto keys = sorted_desc(stats.key_counts);

    uint64_t total_kv_occ = 0;
    for (const auto& [k, c] : kv) total_kv_occ += c;

    std::string ds = infile;
    for (char& c : ds) if (c == '\\') c = '/';

    std::time_t now = std::time(nullptr);
    char datebuf[20];
    std::strftime(datebuf, sizeof(datebuf), "%Y-%m-%d", std::localtime(&now));

    std::ofstream out(outfile);
    out << R"(<!DOCTYPE html><html lang="de"><head><meta charset="UTF-8">
<title>Tag-Histogramm</title>
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
 .cum50{color:#b45309}
 .cum90{color:#dc2626}
 .note{font-size:16px;color:#444;margin-top:30px}
 code{font-family:Consolas,monospace;font-size:16px}
</style></head><body>
)";

    out << "<h1>Tag-Histogramm</h1>\n"
        << "<p class=\"meta\">" << hesc(ds)
        << " &nbsp;&middot;&nbsp; Scan: " << fmt_f(secs, 1) << " s"
        << " &nbsp;&middot;&nbsp; Erzeugt " << datebuf << "</p>\n"
        << "<p>Knoten gesamt: <strong>" << fmt_int(stats.total_nodes) << "</strong>"
        << " &nbsp;&middot;&nbsp; mit Tags: <strong>" << fmt_int(stats.tagged_nodes) << "</strong>"
        << " &nbsp;&middot;&nbsp; verschiedene Keys: <strong>" << fmt_int(keys.size()) << "</strong>"
        << " &nbsp;&middot;&nbsp; verschiedene key=value: <strong>" << fmt_int(kv.size()) << "</strong>"
        << " (Freitext ausgeklammert)</p>\n";

    out << "<h2>Keys nach Häufigkeit (Top 60)</h2>\n"
        << "<table><thead><tr>"
        << "<th class=\"num\" style=\"width:5em\">Rang</th>"
        << "<th>Key</th>"
        << "<th class=\"num\">Anzahl</th>"
        << "</tr></thead><tbody>\n";
    for (size_t i = 0; i < std::min<size_t>(60, keys.size()); ++i) {
        out << "<tr>"
            << "<td class=\"num\">" << (i + 1) << "</td>"
            << "<td><code>" << hesc(keys[i].first) << "</code></td>"
            << "<td class=\"num\">" << fmt_int(keys[i].second) << "</td>"
            << "</tr>\n";
    }
    out << "</tbody></table>\n";

    out << "<h2>key=value nach Häufigkeit (Top 200)</h2>\n"
        << "<p class=\"meta\">Spalte <em>kum%</em> = Anteil der bis hier aufsummierten Vorkommen "
        << "an allen " << fmt_int(total_kv_occ) << " enumerierten Tag-Vorkommen "
        << "(Freitext-Keys ausgeklammert). "
        << "<span class=\"cum50\">Orange ab 50&nbsp;%</span>, "
        << "<span class=\"cum90\">Rot ab 90&nbsp;%</span>.</p>\n"
        << "<table><thead><tr>"
        << "<th class=\"num\" style=\"width:5em\">Rang</th>"
        << "<th>key=value</th>"
        << "<th class=\"num\">Anzahl</th>"
        << "<th class=\"num\">kum%</th>"
        << "</tr></thead><tbody>\n";

    uint64_t cum = 0;
    for (size_t i = 0; i < std::min<size_t>(200, kv.size()); ++i) {
        cum += kv[i].second;
        double cumpct = total_kv_occ ? 100.0 * cum / total_kv_occ : 0.0;
        const char* cls = cumpct >= 90.0 ? "num cum90" : (cumpct >= 50.0 ? "num cum50" : "num");
        out << "<tr>"
            << "<td class=\"num\">" << (i + 1) << "</td>"
            << "<td><code>" << hesc(kv[i].first) << "</code></td>"
            << "<td class=\"num\">" << fmt_int(kv[i].second) << "</td>"
            << "<td class=\"" << cls << "\">" << fmt_f(cumpct, 2) << "&nbsp;%</td>"
            << "</tr>\n";
    }
    out << "</tbody></table>\n"
        << "<p class=\"note\">Freitext-Keys (<code>name</code>, <code>addr:*</code>, "
        << "<code>ref</code>, &hellip;) werden nur auf Key-Ebene gezählt, nicht als key=value &mdash; "
        << "ihre Werte sind zu individuell (Millionen einzigartiger Namen, Adressen, URLs).</p>\n"
        << "</body></html>\n";

    std::cout << "Done in " << std::fixed << std::setprecision(1) << secs
              << " s. HTML -> " << outfile << "\n"
              << "  Knoten gesamt: " << stats.total_nodes
              << ", mit Tags: " << stats.tagged_nodes << "\n";
    return 0;
}
