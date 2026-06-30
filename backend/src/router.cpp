#include "router.hpp"
#include "spatial_index.hpp"
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>

namespace routenplaner {

    Router::Router(const Graph& graph) : graph_(graph) {}

    RouteResult Router::dijkstra(uint64_t from, uint64_t to) const {
        if (!graph_.has_node(from) || !graph_.has_node(to)) {
            return RouteResult{false, 0.0, {}, {}};
        }

        // Min-heap (distance, node_id)
        using PQEntry = std::pair<double, uint64_t>;
        std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<>> pq;

        std::unordered_map<uint64_t, double> dist;
        std::unordered_map<uint64_t, uint64_t> prev;

        dist[from] = 0.0;
        pq.push({0.0, from});

        while (!pq.empty()) {
            auto [d, u] = pq.top();
            pq.pop();

            // Ziel erreicht
            if (u == to) break;

            // Überspringen
            if (d > dist[u]) continue;

            for (const auto& edge : graph_.neighbors(u)) {
                double new_dist = d + edge.weight;

                auto it = dist.find(edge.target_node);
                if (it == dist.end() || new_dist < it->second) {
                    dist[edge.target_node] = new_dist;
                    prev[edge.target_node] = u;
                    pq.push({new_dist, edge.target_node});
                }
            }
        }


        if (dist.find(to) == dist.end()) {
            return RouteResult{false, 0.0, {}, {}};
        }

        // Pfad
        std::vector<uint64_t> path;
        for (uint64_t node = to; node != from; ) {
            path.push_back(node);
            auto it = prev.find(node);
            if (it == prev.end()) {
                return RouteResult{false, 0.0, {}, {}};
            }
            node = it->second;
        }
        path.push_back(from);
        std::reverse(path.begin(), path.end());

        // Route erstellen
        auto geometry = build_route(path);

        return RouteResult{true, dist[to], std::move(path), std::move(geometry)};
    }

    DijkstraState Router::capped_multisource(
        const std::vector<std::pair<uint64_t, double>>& seeds,
        double cap) const
    {
        DijkstraState st;

        // Min-heap (distance, node_id)
        using PQEntry = std::pair<double, uint64_t>;
        std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<>> pq;

        // Alle Quellknoten mit ihrer Anfangsdistanz einspeisen.
        for (const auto& [node, d] : seeds) {
            if (d > cap) { st.cap_limited = true; continue; }   // seed jenseits der Schranke
            if (!graph_.has_node(node)) continue;
            auto it = st.dist.find(node);
            if (it == st.dist.end() || d < it->second) {
                st.dist[node] = d;                 // seeds: bewusst kein prev
                pq.push({d, node});
            }
        }

        while (!pq.empty()) {
            auto [d, u] = pq.top();
            pq.pop();

            if (d > cap) break;                    // nur Knoten <= cap settlen (Sicherheit)

            auto uit = st.dist.find(u);
            if (uit == st.dist.end() || d > uit->second) continue;  // veraltet

            for (const auto& edge : graph_.neighbors(u)) {
                double nd = d + edge.weight;
                if (nd > cap) { st.cap_limited = true; continue; }  // jenseits der Schranke kappen

                auto it = st.dist.find(edge.target_node);
                if (it == st.dist.end() || nd < it->second) {
                    st.dist[edge.target_node] = nd;
                    st.prev[edge.target_node] = u;
                    pq.push({nd, edge.target_node});
                }
            }
        }

        return st;
    }

