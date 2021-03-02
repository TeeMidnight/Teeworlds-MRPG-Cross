/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <teeother/components/localization.h>

#include <game/server/gamecontext.h>
#include "ItemInformation.h"

const char* CItemInformation::GetName(CPlayer* pPlayer) const
{
	if(!pPlayer) 
		return m_aName;
	return pPlayer->GS()->Server()->Localization()->Localize(pPlayer->GetLanguage(), m_aName);
}

const char* CItemInformation::GetDesc(CPlayer* pPlayer) const
{
	if(!pPlayer) 
		return m_aDesc;
	return pPlayer->GS()->Server()->Localization()->Localize(pPlayer->GetLanguage(), m_aDesc);
}

int CItemInformation::GetInfoEnchantStats(int AttributeID) const
{
	for(int i = 0; i < STATS_MAX_FOR_ITEM; i++)
	{
		if(CGS::ms_aAttributsInfo.find(m_aAttribute[i]) != CGS::ms_aAttributsInfo.end() && m_aAttribute[i] == AttributeID)
			return m_aAttributeCount[i];
	}
	return 0;
}

int CItemInformation::GetInfoEnchantStats(int AttributeID, int Enchant) const
{
	const int StatSize = GetInfoEnchantStats(AttributeID);
	if(StatSize <= 0)
		return 0;

	const int PercentEnchant = translate_to_percent_rest(StatSize, PERCENT_OF_ENCHANT);
	int EnchantStat = StatSize + PercentEnchant * (1 + Enchant);

	// the case when with percent will back 0
	if(PercentEnchant <= 0)
		EnchantStat += Enchant;

	return EnchantStat;
}

int CItemInformation::GetEnchantPrice(int EnchantLevel) const
{
	int FinishedPrice = 0;
	for(int i = 0; i < STATS_MAX_FOR_ITEM; i++)
	{
		if(CGS::ms_aAttributsInfo.find(m_aAttribute[i]) == CGS::ms_aAttributsInfo.end())
			continue;

		int UpgradePrice;
		const int Attribute = m_aAttribute[i];
		const int TypeAttribute = CGS::ms_aAttributsInfo[Attribute].m_Type;

		// strength stats
		if(TypeAttribute == AtributType::AtHardtype)
			UpgradePrice = max(20, CGS::ms_aAttributsInfo[Attribute].m_UpgradePrice) * 15;
		
		// weapon and job stats
		else if(TypeAttribute == AtributType::AtJob || TypeAttribute == AtributType::AtWeapon || Attribute == Stats::StLuckyDropItem)
			UpgradePrice = max(40, CGS::ms_aAttributsInfo[Attribute].m_UpgradePrice) * 15;
		
		// other stats
		else
			UpgradePrice = max(5, CGS::ms_aAttributsInfo[Attribute].m_UpgradePrice) * 15;

		const int PercentEnchant = max(1, translate_to_percent_rest(m_aAttributeCount[i], PERCENT_OF_ENCHANT));
		FinishedPrice += UpgradePrice * (PercentEnchant * (1 + EnchantLevel));
	}
	return FinishedPrice;
}

bool CItemInformation::IsEnchantable() const
{
	for(int i = 0; i < STATS_MAX_FOR_ITEM; i++)
	{
		if(CGS::ms_aAttributsInfo.find(m_aAttribute[i]) != CGS::ms_aAttributsInfo.end() && m_aAttributeCount[i] > 0)
			return true;
	}
	return false;
}

bool CItemInformation::IsEnchantMaxLevel(int Enchant) const
{
	for(int i = 0; i < STATS_MAX_FOR_ITEM; i++)
	{
		if(CGS::ms_aAttributsInfo.find(m_aAttribute[i]) == CGS::ms_aAttributsInfo.end() || m_aAttributeCount[i] <= 0)
			continue;

		const int EnchantMax = m_aAttributeCount[i] + translate_to_percent_rest(m_aAttributeCount[i], PERCENT_MAXIMUM_ENCHANT);
		if(GetInfoEnchantStats(m_aAttribute[i], Enchant) > EnchantMax)
			return true;
	}
	return false;
}

void CItemInformation::FormatAttributes(char* pBuffer, int Size, int Enchant) const
{
	dynamic_string Buffer;
	for(int i = 0; i < STATS_MAX_FOR_ITEM; i++)
	{
		if(CGS::ms_aAttributsInfo.find(m_aAttribute[i]) == CGS::ms_aAttributsInfo.end() || m_aAttributeCount[i] <= 0)
			continue;

		const int BonusID = m_aAttribute[i];
		const int BonusCount = GetInfoEnchantStats(BonusID, Enchant);

		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "%s+%d ", CGS::ms_aAttributsInfo[BonusID].m_aName, BonusCount);
		Buffer.append_at(Buffer.length(), aBuf);
	}
	str_copy(pBuffer, Buffer.buffer(), Size);
	Buffer.clear();
}

void CItemInformation::FormatEnchantLevel(char* pBuffer, int Size, int Enchant) const
{
	if(Enchant > 0)
	{
		str_format(pBuffer, Size, "[%s]", IsEnchantMaxLevel(Enchant) ? "Max" : std::string("+" + std::to_string(Enchant)).c_str());
		return;
	}
	str_copy(pBuffer, "\0", Size);
}