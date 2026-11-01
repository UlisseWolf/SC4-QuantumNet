////////////////////////////////////////////////////////////////////////
//
// SC4-QuantumNet - DLL port of the QUANTUMNET Lua engine.
// Based on the NEON ENGINE by RobertaME / Ulisse Wolf.
//
////////////////////////////////////////////////////////////////////////

#include "QuantumNetSimulation.h"
#include <algorithm>

namespace QuantumNet
{
	Config Config::Defaults()
	{
		Config cfg;

		// Former PACK_CONSTANTS.lua: qnet_rating + quantumnet_building_costs_monthly.
		cfg.producers = {
			{ 0xBB340000, 400.0,  900.0,  "QuantumNet Data Hub Small", "" },
			{ 0xBB340001, 1600.0, 3600.0, "QuantumNet Data Hub Large", "" },
		};

		// Former PACK_CONSTANTS.lua: qnet_demand_rating. Only the Fusion Power
		// Plant had a real ID; the others are added through SC4QuantumNet.ini.
		cfg.consumers = {
			{ 0x78A0F71A, 500.0, 0.0, "Fusion Power Plant", "Fusion" },
		};

		cfg.hubSmallLockedText = "QuantumNet Data Hub (Small): requires 8,000 Sims and an average EQ of 300.";
		cfg.hubLargeLockedText = "QuantumNet Data Hub (Large): requires 30,000 Sims, an average EQ of 600 and 2,000 R$$$ Sims.";

		return cfg;
	}

	static uint32_t CountOf(const CityInputs& in, uint32_t id)
	{
		if (id == 0)
		{
			return 0; // equivalent of the Lua "XXXXXXXX" placeholder
		}

		auto it = in.buildingCounts.find(id);
		return it != in.buildingCounts.end() ? it->second : 0;
	}

	uint32_t SumBuildingCounts(const std::vector<BuildingEntry>& table, const CityInputs& in)
	{
		uint32_t total = 0;
		for (const BuildingEntry& e : table)
		{
			total += CountOf(in, e.buildingID);
		}
		return total;
	}

	double SumBuildingCapacity(const std::vector<BuildingEntry>& table, const CityInputs& in)
	{
		double total = 0;
		for (const BuildingEntry& e : table)
		{
			total += CountOf(in, e.buildingID) * e.rating;
		}
		return total;
	}

	double SumBuildingCost(const std::vector<BuildingEntry>& table, const CityInputs& in)
	{
		double total = 0;
		for (const BuildingEntry& e : table)
		{
			total += CountOf(in, e.buildingID) * e.monthlyCost;
		}
		return total;
	}

	double SumConsumerDemand(const std::vector<BuildingEntry>& consumers, const CityInputs& in,
	                         std::map<std::string, double>& byClass)
	{
		byClass.clear();
		double total = 0;
		for (const BuildingEntry& e : consumers)
		{
			const double demand = CountOf(in, e.buildingID) * e.rating;
			byClass[e.consumerClass.empty() ? e.name : e.consumerClass] += demand;
			total += demand;
		}
		return total;
	}

	void RetractRegionalContribution(const Config& cfg, IRegionalSupply* region, GridState& s)
	{
		if (region && region->IsAvailable())
		{
			if (s.prevSupplyReported > 0)
			{
				region->RemoveFromSupply(cfg.resourceID, s.prevSupplyReported);
			}
			if (s.prevDemandReported > 0)
			{
				region->RemoveFromDemand(cfg.resourceID, s.prevDemandReported);
			}
		}
		s.prevSupplyReported = 0;
		s.prevDemandReported = 0;
	}

	void RunBalance(const Config& cfg, const CityInputs& in, IRegionalSupply* region, GridState& s)
	{
		const Ratios& r = cfg.ratios;

		s.producerCount = SumBuildingCounts(cfg.producers, in);

		// Wealth factor: richer Sims contribute proportionally more signal,
		// normalized by total residential population (centers around 1.0).
		const double totalRPop = std::max(in.r1Population + in.r2Population + in.r3Population, 1.0);
		s.wealthFactor = ((in.r1Population * r.wealthWeightLow)
		                + (in.r2Population * r.wealthWeightMed)
		                + (in.r3Population * r.wealthWeightHigh)) / totalRPop;

		// Education factor: city average EQ against a tunable reference, clamped.
		const double eduRef = r.educationReference > 0 ? r.educationReference : 1.0;
		s.educationFactor = std::max(r.educationFactorMin, std::min(r.educationFactorMax, in.education / eduRef));

		s.socioeconomicFactor = (s.wealthFactor + s.educationFactor) / 2.0;

		s.producerBaseCapacity = SumBuildingCapacity(cfg.producers, in);
		s.producerCapacity = s.producerBaseCapacity * s.socioeconomicFactor;

		s.totalDemand = SumConsumerDemand(cfg.consumers, in, s.demandByClass);
		s.localBalance = s.producerCapacity - s.totalDemand;

		const double surplus = std::max(s.localBalance, 0.0);
		const double deficit = std::max(-s.localBalance, 0.0);

		const bool canTrade = region && region->IsAvailable();

		if (canTrade)
		{
			// The regional pool is cumulative: retract last cycle's contribution
			// before registering the new one.
			RetractRegionalContribution(cfg, region, s);

			if (surplus > 0)
			{
				region->AddToSupply(cfg.resourceID, surplus);
			}
			if (deficit > 0)
			{
				region->AddToDemand(cfg.resourceID, deficit);
			}

			s.prevSupplyReported = surplus;
			s.prevDemandReported = deficit;

			s.regionalAvailable = std::max(region->GetResourceQuantity(cfg.resourceID), 0.0);
			s.imported = std::min(deficit, s.regionalAvailable);
			s.exported = surplus; // simplification kept from the Lua engine
		}
		else
		{
			s.regionalAvailable = 0;
			s.imported = 0;
			s.exported = 0;
		}

		s.unmetDemand = deficit - s.imported;

		s.supplyTotal = s.producerCapacity + s.imported;
		s.satisfactionRatio = std::min(s.supplyTotal / std::max(s.totalDemand, 1.0), 1.0);

		s.tradeFundsDelta = (s.exported * r.sellPricePerUnit) - (s.imported * r.buyPricePerUnit);

		s.monthlyBuildingCost = SumBuildingCost(cfg.producers, in);
	}

	RewardStatus EvaluateRewards(const Config& cfg, const CityInputs& in)
	{
		const UnlockThresholds& u = cfg.unlocks;
		RewardStatus st;

		st.hubSmall = (in.population < u.hubSmallPop || in.education < u.hubSmallEdu)
			? RewardState::Locked : RewardState::Available;

		st.hubLarge = (in.population < u.hubLargePop
		            || in.education < u.hubLargeEdu
		            || in.r3Population < u.hubLargeR3Pop)
			? RewardState::Locked : RewardState::Available;

		return st;
	}

	bool GetReturnValue(const GridState& s, const std::string& retType, double& out)
	{
		if (retType == "B") { out = s.satisfactionRatio; return true; }
		if (retType == "I") { out = s.imported; return true; }
		if (retType == "X") { out = s.exported; return true; }
		if (retType == "U") { out = s.unmetDemand; return true; }
		return false;
	}
}
