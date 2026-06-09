#include "server.hpp"

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



        // POI API
        if (target.rfind("/pois", 0) == 0) {
            auto qpos = target.find('?');
            auto params = (qpos != std::string::npos)
                ? parse_query(target.substr(qpos + 1))
                : std::unordered_map<std::string, std::string>{};

            std::string category = params.count("category") ? params["category"] : "";
            std::string type     = params.count("type")     ? params["type"]     : "";

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
                    results = pois.query_bbox(a, b, c, d, category, type);
                } catch (...) {
                    return make_response(http::status::bad_request,
                        R"({"error":"Invalid bbox"})");
                }
            } else {
                return make_response(http::status::bad_request,
                    R"({"error":"bbox required"})");
            }

            json fc;
            fc["type"] = "FeatureCollection";
            fc["features"] = json::array();
            for (const auto* poi : results) {
                json f;
                f["type"] = "Feature";
                f["geometry"]["type"] = "Point";
                f["geometry"]["coordinates"] = {poi->coords.lng, poi->coords.lat};
                f["properties"]["id"]       = poi->id;
                f["properties"]["category"] = poi->category;
                f["properties"]["type"]     = poi->type;
                f["properties"]["name"]     = poi->name;
                fc["features"].push_back(f);
            }
            return make_response(http::status::ok, fc.dump());
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
                        const Router& router, const POICollection& pois) {
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

            auto response = handle_request(root, router, pois, std::move(req));
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
        : address_(address), port_(port), doc_root_(root), router_(router), pois_(pois) {}

    void Server::run() {
        net::io_context ioc{1};

        tcp::acceptor acceptor{ioc, {net::ip::make_address(address_), port_}};


        std::cout << "  Starting..." << std::endl;
        std::cout << "  Address: http://" << address_ << ":" << port_ << std::endl;

        while (true) {
            tcp::socket socket{ioc};
            acceptor.accept(socket);

            std::thread([s = std::move(socket), &root = doc_root_,
                        &router = router_, &pois = pois_]() mutable {
                session(std::move(s), root, router, pois);
            }).detach();
        }
    }

}
