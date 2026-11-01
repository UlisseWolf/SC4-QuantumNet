////////////////////////////////////////////////////////////////////////
//
// SC4-QuantumNet - DLL port of the QUANTUMNET Lua engine.
// Based on the NEON ENGINE by RobertaME / Ulisse Wolf.
//
// Recreates the ControlNet mechanic from SimCity (2013, Maxis/EA)
// Cities of Tomorrow. In this pack the resource is called "QuantumNet"
// (QNU = QuantumNet Units per day).
//
// This header is deliberately free of any SC4/Windows dependency so the
// whole simulation can be unit-tested on any platform. The DLL director
// gathers the city inputs, calls RunBalance() and applies the outcome.
//
////////////////////////////////////////////////////////////////////////

#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace QuantumNet
{
	// One tracked building: a producer (rating = base QNU/day) or a
	// consumer (rating = QNU/day needed to run at full efficiency).
	struct BuildingEntry
	{
		uint32_t buildingID = 0;   // building exemplar ID (OG_ID)
		double rating = 0.0;       // QNU/day
		double monthlyCost = 0.0;  // Simoleons/month (informational, see README)
		std::string name;
		std::string consumerClass; // consumers only: "Fusion", "Tidal", ...
	};

	struct Ratios
	{
		double wealthWeightLow = 0.4;
		double wealthWeightMed = 1.0;
		double wealthWeightHigh = 2.2;
		double educationReference = 500.0;
		double educationFactorMin = 0.5;
		double educationFactorMax = 2.0;
		double buyPricePerUnit = 3.0;
		double sellPricePerUnit = 2.0;
	};

	struct UnlockThresholds
	{
		double hubSmallPop = 8000;
		double hubSmallEdu = 300;
		double hubLargePop = 30000;
		double hubLargeEdu = 600;
		double hubLargeR3Pop = 2000;
	};

	struct Config
	{
		uint32_t resourceID = 0x514E4554; // "QNET" - must match sc4-regional-supply-demand
		uint32_t hubSmallID = 0xBB340000;
		uint32_t hubLargeID = 0xBB340001;
		std::vector<BuildingEntry> producers;
		std::vector<BuildingEntry> consumers;
		Ratios ratios;
		UnlockThresholds unlocks;
		std::string hubSmallLockedText;
		std::string hubLargeLockedText;
		bool enableRewards = true;
		bool enableRegionalTrade = true;
		bool enableTradeFunds = true;
		std::string logLevel = "Info"; // Error | Info | Debug

		static Config Defaults();
	};

	// Snapshot of the native game variables the Lua script read.
	struct CityInputs
	{
		double population = 0;   // game.g_population
		double r1Population = 0; // game.g_city_r1_population
		double r2Population = 0; // game.g_city_r2_population
		double r3Population = 0; // game.g_city_r3_population
		double education = 0;    // game.ga_education (city average EQ)
		std::unordered_map<uint32_t, uint32_t> buildingCounts; // buildingID -> count
	};

	// Abstraction over the sc4-regional-supply-demand mod.
	class IRegionalSupply
	{
	public:
		virtual ~IRegionalSupply() = default;
		virtual bool IsAvailable() = 0;
		virtual void AddToSupply(uint32_t resourceID, double amount) = 0;
		virtual void AddToDemand(uint32_t resourceID, double amount) = 0;
		virtual void RemoveFromSupply(uint32_t resourceID, double amount) = 0;
		virtual void RemoveFromDemand(uint32_t resourceID, double amount) = 0;
		virtual double GetResourceQuantity(uint32_t resourceID) = 0;
	};

	// Port of the Lua table quantumnet_grid_var.
	struct GridState
	{
		uint32_t producerCount = 0;

		double wealthFactor = 0;
		double educationFactor = 0;
		double socioeconomicFactor = 0;

		double producerBaseCapacity = 0;
		double producerCapacity = 0;

		std::map<std::string, double> demandByClass;
		double totalDemand = 0;

		double localBalance = 0;
		double prevSupplyReported = 0;
		double prevDemandReported = 0;

		double regionalAvailable = 0;
		double imported = 0;
		double exported = 0;
		double unmetDemand = 0;

		double supplyTotal = 0;
		double satisfactionRatio = 0;

		double monthlyBuildingCost = 0;

		// Net Simoleons resulting from regional trade in the last settlement
		// (positive = income from exports, negative = import bill).
		double tradeFundsDelta = 0;
	};

	enum class RewardState { Locked, Available };

	struct RewardStatus
	{
		RewardState hubSmall = RewardState::Locked;
		RewardState hubLarge = RewardState::Locked;
	};

	uint32_t SumBuildingCounts(const std::vector<BuildingEntry>& table, const CityInputs& in);
	double SumBuildingCapacity(const std::vector<BuildingEntry>& table, const CityInputs& in);
	double SumBuildingCost(const std::vector<BuildingEntry>& table, const CityInputs& in);
	double SumConsumerDemand(const std::vector<BuildingEntry>& consumers, const CityInputs& in,
	                         std::map<std::string, double>& byClass);

	// Port of run_quantumnet_balance_variables(). Recomputes the whole grid
	// state and settles the regional trade (pass nullptr to skip trading).
	// Funds are NOT touched here: the caller applies state.tradeFundsDelta.
	void RunBalance(const Config& cfg, const CityInputs& in, IRegionalSupply* region, GridState& state);

	// Retracts this city's contribution from the regional pool (used when
	// trading is disabled at runtime or the resource ID changes).
	void RetractRegionalContribution(const Config& cfg, IRegionalSupply* region, GridState& state);

	// Port of the two Advisor reward conditions.
	RewardStatus EvaluateRewards(const Config& cfg, const CityInputs& in);

	// Port of the ret_type switch in run_quantumnet_balance_variables():
	// "B" satisfaction, "I" imported, "X" exported, "U" unmet demand.
	bool GetReturnValue(const GridState& state, const std::string& retType, double& out);
}
