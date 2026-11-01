////////////////////////////////////////////////////////////////////////
//
// SC4-QuantumNet - platform-independent tests for the simulation core.
// Expected values were cross-checked against the original QUANTUMNET.lua
// running in a Lua interpreter with mocked game variables.
//
////////////////////////////////////////////////////////////////////////

#include "QuantumNetConfig.h"
#include "QuantumNetSimulation.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace QuantumNet;

static int gFailures = 0;
static int gChecks = 0;

#define CHECK(cond) do { ++gChecks; if (!(cond)) { ++gFailures; \
	std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_NEAR(a, b) do { ++gChecks; const double _a = (a), _b = (b); \
	if (std::fabs(_a - _b) > 1e-6 * (1.0 + std::fabs(_b))) { ++gFailures; \
	std::printf("FAIL %s:%d  %s = %.9f, expected %.9f\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

// Mock of the sc4-regional-supply-demand pool.
struct MockRegion : IRegionalSupply
{
	double supply = 0, demand = 0, available = 0;
	std::vector<std::string> calls;

	bool IsAvailable() override { return true; }
	void AddToSupply(uint32_t, double a) override { supply += a; calls.push_back("add_supply"); }
	void AddToDemand(uint32_t, double a) override { demand += a; calls.push_back("add_demand"); }
	void RemoveFromSupply(uint32_t, double a) override { supply -= a; calls.push_back("remove_supply"); }
	void RemoveFromDemand(uint32_t, double a) override { demand -= a; calls.push_back("remove_demand"); }
	double GetResourceQuantity(uint32_t) override { return available; }
};

static void TestEmptyCity()
{
	const Config cfg = Config::Defaults();
	CityInputs in;
	GridState s;
	MockRegion region;
	RunBalance(cfg, in, &region, s);

	CHECK(s.producerCount == 0);
	CHECK_NEAR(s.wealthFactor, 0.0);
	CHECK_NEAR(s.educationFactor, 0.5);          // clamped to EDUCATION_FACTOR_MIN
	CHECK_NEAR(s.socioeconomicFactor, 0.25);
	CHECK_NEAR(s.totalDemand, 0.0);
	CHECK_NEAR(s.satisfactionRatio, 0.0);        // Lua quirk preserved: 0 / max(0,1)
	CHECK(region.calls.empty());
}

static void TestSurplusExport()
{
	const Config cfg = Config::Defaults();
	CityInputs in;
	in.population = 40000;
	in.r1Population = 10000;
	in.r2Population = 20000;
	in.r3Population = 5000;
	in.education = 650;
	in.buildingCounts[0xBB340000] = 2;  // 2 x small hub
	in.buildingCounts[0xBB340001] = 1;  // 1 x large hub
	in.buildingCounts[0x78A0F71A] = 1;  // 1 x fusion

	GridState s;
	MockRegion region;
	RunBalance(cfg, in, &region, s);

	const double wealth = (10000 * 0.4 + 20000 * 1.0 + 5000 * 2.2) / 35000.0;
	const double edu = 650.0 / 500.0;
	const double socio = (wealth + edu) / 2.0;
	const double production = 2400.0 * socio;

	CHECK(s.producerCount == 3);
	CHECK_NEAR(s.wealthFactor, wealth);
	CHECK_NEAR(s.educationFactor, edu);
	CHECK_NEAR(s.producerBaseCapacity, 2400.0);
	CHECK_NEAR(s.producerCapacity, production);
	CHECK_NEAR(s.totalDemand, 500.0);
	CHECK_NEAR(s.demandByClass.at("Fusion"), 500.0);
	CHECK_NEAR(s.exported, production - 500.0);
	CHECK_NEAR(s.imported, 0.0);
	CHECK_NEAR(s.satisfactionRatio, 1.0);
	CHECK_NEAR(region.supply, production - 500.0);
	CHECK_NEAR(s.tradeFundsDelta, (production - 500.0) * 2.0);
	CHECK_NEAR(s.monthlyBuildingCost, 2 * 900.0 + 3600.0);
}

static void TestDeficitImportAndRetraction()
{
	const Config cfg = Config::Defaults();
	CityInputs in;
	in.r2Population = 1000;
	in.education = 500;                // education factor 1.0, wealth 1.0
	in.buildingCounts[0xBB340000] = 1; // 400 QNU
	in.buildingCounts[0x78A0F71A] = 2; // 1000 QNU demanded

	GridState s;
	MockRegion region;
	region.available = 250;
	RunBalance(cfg, in, &region, s);

	CHECK_NEAR(s.producerCapacity, 400.0);
	CHECK_NEAR(s.localBalance, -600.0);
	CHECK_NEAR(s.imported, 250.0);
	CHECK_NEAR(s.unmetDemand, 350.0);
	CHECK_NEAR(s.satisfactionRatio, 650.0 / 1000.0);
	CHECK_NEAR(s.tradeFundsDelta, -750.0);
	CHECK_NEAR(region.demand, 600.0);

	// Second cycle: previous contribution must be retracted before re-adding.
	region.calls.clear();
	RunBalance(cfg, in, &region, s);
	CHECK(region.calls.size() == 2);
	CHECK(region.calls[0] == "remove_demand");
	CHECK(region.calls[1] == "add_demand");
	CHECK_NEAR(region.demand, 600.0);

	// Local-only mode (regional mod missing).
	GridState local;
	RunBalance(cfg, in, nullptr, local);
	CHECK_NEAR(local.imported, 0.0);
	CHECK_NEAR(local.unmetDemand, 600.0);
	CHECK_NEAR(local.satisfactionRatio, 0.4);
	CHECK_NEAR(local.tradeFundsDelta, 0.0);
}

static void TestEducationClamp()
{
	const Config cfg = Config::Defaults();
	CityInputs in;
	in.education = 5000;
	GridState s;
	RunBalance(cfg, in, nullptr, s);
	CHECK_NEAR(s.educationFactor, 2.0);
}

static void TestRewards()
{
	const Config cfg = Config::Defaults();
	CityInputs in;

	in.population = 7999; in.education = 1000;
	CHECK(EvaluateRewards(cfg, in).hubSmall == RewardState::Locked);

	in.population = 8000; in.education = 300;
	CHECK(EvaluateRewards(cfg, in).hubSmall == RewardState::Available);
	CHECK(EvaluateRewards(cfg, in).hubLarge == RewardState::Locked);

	in.population = 30000; in.education = 600; in.r3Population = 1999;
	CHECK(EvaluateRewards(cfg, in).hubLarge == RewardState::Locked);

	in.r3Population = 2000;
	CHECK(EvaluateRewards(cfg, in).hubLarge == RewardState::Available);
}

static void TestReturnValues()
{
	GridState s;
	s.satisfactionRatio = 0.5; s.imported = 1; s.exported = 2; s.unmetDemand = 3;
	double v = 0;
	CHECK(GetReturnValue(s, "B", v) && v == 0.5);
	CHECK(GetReturnValue(s, "I", v) && v == 1);
	CHECK(GetReturnValue(s, "X", v) && v == 2);
	CHECK(GetReturnValue(s, "U", v) && v == 3);
	CHECK(!GetReturnValue(s, "", v));
}

static void TestIni()
{
	const std::string ini =
		"\xEF\xBB\xBF; comment\n"
		"[General]\n"
		"ResourceID = 0x12345678\n"
		"EnableTradeFunds = false\n"
		"LogLevel = Debug\n"
		"[Ratios]\n"
		"EDUCATION_REFERENCE = 400\n"
		"SELL_PRICE_PER_UNIT = 2.5\n"
		"[Unlocks]\n"
		"QNET_HUB_SMALL_POP = 100\n"
		"[Rewards]\n"
		"HubSmallBuildingID = 0xAABBCCDD\n"
		"HubSmallLockedText = Need more Sims, please\n"
		"[Producers]\n"
		"0x11111111 = 100, 50, Test Hub, with comma\n"
		"[Consumers]\n"
		"0x22222222 = 250, W2E, Waste-to-Energy Plant Small\n"
		"0x33333333 = 150, Tidal\n"
		"[Consumers]\n"
		"bogus = 1\n"
		"NoEquals\n";

	Config cfg = Config::Defaults();
	ConfigLoadResult result;
	ApplyIni(ini, cfg, result);

	CHECK(cfg.resourceID == 0x12345678);
	CHECK(!cfg.enableTradeFunds);
	CHECK(cfg.logLevel == "Debug");
	CHECK_NEAR(cfg.ratios.educationReference, 400.0);
	CHECK_NEAR(cfg.ratios.sellPricePerUnit, 2.5);
	CHECK_NEAR(cfg.unlocks.hubSmallPop, 100.0);
	CHECK(cfg.hubSmallID == 0xAABBCCDD);
	CHECK(cfg.hubSmallLockedText == "Need more Sims, please");
	CHECK(cfg.producers.size() == 1);
	CHECK(cfg.producers[0].buildingID == 0x11111111);
	CHECK_NEAR(cfg.producers[0].monthlyCost, 50.0);
	CHECK(cfg.producers[0].name == "Test Hub, with comma");
	CHECK(cfg.consumers.size() == 2);
	CHECK(cfg.consumers[0].consumerClass == "W2E");
	CHECK(cfg.consumers[1].consumerClass == "Tidal");
	CHECK(result.warnings.size() == 2);   // "bogus" and "NoEquals"

	uint32_t id = 0;
	CHECK(ParseUInt32("0xBB340000", id) && id == 0xBB340000);
	CHECK(ParseUInt32("42", id) && id == 42);
	CHECK(!ParseUInt32("0x1FFFFFFFF", id));
	CHECK(!ParseUInt32("XXXXXXXX", id));
}

#ifndef QUANTUMNET_SAMPLE_INI
#define QUANTUMNET_SAMPLE_INI "config/SC4QuantumNet.ini"
#endif

static void TestShippedIniMatchesDefaults()
{
	ConfigLoadResult result;
	const Config cfg = LoadConfig(QUANTUMNET_SAMPLE_INI, result);
	const Config def = Config::Defaults();

	CHECK(result.fileFound);
	for (const std::string& w : result.warnings) std::printf("  ini warning: %s\n", w.c_str());
	CHECK(result.warnings.empty());
	CHECK(cfg.resourceID == def.resourceID);
	CHECK(cfg.hubSmallID == def.hubSmallID && cfg.hubLargeID == def.hubLargeID);
	CHECK_NEAR(cfg.ratios.wealthWeightHigh, def.ratios.wealthWeightHigh);
	CHECK_NEAR(cfg.ratios.educationReference, def.ratios.educationReference);
	CHECK_NEAR(cfg.ratios.buyPricePerUnit, def.ratios.buyPricePerUnit);
	CHECK_NEAR(cfg.unlocks.hubLargeR3Pop, def.unlocks.hubLargeR3Pop);
	CHECK(cfg.producers.size() == def.producers.size());
	for (size_t i = 0; i < cfg.producers.size() && i < def.producers.size(); ++i)
	{
		CHECK(cfg.producers[i].buildingID == def.producers[i].buildingID);
		CHECK_NEAR(cfg.producers[i].rating, def.producers[i].rating);
		CHECK_NEAR(cfg.producers[i].monthlyCost, def.producers[i].monthlyCost);
	}
	CHECK(cfg.consumers.size() == 1 && cfg.consumers[0].buildingID == 0x78A0F71A);
	CHECK(cfg.hubSmallLockedText == def.hubSmallLockedText);
}

int main()
{
	TestEmptyCity();
	TestSurplusExport();
	TestDeficitImportAndRetraction();
	TestEducationClamp();
	TestRewards();
	TestReturnValues();
	TestIni();
	TestShippedIniMatchesDefaults();

	std::printf("%d/%d checks passed\n", gChecks - gFailures, gChecks);
	return gFailures == 0 ? 0 : 1;
}