    SequencedRouteResult Router::sequenced_route(
        uint64_t s, uint64_t t,
        const std::vector<Category>& seq,
        const GridIndex& grid,
        double d_start, double d_factor,
        bool with_exploration) const
    {
        using Clock = std::chrono::high_resolution_clock;
        auto query_start = Clock::now();

        SequencedRouteResult result;

        if (!graph_.has_node(s) || !graph_.has_node(t)) return result;
        if (d_start <= 0.0 || d_factor <= 1.0) return result;

        const LatLng s_coords = graph_.node_coords(s);
        constexpr double PI        = 3.14159265358979323846;
        constexpr double DEG_PER_M = 1.0 / 111000.0;

        double cap = d_start;

        while (true) {
            auto round_start = Clock::now();

            // Bounding-Box um s mit Radius cap (Meter; Distanzgewichte => 1:1).
            // Enthaelt jede Facility, die innerhalb Distanz cap von s erreichbar ist.
            double d_lat = cap * DEG_PER_M;
            double d_lng = cap * DEG_PER_M / std::cos(s_coords.lat * PI / 180.0);
            double lat_min = s_coords.lat - d_lat, lat_max = s_coords.lat + d_lat;
            double lng_min = s_coords.lng - d_lng, lng_max = s_coords.lng + d_lng;

            // Kategorie-Knoten innerhalb der Box (per Grid-Index):
            // cat_nodes[i] = Knotenmenge (fuer Seeding/Backtrack-Stop),
            // cat_poi[i]   = Knoten -> POI (fuer die Ausgabe der gewaehlten Facility).
            const size_t l = seq.size();
            std::vector<std::unordered_set<uint64_t>> cat_nodes(l);
            std::vector<std::unordered_map<uint64_t, const POI*>> cat_poi(l);
            for (size_t i = 0; i < l; ++i) {
                auto hits = grid.query_bbox(lat_min, lng_min, lat_max, lng_max, seq[i]);
                for (const POI* p : hits) {
                    cat_nodes[i].insert(p->nearest_node_id);
                    cat_poi[i].emplace(p->nearest_node_id, p);  // erster gewinnt
                }
            }

            // Phase 0: Dijkstra von s. Alle Phasen-States aufheben (Rekonstruktion).
            std::vector<DijkstraState> states;
            states.push_back(capped_multisource({{s, 0.0}}, cap));
            bool any_cap_limited = states[0].cap_limited;

            // Phasen 1..l: Mehrquellen-Dijkstra je Kategorie.
            bool reached = true;
            for (size_t i = 0; i < l; ++i) {
                std::vector<std::pair<uint64_t, double>> seeds;
                seeds.reserve(cat_nodes[i].size());
                for (uint64_t n : cat_nodes[i]) {
                    auto it = states.back().dist.find(n);
                    if (it != states.back().dist.end()) seeds.push_back({n, it->second});
                }
                if (seeds.empty()) { reached = false; break; }  // keine Facility erreichbar
                states.push_back(capped_multisource(seeds, cap));
                any_cap_limited |= states.back().cap_limited;
            }

            // t innerhalb der Schranke erreicht?
            double total = 0.0;
            if (reached) {
                auto it = states.back().dist.find(t);
                if (it != states.back().dist.end()) total = it->second;
                else reached = false;
            }

            size_t settled = 0;
            for (const auto& st : states) settled += st.dist.size();

            DoublingRound round;
            round.cap            = cap;
            round.bbox_radius_m  = cap;
            round.reached_target = reached;
            round.total_time     = reached ? total : 0.0;
            round.settled        = settled;
            round.time_ms        = std::chrono::duration<double, std::milli>(
                                       Clock::now() - round_start).count();
            result.rounds.push_back(round);

            if (reached) {
                result.found       = true;
                result.total_time  = total;
                result.rounds_used = static_cast<int>(result.rounds.size());

                // --- Rekonstruktion: Facility-Kette rueckwaerts ---
                // backtrack: von `end` ueber prev bis zu einem Knoten in `stops`.
                // Liefert den Pfad in Vorwaertsrichtung (stop .. end) und stop_node.
                auto backtrack = [](const DijkstraState& st, uint64_t end,
                                    const std::unordered_set<uint64_t>& stops,
                                    uint64_t& stop_node) {
                    std::vector<uint64_t> path{ end };
                    uint64_t cur = end;
                    while (stops.find(cur) == stops.end()) {
                        auto it = st.prev.find(cur);
                        if (it == st.prev.end()) break;  // Sicherheits-Abbruch (seed/Quelle)
                        cur = it->second;
                        path.push_back(cur);
                    }
                    stop_node = cur;
                    std::reverse(path.begin(), path.end());
                    return path;
                };

                std::vector<uint64_t> f(l);                       // f[i] = gewaehlter Knoten der Kategorie i+1
                std::vector<std::vector<uint64_t>> seg_paths(l + 1);

                // Abschnitte l..1: states[i], stop = Kategorie i (cat_nodes[i-1]).
                uint64_t cur_end = t;
                for (size_t i = l; i >= 1; --i) {
                    uint64_t stop = 0;
                    seg_paths[i] = backtrack(states[i], cur_end, cat_nodes[i - 1], stop);
                    f[i - 1] = stop;
                    cur_end  = stop;
                    if (i == 1) break;  // size_t-Unterlauf vermeiden
                }
                // Abschnitt 0: states[0], von cur_end (=f_1 bzw. t falls l==0) zurueck nach s.
                std::unordered_set<uint64_t> s_set{ s };
                uint64_t stop0 = 0;
                seg_paths[0] = backtrack(states[0], cur_end, s_set, stop0);

                // Segmente bauen (Geometrie + Gewicht aus Distanzdifferenz).
                result.segments.resize(l + 1);
                for (size_t i = 0; i <= l; ++i) {
                    const auto& path = seg_paths[i];
                    RouteSegment seg;
                    if (!path.empty()) {
                        seg.from_node = path.front();
                        seg.to_node   = path.back();
                        seg.geometry  = build_route(path);
                        auto de = states[i].dist.find(seg.to_node);
                        auto df = states[i].dist.find(seg.from_node);
                        if (de != states[i].dist.end() && df != states[i].dist.end())
                            seg.weight = de->second - df->second;
                    }
                    result.segments[i] = std::move(seg);
                }
                // Gewaehlte Facilities: Ende von Abschnitt i (i<l) ist Facility f_{i+1}.
                for (size_t i = 0; i < l; ++i) {
                    const POI* fac = nullptr;
                    auto it = cat_poi[i].find(f[i]);
                    if (it != cat_poi[i].end()) fac = it->second;
                    result.segments[i].facility = fac;
                    result.chosen.push_back(fac);
                }

                // --- Optional: Suchbaeume ab den gewaehlten POIs ---
                // POI f[j] (Kategorie j+1) ist seed in Phase j+1 (states[j+1]).
                // Der von f[j] ausgehende Teilbaum = Exploration Richtung naechster
                // Kategorie/Ziel. prev wird zu einer Kinderliste invertiert.
                if (with_exploration) {
                    constexpr size_t EDGE_CAP = 50000;   // Schutz gegen riesige Antworten
                    result.exploration.resize(l);
                    for (size_t j = 0; j < l; ++j) {
                        const DijkstraState& st = states[j + 1];
                        std::unordered_map<uint64_t, std::vector<uint64_t>> children;
                        children.reserve(st.prev.size());
                        for (const auto& [node, par] : st.prev)
                            children[par].push_back(node);

                        ExplorationTree tree;

                        // Mitbewerber: erreichbare Facilities derselben Kategorie
                        // (seeds dieser Phase, ausser dem gewaehlten f[j]).
                        for (const auto& [node, poi] : cat_poi[j]) {
                            if (node == f[j]) continue;
                            if (states[j].dist.count(node)) tree.candidates.push_back(poi);
                        }

                        std::vector<uint64_t> stack{ f[j] };
                        while (!stack.empty() && tree.edges.size() < EDGE_CAP) {
                            uint64_t u = stack.back();
                            stack.pop_back();
                            auto ch = children.find(u);
                            if (ch == children.end()) continue;
                            for (uint64_t v : ch->second) {
                                std::vector<LatLng> geom;
                                for (const auto& e : graph_.neighbors(u)) {
                                    if (e.target_node == v) {
                                        if (!e.geometry.empty()) geom = e.geometry;
                                        break;
                                    }
                                }
                                if (geom.empty())
                                    geom = { graph_.node_coords(u), graph_.node_coords(v) };
                                tree.edges.push_back(std::move(geom));
                                stack.push_back(v);
                                if (tree.edges.size() >= EDGE_CAP) break;
                            }
                        }
                        result.exploration[j] = std::move(tree);
                    }
                }

                result.query_time_ms = std::chrono::duration<double, std::milli>(
                                           Clock::now() - query_start).count();
                return result;
            }

            // Abbruch: keine Phase wurde durch die Schranke begrenzt => die
            // erreichbare Region ist vollstaendig exploriert; eine groessere
            // Schranke aendert nichts => es existiert keine Route.
            if (!any_cap_limited) {
                result.rounds_used = static_cast<int>(result.rounds.size());
                result.query_time_ms = std::chrono::duration<double, std::milli>(
                                           Clock::now() - query_start).count();
                return result;  // found = false
            }
            cap *= d_factor;
        }
    }

