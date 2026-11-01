////////////////////////////////////////////////////////////////////////
//
// SC4-QuantumNet - bridge to the sc4-regional-supply-demand mod.
//
////////////////////////////////////////////////////////////////////////

#include "RegionalSupplyLuaBridge.h"
#include "Logger.h"
#include "cISCLua.h"

namespace
{
	constexpr const char* kRegionalSupplyTable = "regional_supply";
}

RegionalSupplyLuaBridge::RegionalSupplyLuaBridge(cISCLua* lua)
	: pLua(lua)
{
}

bool RegionalSupplyLuaBridge::IsAvailable()
{
	if (!pLua)
	{
		return false;
	}

	const int32_t top = pLua->GetTop();
	pLua->GetGlobal(kRegionalSupplyTable);
	const bool ok = pLua->IsTable(-1);
	pLua->SetTop(top);
	return ok;
}

bool RegionalSupplyLuaBridge::Call(const char* name, uint32_t resourceID, const double* amount, double* result)
{
	if (!pLua)
	{
		return false;
	}

	const int32_t top = pLua->GetTop();
	bool ok = false;

	pLua->GetGlobal(kRegionalSupplyTable);
	if (pLua->IsTable(-1))
	{
		pLua->PushString(name);
		pLua->GetTable(-2);

		if (pLua->IsFunction(-1))
		{
			pLua->PushNumber(static_cast<double>(resourceID));
			int32_t nargs = 1;
			if (amount)
			{
				pLua->PushNumber(*amount);
				nargs = 2;
			}

			const int32_t status = pLua->CallProtected(nargs, 1);
			if (status == 0)
			{
				ok = true;
				if (result)
				{
					*result = pLua->IsNumber(-1) ? pLua->ToNumber(-1) : 0.0;
				}
			}
			else
			{
				const char* err = pLua->IsString(-1) ? pLua->ToString(-1) : "unknown error";
				Logger::Get().WriteFormatted(LogLevel::Error, "regional_supply.%s failed: %s", name, err ? err : "");
			}
		}
		else
		{
			Logger::Get().WriteFormatted(LogLevel::Error, "regional_supply.%s is not a function.", name);
		}
	}

	pLua->SetTop(top);
	return ok;
}

void RegionalSupplyLuaBridge::AddToSupply(uint32_t resourceID, double amount)
{
	Call("add_to_supply", resourceID, &amount, nullptr);
}

void RegionalSupplyLuaBridge::AddToDemand(uint32_t resourceID, double amount)
{
	Call("add_to_demand", resourceID, &amount, nullptr);
}

void RegionalSupplyLuaBridge::RemoveFromSupply(uint32_t resourceID, double amount)
{
	Call("remove_from_supply", resourceID, &amount, nullptr);
}

void RegionalSupplyLuaBridge::RemoveFromDemand(uint32_t resourceID, double amount)
{
	Call("remove_from_demand", resourceID, &amount, nullptr);
}

double RegionalSupplyLuaBridge::GetResourceQuantity(uint32_t resourceID)
{
	double result = 0.0;
	return Call("get_resource_quantity", resourceID, nullptr, &result) ? result : 0.0;
}
