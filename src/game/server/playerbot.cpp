/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "entities/botai/character_bot_ai.h"
#include "gamecontext.h"
#include "mmocore/PathFinder.h"

#include "playerbot.h"

MACRO_ALLOC_POOL_ID_IMPL(CPlayerBot, MAX_CLIENTS * ENGINE_MAX_WORLDS + MAX_CLIENTS)

IServer* CPlayer::Server() const { return m_pGS->Server(); };

CPlayerBot::CPlayerBot(CGS *pGS, int ClientID, int BotID, int SubBotID, int SpawnPoint)
: CPlayer(pGS, ClientID), m_BotType(SpawnPoint), m_BotID(BotID), m_SubBotID(SubBotID), m_BotHealth(0)
{
	m_Spawned = true;
	m_DungeonAllowedSpawn = false;
	m_aPlayerTick[TickState::Respawn] = Server()->Tick();
	(this)->SendClientInfo(-1);
}

CPlayerBot::~CPlayerBot() 
{
	for(int i = 0; i < MAX_PLAYERS; i++)
		BotJob::ms_aDataBot[m_BotID].m_aAlreadySnapQuestBot[i] = false;

	CNetMsg_Sv_ClientDrop Msg;
	Msg.m_ClientID = m_ClientID;
	Msg.m_pReason = "\0";
	Msg.m_Silent = true;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, -1, (this)->GetPlayerWorldID());

	delete m_pCharacter;
	m_pCharacter = nullptr;
}

void CPlayerBot::Tick()
{
	if(!Server()->ClientIngame(m_ClientID))
		return;

	Server()->SetClientScore(m_ClientID, 1);
	if(m_pCharacter && !m_pCharacter->IsAlive())
	{
		delete m_pCharacter;
		m_pCharacter = nullptr;
	}

	if(m_pCharacter)
	{
		if(m_pCharacter->IsAlive() && GS()->CheckingPlayersDistance(m_pCharacter->GetPos(), 1000.0f))
		{
			TickThreadMobsPathFinder();
			m_ViewPos = m_pCharacter->GetPos();
		}
		else if(!m_WayPoints.empty())
		{
			m_PathSize = 0;
			m_WayPoints.clear();
		}
	}
	else if(m_Spawned && m_aPlayerTick[TickState::Respawn]+Server()->TickSpeed()*3 <= Server()->Tick())
		TryRespawn();

	HandleTuningParams();
}

int CPlayerBot::GetStartHealth()
{
	if(m_BotType == BotsTypes::TYPE_BOT_MOB)
		return GetAttributeCount(Stats::StHardness);
	return 10;	
}

int CPlayerBot::GetAttributeCount(int BonusID, bool Really)
{
	if(m_BotType != BotsTypes::TYPE_BOT_MOB)
		return 10;

	// get stats from the bot's equipment
	int AttributeEx = BotJob::ms_aMobBot[m_SubBotID].m_Power;
	for (int i = 0; i < MAX_EQUIPPED_SLOTS_BOTS; i++)
	{
		const int ItemID = GetEquippedItemID(i);
		const int ItemBonusCount = GS()->GetItemInfo(ItemID).GetInfoEnchantStats(BonusID);
		if (ItemID > 0 && ItemBonusCount > 0)
			AttributeEx += ItemBonusCount;
	}
	
	// spread weapons
	if(BonusID == Stats::StSpreadShotgun || BonusID == Stats::StSpreadGrenade || BonusID == Stats::StSpreadRifle)
		AttributeEx = BotJob::ms_aMobBot[m_SubBotID].m_Spread;

	// all attribute stats without hardness
	else if(BonusID != Stats::StHardness && CGS::ms_aAttributsInfo[BonusID].m_Devide > 0)
	{
		AttributeEx /= CGS::ms_aAttributsInfo[BonusID].m_Devide;
		if(CGS::ms_aAttributsInfo[BonusID].m_Type == AtHardtype)
			AttributeEx /= BotJob::ms_aMobBot[m_SubBotID].m_Boss ? 30 : 2;
	}

	return AttributeEx;
}

