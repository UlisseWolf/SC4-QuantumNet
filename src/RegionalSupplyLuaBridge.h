////////////////////////////////////////////////////////////////////////
//
// SC4-QuantumNet - bridge to the sc4-regional-supply-demand mod.
//
// That mod exposes its API to the advisor Lua context as the global
// table "regional_supply" (add_to_supply, add_to_demand, remove_from_supply,
// remove_from_demand, get_resource_quantity). The former QUANTUMNET.lua
// called those functions directly; this bridge calls the very same
// functions from C++ through the advisor scripting context, so the
// regional pool keeps working exactly as before without any Lua file
// belonging to QuantumNet.
//
// If the table is missing (mod not installed), IsAvailable() returns
// false and the simulation runs local-only: no imports, no exports.
//
////////////////////////////////////////////////////////////////////////

#pragma once
#include "QuantumNetSimulation.h"

class cISCLua;

class RegionalSupplyLuaBridge final : public QuantumNet::IRegionalSupply
{
public:
	explicit RegionalSupplyLuaBridge(cISCLua* pLua);

	bool IsAvailable() override;
	void AddToSupply(uint32_t resourceID, double amount) override;
	void AddToDemand(uint32_t resourceID, double amount) override;
	void RemoveFromSupply(uint32_t resourceID, double amount) override;
	void RemoveFromDemand(uint32_t resourceID, double amount) override;
	double GetResourceQuantity(uint32_t resourceID) override;

private:
	// Calls regional_supply.<name>(resourceID[, amount]); returns true on
	// success and stores the first numeric result (if any) in result.
	bool Call(const char* name, uint32_t resourceID, const double* amount, double* result);

	cISCLua* pLua;
};
