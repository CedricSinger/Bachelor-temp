#pragma once

#include "graph.hpp"
#include "poi.hpp"
#include <vector>
#include <optional>
#include <utility>
#include <unordered_map>

namespace routenplaner {

    class GridIndex;   // Vorwaertsdeklaration (Definition in spatial_index.hpp)

    // Ergebnis der Anfrage
    struct RouteResult {
        bool found = false;
        double distance = 0.0;
        std::vector<uint64_t> node_path;
        std::vector<LatLng> geometry;
    };

    // Eine Kategorie der Sequenz = Tag-Filter (z.B. {(amenity, fuel)}).
    using Category = TagFilter;

    // Ein Teilstueck der Gesamtroute (s->C1, C1->C2, ..., Cl->t).
    struct RouteSegment {
        uint64_t from_node = 0;
        uint64_t to_node = 0;
        double   weight = 0.0;            // Distanz/Zeit dieses Abschnitts
        std::vector<LatLng> geometry;
        const POI* facility = nullptr;    // am Abschnittsende gewaehlte Facility (nullptr beim letzten Abschnitt nach t)
    };

    // Protokoll einer Verdopplungsrunde (fuer Debugging/Visualisierung).
    struct DoublingRound {
        double cap = 0.0;             // Schranke D' dieser Runde
        double bbox_radius_m = 0.0;   // Radius der Kategorie-Suche in Metern
        bool   reached_target = false;
        double total_time = 0.0;      // gueltig falls reached_target
        double time_ms = 0.0;         // Laufzeit dieser Runde
        size_t settled = 0;           // settled-Knoten ueber alle Phasen der Runde
    };

    // Dijkstra-Suchbaum, der von einem gewaehlten POI ausgeht (fuer die
    // Visualisierung): jede Kante als Polyline. candidates = die uebrigen
    // erreichbaren Facilities derselben Kategorie (Mitbewerber-seeds dieser Phase).
    struct ExplorationTree {
        std::vector<std::vector<LatLng>> edges;
        std::vector<const POI*> candidates;
    };

    struct SequencedRouteResult {
        bool found = false;
        double total_time = 0.0;
        std::vector<RouteSegment> segments;   // l+1 Abschnitte
        std::vector<const POI*> chosen;       // l gewaehlte Facilities
        std::vector<DoublingRound> rounds;    // Verlauf der Verdopplung
        int rounds_used = 0;
        double query_time_ms = 0.0;           // Gesamtlaufzeit der Anfrage
        // Suchbaeume je gewaehltem POI (nur falls with_exploration gesetzt).
        std::vector<ExplorationTree> exploration;
    };

    // Ergebnis eines (Mehrquellen-)Dijkstra-Laufs: settled-Distanzen und
    // Vorgaengerzeiger. Quellknoten (seeds) haben KEINEN prev-Eintrag, damit
    // die Pfadrekonstruktion an einem seed stoppen kann.
    struct DijkstraState {
        std::unordered_map<uint64_t, double>   dist;
        std::unordered_map<uint64_t, uint64_t> prev;
        // true, wenn der Lauf durch die Schranke begrenzt wurde (mind. eine
        // Kante/ein seed wegen Distanz > cap verworfen). false => die
        // erreichbare Region wurde vollstaendig exploriert; eine groessere
        // Schranke wuerde am Ergebnis nichts aendern.
        bool cap_limited = false;
    };

    class Router {
    public:
        explicit Router(const Graph& graph);

        // Dijkstra zwischen Knoten
        RouteResult dijkstra(uint64_t from, uint64_t to) const;

        // Route zwischen Koordinaten
        RouteResult route(double from_lat, double from_lng,
                        double to_lat, double to_lng) const;

        // Sequenzierte Route mit iterativer Verdopplung (Knoten-Variante).
        // seq: Kategorien in Besuchsreihenfolge. d_start: Start-Schranke,
        // d_factor: Verdopplungsfaktor (>1).
        SequencedRouteResult sequenced_route(
            uint64_t s, uint64_t t,
            const std::vector<Category>& seq,
            const GridIndex& grid,
            double d_start, double d_factor,
            bool with_exploration = false) const;

        // Koordinaten-Variante (snappt auf naechste Knoten).
        SequencedRouteResult sequenced_route(
            double s_lat, double s_lng, double t_lat, double t_lng,
            const std::vector<Category>& seq,
            const GridIndex& grid,
            double d_start, double d_factor,
            bool with_exploration = false) const;

    private:
        const Graph& graph_;

        // Route erstellen
        std::vector<LatLng> build_route(const std::vector<uint64_t>& path) const;

        // Mehrquellen-Dijkstra mit Distanzschranke.
        // seeds: (Knoten, Anfangsdistanz). Der Lauf settled nur Knoten mit
        // Distanz <= cap (Abbruch sobald das PQ-Minimum cap ueberschreitet).
        DijkstraState capped_multisource(
            const std::vector<std::pair<uint64_t, double>>& seeds,
            double cap) const;
    };

}
