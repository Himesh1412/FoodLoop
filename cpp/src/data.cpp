#include "data.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace foodloop {
namespace {

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    std::string::size_type b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    std::string::size_type e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::istringstream in(s);
    std::string cell;
    while (std::getline(in, cell, sep)) parts.push_back(trim(cell));
    if (!s.empty() && s[s.size() - 1] == sep) parts.push_back("");  // getline drops a trailing empty cell
    return parts;
}

// One data row of a CSV file, looked up by column name.
struct CsvRow {
    std::map<std::string, std::string> cells;
    std::string where;  // "file line N", used in error messages

    bool has(const std::string& col) const { return cells.count(col) > 0; }

    double numberOr(const std::string& col, double fallback) const {
        return has(col) && !text(col).empty() ? number(col) : fallback;
    }

    const std::string& text(const std::string& col) const {
        std::map<std::string, std::string>::const_iterator it = cells.find(col);
        if (it == cells.end()) throw std::runtime_error(where + ": missing column '" + col + "'");
        return it->second;
    }

    double number(const std::string& col) const {
        const std::string& s = text(col);
        char* end = 0;
        double v = std::strtod(s.c_str(), &end);
        if (s.empty() || *end != '\0') throw std::runtime_error(where + ": '" + col + "' is not a number: " + s);
        return v;
    }

    int integer(const std::string& col) const {
        const std::string& s = text(col);
        char* end = 0;
        long v = std::strtol(s.c_str(), &end, 10);
        if (s.empty() || *end != '\0') throw std::runtime_error(where + ": '" + col + "' is not an integer: " + s);
        return static_cast<int>(v);
    }
};

std::vector<CsvRow> readCsv(const std::string& path) {
    std::ifstream in(path.c_str());
    if (!in) throw std::runtime_error("cannot open " + path);

    std::string line;
    if (!std::getline(in, line)) throw std::runtime_error(path + " is empty");
    if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);  // UTF-8 BOM added by Excel
    const std::vector<std::string> header = split(trim(line), ',');

    std::vector<CsvRow> rows;
    for (int lineNo = 2; std::getline(in, line); ++lineNo) {
        line = trim(line);
        if (line.empty()) continue;

        std::ostringstream where;
        where << path << " line " << lineNo;
        const std::vector<std::string> cells = split(line, ',');
        if (cells.size() != header.size()) {
            std::ostringstream msg;
            msg << where.str() << ": expected " << header.size() << " cells, found " << cells.size();
            throw std::runtime_error(msg.str());
        }

        CsvRow row;
        row.where = where.str();
        for (size_t i = 0; i < header.size(); ++i) row.cells[header[i]] = cells[i];
        rows.push_back(row);
    }
    return rows;
}

template <class T>
std::set<std::string> uniqueIds(const std::vector<T>& rows, std::string T::*id, const std::string& what) {
    std::set<std::string> ids;
    for (const T& r : rows) {
        if ((r.*id).empty()) throw std::runtime_error(what + " with an empty id");
        if (!ids.insert(r.*id).second) throw std::runtime_error("duplicate " + what + " id '" + r.*id + "'");
    }
    return ids;
}

void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

double parseNumber(const std::string& s, const std::string& where) {
    char* end = 0;
    double v = std::strtod(s.c_str(), &end);
    if (s.empty() || *end != '\0') throw std::runtime_error(where + ": not a number: " + s);
    return v;
}

}  // namespace

const std::vector<std::string>& categories() {
    static const std::vector<std::string> cats = {"VEG", "NON_VEG", "VEGAN"};
    return cats;
}

int Ngo::demandFor(const std::string& category) const {
    std::map<std::string, int>::const_iterator it = demand_kg.find(category);
    return it == demand_kg.end() ? 0 : it->second;
}

