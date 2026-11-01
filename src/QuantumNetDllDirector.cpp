////////////////////////////////////////////////////////////////////////
//
// SC4-QuantumNet - DLL director.
// Based on the NEON ENGINE by RobertaME / Ulisse Wolf.
//
// Native replacement for QUANTUMNET.lua (and for the QuantumNet section
// of PACK_CONSTANTS.lua). Once a month it:
//   1. counts the QuantumNet producers/consumers in the city,
//   2. reads population by wealth and the average EQ,
//   3. recomputes the QuantumNet grid (RunBalance),
//   4. settles the regional trade through sc4-regional-supply-demand,
//   5. charges/pays the trade in Simoleons,
//   6. unlocks/locks the two QuantumNet Data Hub rewards,
//   7. publishes game.g_quantumnet_* variables for the UI and other scripts.
//
// For backward compatibility with NEON/AQUA/WASTE Lua scripts it also
// registers the global Lua functions the old script provided:
//   get_quantumnet_efficiency_multiplier()
//   run_quantumnet_balance_variables([ret_type])   -- read-only now
//   get_quantumnet_building_cost_monthly()
//
////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "QuantumNetConfig.h"
#include "QuantumNetSimulation.h"
#include "RegionalSupplyLuaBridge.h"
#include "version.h"

#include "cIGZFrameWork.h"
#include "cIGZMessage2Standard.h"
#include "cIGZMessageServer2.h"
#include "cISC4AdvisorSystem.h"
#include "cISC4BudgetSimulator.h"
#include "cISC4BuildingDevelopmentSimulator.h"
#include "cISC4BuildingOccupant.h"
#include "cISC4City.h"
#include "cISC4CivicBuildingSimulator.h"
#include "cISC4Lot.h"
#include "cISC4LotManager.h"
#include "cISC4Occupant.h"
#include "cISC4OccupantManager.h"
#include "cISC4ResidentialSimulator.h"
#include "cISCLua.h"
#include "cRZAutoRefCount.h"
#include "cRZMessage2COMDirector.h"
#include "GZServPtrs.h"
#include "SC4String.h"
#include "SCLuaUtil.h"

#include <cmath>
#include <filesystem>
#include <unordered_set>
#include <windows.h>

namespace
{
	constexpr uint32_t kQuantumNetDirectorID = 0xDB4BB03F;

	constexpr uint32_t kSC4MessagePostCityInit = 0x26D31EC1;
	constexpr uint32_t kSC4MessagePreCityShutdown = 0x26D31EC2;
	constexpr uint32_t kSC4MessageSimNewMonth = 0x66956816;

	constexpr uint32_t kOccupantTypeBuilding = 0x278128A0;

	constexpr const char* kConfigFileName = "SC4QuantumNet.ini";
	constexpr const char* kLogFileName = "SC4QuantumNet.log";

	std::filesystem::path GetDllFolderPath()
	{
		HMODULE module = nullptr;
		wchar_t buffer[MAX_PATH * 2] = {};

		if (GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&GetDllFolderPath),
				&module)
			&& GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer))) != 0)
		{
			return std::filesystem::path(buffer).parent_path();
		}

		return std::filesystem::current_path();
	}
}

class QuantumNetDllDirector;
static QuantumNetDllDirector* gDirector = nullptr;

class QuantumNetDllDirector final : public cRZMessage2COMDirector
{
public:
	QuantumNetDllDirector()
		: pCity(nullptr),
		  pAdvisorSystem(nullptr),
		  pBudgetSim(nullptr),
		  pCivicSim(nullptr),
		  pResidentialSim(nullptr),
		  pLotManager(nullptr),
		  pOccupantManager(nullptr),
		  rewardsApplied(false)
	{
		gDirector = this;

		const std::filesystem::path dllFolder = GetDllFolderPath();
		Logger& log = Logger::Get();
		log.Init(dllFolder / kLogFileName, LogLevel::Info);
		log.Write(LogLevel::Info, "SC4QuantumNet v" PLUGIN_VERSION_STR);

		QuantumNet::ConfigLoadResult result;
		config = QuantumNet::LoadConfig(dllFolder / kConfigFileName, result);
		log.SetLevel(log.ParseLevel(config.logLevel));

		log.Write(LogLevel::Info, result.fileFound
			? "Loaded SC4QuantumNet.ini."
			: "SC4QuantumNet.ini not found, using built-in defaults.");
		for (const std::string& w : result.warnings)
		{
			log.Write(LogLevel::Error, "SC4QuantumNet.ini " + w);
		}
		log.WriteFormatted(LogLevel::Info, "%u producer type(s), %u consumer type(s), resource ID 0x%08X.",
			static_cast<unsigned>(config.producers.size()),
			static_cast<unsigned>(config.consumers.size()),
			config.resourceID);
	}

