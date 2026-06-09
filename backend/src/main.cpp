#include "graph.hpp"
#include "router.hpp"
#include "server.hpp"
#include "poi.hpp"

#include <iostream>
#include <string>
#include <filesystem>

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
        std::cerr << "Warning: document root '" << root
                  << "' not found. Static files won't be served." << std::endl;
    }

    try {
        // Graph und POI laden
        if (osm_file.empty()) {
            std::cerr << "No graph file found!" << std::endl;
            return 1;
        }
        routenplaner::POICollection pois;
        auto graph = routenplaner::Graph::load_from_pbf(osm_file, pois);

        routenplaner::Router router(graph);
        routenplaner::Server server(address, port, root, router, pois);
        server.run();

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
