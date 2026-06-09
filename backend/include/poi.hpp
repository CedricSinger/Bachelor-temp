#pragma once

#include "graph.hpp"
#include <string>
#include <vector>

namespace routenplaner {

    struct POI {
        uint64_t id;
        LatLng coords;
        std::string category;
        std::string type;
        std::string name;
        uint64_t nearest_node_id = 0;
    };

    class POICollection {
    public:
        void add(POI poi);

        // nearest_node_id für alle POIs setzen
        void snap_to_graph(const Graph& graph);

        // Alle POIs in Bounding Box
        std::vector<const POI*> query_bbox(
            double lat_min, double lng_min,
            double lat_max, double lng_max,
            const std::string& category = "",
            const std::string& type = "") const;

        // N nächste POIs
        std::vector<const POI*> nearest(
            double lat, double lng,
            const std::string& category = "",
            const std::string& type = "",
            size_t max_results = 10) const;

        size_t size() const { return pois_.size(); }

    private:
        std::vector<POI> pois_;
    };

}