	uint32_t GetDirectorID() const override
	{
		return kQuantumNetDirectorID;
	}

	bool OnStart(cIGZCOM* /*pCOM*/) override
	{
		cIGZFrameWork* const pFramework = RZGetFrameWork();
		if (pFramework->GetState() < cIGZFrameWork::kStatePreAppInit)
		{
			pFramework->AddHook(this);
		}
		else
		{
			PreAppInit();
		}
		return true;
	}

	bool PostAppInit() override
	{
		cIGZMessageServer2Ptr pMsgServ;
		if (!pMsgServ)
		{
			Logger::Get().Write(LogLevel::Error, "Message server unavailable.");
			return false;
		}

		for (uint32_t id : { kSC4MessagePostCityInit, kSC4MessagePreCityShutdown, kSC4MessageSimNewMonth })
		{
			if (!pMsgServ->AddNotification(this, id))
			{
				Logger::Get().Write(LogLevel::Error, "Failed to subscribe to the required notifications.");
				return false;
			}
		}
		return true;
	}

	bool DoMessage(cIGZMessage2* pMessage) override
	{
		switch (pMessage->GetType())
		{
		case kSC4MessagePostCityInit:
			PostCityInit(static_cast<cIGZMessage2Standard*>(pMessage));
			break;
		case kSC4MessagePreCityShutdown:
			PreCityShutdown();
			break;
		case kSC4MessageSimNewMonth:
			Update(true);
			break;
		}
		return true;
	}

	// ---- state exposed to the Lua compatibility functions ----
	const QuantumNet::GridState& State() const { return state; }

private:
	void PostCityInit(cIGZMessage2Standard* pStandardMsg)
	{
		pCity = static_cast<cISC4City*>(pStandardMsg->GetVoid1());
		if (!pCity)
		{
			return;
		}

		pAdvisorSystem = pCity->GetAdvisorSystem();
		pBudgetSim = pCity->GetBudgetSimulator();
		pCivicSim = pCity->GetCivicBuildingSimulator();
		pResidentialSim = pCity->GetResidentialSimulator();
		pLotManager = pCity->GetLotManager();
		pOccupantManager = pCity->GetOccupantManager();

		state = QuantumNet::GridState();
		rewardsApplied = false;
		lastRewards = QuantumNet::RewardStatus();

		RegisterLuaFunctions();

		// First evaluation: register with the region and set rewards, but do
		// not move money until the first simulated month.
		Update(false);
	}

	void PreCityShutdown()
	{
		pCity = nullptr;
		pAdvisorSystem = nullptr;
		pBudgetSim = nullptr;
		pCivicSim = nullptr;
		pResidentialSim = nullptr;
		pLotManager = nullptr;
		pOccupantManager = nullptr;
		state = QuantumNet::GridState();
	}

	// ---------------------------------------------------------------
	// Input gathering (replaces adv_game_data + automata counters)
	// ---------------------------------------------------------------

	struct ScanContext
	{
		QuantumNetDllDirector* self;
		QuantumNet::CityInputs* inputs;
		std::unordered_set<cISC4Occupant*> seenOccupants;
		std::unordered_set<cISC4Lot*> seenLots;
	};

	static bool ScanOccupant(cISC4Occupant* pOccupant, void* pData)
	{
		auto* ctx = static_cast<ScanContext*>(pData);

		if (!pOccupant || !ctx->seenOccupants.insert(pOccupant).second)
		{
			return true; // already counted (multi-cell building)
		}

		cRZAutoRefCount<cISC4BuildingOccupant> pBuilding;
		if (pOccupant->QueryInterface(GZIID_cISC4BuildingOccupant, pBuilding.AsPPVoid()) && pBuilding)
		{
			ctx->inputs->buildingCounts[pBuilding->GetBuildingType()]++;
		}

		cISC4LotManager* pLots = ctx->self->pLotManager;
		cISC4Lot* pLot = pLots ? pLots->GetOccupantLot(pOccupant) : nullptr;
		if (pLot && ctx->seenLots.insert(pLot).second)
		{
			using DT = cISC4BuildingDevelopmentSimulator::DeveloperType;
			ctx->inputs->r1Population += pLot->GetPopulation(DT::ResidentialLowWealth);
			ctx->inputs->r2Population += pLot->GetPopulation(DT::ResidentialMediumWealth);
			ctx->inputs->r3Population += pLot->GetPopulation(DT::ResidentialHighWealth);
		}

		return true; // keep iterating
	}