void CPlayerBot::TryRespawn()
{
	// close spawn mobs on non allowed spawn dungeon
	if (GS()->IsDungeon() && !m_DungeonAllowedSpawn && m_BotType == BotsTypes::TYPE_BOT_MOB)
		return;

	vec2 SpawnPos;
	const int SpawnType = m_BotType;
	if(SpawnType == BotsTypes::TYPE_BOT_MOB)
	{
		vec2 MobRespawnPosition = vec2(BotJob::ms_aMobBot[m_SubBotID].m_PositionX, BotJob::ms_aMobBot[m_SubBotID].m_PositionY);
		if(!GS()->m_pController->CanSpawn(m_BotType, &SpawnPos, MobRespawnPosition))
			return;
	}
	else if(SpawnType == BotsTypes::TYPE_BOT_NPC)
		SpawnPos = vec2(BotJob::ms_aNpcBot[m_SubBotID].m_PositionX, BotJob::ms_aNpcBot[m_SubBotID].m_PositionY);
	else if(SpawnType == BotsTypes::TYPE_BOT_QUEST)
		SpawnPos = vec2(BotJob::ms_aQuestBot[m_SubBotID].m_PositionX, BotJob::ms_aQuestBot[m_SubBotID].m_PositionY);
	
	const int AllocMemoryCell = MAX_CLIENTS*GS()->GetWorldID()+m_ClientID;
	m_pCharacter = new(AllocMemoryCell) CCharacterBotAI(&GS()->m_World);
	m_pCharacter->Spawn(this, SpawnPos);

	// so that no effects can be seen that an NPC that is not visible to one player is visible to another player
	if(SpawnType != BotsTypes::TYPE_BOT_QUEST)
		GS()->CreatePlayerSpawn(SpawnPos);

	// reset the dungeon, if allowed
	if (SpawnType == BotsTypes::TYPE_BOT_MOB && GS()->IsDungeon() && m_DungeonAllowedSpawn)
		m_DungeonAllowedSpawn = false;
}

/*
	0 - empty
	1 - is active draw only bot
	2 - is active draw bot and entities
*/
int CPlayerBot::IsActiveSnappingBot(int SnappingClient) const
{
	CPlayer* pSnappingPlayer = GS()->m_apPlayers[SnappingClient];
	if(SnappingClient < 0 || SnappingClient >= MAX_PLAYERS || !pSnappingPlayer)
		return 0;

	if(m_BotType == BotsTypes::TYPE_BOT_QUEST)
	{
		const int QuestID = BotJob::ms_aQuestBot[m_SubBotID].m_QuestID;
		if(pSnappingPlayer->GetQuest(QuestID).GetState() != QuestState::QUEST_ACCEPT) 
			return 0;

		if(BotJob::ms_aQuestBot[m_SubBotID].m_Step != pSnappingPlayer->GetQuest(QuestID).m_Step)
			return 0;
		
		// [first] quest bot active for player
		BotJob::ms_aDataBot[m_BotID].m_aAlreadySnapQuestBot[SnappingClient] = true;
	}

	if(m_BotType == BotsTypes::TYPE_BOT_NPC)
	{
		// [second] skip snapping for npc already snap on quest state
		if(BotJob::ms_aDataBot[m_BotID].m_aAlreadySnapQuestBot[SnappingClient])
			return 0;
		
		if(!IsActiveQuests(SnappingClient))
			return 1;
	}
	return 2;
}

