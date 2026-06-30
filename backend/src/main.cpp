#include "graph.hpp"
#include "router.hpp"
#include "server.hpp"
#include "poi.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <chrono>

int main(int argc, char* argv[]) {
    std::string address = "0.0.0.0";
    uint16_t port = 8080;
    std::string root = "./frontend";
    std::string osm_file;



    // Args auslesen
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if ((arg == "--address" || arg == "-a") && i + 1 < argc) {
            address = argv[++i];
        } else if ((arg == "--docroot" || arg == "-d") && i + 1 < argc) {
            root = argv[++i];
        } else if ((arg == "--osm" || arg == "-o") && i + 1 < argc) {
            osm_file = argv[++i];
        }
    }

    if (!std::filesystem::exists(root)) {
        std::cerr << "Warning: document root '" << root << "' not found. Static files won't be served." << std::endl;
    }
    
    
    
    // Starten
    try {
        // Graph und POI laden
        if (osm_file.empty()) {
            std::cerr << "No graph file found!" << std::endl;
            return 1;
        }
        routenplaner::POICollection pois;
        auto load_start = std::chrono::high_resolution_clock::now();
        auto graph = routenplaner::Graph::load_from_pbf(osm_file, pois);
        auto load_ms = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - load_start).count();
        std::cout << "Dataset loaded in " << (load_ms / 1000.0) << " s" << std::endl;

        // Knoten-Index aufbauen und POIs auf Graphknoten snappen (fuer Routing).
        auto snap_start = std::chrono::high_resolution_clock::now();
        graph.build_node_index();
        pois.snap_to_graph(graph);
        auto snap_ms = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - snap_start).count();
        std::cout << "Node index built + POIs snapped in " << (snap_ms / 1000.0) << " s" << std::endl;

        // Stats auslesen: Anzahl POIs je Tag-Key
        auto key_stats = pois.key_stats();
        std::ofstream stats_file("poi_stats.txt");
        if (stats_file) {
            stats_file << "POI Statistics (" << pois.size() << " POIs, "
                       << key_stats.size() << " distinct keys)\n\n";
            for (const auto& [key, count] : key_stats) {
                stats_file << key << ": " << count << "\n";
            }
            std::cout << "POI stats written to poi_stats.txt (" << pois.size() << " total)" << std::endl;
        }

        routenplaner::Router router(graph);
        routenplaner::Server server(address, port, root, router, pois);
        server.run();

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