void validateDataset(const Dataset& d) {
    const std::set<std::string> nodes = uniqueIds(d.nodes, &Node::id, "junction");
    const std::set<std::string> donors = uniqueIds(d.donors, &Donor::id, "donor");
    uniqueIds(d.ngos, &Ngo::id, "NGO");
    uniqueIds(d.vehicles, &Vehicle::id, "vehicle");
    uniqueIds(d.packages, &Package::id, "lot");
    const std::set<std::string> depots = uniqueIds(d.depots, &Depot::id, "depot");
    for (const Depot& x : d.depots) require(nodes.count(x.node_id) > 0, "depot " + x.id + " is at an unknown junction");

    for (const Road& r : d.roads) {
        require(nodes.count(r.u) && nodes.count(r.v), "road " + r.u + "-" + r.v + " uses an unknown junction");
        require(r.distance_km >= 0, "road " + r.u + "-" + r.v + " has negative length");
    }
    for (const Donor& x : d.donors) require(nodes.count(x.node_id) > 0, "donor " + x.id + " is at an unknown junction");
    for (const Ngo& n : d.ngos) {
        require(nodes.count(n.node_id) > 0, "NGO " + n.id + " is at an unknown junction");
        for (const std::string& cat : categories())
            require(n.demandFor(cat) >= 0, "NGO " + n.id + " has negative " + cat + " demand");
    }
    for (const Vehicle& v : d.vehicles) {
        if (d.fleetMode()) require(depots.count(v.depot_id) > 0, "vehicle " + v.id + " starts at an unknown depot");
        else require(donors.count(v.donor_id) > 0, "vehicle " + v.id + " belongs to an unknown donor");
        require(v.capacity_kg > 0, "vehicle " + v.id + " must have capacity > 0");
        require(v.speed_kmph > 0, "vehicle " + v.id + " must have speed > 0");
        require(v.cost_per_km_inr >= 0, "vehicle " + v.id + " has a negative cost per km");
    }
    for (const Package& p : d.packages) {
        require(donors.count(p.donor_id) > 0, "lot " + p.id + " belongs to an unknown donor");
        require(p.weight_kg > 0, "lot " + p.id + " must have weight_kg > 0");
        require(p.meals >= 0, "lot " + p.id + " has negative meals");
        require(p.expiry_hours >= 0, "lot " + p.id + " has negative expiry_hours (use 0 for expired food)");
        require(p.ready_hours >= 0, "lot " + p.id + " has negative ready_hours");
        require(p.pickupClose() >= p.ready_hours, "lot " + p.id + " closes for pickup before it is ready");
    }
    for (const Ngo& n : d.ngos) {
        require(n.open_hours >= 0, "NGO " + n.id + " has negative open_hours");
        require(!n.hasClose() || n.close_hours >= n.open_hours, "NGO " + n.id + " closes before it opens");
    }
    if (d.fleetMode()) require(!d.vehicles.empty(), "fleet mode needs at least one vehicle");
}

Params loadParams(const std::string& path) {
    std::map<std::string, std::string> values;
    for (const CsvRow& r : readCsv(path)) values[r.text("key")] = r.text("value");

    const char* keys[] = {"handling_hours", "tier_boundaries_hours", "crate_kg",
                          "tier_weights",   "shift_hours",           "low_load_pct",
                          "rate_inr_per_kg_km"};
    for (const char* k : keys)
        if (values.find(k) == values.end()) throw std::runtime_error(path + ": missing key '" + k + "'");

    Params p;
    p.handling_hours = parseNumber(values["handling_hours"], path + " handling_hours");
    for (const std::string& b : split(values["tier_boundaries_hours"], ';'))
        p.tier_boundaries_hours.push_back(parseNumber(b, path + " tier_boundaries_hours"));
    for (size_t i = 1; i < p.tier_boundaries_hours.size(); ++i)
        if (p.tier_boundaries_hours[i] <= p.tier_boundaries_hours[i - 1])
            throw std::runtime_error(path + ": tier_boundaries_hours must be increasing");

    const double crate = parseNumber(values["crate_kg"], path + " crate_kg");
    if (crate < 1 || crate != std::floor(crate)) throw std::runtime_error(path + ": crate_kg must be a whole number >= 1");
    p.crate_kg = static_cast<int>(crate);

    for (const std::string& w : split(values["tier_weights"], ';')) {
        const double weight = parseNumber(w, path + " tier_weights");
        if (weight < 1 || weight != std::floor(weight))
            throw std::runtime_error(path + ": tier_weights must be whole numbers >= 1");
        p.tier_weights.push_back(static_cast<int>(weight));
    }
    if (p.tier_weights.size() != p.tier_boundaries_hours.size() + 1)
        throw std::runtime_error(path + ": tier_weights needs one value per tier");

    p.shift_hours = parseNumber(values["shift_hours"], path + " shift_hours");
    p.low_load_pct = parseNumber(values["low_load_pct"], path + " low_load_pct");
    p.rate_inr_per_kg_km = parseNumber(values["rate_inr_per_kg_km"], path + " rate_inr_per_kg_km");
    if (values.count("vehicle_fixed_cost_inr"))
        p.vehicle_fixed_cost_inr = parseNumber(values["vehicle_fixed_cost_inr"], path + " vehicle_fixed_cost_inr");
    if (values.count("time_gap_weight"))
        p.time_gap_weight = parseNumber(values["time_gap_weight"], path + " time_gap_weight");
    if (values.count("unmet_penalty_inr"))
        p.unmet_penalty_inr = parseNumber(values["unmet_penalty_inr"], path + " unmet_penalty_inr");
    if (p.rate_inr_per_kg_km < 0) throw std::runtime_error(path + ": rate_inr_per_kg_km must be >= 0");
    if (p.handling_hours < 0) throw std::runtime_error(path + ": handling_hours must be >= 0");
    return p;
}

