#include "router.hpp"
#include <queue>
#include <unordered_map>
#include <algorithm>
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
