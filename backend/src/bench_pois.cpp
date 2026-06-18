// Mess-Tool: speichert ALLE getaggten OSM-Knoten als POIs
// und misst Abfrage-Zeiten (Grid- vs. R-Tree-Index) bei 10 km und 50 km Radius
// für festgelegte Zonen unterschiedlicher POI-Dichte (Standard: Essen (dicht) und Würzburg (dünn))
// Schreibt Ergebnisse als JSON und HTML

#include "poi.hpp"
#include "spatial_index.hpp"

#include <osmium/io/pbf_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

using namespace routenplaner;

namespace {

    struct Collector : public osmium::handler::Handler {
        std::vector<POI>& pois;
        TagDict& dict;
        Collector(std::vector<POI>& p, TagDict& d) : pois(p), dict(d) {}
        void node(const osmium::Node& n) {
            if (!n.location().valid()) return;
            const auto& tags = n.tags();
            if (tags.empty()) return;
            POI p;
            p.id     = static_cast<uint64_t>(n.id());
            p.coords = {n.location().lat(), n.location().lon()};
            p.tags.reserve(tags.size());
            for (const auto& t : tags)
                p.tags.emplace_back(dict.intern(t.key()), dict.intern(t.value()));
            pois.push_back(std::move(p));
        }
    };

    double peak_ram_mb() {
    #ifdef _WIN32
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
            return pmc.PeakWorkingSetSize / (1024.0 * 1024.0);
        return -1.0;
    #else
        struct rusage ru;
        if (getrusage(RUSAGE_SELF, &ru) == 0) {
    #if defined(__APPLE__)
            return ru.ru_maxrss / (1024.0 * 1024.0);
    #else
            return ru.ru_maxrss / 1024.0;
    #endif
        }
        return -1.0;
    #endif
    }

    struct QStat { double avg_ms = 0, min_ms = 1e30, max_ms = 0, avg_found = 0; };

    template <typename Index>
    QStat run_queries(const Index& idx, double clat, double clng, double radius_m, int n,
                      const TagFilter& filter) {
        constexpr double DEG = 1.0 / 111000.0;
        double d_lat = radius_m * DEG;
        double d_lng = radius_m * DEG / std::cos(clat * M_PI / 180.0);
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> jlat(-0.3 * d_lat, 0.3 * d_lat);
        std::uniform_real_distribution<double> jlng(-0.3 * d_lng, 0.3 * d_lng);
        QStat s; double total = 0, found = 0;
        using Clock = std::chrono::high_resolution_clock;
        for (int i = 0; i < n; ++i) {
            double la = clat + jlat(rng), lo = clng + jlng(rng);
            auto t0 = Clock::now();
            auto res = idx.query_bbox(la - d_lat, lo - d_lng, la + d_lat, lo + d_lng, filter);
            double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            total += ms; s.min_ms = std::min(s.min_ms, ms); s.max_ms = std::max(s.max_ms, ms);
            found += res.size();
        }
        s.avg_ms = total / n; s.avg_found = found / n;
        return s;
    }

    struct Zone { std::string name; double lat, lng; };

    struct CaseResult {
        std::string name;
        double radius_m;
        std::string cat, type;
        QStat grid, rtree;
    };

    struct ZoneResult {
        Zone zone;
        std::vector<CaseResult> cases;
    };

    // ── number/HTML helpers ───────────────────────────────────────────────────

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

    // ── JSON output ───────────────────────────────────────────────────────────

    void write_qstat_json(std::ofstream& f, const char* key, const QStat& s, bool comma) {
        f << "          \"" << key << "\": {"
          << "\"avg_ms\":" << s.avg_ms << ",\"min_ms\":" << s.min_ms
          << ",\"max_ms\":" << s.max_ms << ",\"found\":" << s.avg_found << "}"
          << (comma ? "," : "") << "\n";
    }

