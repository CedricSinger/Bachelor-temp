#pragma once

#include "poi.hpp"
#include <vector>
#include <string>
#include <map>

#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>

namespace bg  = boost::geometry;
namespace bgi = boost::geometry::index;

namespace routenplaner {

struct IndexTiming {
    double avg_ms   = 0.0;
    double min_ms   = 0.0;
    double max_ms   = 0.0;
    double total_ms = 0.0;
};

struct BenchmarkResult {
    int    n_queries     = 0;
    double center_lat    = 0.0;
    double center_lng    = 0.0;
    double radius_m      = 0.0;
    double avg_found     = 0.0;
    size_t total_pois    = 0;
    size_t filter_pois   = 0;
    std::map<std::string, IndexTiming> timings;
};

// Linear (brute force)
class LinearIndex {
public:
    explicit LinearIndex(const std::vector<POI>& pois);
    std::vector<const POI*> query_bbox(
        double lat_min, double lng_min,
        double lat_max, double lng_max,
        const TagFilter& filter = {}) const;
private:
    std::vector<const POI*> pois_;
};

// Grid
class GridIndex {
public:
    GridIndex(const std::vector<POI>& pois, int cells_per_side = 0);
    GridIndex(const std::vector<const POI*>& poi_ptrs, int cells_per_side = 0);
    std::vector<const POI*> query_bbox(
        double lat_min, double lng_min,
        double lat_max, double lng_max,
        const TagFilter& filter = {}) const;
    int cells_per_side() const { return cells_; }
private:
    double lat_min_, lat_max_, lng_min_, lng_max_;
    int    cells_;
    std::vector<std::vector<const POI*>> grid_;
    void build(const std::vector<const POI*>& pois, int cells_per_side);
    int to_row(double lat) const;
    int to_col(double lng) const;
};

// Boost.Geometry R-Tree
using BgPoint    = bg::model::point<double, 2, bg::cs::cartesian>;
using BgBox      = bg::model::box<BgPoint>;
using RTreeEntry = std::pair<BgPoint, const POI*>;
using RTree      = bgi::rtree<RTreeEntry, bgi::quadratic<16>>;

class RTreeIndex {
public:
    explicit RTreeIndex(const std::vector<POI>& pois);
    std::vector<const POI*> query_bbox(
        double lat_min, double lng_min,
        double lat_max, double lng_max,
        const TagFilter& filter = {}) const;
private:
    RTree rtree_;
};

BenchmarkResult run_benchmark(
    const LinearIndex&         linear,
    const GridIndex&           grid,
    const RTreeIndex&          rtree,
    double center_lat, double center_lng,
    double radius_m, int n_queries,
    const TagFilter& filter = {});

}
