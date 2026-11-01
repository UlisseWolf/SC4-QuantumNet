////////////////////////////////////////////////////////////////////////
//
// SC4-QuantumNet - configuration loader.
//
////////////////////////////////////////////////////////////////////////

#include "QuantumNetConfig.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace QuantumNet
{
	namespace
	{
		std::string Trim(const std::string& s)
		{
			size_t b = 0, e = s.size();
			while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
			while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
			return s.substr(b, e - b);
		}

		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		std::vector<std::string> SplitCsv(const std::string& s, size_t maxParts)
		{
			// The last field keeps any further commas (building names may contain them).
			std::vector<std::string> parts;
			size_t start = 0;
			while (parts.size() + 1 < maxParts)
			{
				size_t comma = s.find(',', start);
				if (comma == std::string::npos) break;
				parts.push_back(Trim(s.substr(start, comma - start)));
				start = comma + 1;
			}
			parts.push_back(Trim(s.substr(start)));
			return parts;
		}

		bool ParseDouble(const std::string& text, double& out)
		{
			const std::string t = Trim(text);
			if (t.empty()) return false;
			char* end = nullptr;
			const double v = std::strtod(t.c_str(), &end);
			if (end == t.c_str() || *end != '\0') return false;
			out = v;
			return true;
		}

		bool ParseBool(const std::string& text, bool& out)
		{
			const std::string t = Lower(Trim(text));
			if (t == "1" || t == "true" || t == "yes" || t == "on") { out = true; return true; }
			if (t == "0" || t == "false" || t == "no" || t == "off") { out = false; return true; }
			return false;
		}

		struct Ctx
		{
			Config& cfg;
			ConfigLoadResult& result;
			int line;
			bool producersReset = false;
			bool consumersReset = false;

			void Warn(const std::string& msg)
			{
				result.warnings.push_back("line " + std::to_string(line) + ": " + msg);
			}
		};

		void SetDouble(Ctx& ctx, const std::string& key, const std::string& value, double& target)
		{
			if (!ParseDouble(value, target)) ctx.Warn("invalid number for " + key);
		}

		void SetBool(Ctx& ctx, const std::string& key, const std::string& value, bool& target)
		{
			if (!ParseBool(value, target)) ctx.Warn("invalid boolean for " + key);
		}

		void SetId(Ctx& ctx, const std::string& key, const std::string& value, uint32_t& target)
		{
			if (!ParseUInt32(value, target)) ctx.Warn("invalid ID for " + key);
		}

		void HandleGeneral(Ctx& ctx, const std::string& k, const std::string& v)
		{
			Config& c = ctx.cfg;
			if (k == "resourceid") SetId(ctx, k, v, c.resourceID);
			else if (k == "enablerewards") SetBool(ctx, k, v, c.enableRewards);
			else if (k == "enableregionaltrade") SetBool(ctx, k, v, c.enableRegionalTrade);
			else if (k == "enabletradefunds") SetBool(ctx, k, v, c.enableTradeFunds);
			else if (k == "loglevel") c.logLevel = v;
			else ctx.Warn("unknown key [General] " + k);
		}

		void HandleRatios(Ctx& ctx, const std::string& k, const std::string& v)
		{
			Ratios& r = ctx.cfg.ratios;
			if (k == "wealth_weight_low") SetDouble(ctx, k, v, r.wealthWeightLow);
			else if (k == "wealth_weight_med") SetDouble(ctx, k, v, r.wealthWeightMed);
			else if (k == "wealth_weight_high") SetDouble(ctx, k, v, r.wealthWeightHigh);
			else if (k == "education_reference") SetDouble(ctx, k, v, r.educationReference);
			else if (k == "education_factor_min") SetDouble(ctx, k, v, r.educationFactorMin);
			else if (k == "education_factor_max") SetDouble(ctx, k, v, r.educationFactorMax);
			else if (k == "buy_price_per_unit") SetDouble(ctx, k, v, r.buyPricePerUnit);
			else if (k == "sell_price_per_unit") SetDouble(ctx, k, v, r.sellPricePerUnit);
			else ctx.Warn("unknown key [Ratios] " + k);
		}

		void HandleUnlocks(Ctx& ctx, const std::string& k, const std::string& v)
		{
			UnlockThresholds& u = ctx.cfg.unlocks;
			if (k == "qnet_hub_small_pop") SetDouble(ctx, k, v, u.hubSmallPop);
			else if (k == "qnet_hub_small_edu") SetDouble(ctx, k, v, u.hubSmallEdu);
			else if (k == "qnet_hub_large_pop") SetDouble(ctx, k, v, u.hubLargePop);
			else if (k == "qnet_hub_large_edu") SetDouble(ctx, k, v, u.hubLargeEdu);
			else if (k == "qnet_hub_large_r3_pop") SetDouble(ctx, k, v, u.hubLargeR3Pop);
			else ctx.Warn("unknown key [Unlocks] " + k);
		}

		void HandleRewards(Ctx& ctx, const std::string& k, const std::string& v)
		{
			Config& c = ctx.cfg;
			if (k == "hubsmallbuildingid") SetId(ctx, k, v, c.hubSmallID);
			else if (k == "hublargebuildingid") SetId(ctx, k, v, c.hubLargeID);
			else if (k == "hubsmalllockedtext") c.hubSmallLockedText = v;
			else if (k == "hublargelockedtext") c.hubLargeLockedText = v;
			else ctx.Warn("unknown key [Rewards] " + k);
		}

		// [Producers]  0xBB340000 = <QNU/day>, <monthly cost>, <name>
		void HandleProducer(Ctx& ctx, const std::string& k, const std::string& v)
		{
			if (!ctx.producersReset) { ctx.cfg.producers.clear(); ctx.producersReset = true; }

			BuildingEntry e;
			const auto p = SplitCsv(v, 3);
			if (!ParseUInt32(k, e.buildingID) || !ParseDouble(p[0], e.rating))
			{
				ctx.Warn("invalid producer entry '" + k + "'");
				return;
			}
			if (p.size() > 1 && !p[1].empty() && !ParseDouble(p[1], e.monthlyCost))
			{
				ctx.Warn("invalid monthly cost for producer '" + k + "'");
			}
			e.name = p.size() > 2 ? p[2] : k;
			ctx.cfg.producers.push_back(e);
		}

		// [Consumers]  0x78A0F71A = <QNU/day>, <class>, <name>
		void HandleConsumer(Ctx& ctx, const std::string& k, const std::string& v)
		{
			if (!ctx.consumersReset) { ctx.cfg.consumers.clear(); ctx.consumersReset = true; }

			BuildingEntry e;
			const auto p = SplitCsv(v, 3);
			if (!ParseUInt32(k, e.buildingID) || !ParseDouble(p[0], e.rating))
			{
				ctx.Warn("invalid consumer entry '" + k + "'");
				return;
			}
			e.consumerClass = p.size() > 1 && !p[1].empty() ? p[1] : "Other";
			e.name = p.size() > 2 ? p[2] : e.consumerClass;
			ctx.cfg.consumers.push_back(e);
		}
	}

	bool ParseUInt32(const std::string& text, uint32_t& out)
	{
		std::string t = Trim(text);
		int base = 10;
		if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
		{
			t = t.substr(2);
			base = 16;
		}
		if (t.empty() || t.size() > (base == 16 ? 8u : 10u)) return false;
		char* end = nullptr;
		const unsigned long long v = std::strtoull(t.c_str(), &end, base);
		if (*end != '\0' || v > 0xFFFFFFFFull) return false;
		out = static_cast<uint32_t>(v);
		return true;
	}

	void ApplyIni(const std::string& text, Config& cfg, ConfigLoadResult& result)
	{
		Ctx ctx{ cfg, result, 0 };
		std::string section;
		std::istringstream stream(text);
		std::string raw;

		while (std::getline(stream, raw))
		{
			++ctx.line;
			if (ctx.line == 1 && raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0)
			{
				raw.erase(0, 3); // UTF-8 BOM
			}

			const std::string line = Trim(raw);
			if (line.empty() || line[0] == ';' || line[0] == '#') continue;

			if (line.front() == '[')
			{
				if (line.back() != ']') { ctx.Warn("malformed section header"); continue; }
				section = Lower(Trim(line.substr(1, line.size() - 2)));
				continue;
			}

			const size_t eq = line.find('=');
			if (eq == std::string::npos) { ctx.Warn("expected key = value"); continue; }

			const std::string key = Trim(line.substr(0, eq));
			std::string value = line.substr(eq + 1);

			// Inline comment: a ';' preceded by whitespace.
			for (size_t i = 1; i < value.size(); ++i)
			{
				if (value[i] == ';' && std::isspace(static_cast<unsigned char>(value[i - 1])))
				{
					value.erase(i);
					break;
				}
			}
			value = Trim(value);
			const std::string k = Lower(key);

			if (section == "general") HandleGeneral(ctx, k, value);
			else if (section == "ratios") HandleRatios(ctx, k, value);
			else if (section == "unlocks") HandleUnlocks(ctx, k, value);
			else if (section == "rewards") HandleRewards(ctx, k, value);
			else if (section == "producers") HandleProducer(ctx, key, value);
			else if (section == "consumers") HandleConsumer(ctx, key, value);
			else ctx.Warn("key outside a known section: " + key);
		}
	}

	Config LoadConfig(const std::filesystem::path& path, ConfigLoadResult& result)
	{
		Config cfg = Config::Defaults();

		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			result.fileFound = false;
			return cfg;
		}

		result.fileFound = true;
		std::ostringstream ss;
		ss << file.rdbuf();
		ApplyIni(ss.str(), cfg, result);
		return cfg;
	}
}