void CPlayerBot::Snap(int SnappingClient)
{
	if(!Server()->ClientIngame(m_ClientID) || !IsActiveSnappingBot(SnappingClient))
		return;

	CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, m_ClientID, sizeof(CNetObj_PlayerInfo)));
	if(!pPlayerInfo)
		return;

	pPlayerInfo->m_PlayerFlags = PLAYERFLAG_READY;
	pPlayerInfo->m_Latency = 0;
	pPlayerInfo->m_Score = (m_BotType == BotsTypes::TYPE_BOT_MOB ? BotJob::ms_aMobBot[m_SubBotID].m_Level : 1);

	// --------------------- CUSTOM ----------------------
	if(!GS()->IsMmoClient(SnappingClient))
		return;

	CNetObj_Mmo_ClientInfo *pClientInfo = static_cast<CNetObj_Mmo_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_MMO_CLIENTINFO, m_ClientID, sizeof(CNetObj_Mmo_ClientInfo)));
	if(!pClientInfo)
		return;

	bool local_ClientID = (m_ClientID == SnappingClient);
	pClientInfo->m_Local = local_ClientID;
	pClientInfo->m_MoodType = GetMoodState(SnappingClient);
	pClientInfo->m_WorldType = GS()->Mmo()->WorldSwap()->GetWorldType();
	pClientInfo->m_HealthStart = GetStartHealth();
	pClientInfo->m_Health = GetHealth();
	pClientInfo->m_Level = GetBotLevel();
	pClientInfo->m_ActiveQuest = IsActiveQuests(SnappingClient);
	StrToInts(pClientInfo->m_StateName, 6, GetStatusBot());
}

int CPlayerBot::GetMoodState(int SnappingClient) const
{
	if(GetBotType() == BotsTypes::TYPE_BOT_MOB)
	{
		CCharacterBotAI *pChr = (CCharacterBotAI *)m_pCharacter;
		if(pChr && pChr->GetBotTarget() != m_ClientID)
		{
			if(pChr->GetBotTarget() == SnappingClient)
				return MOOD_AGRESSED_TANK;
			else 
				return MOOD_AGRESSED_OTHER;
		}
		else 
			return MOOD_ANGRY;
	}
	else if(GetBotType() == BotsTypes::TYPE_BOT_NPC)
		return MOOD_FRIENDLY;
	else if(GetBotType() == BotsTypes::TYPE_BOT_QUEST)
		return MOOD_QUESTING;
	return MOOD_NORMAL;
}

int CPlayerBot::GetBotLevel() const
{
	return (m_BotType == BotsTypes::TYPE_BOT_MOB ? BotJob::ms_aMobBot[m_SubBotID].m_Level : 1);
}

bool CPlayerBot::IsActiveQuests(int SnapClientID) const
{
	CPlayer* pSnappingPlayer = GS()->m_apPlayers[SnapClientID];
	if (SnapClientID >= MAX_PLAYERS || SnapClientID < 0 || !pSnappingPlayer)
		return false;

	if (m_BotType == BotsTypes::TYPE_BOT_QUEST)
		return true;

	if(m_BotType == BotsTypes::TYPE_BOT_NPC)
	{
		const int GivesQuest = GS()->Mmo()->BotsData()->GetQuestNPC(m_SubBotID);
		if(BotJob::ms_aNpcBot[m_SubBotID].m_Function == FunctionsNPC::FUNCTION_NPC_GIVE_QUEST && 
			pSnappingPlayer->GetQuest(GivesQuest).GetState() == QuestState::QUEST_NO_ACCEPT)
			return true;

		return false;
	}
	return false;
}

int CPlayerBot::GetEquippedItemID(int EquipID, int SkipItemID) const
{
	if (EquipID < EQUIP_HAMMER || EquipID > EQUIP_WINGS || EquipID == EQUIP_MINER)
		return -1;

	return BotJob::ms_aDataBot[m_BotID].m_aEquipSlot[EquipID];
}

const char* CPlayerBot::GetStatusBot() const
{
	if (m_BotType == BotsTypes::TYPE_BOT_QUEST)
	{
		const int QuestID = BotJob::ms_aQuestBot[m_SubBotID].m_QuestID;
		return GS()->GetQuestInfo(QuestID).GetName();
	}
	else if (m_BotType == BotsTypes::TYPE_BOT_MOB && BotJob::ms_aMobBot[m_SubBotID].m_Boss)
	{
		if (GS()->IsDungeon())
			return "Boss";
		
		return "Raid";
	}

	return "\0";
}

