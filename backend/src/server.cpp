#include "server.hpp"
#include "spatial_index.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace beast = boost::beast;
namespace http  = beast::http;
namespace net   = boost::asio;
using tcp       = net::ip::tcp;
using json      = nlohmann::json;

namespace routenplaner {


    static std::string mime_type(const std::string& path) {
        auto ext = path.substr(path.find_last_of('.') + 1);
        if (ext == "html" || ext == "htm") return "text/html";
        if (ext == "css")  return "text/css";
        if (ext == "js")   return "application/javascript";
        if (ext == "json") return "application/json";
        if (ext == "png")  return "image/png";
        if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
        if (ext == "svg")  return "image/svg+xml";
        if (ext == "ico")  return "image/x-icon";
        return "application/octet-stream";
    }

    static std::string read_file(const std::string& path) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) return "";
        std::ostringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    }

    // Anfrage parsen
    static std::unordered_map<std::string, std::string>
    parse_query(const std::string& query) {
        std::unordered_map<std::string, std::string> params;
        std::istringstream iss(query);
        std::string pair;
        while (std::getline(iss, pair, '&')) {
            auto eq = pair.find('=');
            if (eq != std::string::npos) {
                params[pair.substr(0, eq)] = pair.substr(eq + 1);
            }
        }
        return params;
    }

    // Request Handler
    template <class Body, class Allocator>
    static http::response<http::string_body>
    handle_request(
        const std::string& root,
        const Router& router,
        const POICollection& pois,
        const LinearIndex& linear_idx_,
        const GridIndex& grid_idx_,
        const RTreeIndex& rtree_idx_,
        http::request<Body, http::basic_fields<Allocator>>&& req)
    {
        // Response-Struktur
        auto make_response = [&](http::status status, const std::string& body,
                                const std::string& content_type = "application/json") {
            http::response<http::string_body> res{status, req.version()};
            res.set(http::field::server, "Routenplaner/0.1");
            res.set(http::field::content_type, content_type);
            // CORS headers for development
            res.set(http::field::access_control_allow_origin, "*");
            res.set(http::field::access_control_allow_methods, "GET, OPTIONS");
            res.set(http::field::access_control_allow_headers, "Content-Type");
            res.keep_alive(req.keep_alive());
            res.body() = body;
            res.prepare_payload();
            return res;
        };

        // CORS
        if (req.method() == http::verb::options) {
            return make_response(http::status::no_content, "");
        }


        std::string target = std::string(req.target());


        // Routing API
        if (target.rfind("/route?", 0) == 0 || target == "/route") {
            auto qpos = target.find('?');
            if (qpos == std::string::npos) {
                return make_response(http::status::bad_request,
                    R"({"error":"Missing parameters"})");
            }

            auto params = parse_query(target.substr(qpos + 1));

            if (params.find("from") == params.end() || params.find("to") == params.end()) {
                return make_response(http::status::bad_request,
                    R"({"error":"Missing start or end"})");
            }

            // Koordinaten parsen
            try {
                auto parse_coord = [](const std::string& s) -> std::pair<double, double> {
                    auto comma = s.find(',');
                    if (comma == std::string::npos) throw std::invalid_argument("no comma");
                    return {std::stod(s.substr(0, comma)), std::stod(s.substr(comma + 1))};
                };

                auto [from_lat, from_lng] = parse_coord(params["from"]);
                auto [to_lat, to_lng] = parse_coord(params["to"]);

                std::cout << "[API] Route request: "
                        << from_lat << "," << from_lng << " -> "
                        << to_lat << "," << to_lng << std::endl;

                auto result = router.route(from_lat, from_lng, to_lat, to_lng);

                // Response bauen
                json response;
                response["success"] = result.found;

                if (result.found) {
                    response["distance_m"] = result.distance;
                    response["distance_km"] = std::round(result.distance / 10.0) / 100.0;

                    // LineString
                    json coordinates = json::array();
                    for (const auto& pt : result.geometry) {
                        coordinates.push_back({pt.lng, pt.lat});
                    }

                    json geometry;
                    geometry["type"] = "LineString";
                    geometry["coordinates"] = coordinates;

                    json properties;
                    properties["distance_m"] = result.distance;
                    properties["node_count"] = result.node_path.size();

                    json route;
                    route["type"] = "Feature";
                    route["geometry"] = geometry;
                    route["properties"] = properties;

                    response["route"] = route;
                } else {
                    response["error"] = "No route found";
                }

                return make_response(http::status::ok, response.dump());

            } catch (const std::exception& e) {
                json err;
                err["error"] = std::string("Invalid parameters: ") + e.what();
                return make_response(http::status::bad_request, err.dump());
            }
        }



        // Sequenced Route API (iterative Verdopplung)
        if (target.rfind("/sequenced?", 0) == 0 || target == "/sequenced") {
            auto qpos = target.find('?');
            if (qpos == std::string::npos) {
                return make_response(http::status::bad_request,
                    R"({"error":"Missing parameters"})");
            }
            auto params = parse_query(target.substr(qpos + 1));

            if (!params.count("from") || !params.count("to") || !params.count("seq")) {
                return make_response(http::status::bad_request,
                    R"({"error":"Missing from, to or seq"})");
            }

            try {
                auto parse_coord = [](const std::string& s) -> std::pair<double, double> {
                    auto comma = s.find(',');
                    if (comma == std::string::npos) throw std::invalid_argument("no comma");
                    return {std::stod(s.substr(0, comma)), std::stod(s.substr(comma + 1))};
                };
                auto [from_lat, from_lng] = parse_coord(params["from"]);
                auto [to_lat, to_lng]     = parse_coord(params["to"]);

                double d_start  = params.count("d_start")  ? std::stod(params["d_start"])  : 2000.0;
                double d_factor = params.count("d_factor") ? std::stod(params["d_factor"]) : 2.0;
                bool   explore  = params.count("explore") && params["explore"] == "1";

                // seq = "key=value;key=value;..." -> Kategorien (Tag-Filter) in Reihenfolge
                std::vector<Category> seq;
                const std::string& s = params["seq"];
                size_t start = 0;
                bool unknown = false;
                while (start <= s.size()) {
                    size_t semi = s.find(';', start);
                    std::string item = (semi == std::string::npos)
                        ? s.substr(start) : s.substr(start, semi - start);
                    if (!item.empty()) {
                        auto eq = item.find('=');
                        if (eq == std::string::npos) {
                            unknown = true;
                        } else {
                            uint32_t kid = pois.dict().id_of(item.substr(0, eq));
                            uint32_t vid = pois.dict().id_of(item.substr(eq + 1));
                            if (kid == TagDict::INVALID || vid == TagDict::INVALID) {
                                unknown = true;
                            } else {
                                Category c;
                                c.push_back({kid, vid});
                                seq.push_back(std::move(c));
                            }
                        }
                    }
                    if (semi == std::string::npos) break;
                    start = semi + 1;
                }

                if (unknown || seq.empty()) {
                    return make_response(http::status::ok,
                        R"({"success":false,"error":"Unknown or empty category in seq"})");
                }

                std::cout << "[API] Sequenced request: " << from_lat << "," << from_lng
                          << " -> " << to_lat << "," << to_lng
                          << " seq=" << s << " d_start=" << d_start
                          << " d_factor=" << d_factor << std::endl;

                auto res = router.sequenced_route(from_lat, from_lng, to_lat, to_lng,
                                                  seq, grid_idx_, d_start, d_factor, explore);

                std::cout << "[API] Sequenced "
                          << (res.found ? "found" : "not found")
                          << " in " << res.query_time_ms << " ms, "
                          << res.rounds_used << " rounds" << std::endl;

                const TagDict& dict = pois.dict();
                json response;
                response["success"]       = res.found;
                response["rounds_used"]   = res.rounds_used;
                response["query_time_ms"] = res.query_time_ms;

                json rounds = json::array();
                for (const auto& r : res.rounds) {
                    rounds.push_back({
                        {"cap", r.cap},
                        {"radius_m", r.bbox_radius_m},
                        {"reached", r.reached_target},
                        {"total", r.total_time},
                        {"time_ms", r.time_ms},
                        {"settled", r.settled}
                    });
                }
                response["rounds"] = std::move(rounds);

                if (res.found) {
                    response["total_distance_m"]  = res.total_time;
                    response["total_distance_km"] = std::round(res.total_time / 10.0) / 100.0;

                    // Segmente als FeatureCollection (ein LineString je Abschnitt)
                    json fc;
                    fc["type"]     = "FeatureCollection";
                    fc["features"] = json::array();
                    for (size_t i = 0; i < res.segments.size(); ++i) {
                        const auto& seg = res.segments[i];
                        json coords = json::array();
                        for (const auto& pt : seg.geometry)
                            coords.push_back({pt.lng, pt.lat});

                        json props;
                        props["leg"]    = i;
                        props["weight"] = seg.weight;
                        if (seg.facility) {
                            json tags = json::object();
                            for (const auto& [k, v] : seg.facility->tags)
                                tags[dict.str(k)] = dict.str(v);
                            props["facility_id"]   = seg.facility->id;
                            props["facility_tags"] = std::move(tags);
                        }

                        json feat;
                        feat["type"]                 = "Feature";
                        feat["geometry"]["type"]     = "LineString";
                        feat["geometry"]["coordinates"] = std::move(coords);
                        feat["properties"]           = std::move(props);
                        fc["features"].push_back(std::move(feat));
                    }
                    response["route"] = std::move(fc);

                    // Gewaehlte Facilities (fuer Marker)
                    json chosen = json::array();
                    for (const POI* p : res.chosen) {
                        if (!p) continue;
                        json tags = json::object();
                        for (const auto& [k, v] : p->tags)
                            tags[dict.str(k)] = dict.str(v);
                        chosen.push_back({
                            {"id", p->id},
                            {"coordinates", {p->coords.lng, p->coords.lat}},
                            {"tags", std::move(tags)}
                        });
                    }
                    response["chosen"] = std::move(chosen);

                    // Optionale Suchbaeume je gewaehltem POI (MultiLineString)
                    if (!res.exploration.empty()) {
                        uint32_t name_key = dict.id_of("name");
                        json expl = json::array();
                        for (size_t j = 0; j < res.exploration.size(); ++j) {
                            json mls = json::array();
                            for (const auto& edge : res.exploration[j].edges) {
                                json line = json::array();
                                for (const auto& pt : edge)
                                    line.push_back({pt.lng, pt.lat});
                                mls.push_back(std::move(line));
                            }

                            // Mitbewerber-Facilities derselben Kategorie
                            json cands = json::array();
                            for (const POI* p : res.exploration[j].candidates) {
                                std::string nm;
                                if (name_key != TagDict::INVALID)
                                    for (const auto& [k, v] : p->tags)
                                        if (k == name_key) { nm = dict.str(v); break; }
                                cands.push_back({
                                    {"coordinates", {p->coords.lng, p->coords.lat}},
                                    {"name", nm}
                                });
                            }

                            json feat;
                            feat["type"]                     = "Feature";
                            feat["geometry"]["type"]         = "MultiLineString";
                            feat["geometry"]["coordinates"]  = std::move(mls);
                            feat["properties"]["poi_index"]  = j;
                            feat["properties"]["candidates"] = std::move(cands);
                            expl.push_back(std::move(feat));
                        }
                        response["exploration"] = std::move(expl);
                    }
                } else {
                    response["error"] = "No route found";
                }

                return make_response(http::status::ok, response.dump());

            } catch (const std::exception& e) {
                json err;
                err["error"] = std::string("Invalid parameters: ") + e.what();
                return make_response(http::status::bad_request, err.dump());
            }
        }

        // POI API
        if (target.rfind("/pois", 0) == 0) {
            auto qpos = target.find('?');
            auto params = (qpos != std::string::npos)
                ? parse_query(target.substr(qpos + 1))
                : std::unordered_map<std::string, std::string>{};

            std::string key   = params.count("key")   ? params["key"]   : "";
            std::string value = params.count("value") ? params["value"] : "";

            // key=value -> Tag-Filter (IDs). Unbekannter Key/Wert -> keine Treffer.
            TagFilter filter;
            bool impossible = false;
            if (!key.empty() && !value.empty()) {
                uint32_t kid = pois.dict().id_of(key);
                uint32_t vid = pois.dict().id_of(value);
                if (kid == TagDict::INVALID || vid == TagDict::INVALID) impossible = true;
                else filter.push_back({kid, vid});
            }

            std::vector<const POI*> results;

            if (params.count("bbox")) {
                try {
                    auto [a, b, c, d] = [&]() {
                        auto s = params["bbox"];
                        auto p1 = s.find(',');
                        auto p2 = s.find(',', p1 + 1);
                        auto p3 = s.find(',', p2 + 1);
                        return std::make_tuple(
                            std::stod(s.substr(0, p1)),
                            std::stod(s.substr(p1 + 1, p2 - p1 - 1)),
                            std::stod(s.substr(p2 + 1, p3 - p2 - 1)),
                            std::stod(s.substr(p3 + 1)));
                    }();
                    if (!impossible) results = pois.query_bbox(a, b, c, d, filter);
                } catch (...) {
                    return make_response(http::status::bad_request,
                        R"({"error":"Invalid bbox"})");
                }
            } else {
                return make_response(http::status::bad_request,
                    R"({"error":"bbox required"})");
            }

            const TagDict& dict = pois.dict();
            json fc;
            fc["type"] = "FeatureCollection";
            fc["features"] = json::array();
            for (const auto* poi : results) {
                json f;
                f["type"] = "Feature";
                f["geometry"]["type"] = "Point";
                f["geometry"]["coordinates"] = {poi->coords.lng, poi->coords.lat};
                f["properties"]["id"] = poi->id;
                json tagsj = json::object();
                for (const auto& [k, v] : poi->tags)
                    tagsj[dict.str(k)] = dict.str(v);
                f["properties"]["tags"] = std::move(tagsj);
                fc["features"].push_back(f);
            }
            return make_response(http::status::ok, fc.dump());
        }

        if (target.rfind("/benchmark", 0) == 0) {
            auto qpos = target.find('?');
            auto params = (qpos != std::string::npos)
                ? parse_query(target.substr(qpos + 1))
                : std::unordered_map<std::string, std::string>{};

            try {
                double center_lat = std::stod(params.count("lat") ? params["lat"] : "48.775");
                double center_lng = std::stod(params.count("lng") ? params["lng"] : "9.182");
                double radius_m   = std::stod(params.count("radius") ? params["radius"] : "1000");
                int    n_queries  = std::stoi(params.count("n") ? params["n"] : "50");
                n_queries = std::max(1, std::min(500, n_queries));

                std::string key   = params.count("key")   ? params["key"]   : "";
                std::string value = params.count("value") ? params["value"] : "";

                TagFilter filter;
                if (!key.empty() && !value.empty()) {
                    uint32_t kid = pois.dict().id_of(key);
                    uint32_t vid = pois.dict().id_of(value);
                    if (kid != TagDict::INVALID && vid != TagDict::INVALID)
                        filter.push_back({kid, vid});
                }

                auto bench = run_benchmark(
                    linear_idx_, grid_idx_, rtree_idx_,
                    center_lat, center_lng, radius_m, n_queries, filter);

                // Gesamtzahlen aus dem Datensatz ergänzen
                bench.total_pois = pois.size();
                if (!filter.empty()) {
                    for (const auto& poi : pois.all())
                        if (poi_matches(poi, filter)) ++bench.filter_pois;
                }

                json res;
                res["n_queries"]     = bench.n_queries;
                res["center_lat"]    = bench.center_lat;
                res["center_lng"]    = bench.center_lng;
                res["radius_m"]      = bench.radius_m;
                res["avg_found"]     = bench.avg_found;
                res["total_pois"]    = bench.total_pois;
                res["filter_pois"]   = bench.filter_pois;
                res["timings"]       = json::object();
                for (const auto& [name, t] : bench.timings) {
                    res["timings"][name] = {
                        {"avg_ms",   t.avg_ms},
                        {"min_ms",   t.min_ms},
                        {"max_ms",   t.max_ms},
                        {"total_ms", t.total_ms}
                    };
                }
                return make_response(http::status::ok, res.dump());

            } catch (const std::exception& e) {
                json err;
                err["error"] = std::string("Invalid parameters: ") + e.what();
                return make_response(http::status::bad_request, err.dump());
            }
        }

        if (target == "/stats") {
            auto key_stats = pois.key_stats();
            json result;
            result["total"] = pois.size();
            result["keys"] = json::object();
            for (const auto& [key, count] : key_stats) {
                result["keys"][key] = count;
            }
            return make_response(http::status::ok, result.dump());
        }

        if (target == "/info") {
            json info;
            info["name"] = "Routenplaner";
            info["version"] = "0.1.0";
            info["license"] = "Apache-2.0";
            return make_response(http::status::ok, info.dump());
        }



        if (target == "/") target = "/index.html";

        std::string filepath = root + target;
        std::string body = read_file(filepath);



        if (body.empty()) {
            return make_response(http::status::not_found,
                R"({"error":"Not found"})", "application/json");
        }



        return make_response(http::status::ok, body, mime_type(filepath));
    }



    static void session(tcp::socket socket, const std::string& root,
                        const Router& router, const POICollection& pois,
                        const LinearIndex& linear_idx, const GridIndex& grid_idx,
                        const RTreeIndex& rtree_idx) {
        beast::error_code ec;
        beast::flat_buffer buffer;

        while (true) {
            http::request<http::string_body> req;
            http::read(socket, buffer, req, ec);

            if (ec == http::error::end_of_stream) break;
            if (ec) {
                std::cerr << "[session] read error: " << ec.message() << std::endl;
                break;
            }

            auto response = handle_request(root, router, pois,
                linear_idx, grid_idx, rtree_idx, std::move(req));
            http::write(socket, response, ec);

            if (ec) {
                std::cerr << "[session] write error: " << ec.message() << std::endl;
                break;
            }

            if (!response.keep_alive()) break;
        }

        socket.shutdown(tcp::socket::shutdown_send, ec);
    }

    // Server
    Server::Server(const std::string& address, uint16_t port,
                const std::string& root, const Router& router,
                const POICollection& pois)
        : address_(address), port_(port), doc_root_(root), router_(router), pois_(pois),
          linear_idx_(pois.all()),
          grid_idx_(pois.all()),
          rtree_idx_(pois.all())
    {
        std::cout << "Spatial indices built (grid: "
                  << grid_idx_.cells_per_side() << "x" << grid_idx_.cells_per_side()
                  << ")" << std::endl;
    }

    void Server::run() {
        net::io_context ioc{1};

        tcp::acceptor acceptor{ioc, {net::ip::make_address(address_), port_}};


        std::cout << "  Starting..." << std::endl;
        std::cout << "  Address: http://" << address_ << ":" << port_ << std::endl;

        while (true) {
            tcp::socket socket{ioc};
            acceptor.accept(socket);

            std::thread([s = std::move(socket), &root = doc_root_,
                        &router = router_, &pois = pois_,
                        &linear = linear_idx_, &grid = grid_idx_,
                        &rtree = rtree_idx_]() mutable {
                session(std::move(s), root, router, pois, linear, grid, rtree);
            }).detach();
        }
    }

}