	QuantumNet::CityInputs GatherInputs()
	{
		QuantumNet::CityInputs in;

		if (pResidentialSim)
		{
			in.population = pResidentialSim->GetPopulation();
			in.education = pResidentialSim->GetGlobalEQ();
		}

		if (pOccupantManager)
		{
			ScanContext ctx{ this, &in, {}, {} };
			pOccupantManager->IterateOccupants(ScanOccupant, &ctx, nullptr, nullptr, kOccupantTypeBuilding);
		}

		return in;
	}

	// ---------------------------------------------------------------
	// Monthly update
	// ---------------------------------------------------------------

	void Update(bool settleFunds)
	{
		if (!pCity)
		{
			return;
		}

		Logger& log = Logger::Get();
		const QuantumNet::CityInputs in = GatherInputs();

		cISCLua* pLua = pAdvisorSystem ? pAdvisorSystem->GetScriptingContext() : nullptr;
		RegionalSupplyLuaBridge region(pLua);

		const bool tradeAvailable = config.enableRegionalTrade && region.IsAvailable();
		if (config.enableRegionalTrade && !tradeAvailable && !warnedNoRegion)
		{
			warnedNoRegion = true;
			log.Write(LogLevel::Error,
				"regional_supply Lua table not found: is sc4-regional-supply-demand installed? "
				"QuantumNet runs local-only (no imports/exports).");
		}

		QuantumNet::RunBalance(config, in, tradeAvailable ? &region : nullptr, state);

		if (settleFunds && config.enableTradeFunds && pBudgetSim)
		{
			const int64_t delta = static_cast<int64_t>(std::llround(state.tradeFundsDelta));
			if (delta > 0)
			{
				pBudgetSim->DepositFunds(delta);
			}
			else if (delta < 0)
			{
				pBudgetSim->WithdrawFunds(-delta);
			}
		}

		if (config.enableRewards)
		{
			ApplyRewards(QuantumNet::EvaluateRewards(config, in));
		}

		PublishGlobals();

		log.WriteFormatted(LogLevel::Debug,
			"pop=%.0f r1=%.0f r2=%.0f r3=%.0f EQ=%.1f | producers=%u prod=%.1f demand=%.1f "
			"imp=%.1f exp=%.1f unmet=%.1f sat=%.3f funds=%+.0f",
			in.population, in.r1Population, in.r2Population, in.r3Population, in.education,
			state.producerCount, state.producerCapacity, state.totalDemand,
			state.imported, state.exported, state.unmetDemand, state.satisfactionRatio,
			settleFunds ? state.tradeFundsDelta : 0.0);
	}

	// ---------------------------------------------------------------
	// Rewards (replaces create_reward_cityplanning records)
	// ---------------------------------------------------------------

	void ApplyRewards(const QuantumNet::RewardStatus& st)
	{
		if (rewardsApplied && st.hubSmall == lastRewards.hubSmall && st.hubLarge == lastRewards.hubLarge)
		{
			return;
		}

		SetRewardState(config.hubSmallID, st.hubSmall, config.hubSmallLockedText);
		SetRewardState(config.hubLargeID, st.hubLarge, config.hubLargeLockedText);

		lastRewards = st;
		rewardsApplied = true;
	}

	void SetRewardState(uint32_t buildingID, QuantumNet::RewardState rs, const std::string& lockedText)
	{
		if (!pCivicSim || buildingID == 0)
		{
			return;
		}

		using CBS = cISC4CivicBuildingSimulator;
		CBS::ConditionalBuildingStatus status;
		status.status = CBS::ConditionalBuildingStatusNone;
		status.buildingCount = 0;

		if (const CBS::ConditionalBuildingStatus* existing = pCivicSim->GetConditionalBuildingStatus(buildingID))
		{
			status.status = existing->status;
			status.buildingCount = existing->buildingCount;
		}

		uint8_t flags = static_cast<uint8_t>(status.status);
		flags &= static_cast<uint8_t>(~(CBS::ConditionalBuildingStatusHidden | CBS::ConditionalBuildingStatusAvailable));

		if (rs == QuantumNet::RewardState::Available)
		{
			flags |= CBS::ConditionalBuildingStatusAvailable;
			status.itemToolTip = SC4String("");
		}
		else
		{
			status.itemToolTip = SC4String(lockedText.c_str());
		}
		status.status = static_cast<CBS::eConditionalBuildingStatus>(flags);

		if (!pCivicSim->UpdateConditionalBuildingStatus(buildingID, &status))
		{
			Logger::Get().WriteFormatted(LogLevel::Error,
				"Could not update the reward status of building 0x%08X "
				"(is it a conditional/reward building?).", buildingID);
		}
		else
		{
			Logger::Get().WriteFormatted(LogLevel::Info, "Building 0x%08X is now %s.", buildingID,
				rs == QuantumNet::RewardState::Available ? "available" : "locked");
		}
	}