void CPlayerBot::GenerateNick(char* buffer, int size_buffer)
{
	static const int SIZE_GENERATE = 10;
	const char* FirstPos[SIZE_GENERATE] = { "Ja", "Qu", "Je", "Di", "Xo", "Us", "St", "Th", "Ge", "Re" };
	const char* LastPos[SIZE_GENERATE] = { "de", "sa", "ul", "ma", "sa", "py", "as", "al", "ly", "in" };
	if(GetBotType() == BotsTypes::TYPE_BOT_MOB && BotJob::ms_aMobBot[m_SubBotID].m_Spread > 0)
	{
		char aBuf[24];
		str_format(aBuf, sizeof(aBuf), "%s %s%s", BotJob::ms_aDataBot[m_BotID].m_aNameBot, FirstPos[random_int() % SIZE_GENERATE], LastPos[random_int() % SIZE_GENERATE]);
		str_copy(buffer, aBuf, size_buffer);
	}
	else if(GetBotType() == BotsTypes::TYPE_BOT_QUEST && BotJob::ms_aQuestBot[m_SubBotID].m_GenerateNick)
	{
		char aBuf[24];
		str_format(aBuf, sizeof(aBuf), "%s %s%s", BotJob::ms_aDataBot[m_BotID].m_aNameBot, FirstPos[random_int() % SIZE_GENERATE], LastPos[random_int() % SIZE_GENERATE]);
		str_copy(buffer, aBuf, size_buffer);
	}
	else
		str_copy(buffer, BotJob::ms_aDataBot[m_BotID].m_aNameBot, size_buffer);
}

// thread path finder
void CPlayerBot::TickThreadMobsPathFinder()
{
	if(!m_pCharacter || !m_pCharacter->IsAlive())
		return;

	if(GetBotType() == BotsTypes::TYPE_BOT_MOB)
	{
		if(m_TargetPos != vec2(0, 0) && (Server()->Tick() + 3 * m_ClientID) % (Server()->TickSpeed()) == 0)
			GS()->Mmo()->BotsData()->FindThreadPath(this, m_ViewPos, m_TargetPos);
		else if(m_TargetPos == vec2(0, 0) || distance(m_ViewPos, m_TargetPos) < 60.0f)
		{
			m_LastPosTick = Server()->Tick() + (Server()->TickSpeed() * 2 + rand() % 4);
			GS()->Mmo()->BotsData()->GetThreadRandomWaypointTarget(this);
		}
	}
}

void CPlayerBot::SendClientInfo(int TargetID)
{
	if((TargetID != -1 && (TargetID < 0 || TargetID >= MAX_PLAYERS || !Server()->ClientIngame(TargetID))) || m_BotType == BotsTypes::TYPE_BOT_FAKE)
		return;

	CNetMsg_Sv_ClientInfo ClientInfoMsg;
	ClientInfoMsg.m_ClientID = m_ClientID;
	ClientInfoMsg.m_Local = (bool)(m_ClientID == TargetID);
	ClientInfoMsg.m_Team = GetTeam();

	char aNickname[24];
	GenerateNick(aNickname, sizeof(aNickname));
	ClientInfoMsg.m_pName = aNickname;
	ClientInfoMsg.m_pClan = "::Bots::";
	ClientInfoMsg.m_Country = 0;
	ClientInfoMsg.m_Silent = true;
	for (int p = 0; p < 6; p++)
	{
		ClientInfoMsg.m_apSkinPartNames[p] = BotJob::ms_aDataBot[m_BotID].m_aaSkinNameBot[p];
		ClientInfoMsg.m_aUseCustomColors[p] = BotJob::ms_aDataBot[m_BotID].m_aUseCustomBot[p];
		ClientInfoMsg.m_aSkinPartColors[p] = BotJob::ms_aDataBot[m_BotID].m_aSkinColorBot[p];
	}

	// bot data it non static have accept it only world where it
	Server()->SendPackMsg(&ClientInfoMsg, MSGFLAG_VITAL|MSGFLAG_NORECORD, TargetID, GetPlayerWorldID());
}

int CPlayerBot::GetPlayerWorldID() const
{
	if(m_BotType == BotsTypes::TYPE_BOT_MOB) 
		return BotJob::ms_aMobBot[m_SubBotID].m_WorldID;
	else if(m_BotType == BotsTypes::TYPE_BOT_NPC) 
		return BotJob::ms_aNpcBot[m_SubBotID].m_WorldID;
	return BotJob::ms_aQuestBot[m_SubBotID].m_WorldID;
}