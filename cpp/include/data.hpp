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
};

struct Vehicle {
    std::string id;
    std::string donor_id;
    std::string type;
    int capacity_kg;
    double speed_kmph;
    double cost_per_km_inr;
};

struct Package {
    std::string id;
    std::string donor_id;
    std::string item;
    std::string category;  // VEG, NON_VEG or VEGAN
    int meals;             // informational only
    int weight_kg;         // quantity q_k used by the optimisation
    double expiry_hours;   // remaining shelf life e_k
};

struct Ngo {
    std::string id;
    std::string name;
    std::string type;
    std::string node_id;
    std::map<std::string, int> demand_kg;  // d_jf per category; 0 = category not accepted

    int demandFor(const std::string& category) const;
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
};

struct Dataset {
    std::vector<Node> nodes;
    std::vector<Road> roads;
    std::vector<Donor> donors;
    std::vector<Vehicle> vehicles;
    std::vector<Package> packages;
    std::vector<Ngo> ngos;
};

// Loads the six CSV files from a dataset folder such as "data/demo", then validates them.
// Throws std::runtime_error naming the file and line on any bad input.
Dataset loadDataset(const std::string& dir);

// Workflow step 2: checks the values the algorithms rely on (unique ids, valid references,
// supply > 0, demand >= 0, expiry >= 0, vehicle capacity and speed > 0).
// Throws std::runtime_error describing the first problem found.
void validateDataset(const Dataset& data);

// Loads a key,value CSV such as config/params.csv.
Params loadParams(const std::string& path);

}  // namespace foodloop