	// ---------------------------------------------------------------
	// Lua interoperability
	// ---------------------------------------------------------------

	void PublishGlobals()
	{
		if (!pAdvisorSystem)
		{
			return;
		}

		pAdvisorSystem->SetGlobalValue("g_quantumnet_satisfaction", state.satisfactionRatio);
		pAdvisorSystem->SetGlobalValue("g_quantumnet_production", state.producerCapacity);
		pAdvisorSystem->SetGlobalValue("g_quantumnet_demand", state.totalDemand);
		pAdvisorSystem->SetGlobalValue("g_quantumnet_imported", state.imported);
		pAdvisorSystem->SetGlobalValue("g_quantumnet_exported", state.exported);
		pAdvisorSystem->SetGlobalValue("g_quantumnet_unmet_demand", state.unmetDemand);
		pAdvisorSystem->SetGlobalValue("g_quantumnet_producer_count", static_cast<double>(state.producerCount));
		pAdvisorSystem->SetGlobalValue("g_quantumnet_socioeconomic_factor", state.socioeconomicFactor);
		pAdvisorSystem->SetGlobalValue("g_quantumnet_cost_monthly", state.monthlyBuildingCost);
	}

	static int32_t LuaGetEfficiencyMultiplier(lua_State* L)
	{
		cRZAutoRefCount<cISCLua> pLua = SCLuaUtil::GetISCLuaFromFunctionState(L);
		if (!pLua) return 0;
		pLua->PushNumber(gDirector ? gDirector->State().satisfactionRatio : 0.0);
		return 1;
	}

	// Kept for compatibility: returns the values of the last monthly
	// settlement instead of recomputing (the Lua version charged the trade
	// again on every call, which multiplied the costs).
	static int32_t LuaRunBalanceVariables(lua_State* L)
	{
		cRZAutoRefCount<cISCLua> pLua = SCLuaUtil::GetISCLuaFromFunctionState(L);
		if (!pLua) return 0;

		if (gDirector && pLua->GetTop() >= 1 && pLua->IsString(1))
		{
			const char* retType = pLua->ToString(1);
			double value = 0;
			if (retType && QuantumNet::GetReturnValue(gDirector->State(), retType, value))
			{
				pLua->PushNumber(value);
				return 1;
			}
		}
		return 0; // nil, like the Lua original for an unknown ret_type
	}

	static int32_t LuaGetBuildingCostMonthly(lua_State* L)
	{
		cRZAutoRefCount<cISCLua> pLua = SCLuaUtil::GetISCLuaFromFunctionState(L);
		if (!pLua) return 0;
		pLua->PushNumber(gDirector ? gDirector->State().monthlyBuildingCost : 0.0);
		return 1;
	}

	void RegisterLuaFunctions()
	{
		struct Fn { const char* name; lua_CFunction fn; };
		const Fn functions[] = {
			{ "get_quantumnet_efficiency_multiplier", LuaGetEfficiencyMultiplier },
			{ "run_quantumnet_balance_variables", LuaRunBalanceVariables },
			{ "get_quantumnet_building_cost_monthly", LuaGetBuildingCostMonthly },
		};

		for (const Fn& f : functions)
		{
			const auto status = SCLuaUtil::RegisterLuaFunction(pAdvisorSystem, nullptr, f.name, f.fn);
			if (status != SCLuaUtil::RegisterLuaFunctionStatus::Ok)
			{
				Logger::Get().WriteFormatted(LogLevel::Error, "Failed to register Lua function %s (status %d).",
					f.name, static_cast<int>(status));
			}
		}
	}

	QuantumNet::Config config;
	QuantumNet::GridState state;
	QuantumNet::RewardStatus lastRewards;

	cISC4City* pCity;
	cISC4AdvisorSystem* pAdvisorSystem;
	cISC4BudgetSimulator* pBudgetSim;
	cISC4CivicBuildingSimulator* pCivicSim;
	cISC4ResidentialSimulator* pResidentialSim;
	cISC4LotManager* pLotManager;
	cISC4OccupantManager* pOccupantManager;
	bool rewardsApplied;
	bool warnedNoRegion = false;
};

cRZCOMDllDirector* RZGetCOMDllDirector()
{
	static QuantumNetDllDirector sDirector;
	return &sDirector;
}
