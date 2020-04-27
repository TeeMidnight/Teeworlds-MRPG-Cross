/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include "world_swap_job.h"

using namespace sqlstr;
std::map < int , WorldSwapJob::StructSwapWorld > WorldSwapJob::WorldSwap;
std::list < WorldSwapJob::StructPositionLogic > WorldSwapJob::WorldPositionLogic;

void WorldSwapJob::UpdateWorldsList()
{
	for (int i = 0; i < COUNT_WORLD; i++)
	{
		CSqlString<32> world_name = CSqlString<32>(GS()->Server()->GetWorldName(i));
		boost::scoped_ptr<ResultSet> RES(SJK.SD("*", "ENUM_WORLDS", "WHERE WorldID = '%d'", i));
		if (!RES->next()) { SJK.ID("ENUM_WORLDS", "(WorldID, Name) VALUES ('%d', '%s')", i, world_name.cstr()); }
		else { SJK.UD("ENUM_WORLDS", "Name = '%s' WHERE WorldID = '%d'", world_name.cstr(), i); }
	}
}

void WorldSwapJob::OnInitGlobal() 
{ 
	boost::scoped_ptr<ResultSet> RES(SJK.SD("*", "tw_world_swap"));
	while(RES->next())
	{
		const int ID = RES->getInt("ID");
		WorldSwap[ID].Level = RES->getInt("Level");
		WorldSwap[ID].PositionX = RES->getInt("PositionX");
		WorldSwap[ID].PositionY = RES->getInt("PositionY");
		WorldSwap[ID].WorldID = RES->getInt("WorldID");		
		WorldSwap[ID].TwoPositionX = RES->getInt("TwoPositionX");
		WorldSwap[ID].TwoPositionY = RES->getInt("TwoPositionY");
		WorldSwap[ID].TwoWorldID = RES->getInt("TwoWorldID");
	}

	for(const auto& swapw : WorldSwap)
	{
		StructPositionLogic pPositionLogic;
		pPositionLogic.BaseWorldID = swapw.second.WorldID;
		pPositionLogic.FindWorldID = swapw.second.TwoWorldID;
		pPositionLogic.Position = vec2(swapw.second.TwoPositionX, swapw.second.TwoPositionY);
		WorldPositionLogic.push_back(pPositionLogic);
			
		pPositionLogic.BaseWorldID = swapw.second.TwoWorldID;
		pPositionLogic.FindWorldID = swapw.second.WorldID;
		pPositionLogic.Position = vec2(swapw.second.PositionX, swapw.second.PositionY);
		WorldPositionLogic.push_back(pPositionLogic);
	}
	UpdateWorldsList();
	Job()->ShowLoadingProgress("Worlds Swap", WorldSwap.size());
	Job()->ShowLoadingProgress("Worlds Swap Logic", WorldPositionLogic.size());
}

bool WorldSwapJob::OnPlayerHandleTile(CCharacter *pChr, int IndexCollision)
{
	CPlayer *pPlayer = pChr->GetPlayer();
	if(pChr->GetHelper()->TileEnter(IndexCollision, TILE_WORLD_SWAP))
	{
		pChr->m_Core.m_ProtectHooked = pChr->m_NoAllowDamage = true;
		return true;
	}
	else if(pChr->GetHelper()->TileExit(IndexCollision, TILE_WORLD_SWAP))
	{
		pChr->m_Core.m_ProtectHooked = pChr->m_NoAllowDamage = false;
		return true;	
	}

	
	if(pChr->GetHelper()->BoolIndex(TILE_WORLD_SWAP))
	{
		if(ChangingWorld(pPlayer->GetCID(), pChr->m_Core.m_Pos))
			return true;
	}
	return false;
}

int WorldSwapJob::GetSwapID(vec2 Pos)
{
	for(const auto& sw : WorldSwap)
	{
		if (sw.second.WorldID == GS()->GetWorldID())
		{
			vec2 SwapPosition = vec2(sw.second.PositionX, sw.second.PositionY);
			if (distance(SwapPosition, Pos) < 400)
				return sw.first;
			continue;
		}
		
		if (sw.second.TwoWorldID == GS()->GetWorldID())
		{
			vec2 SwapPosition = vec2(sw.second.TwoPositionX, sw.second.TwoPositionY);
			if (distance(SwapPosition, Pos) < 400)
				return sw.first;
			continue;
		}
	}
	return -1;
}

int WorldSwapJob::GetWorldLevel() const
{
	if (GS()->IsDungeon())
	{
		int DungeonID = GS()->DungeonID();
		return DungeonJob::Dungeon[DungeonID].Level;
	}

	for (const auto& sw : WorldSwap)
	{
		if (sw.second.WorldID == GS()->GetWorldID() || sw.second.TwoWorldID == GS()->GetWorldID())
			return sw.second.Level;
	}
	return -1;
}

bool WorldSwapJob::ChangingWorld(int ClientID, vec2 Pos)
{
	CPlayer *pPlayer = GS()->GetPlayer(ClientID);
	if(!pPlayer) return true;

	int SwapID = GetSwapID(Pos);
	if (WorldSwap.find(SwapID) != WorldSwap.end())
	{
		if (pPlayer->Acc().Level < WorldSwap[SwapID].Level)
		{
			GS()->SBL(ClientID, BroadcastPriority::BROADCAST_GAME_WARNING, 100, "Required {INT} level!", &WorldSwap[SwapID].Level);
			return false;
		}

		if (WorldSwap[SwapID].WorldID == GS()->GetWorldID())
		{
			pPlayer->Acc().TeleportX = WorldSwap[SwapID].TwoPositionX;
			pPlayer->Acc().TeleportY = WorldSwap[SwapID].TwoPositionY;
			GS()->Server()->ChangeWorld(ClientID, WorldSwap[SwapID].TwoWorldID);
			return true;
		}

		pPlayer->Acc().TeleportX = WorldSwap[SwapID].PositionX;
		pPlayer->Acc().TeleportY = WorldSwap[SwapID].PositionY;
		GS()->Server()->ChangeWorld(ClientID, WorldSwap[SwapID].WorldID);
		return true;
	}
	return false;
}

vec2 WorldSwapJob::GetPositionQuestBot(int ClientID, int QuestID)
{
	int playerTalkProgress = QuestBase::Quests[ClientID][QuestID].Progress;
	ContextBots::QuestBotInfo FindBot = Job()->Quest()->GetQuestBot(QuestID, playerTalkProgress);
	if(FindBot.IsActive())
	{
		if(GS()->GetWorldID() == FindBot.WorldID)
			return vec2(FindBot.PositionX, FindBot.PositionY);

		int TargetWorldID = FindBot.WorldID;
		for(const auto& swp : WorldPositionLogic)
		{
			if(TargetWorldID != swp.BaseWorldID) continue;
			TargetWorldID = swp.FindWorldID;

			if(GS()->GetWorldID() == swp.FindWorldID)
				return swp.Position;
		}
	}
	return vec2(0.0f, 0.0f);
}

int WorldSwapJob::GetWorldType() const
{
	if(GS()->DungeonID())
		return WORLD_DUNGEON;
	else 
		return WORLD_STANDARD;
}