    void write_json(const std::string& path, const std::string& infile,
                    size_t n_pois, double peak_mb, double load_s,
                    double grid_build, double rt_build, int grid_cells, int n_queries,
                    const std::vector<ZoneResult>& zones) {
        std::ofstream f(path);
        f.setf(std::ios::fixed); f.precision(4);
        std::string ds = infile;
        for (char& c : ds) if (c == '\\') c = '/';

        f << "{\n  \"dataset\": \"" << ds << "\",\n"
          << "  \"pois\": " << n_pois << ",\n"
          << "  \"peak_ram_mb\": " << peak_mb << ",\n"
          << "  \"bytes_per_poi\": " << (peak_mb * 1024.0 * 1024.0 / n_pois) << ",\n"
          << "  \"load_s\": " << load_s << ",\n"
          << "  \"grid_build_s\": " << grid_build << ",\n"
          << "  \"rtree_build_s\": " << rt_build << ",\n"
          << "  \"grid_cells\": " << grid_cells << ",\n"
          << "  \"n_queries\": " << n_queries << ",\n"
          << "  \"zones\": [\n";

        for (size_t zi = 0; zi < zones.size(); ++zi) {
            const auto& zr = zones[zi];
            f << "    {\n      \"name\": \"" << zr.zone.name
              << "\", \"lat\": " << zr.zone.lat << ", \"lng\": " << zr.zone.lng
              << ",\n      \"cases\": [\n";
            for (size_t ci = 0; ci < zr.cases.size(); ++ci) {
                const auto& c = zr.cases[ci];
                f << "        {\n          \"name\": \"" << c.name << "\",\n"
                  << "          \"radius_m\": " << c.radius_m << ",\n"
                  << "          \"category\": \"" << c.cat << "\", \"type\": \"" << c.type << "\",\n"
                  << "          \"results\": {\n";
                write_qstat_json(f, "grid",  c.grid,  true);
                write_qstat_json(f, "rtree", c.rtree, false);
                f << "          }\n        }" << (ci + 1 < zr.cases.size() ? "," : "") << "\n";
            }
            f << "      ]\n    }" << (zi + 1 < zones.size() ? "," : "") << "\n";
        }
        f << "  ]\n}\n";
    }

    // ── HTML output ───────────────────────────────────────────────────────────

