#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>

namespace routenplaner {

class POICollection;

    struct LatLng {
        double lat = 0.0;
        double lng = 0.0;

        double distance_to(const LatLng& other) const;
    };

    struct Edge {
        uint64_t target_node;
        double weight;
        std::vector<LatLng> geometry;
    };

    class Graph {
    public:
        void add_node(uint64_t id, double lat, double lng);

        void add_edge(uint64_t from, uint64_t to, double weight,
                    std::vector<LatLng> geometry = {});

        // Kante in beide Richtungen
        void add_edge_both_ways(uint64_t from, uint64_t to, double weight,
                                    std::vector<LatLng> geometry = {});

        // Nächsten Knoten zu Koordinate finden
        uint64_t nearest_node(double lat, double lng) const;


        const std::vector<Edge>& neighbors(uint64_t node_id) const;
        const LatLng& node_coords(uint64_t node_id) const;
        bool has_node(uint64_t node_id) const;
        size_t node_count() const { return nodes_.size(); }
        size_t edge_count() const { return edge_count_; }


        // Graph und POIs aus PBF-Datei laden
        static Graph load_from_pbf(const std::string& filepath, POICollection& pois);

    private:
        std::unordered_map<uint64_t, LatLng> nodes_;
        std::unordered_map<uint64_t, std::vector<Edge>> adjacency_;
        size_t edge_count_ = 0;

        static const std::vector<Edge> empty_edges_;
    };

}
