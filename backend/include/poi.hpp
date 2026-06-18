#pragma once

#include "graph.hpp"
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <cstdint>
#include <utility>

namespace routenplaner {

    // String-Interning fuer Tag-Keys und -Werte: jede eindeutige Zeichenkette
    // bekommt eine kompakte uint32-ID. Spart Speicher (jeder POI haelt nur IDs)
    // und beschleunigt Vergleiche (Integer statt String).
    class TagDict {
    public:
        static constexpr uint32_t INVALID = 0xFFFFFFFFu;

        // Fuegt s hinzu (falls neu) und gibt die ID zurueck.
        uint32_t intern(const std::string& s) {
            auto [it, inserted] = index_.try_emplace(s, static_cast<uint32_t>(rev_.size()));
            if (inserted) rev_.push_back(&it->first);
            return it->second;
        }
        // Nur Nachschlagen; INVALID falls unbekannt (fuer Abfragen).
        uint32_t id_of(const std::string& s) const {
            auto it = index_.find(s);
            return it == index_.end() ? INVALID : it->second;
        }
        const std::string& str(uint32_t id) const { return *rev_[id]; }
        size_t size() const { return rev_.size(); }

    private:
        std::unordered_map<std::string, uint32_t> index_;  // String -> ID
        std::vector<const std::string*> rev_;              // ID -> &String (stabil)
    };

    struct POI {
        uint64_t id;
        LatLng   coords;
        std::vector<std::pair<uint32_t, uint32_t>> tags;   // (key_id, value_id)
        uint64_t nearest_node_id = 0;
    };

    // Tag-Filter: ALLE (key,value)-Paare muessen am POI vorhanden sein (AND).
    // Leerer Filter = kein Filter (matcht alles).
    using TagFilter = std::vector<std::pair<uint32_t, uint32_t>>;

    inline bool poi_matches(const POI& p, const TagFilter& f) {
        for (const auto& [k, v] : f) {
            bool found = false;
            for (const auto& [pk, pv] : p.tags)
                if (pk == k && pv == v) { found = true; break; }
            if (!found) return false;
        }
        return true;
    }

    class POICollection {
    public:
        TagDict&       dict()       { return dict_; }
        const TagDict& dict() const { return dict_; }

        void add(POI poi);

        // nearest_node_id fuer alle POIs setzen
        void snap_to_graph(const Graph& graph);

        // Alle POIs in Bounding Box, optional nach Tags gefiltert
        std::vector<const POI*> query_bbox(
            double lat_min, double lng_min,
            double lat_max, double lng_max,
            const TagFilter& filter = {}) const;

        // N naechste POIs (optional gefiltert)
        std::vector<const POI*> nearest(
            double lat, double lng,
            const TagFilter& filter = {},
            size_t max_results = 10) const;

        size_t size() const { return pois_.size(); }

        const std::vector<POI>& all() const { return pois_; }

        // key -> Anzahl POIs mit diesem Key
        std::map<std::string, size_t> key_stats() const;

    private:
        std::vector<POI> pois_;
        TagDict          dict_;
    };

}
