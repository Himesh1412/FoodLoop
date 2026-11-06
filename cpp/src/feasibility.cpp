#include "feasibility.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace foodloop {

std::vector<int> vehicleOfDonor(const Dataset& data) {
    std::vector<int> result(data.donors.size(), -1);
    for (size_t v = 0; v < data.vehicles.size(); ++v) {
        bool found = false;
        for (size_t d = 0; d < data.donors.size(); ++d) {
            if (data.donors[d].id != data.vehicles[v].donor_id) continue;
            if (result[d] != -1) throw std::runtime_error("donor " + data.donors[d].id + " has more than one vehicle");
            result[d] = static_cast<int>(v);
            found = true;
        }
        if (!found) throw std::runtime_error("vehicle " + data.vehicles[v].id + " belongs to unknown donor");
    }
    for (size_t d = 0; d < data.donors.size(); ++d)
        if (result[d] == -1) throw std::runtime_error("donor " + data.donors[d].id + " has no vehicle");
    return result;
}

Feasibility computeFeasibility(const Dataset& data, const DistanceMatrix& m, const Params& params) {
    std::vector<int> vehicle;
    double fleetSpeed = 0;
    if (data.fleetMode()) {
        for (const Vehicle& v : data.vehicles) fleetSpeed = std::max(fleetSpeed, v.speed_kmph);
    } else {
        vehicle = vehicleOfDonor(data);
    }
    std::map<std::string, int> donorIndex;
    for (size_t d = 0; d < data.donors.size(); ++d) donorIndex[data.donors[d].id] = static_cast<int>(d);

    Feasibility f;
    f.byLot.resize(data.packages.size());

    for (size_t k = 0; k < data.packages.size(); ++k) {
        const Package& lot = data.packages[k];
        std::map<std::string, int>::const_iterator di = donorIndex.find(lot.donor_id);
        if (di == donorIndex.end()) throw std::runtime_error("lot " + lot.id + " belongs to unknown donor");
        const int d = di->second;
        const double speed = data.fleetMode() ? fleetSpeed : data.vehicles[vehicle[d]].speed_kmph;
        const bool pickupOpen = lot.ready_hours <= lot.pickupClose() + 1e-9;
        if (lot.expiry_hours <= 0) ++f.expiredLots;  // the expiry rule below removes all its pairs

        for (size_t j = 0; j < data.ngos.size(); ++j) {
            if (data.ngos[j].demandFor(lot.category) <= 0) {
                ++f.removedIncompatible;
                continue;
            }
            const double km = m.km[d][j];
            if (km == INF) {
                ++f.removedUnreachable;
                continue;
            }
            const double hours = km / speed;
            // Handling is split evenly between pickup and delivery service.
            const Ngo& ngo = data.ngos[j];
            const double serviceStart =
                std::max(lot.ready_hours + data.donors[d].pickupService(params.handling_hours) + hours, ngo.open_hours);
            const double servedBy = serviceStart + ngo.deliveryService(params.handling_hours);
            // 1e-9 tolerance so an exact tie (e.g. 1.0 + 0.5 vs 1.5) is not lost to rounding.
            if (servedBy > lot.expiry_hours + 1e-9) {
                ++f.removedExpiry;
                continue;
            }
            if (!pickupOpen || (ngo.hasClose() && serviceStart > ngo.close_hours + 1e-9)) {
                ++f.removedWindow;
                continue;
            }

            LotRoute r;
            r.ngo = static_cast<int>(j);
            r.km = km;
            r.travel_hours = hours;
            // C_ij = D_ij * R. The vehicle's own Rs/km rate is a per-trip cost, used only in Stage 5.
            r.cost_paise_per_kg = static_cast<long long>(std::floor(km * params.rate_inr_per_kg_km * 100.0 + 0.5));
            f.byLot[k].push_back(r);
            ++f.keptPairs;
        }
    }
    return f;
}

}  // namespace foodloop
