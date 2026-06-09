#pragma once

#include "graph.hpp"
#include <vector>
#include <optional>

namespace routenplaner {

    // Ergebnis der Anfrage
    struct RouteResult {
        bool found = false;
        double distance = 0.0;
        std::vector<uint64_t> node_path;
        std::vector<LatLng> geometry;
    };

    class Router {
    public:
        explicit Router(const Graph& graph);

        // Dijkstra zwischen Knoten
        RouteResult dijkstra(uint64_t from, uint64_t to) const;

        // Route zwischen Koordinaten
        RouteResult route(double from_lat, double from_lng,
                        double to_lat, double to_lng) const;

    private:
        const Graph& graph_;

        // Route erstellen
        std::vector<LatLng> build_route(const std::vector<uint64_t>& path) const;
    };

}
