#pragma once

#include "router.hpp"
#include "poi.hpp"
#include "spatial_index.hpp"
#include <string>
#include <cstdint>

namespace routenplaner {

    class Server {
    public:
        Server(const std::string& address, uint16_t port,
            const std::string& doc_root, const Router& router,
            const POICollection& pois);

        void run();

    private:
        std::string address_;
        uint16_t port_;
        std::string doc_root_;
        const Router& router_;
        const POICollection& pois_;
        LinearIndex        linear_idx_;
        GridIndex          grid_idx_;
        RTreeIndex         rtree_idx_;
    };
    
}
