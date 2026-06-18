#include "graph.hpp"
#include "poi.hpp"
#include <limits>
#include <iostream>
#include <unordered_set>
#include <string_view>

#include <osmium/io/pbf_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <osmium/index/map/flex_mem.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>

namespace routenplaner {

    const std::vector<Edge> Graph::empty_edges_ = {};

    double LatLng::distance_to(const LatLng& other) const {
        constexpr double R = 6371000.0;
        double lat1 = lat * M_PI / 180.0;
        double lat2 = other.lat * M_PI / 180.0;
        double dlat = (other.lat - lat) * M_PI / 180.0;
        double dlng = (other.lng - lng) * M_PI / 180.0;

        double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
                std::cos(lat1) * std::cos(lat2) *
                std::sin(dlng / 2) * std::sin(dlng / 2);
        double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
        return R * c;
    }

    void Graph::add_node(uint64_t id, double lat, double lng) {
        nodes_[id] = {lat, lng};
        if (adjacency_.find(id) == adjacency_.end()) {
            adjacency_[id] = {};
        }
    }

    void Graph::add_edge(uint64_t from, uint64_t to, double weight,
                        std::vector<LatLng> geometry) {
        adjacency_[from].push_back({to, weight, std::move(geometry)});
        edge_count_++;
    }

    void Graph::add_edge_both_ways(uint64_t from, uint64_t to, double weight,
                                std::vector<LatLng> geometry) {
        adjacency_[from].push_back({to, weight, geometry});
        std::vector<LatLng> rev_geom(geometry.rbegin(), geometry.rend());
        adjacency_[to].push_back({from, weight, std::move(rev_geom)});
        edge_count_ += 2;
    }

    uint64_t Graph::nearest_node(double lat, double lng) const {
        LatLng target{lat, lng};
        uint64_t best_id = 0;
        double best_dist = std::numeric_limits<double>::max();

        for (const auto& [id, coords] : nodes_) {
            double d = target.distance_to(coords);
            if (d < best_dist) {
                best_dist = d;
                best_id = id;
            }
        }
        return best_id;
    }

    const std::vector<Edge>& Graph::neighbors(uint64_t node_id) const {
        auto it = adjacency_.find(node_id);
        if (it != adjacency_.end()) return it->second;
        return empty_edges_;
    }

    const LatLng& Graph::node_coords(uint64_t node_id) const {
        return nodes_.at(node_id);
    }

    bool Graph::has_node(uint64_t node_id) const {
        return nodes_.find(node_id) != nodes_.end();
    }



    // PBF laden
    namespace {

    const std::unordered_set<std::string> ROUTABLE_HIGHWAYS = {
        "motorway", "trunk", "primary", "secondary", "tertiary",
        "unclassified", "residential", "living_street",
        "motorway_link", "trunk_link", "primary_link",
        "secondary_link", "tertiary_link"
    };

    class OSMHandler : public osmium::handler::Handler {
        public:
            OSMHandler(Graph& graph, POICollection& pois) : graph_(graph), pois_(pois) {}

            void node(const osmium::Node& node) {
                if (!node.location().valid()) return;

                const auto& tags = node.tags();
                if (tags.empty()) return;   // jeder getaggte Knoten ist ein POI

                POI poi;
                poi.id     = static_cast<uint64_t>(node.id());
                poi.coords = {node.location().lat(), node.location().lon()};
                TagDict& dict = pois_.dict();
                poi.tags.reserve(tags.size());
                for (const auto& tag : tags)
                    poi.tags.emplace_back(dict.intern(tag.key()), dict.intern(tag.value()));
                pois_.add(std::move(poi));
            }

            void way(const osmium::Way& way) {
                const char* highway = way.tags()["highway"];
                if (!highway || ROUTABLE_HIGHWAYS.find(highway) == ROUTABLE_HIGHWAYS.end()) return;

                bool oneway = false;
                bool reverse = false;

                const char* junction = way.tags()["junction"];
                if (junction && std::string_view(junction) == "roundabout") {
                    oneway = true;
                }

                const char* oneway_tag = way.tags()["oneway"];
                if (oneway_tag) {
                    std::string_view ow(oneway_tag);
                    if (ow == "yes" || ow == "1" || ow == "true") {
                        oneway = true;
                    } else if (ow == "-1") {
                        oneway = true;
                        reverse = true;
                    }
                }

                const auto& nodes = way.nodes();
                for (size_t i = 0; i + 1 < nodes.size(); ++i) {
                    const auto& n1 = nodes[i];
                    const auto& n2 = nodes[i + 1];

                    if (!n1.location().valid() || !n2.location().valid()) continue;

                    LatLng l1{n1.location().lat(), n1.location().lon()};
                    LatLng l2{n2.location().lat(), n2.location().lon()};

                    graph_.add_node(n1.ref(), l1.lat, l1.lng);
                    graph_.add_node(n2.ref(), l2.lat, l2.lng);

                    double dist = l1.distance_to(l2);

                    if (oneway) {
                        if (reverse)
                            graph_.add_edge(n2.ref(), n1.ref(), dist, {l2, l1});
                        else
                            graph_.add_edge(n1.ref(), n2.ref(), dist, {l1, l2});
                    } else {
                        graph_.add_edge_both_ways(n1.ref(), n2.ref(), dist, {l1, l2});
                    }
                }
            }

        private:
            Graph& graph_;
            POICollection& pois_;
    };

    }

    

    Graph Graph::load_from_pbf(const std::string& filepath, POICollection& pois) {
        using Index = osmium::index::map::FlexMem<osmium::unsigned_object_id_type, osmium::Location>;
        using LocationHandler = osmium::handler::NodeLocationsForWays<Index>;

        std::cout << "Reading graph from: " << filepath << std::endl;

        Graph graph;
        Index index;

        osmium::io::File input_file{filepath};
        osmium::io::Reader reader{input_file,
            osmium::osm_entity_bits::node | osmium::osm_entity_bits::way};

        LocationHandler location_handler{index};
        OSMHandler osm_handler{graph, pois};

        osmium::apply(reader, location_handler, osm_handler);
        reader.close();

        std::cout << "Graph loaded: " << graph.node_count() << " nodes, "
                  << graph.edge_count() << " edges, "
                  << pois.size() << " POIs" << std::endl;

        return graph;
    }

}