Dataset loadDataset(const std::string& dir) {
    Dataset d;

    for (const CsvRow& r : readCsv(dir + "/nodes.csv")) {
        Node n;
        n.id = r.text("node_id");
        n.x_km = r.number("x_km");
        n.y_km = r.number("y_km");
        n.label = r.text("label");
        d.nodes.push_back(n);
    }

    for (const CsvRow& r : readCsv(dir + "/roads.csv")) {
        Road road;
        road.u = r.text("u");
        road.v = r.text("v");
        road.distance_km = r.number("distance_km");
        d.roads.push_back(road);
    }

    for (const CsvRow& r : readCsv(dir + "/donors.csv")) {
        Donor donor;
        donor.id = r.text("donor_id");
        donor.name = r.text("name");
        donor.type = r.text("type");
        donor.node_id = r.text("node_id");
        donor.service_hours = r.numberOr("service_hours", -1);
        d.donors.push_back(donor);
    }

    for (const CsvRow& r : readCsv(dir + "/vehicles.csv")) {
        Vehicle v;
        v.id = r.text("vehicle_id");
        v.donor_id = r.has("donor_id") ? r.text("donor_id") : "";
        v.depot_id = r.has("depot_id") ? r.text("depot_id") : "";
        v.fixed_cost_inr = r.numberOr("fixed_cost_inr", -1);
        v.type = r.text("vehicle_type");
        v.capacity_kg = r.integer("capacity_kg");
        v.speed_kmph = r.number("speed_kmph");
        v.cost_per_km_inr = r.number("cost_per_km_inr");
        d.vehicles.push_back(v);
    }

    for (const CsvRow& r : readCsv(dir + "/packages.csv")) {
        Package p;
        p.id = r.text("package_id");
        p.donor_id = r.text("donor_id");
        p.item = r.text("item");
        p.category = r.text("category");
        if (std::find(categories().begin(), categories().end(), p.category) == categories().end())
            throw std::runtime_error(r.where + ": unknown category '" + p.category + "'");
        p.meals = r.integer("meals");
        p.weight_kg = r.integer("weight_kg");
        p.expiry_hours = r.number("expiry_hours");
        p.ready_hours = r.numberOr("ready_hours", 0);
        p.pickup_close_hours = r.numberOr("pickup_close_hours", -1);
        d.packages.push_back(p);
    }

    for (const CsvRow& r : readCsv(dir + "/ngos.csv")) {
        Ngo n;
        n.id = r.text("ngo_id");
        n.name = r.text("name");
        n.type = r.text("type");
        n.node_id = r.text("node_id");
        for (const std::string& cat : categories()) {
            std::string col = "demand_" + cat + "_kg";  // e.g. demand_non_veg_kg
            for (char& c : col) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            n.demand_kg[cat] = r.integer(col);
        }
        n.open_hours = r.numberOr("open_hours", 0);
        n.close_hours = r.numberOr("close_hours", -1);
        n.service_hours = r.numberOr("service_hours", -1);
        d.ngos.push_back(n);
    }

    std::ifstream depotsFile((dir + "/depots.csv").c_str());
    if (depotsFile) {
        depotsFile.close();
        for (const CsvRow& r : readCsv(dir + "/depots.csv")) {
            Depot x;
            x.id = r.text("depot_id");
            x.name = r.text("name");
            x.type = r.text("type");
            x.node_id = r.text("node_id");
            d.depots.push_back(x);
        }
    }

    validateDataset(d);
    return d;
}

}  // namespace foodloop