    void write_html(const std::string& path, const std::string& infile,
                    size_t n_pois, double peak_mb, double load_s,
                    double grid_build, double rt_build, int grid_cells, int n_queries,
                    const std::vector<ZoneResult>& zones) {
        std::string ds = infile;
        for (char& c : ds) if (c == '\\') c = '/';

        std::time_t now = std::time(nullptr);
        char datebuf[20];
        std::strftime(datebuf, sizeof(datebuf), "%Y-%m-%d", std::localtime(&now));

        std::ofstream f(path);
        f << R"(<!DOCTYPE html><html lang="de"><head><meta charset="UTF-8">
<title>Lokaler POI-Index-Benchmark</title>
<style>
 body{font-family:Georgia,'Times New Roman',serif;max-width:920px;margin:48px auto;padding:0 24px;color:#1a1a1a;line-height:1.5}
 h1{font-size:30px;font-weight:600;margin:0 0 6px}
 h2{font-size:23px;font-weight:600;margin:34px 0 4px}
 h2 .coord{font-size:17px;font-weight:400;color:#666}
 p{font-size:19px}
 .meta{font-size:16px;color:#555;margin:0 0 12px}
 .dichte{font-size:17px;color:#444;margin:0 0 10px}
 table{width:100%;border-collapse:collapse;margin:8px 0}
 th,td{padding:11px 14px;font-size:18px;border-bottom:1px solid #ccc;text-align:left}
 th{border-bottom:2px solid #333}
 td.num,th.num{text-align:right;font-variant-numeric:tabular-nums}
 tr.grp td{background:#f0f0f0;font-weight:600;font-size:18px;padding-top:14px}
 .note{font-size:16px;color:#444;margin-top:30px}
 code{font-family:Consolas,monospace;font-size:16px}
</style></head><body>
)";

        f << "<h1>Lokaler POI-Index-Benchmark</h1>\n"
          << "<p class=\"meta\">" << fmt_int((uint64_t)n_pois)
          << " getaggte Knoten aus " << hesc(ds) << ", komplett im RAM ("
          << fmt_int((uint64_t)std::round(peak_mb)) << " MB, "
          << fmt_int((uint64_t)std::round(peak_mb * 1024.0 * 1024.0 / n_pois)) << " Byte/POI).<br>\n"
          << "GridIndex (" << grid_cells << "&times;" << grid_cells << "): "
          << fmt_f(grid_build, 1) << " s Aufbauzeit &nbsp;&middot;&nbsp; "
          << "R-Tree: " << fmt_f(rt_build, 1) << " s"
          << " &nbsp;&middot;&nbsp; " << n_queries << " Abfragen je Fall"
          << " &nbsp;&middot;&nbsp; Erzeugt " << datebuf << "</p>\n";

        for (const auto& zr : zones) {
            // find 50km bbox-only and 50km filtered for the subtitle
            double pois50 = 0, filter50 = 0;
            std::string filterlabel;
            for (const auto& c : zr.cases) {
                if (std::abs(c.radius_m - 50000) < 1 && c.cat.empty())
                    pois50 = c.grid.avg_found;
                else if (std::abs(c.radius_m - 50000) < 1 && !c.cat.empty()) {
                    filter50 = c.grid.avg_found;
                    filterlabel = c.cat + "=" + c.type;
                }
            }

            f << "<h2>" << hesc(zr.zone.name)
              << " <span class=\"coord\">(" << fmt_f(zr.zone.lat, 4)
              << ", " << fmt_f(zr.zone.lng, 4) << ")</span></h2>\n";

            f << "<p class=\"dichte\">" << fmt_int((uint64_t)std::round(pois50))
              << " POIs im 50-km-Radius";
            if (!filterlabel.empty())
                f << ", davon " << fmt_int((uint64_t)std::round(filter50))
                  << " <code>" << hesc(filterlabel) << "</code>";
            f << "</p>\n";

            f << "<table>\n<thead><tr>"
              << "<th>Index</th>"
              << "<th class=\"num\">avg (ms)</th>"
              << "<th class=\"num\">min (ms)</th>"
              << "<th class=\"num\">max (ms)</th>"
              << "<th class=\"num\">&Oslash; Treffer</th>"
              << "</tr></thead>\n<tbody>\n";

            for (const auto& c : zr.cases) {
                // group header
                std::string radius_label = (c.radius_m >= 1000)
                    ? fmt_int((uint64_t)(c.radius_m / 1000)) + " km"
                    : fmt_int((uint64_t)c.radius_m) + " m";
                std::string filter_label = c.cat.empty()
                    ? "alle Tags"
                    : c.cat + "=" + c.type;
                f << "<tr class=\"grp\"><td colspan=\"5\">"
                  << radius_label << " &nbsp;&middot;&nbsp; " << hesc(filter_label)
                  << "</td></tr>\n";

                // highlight the faster index
                bool grid_faster = c.grid.avg_ms <= c.rtree.avg_ms;

                auto td_avg = [&](const QStat& s, bool faster) {
                    return faster
                        ? "<td class=\"num\"><strong>" + fmt_f(s.avg_ms, 1) + "</strong></td>"
                        : "<td class=\"num\">" + fmt_f(s.avg_ms, 1) + "</td>";
                };

                f << "<tr><td>Grid</td>"
                  << td_avg(c.grid, grid_faster)
                  << "<td class=\"num\">" << fmt_f(c.grid.min_ms, 1) << "</td>"
                  << "<td class=\"num\">" << fmt_f(c.grid.max_ms, 1) << "</td>"
                  << "<td class=\"num\">" << fmt_int((uint64_t)std::round(c.grid.avg_found)) << "</td>"
                  << "</tr>\n";

                f << "<tr><td>R-Tree</td>"
                  << td_avg(c.rtree, !grid_faster)
                  << "<td class=\"num\">" << fmt_f(c.rtree.min_ms, 1) << "</td>"
                  << "<td class=\"num\">" << fmt_f(c.rtree.max_ms, 1) << "</td>"
                  << "<td class=\"num\">" << fmt_int((uint64_t)std::round(c.rtree.avg_found)) << "</td>"
                  << "</tr>\n";
            }
            f << "</tbody></table>\n";
        }

        f << "<p class=\"note\">Lokal: Mittel aus " << n_queries
          << " Abfragen je Fall, Bounding-Box, In-Memory im selben Prozess. "
          << "Fettgedruckte avg-Werte markieren den jeweils schnelleren Index.</p>\n"
          << "</body></html>\n";
    }

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) { std::cerr << "Usage: bench_pois <file.osm.pbf> [out.json] [Name,lat,lng ...]\n"; return 1; }
    std::string infile   = argv[1];
    std::string out_json = (argc > 2) ? argv[2] : "bench_local.json";

    // derive HTML output filename from JSON filename
    std::string out_html = out_json;
    {
        auto dot = out_html.rfind('.');
        if (dot != std::string::npos) out_html.replace(dot, out_html.size() - dot, ".html");
        else out_html += ".html";
    }

    std::vector<Zone> zones;
    for (int i = 3; i < argc; ++i) {
        std::string t = argv[i];
        auto p1 = t.find(','), p2 = t.find(',', p1 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos) {
            std::cerr << "Ignoriere ungueltige Zone: " << t << " (Format Name,lat,lng)\n";
            continue;
        }
        zones.push_back({t.substr(0, p1),
                         std::stod(t.substr(p1 + 1, p2 - p1 - 1)),
                         std::stod(t.substr(p2 + 1))});
    }
    if (zones.empty()) zones = { {"Essen", 51.4556, 7.0116}, {"Wuerzburg", 49.7913, 9.9534} };

    std::cout << "== POI-Abfrage-Messung (ALLE getaggten Knoten, 50 km) ==\nDatei: " << infile << "\n\n";
    using Clock = std::chrono::high_resolution_clock;

    std::vector<POI> pois;
    pois.reserve(22'000'000);
    TagDict dict;
    auto t0 = Clock::now();
    {
        Collector coll(pois, dict);
        osmium::io::File input{infile};
        osmium::io::Reader reader{input, osmium::osm_entity_bits::node};
        osmium::apply(reader, coll);
        reader.close();
    }
    double load_s = std::chrono::duration<double>(Clock::now() - t0).count();
    std::printf("Eingelesen: %zu POIs in %.1f s\n", pois.size(), load_s);

    auto tb = Clock::now();
    GridIndex grid(pois);
    double grid_build = std::chrono::duration<double>(Clock::now() - tb).count();
    tb = Clock::now();
    RTreeIndex rtree(pois);
    double rt_build = std::chrono::duration<double>(Clock::now() - tb).count();

    double peak = peak_ram_mb();
    std::printf("GridIndex (%dx%d) %.1f s, RTreeIndex %.1f s, Peak-RAM %.0f MB (%.0f B/POI)\n\n",
                grid.cells_per_side(), grid.cells_per_side(), grid_build, rt_build,
                peak, peak * 1024.0 * 1024.0 / pois.size());

    struct Case { const char* name; double radius; const char* cat; const char* type; };
    Case cases[] = {
        {"10km bbox-only",          10000, "", ""},
        {"10km amenity=restaurant", 10000, "amenity", "restaurant"},
        {"50km bbox-only",          50000, "", ""},
        {"50km amenity=restaurant", 50000, "amenity", "restaurant"},
    };
    const int N = 15;

    std::vector<ZoneResult> zone_results;

    for (size_t zi = 0; zi < zones.size(); ++zi) {
        const auto& z = zones[zi];
        ZoneResult zr; zr.zone = z;
        std::printf("[Zone %s (%.4f, %.4f)]\n", z.name.c_str(), z.lat, z.lng);

        for (size_t ci = 0; ci < std::size(cases); ++ci) {
            const auto& c = cases[ci];
            TagFilter filter;
            if (c.cat[0] && c.type[0]) {
                uint32_t kid = dict.id_of(c.cat), vid = dict.id_of(c.type);
                if (kid != TagDict::INVALID && vid != TagDict::INVALID)
                    filter.push_back({kid, vid});
            }
            QStat g = run_queries(grid,  z.lat, z.lng, c.radius, N, filter);
            QStat r = run_queries(rtree, z.lat, z.lng, c.radius, N, filter);
            std::printf("  %-26s Grid %8.3f ms | RTree %8.3f ms | Ø Treffer %10.0f\n",
                        c.name, g.avg_ms, r.avg_ms, g.avg_found);
            zr.cases.push_back({c.name, c.radius, c.cat, c.type, g, r});
        }
        zone_results.push_back(zr);
    }

    write_json(out_json, infile, pois.size(), peak, load_s, grid_build, rt_build,
               grid.cells_per_side(), N, zone_results);
    write_html(out_html, infile, pois.size(), peak, load_s, grid_build, rt_build,
               grid.cells_per_side(), N, zone_results);

    std::printf("\nLokale Ergebnisse -> %s\n", out_json.c_str());
    std::printf("HTML-Report      -> %s\n",  out_html.c_str());
    return 0;
}
