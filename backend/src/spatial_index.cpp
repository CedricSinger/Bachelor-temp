#include "spatial_index.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <random>

namespace routenplaner {

// Linearer Index
LinearIndex::LinearIndex(const std::vector<POI>& pois) {
    pois_.reserve(pois.size());
    for (const auto& poi : pois) pois_.push_back(&poi);
}

std::vector<const POI*> LinearIndex::query_bbox(
    double lat_min, double lng_min,
    double lat_max, double lng_max,
    const TagFilter& filter) const
{
    std::vector<const POI*> result;
    for (const auto* poi : pois_) {
        if (poi->coords.lat < lat_min || poi->coords.lat > lat_max) continue;
        if (poi->coords.lng < lng_min || poi->coords.lng > lng_max) continue;
        if (!poi_matches(*poi, filter)) continue;
        result.push_back(poi);
    }
    return result;
}

// Grid Index
GridIndex::GridIndex(const std::vector<POI>& pois, int cells_per_side) {
    std::vector<const POI*> ptrs;
    ptrs.reserve(pois.size());
    for (const auto& poi : pois) ptrs.push_back(&poi);
    build(ptrs, cells_per_side);
}

GridIndex::GridIndex(const std::vector<const POI*>& poi_ptrs, int cells_per_side) {
    build(poi_ptrs, cells_per_side);
}

void GridIndex::build(const std::vector<const POI*>& pois, int cells_per_side) {
    if (cells_per_side <= 0) {
        // Ziel ~15 POIs pro Zelle
        cells_ = static_cast<int>(std::sqrt(static_cast<double>(pois.size()) / 15.0));
        cells_ = std::max(100, std::min(500, cells_));
    } else {
        cells_ = cells_per_side;
    }

    if (pois.empty()) {
        lat_min_ = lat_max_ = lng_min_ = lng_max_ = 0.0;
        grid_.resize(static_cast<size_t>(cells_) * cells_);
        return;
    }

    lat_min_ = lat_max_ = pois[0]->coords.lat;
    lng_min_ = lng_max_ = pois[0]->coords.lng;
    for (const auto* poi : pois) {
        lat_min_ = std::min(lat_min_, poi->coords.lat);
        lat_max_ = std::max(lat_max_, poi->coords.lat);
        lng_min_ = std::min(lng_min_, poi->coords.lng);
        lng_max_ = std::max(lng_max_, poi->coords.lng);
    }
    lat_max_ += 1e-9;
    lng_max_ += 1e-9;

    grid_.resize(static_cast<size_t>(cells_) * cells_);
    for (const auto* poi : pois) {
        grid_[static_cast<size_t>(to_row(poi->coords.lat)) * cells_ + to_col(poi->coords.lng)].push_back(poi);
    }
}

int GridIndex::to_row(double lat) const {
    int r = static_cast<int>((lat - lat_min_) / (lat_max_ - lat_min_) * cells_);
    return std::max(0, std::min(cells_ - 1, r));
}

int GridIndex::to_col(double lng) const {
    int c = static_cast<int>((lng - lng_min_) / (lng_max_ - lng_min_) * cells_);
    return std::max(0, std::min(cells_ - 1, c));
}

std::vector<const POI*> GridIndex::query_bbox(
    double lat_min, double lng_min,
    double lat_max, double lng_max,
    const TagFilter& filter) const
{
    std::vector<const POI*> result;
    int r0 = to_row(lat_min), r1 = to_row(lat_max);
    int c0 = to_col(lng_min), c1 = to_col(lng_max);

    for (int r = r0; r <= r1; ++r) {
        for (int c = c0; c <= c1; ++c) {
            for (const auto* poi : grid_[static_cast<size_t>(r) * cells_ + c]) {
                if (poi->coords.lat < lat_min || poi->coords.lat > lat_max) continue;
                if (poi->coords.lng < lng_min || poi->coords.lng > lng_max) continue;
                if (!poi_matches(*poi, filter)) continue;
                result.push_back(poi);
            }
        }
    }
    return result;
}

// R-Tree Index
RTreeIndex::RTreeIndex(const std::vector<POI>& pois) {
    std::vector<RTreeEntry> entries;
    entries.reserve(pois.size());
    for (const auto& poi : pois)
        entries.push_back({BgPoint(poi.coords.lat, poi.coords.lng), &poi});
    rtree_ = RTree(entries.begin(), entries.end());
}

std::vector<const POI*> RTreeIndex::query_bbox(
    double lat_min, double lng_min,
    double lat_max, double lng_max,
    const TagFilter& filter) const
{
    BgBox query_box(BgPoint(lat_min, lng_min), BgPoint(lat_max, lng_max));
    std::vector<RTreeEntry> hits;

    if (filter.empty()) {
        // Reine Raumabfrage
        rtree_.query(bgi::intersects(query_box), std::back_inserter(hits));
    } else {
        // Inline-Filter: Tag-Prüfung WÄHREND der Traversierung
        rtree_.query(
            bgi::intersects(query_box) && bgi::satisfies([&](const RTreeEntry& e) {
                return poi_matches(*e.second, filter);
            }),
            std::back_inserter(hits));
    }

    std::vector<const POI*> result;
    result.reserve(hits.size());
    for (const auto& [pt, poi] : hits) result.push_back(poi);
    return result;
}

// Benchmark
BenchmarkResult run_benchmark(
    const LinearIndex&         linear,
    const GridIndex&           grid,
    const RTreeIndex&          rtree,
    double center_lat, double center_lng,
    double radius_m, int n_queries,
    const TagFilter& filter)
{
    constexpr double DEG_PER_M = 1.0 / 111000.0;
    double d_lat = radius_m * DEG_PER_M;
    double d_lng = radius_m * DEG_PER_M / std::cos(center_lat * M_PI / 180.0);

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> jitter_lat(-d_lat * 3, d_lat * 3);
    std::uniform_real_distribution<double> jitter_lng(-d_lng * 3, d_lng * 3);

    struct Center { double lat, lng; };
    std::vector<Center> centers;
    centers.reserve(n_queries);
    for (int i = 0; i < n_queries; ++i)
        centers.push_back({center_lat + jitter_lat(rng), center_lng + jitter_lng(rng)});

    using Clock = std::chrono::high_resolution_clock;

    // gibt {timing, gesamt gefundene POIs} zurück
    auto measure = [&](const auto& idx) -> std::pair<IndexTiming, size_t> {
        IndexTiming t;
        t.min_ms = std::numeric_limits<double>::max();
        size_t found = 0;
        for (const auto& c : centers) {
            auto t0 = Clock::now();
            auto res = idx.query_bbox(c.lat - d_lat, c.lng - d_lng,
                                      c.lat + d_lat, c.lng + d_lng,
                                      filter);
            double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            t.total_ms += ms;
            t.min_ms    = std::min(t.min_ms, ms);
            t.max_ms    = std::max(t.max_ms, ms);
            found      += res.size();
        }
        t.avg_ms = t.total_ms / n_queries;
        return {t, found};
    };

    BenchmarkResult result;
    result.n_queries  = n_queries;
    result.center_lat = center_lat;
    result.center_lng = center_lng;
    result.radius_m   = radius_m;

    auto [lin_t, lin_found] = measure(linear);
    result.timings["linear"]    = lin_t;
    result.timings["grid"]      = measure(grid).first;
    result.timings["rtree"]     = measure(rtree).first;

    // alle Indizes liefern dieselbe Treffermenge -> linear als Referenz
    result.avg_found = static_cast<double>(lin_found) / n_queries;
    return result;
}

}