    SequencedRouteResult Router::sequenced_route(
        double s_lat, double s_lng, double t_lat, double t_lng,
        const std::vector<Category>& seq,
        const GridIndex& grid,
        double d_start, double d_factor,
        bool with_exploration) const
    {
        uint64_t s = graph_.nearest_node(s_lat, s_lng);
        uint64_t t = graph_.nearest_node(t_lat, t_lng);
        return sequenced_route(s, t, seq, grid, d_start, d_factor, with_exploration);
    }

    RouteResult Router::route(double from_lat, double from_lng,
                            double to_lat, double to_lng) const {
        uint64_t from_node = graph_.nearest_node(from_lat, from_lng);
        uint64_t to_node = graph_.nearest_node(to_lat, to_lng);

        std::cout << "[route] from nearest node " << from_node
                << " to nearest node " << to_node << std::endl;

        return dijkstra(from_node, to_node);
    }

    std::vector<LatLng> Router::build_route(const std::vector<uint64_t>& path) const {
        std::vector<LatLng> geometry;

        for (size_t i = 0; i < path.size(); i++) {
            const auto& coords = graph_.node_coords(path[i]);

            if (i + 1 < path.size()) {
                // Find the edge from path[i] to path[i+1]
                bool found_edge = false;
                for (const auto& edge : graph_.neighbors(path[i])) {
                    if (edge.target_node == path[i + 1] && !edge.geometry.empty()) {
                        // Geometrie der Kante verwenden
                        for (const auto& pt : edge.geometry) {
                            geometry.push_back(pt);
                        }
                        found_edge = true;
                        break;
                    }
                }
                if (!found_edge) {
                    // Fallback auf Koordinaten
                    geometry.push_back(coords);
                }
            } else {
                geometry.push_back(coords);
            }
        }

        return geometry;
    }

}
