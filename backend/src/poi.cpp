#include "poi.hpp"
#include <algorithm>

namespace routenplaner {

    void POICollection::add(POI poi) {
        pois_.push_back(std::move(poi));
    }

    void POICollection::snap_to_graph(const Graph& graph) {
        for (auto& poi : pois_) {
            poi.nearest_node_id = graph.nearest_node(poi.coords.lat, poi.coords.lng);
        }
    }

    std::vector<const POI*> POICollection::query_bbox(
        double lat_min, double lng_min,
        double lat_max, double lng_max,
        const TagFilter& filter) const
    {
        std::vector<const POI*> result;
        for (const auto& poi : pois_) {
            if (poi.coords.lat < lat_min || poi.coords.lat > lat_max) continue;
            if (poi.coords.lng < lng_min || poi.coords.lng > lng_max) continue;
            if (!poi_matches(poi, filter)) continue;
            result.push_back(&poi);
        }
        return result;
    }

    std::vector<const POI*> POICollection::nearest(
        double lat, double lng,
        const TagFilter& filter,
        size_t max_results) const
    {
        LatLng target{lat, lng};

        std::vector<std::pair<double, const POI*>> candidates;
        candidates.reserve(pois_.size());

        for (const auto& poi : pois_) {
            if (!poi_matches(poi, filter)) continue;
            candidates.push_back({target.distance_to(poi.coords), &poi});
        }

        size_t n = std::min(max_results, candidates.size());
        std::partial_sort(candidates.begin(), candidates.begin() + n, candidates.end());
        candidates.resize(n);

        std::vector<const POI*> result;
        result.reserve(n);
        for (const auto& [dist, poi] : candidates) {
            result.push_back(poi);
        }
        return result;
    }

    std::map<std::string, size_t> POICollection::key_stats() const {
        std::map<std::string, size_t> result;
        for (const auto& poi : pois_) {
            for (const auto& [k, v] : poi.tags) {
                (void)v;
                result[dict_.str(k)]++;
            }
        }
        return result;
    }

}
