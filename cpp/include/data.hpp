// Dataset records and the CSV loader (see docs/problem_formulation.md section 8).
#pragma once

#include <map>
#include <string>
#include <vector>

namespace foodloop {

// Food categories, in the order of the demand_*_kg columns in ngos.csv.
const std::vector<std::string>& categories();

struct Node {
    std::string id;
    double x_km;
    double y_km;
    std::string label;
};

struct Road {
    std::string u;
    std::string v;
    double distance_km;
};

struct Donor {
    std::string id;
    std::string name;
    std::string type;
    std::string node_id;
    double service_hours = -1;  // pickup service time; -1 = handling_hours / 2 (optional column)

    double pickupService(double handling) const { return service_hours < 0 ? handling / 2 : service_hours; }
};

// Fleet mode only: a depot or volunteer location where shared vehicles start and finish.
struct Depot {
    std::string id;
    std::string name;
    std::string type;  // e.g. "depot" or "volunteer"
    std::string node_id;
};

// Donor mode: donor_id is set and the vehicle belongs to that donor (one per donor).
// Fleet mode (depots.csv present): depot_id is set and the vehicle is shared by all donors.
struct Vehicle {
    std::string id;
    std::string donor_id;
    std::string type;
    int capacity_kg;
    double speed_kmph;
    double cost_per_km_inr;
    std::string depot_id = std::string();
    double fixed_cost_inr = -1;  // hiring cost if used; -1 = params.vehicle_fixed_cost_inr (optional column)

    double hiringCost(double fallback) const { return fixed_cost_inr < 0 ? fallback : fixed_cost_inr; }
};

struct Package {
    std::string id;
    std::string donor_id;
    std::string item;
    std::string category;  // VEG, NON_VEG or VEGAN
    int meals;             // informational only
    int weight_kg;         // quantity q_k used by the optimisation
    double expiry_hours;   // remaining shelf life e_k
    double ready_hours = 0;           // food-ready time: earliest pickup (optional column)
    double pickup_close_hours = -1;   // latest pickup; -1 = until expiry (optional column)

    double pickupClose() const { return pickup_close_hours < 0 ? expiry_hours : pickup_close_hours; }
};

struct Ngo {
    std::string id;
    std::string name;
    std::string type;
    std::string node_id;
    std::map<std::string, int> demand_kg;  // d_jf per category; 0 = category not accepted
    double open_hours = 0;     // delivery window: earliest service start (optional column)
    double close_hours = -1;   // latest service start; -1 = no limit (optional column)
    double service_hours = -1; // delivery service time; -1 = handling_hours / 2 (optional column)

    int demandFor(const std::string& category) const;
    bool hasClose() const { return close_hours >= 0; }
    double deliveryService(double handling) const { return service_hours < 0 ? handling / 2 : service_hours; }
};

// Values from config/params.csv.
struct Params {
    double handling_hours;                     // added to travel time in the expiry rule
    std::vector<double> tier_boundaries_hours; // e.g. {6, 24} -> tiers <6, 6-24, >=24
    int crate_kg;                              // allocations are split into crates of at most this size
    std::vector<int> tier_weights;             // Knapsack value per kg, one per tier (e.g. 3, 2, 1)
    double shift_hours;                        // every delivery must be served within the shift
    double low_load_pct;                       // trips below this utilisation are flagged
    double rate_inr_per_kg_km;                 // R in C_ij = D_ij * R (allocation cost per kg)
    double vehicle_fixed_cost_inr = 0;  // optional: hiring cost per vehicle used (base paper: 500)
    double unmet_penalty_inr = 0;       // optional: penalty per unmet unit of pickup or delivery (base paper: 1000)
};

struct Dataset {
    std::vector<Node> nodes;
    std::vector<Road> roads;
    std::vector<Donor> donors;
    std::vector<Vehicle> vehicles;
    std::vector<Package> packages;
    std::vector<Ngo> ngos;
    std::vector<Depot> depots;  // empty = donor mode

    // Fleet mode: shared vehicles start at depots (enhancements 1-2). Donor mode: one vehicle per donor.
    bool fleetMode() const { return !depots.empty(); }
};

// Loads the six CSV files (plus optional depots.csv) from a dataset folder such as "data/demo", then validates them.
// Throws std::runtime_error naming the file and line on any bad input.
Dataset loadDataset(const std::string& dir);

// Workflow step 2: checks the values the algorithms rely on (unique ids, valid references,
// supply > 0, demand >= 0, expiry >= 0, vehicle capacity and speed > 0).
// Throws std::runtime_error describing the first problem found.
void validateDataset(const Dataset& data);

// Loads a key,value CSV such as config/params.csv.
Params loadParams(const std::string& path);

}  // namespace foodloop
