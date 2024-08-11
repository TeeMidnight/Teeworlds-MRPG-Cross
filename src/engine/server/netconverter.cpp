#include <engine/server.h>
#include <engine/message.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol6.h>
#include <engine/shared/snapshot.h>
#include <engine/shared/uuid.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontroller.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <generated/protocol.h>
#include <generated/protocol6.h>
#include <generated/protocolglue.h>

#include <generated/server_data.h>

#include <teeother/components/localization.h>

#include "netconverter.h"

enum
{
    MAX_DDNETSNAP_TYPE=0x7fff,
};

static int PickupTypeTo6(int Pickup)
{
	switch (Pickup)
	{
	case PICKUP_HEALTH: 
		return protocol6::POWERUP_HEALTH;
	case PICKUP_ARMOR: 
		return protocol6::POWERUP_ARMOR;
	case PICKUP_HAMMER: 
	case PICKUP_GUN: 
	case PICKUP_SHOTGUN: 
	case PICKUP_GRENADE:
	case PICKUP_LASER:  
		return protocol6::POWERUP_WEAPON;
	case PICKUP_NINJA: 
		return protocol6::POWERUP_NINJA;
	}
	return 0;
}

static int PickupSubtypeTo6(int Pickup)
{
	switch (Pickup)
	{
	case PICKUP_HEALTH: 
	case PICKUP_ARMOR: 
		return 0;
	case PICKUP_HAMMER: 
		return WEAPON_HAMMER;
	case PICKUP_GUN: 
		return WEAPON_GUN;
	case PICKUP_SHOTGUN: 
		return WEAPON_SHOTGUN;
	case PICKUP_GRENADE:
		return WEAPON_GRENADE;
	case PICKUP_LASER:  
		return WEAPON_LASER;
	case PICKUP_NINJA: 
		return WEAPON_NINJA;
	}
	return 0;
}

static int PlayerFlags_SevenToSix(int Flags)
{
	int Six = 0;
	if(Flags & PLAYERFLAG_CHATTING)
		Six |= protocol6::PLAYERFLAG_CHATTING;
	if(Flags & PLAYERFLAG_SCOREBOARD)
		Six |= protocol6::PLAYERFLAG_SCOREBOARD;
	return Six;
}

enum
{
	STR_TEAM_GAME,
	STR_TEAM_RED,
	STR_TEAM_BLUE,
	STR_TEAM_SPECTATORS,
};

static int GetStrTeam(int Team, bool Teamplay)
{
	if(Teamplay)
	{
		if(Team == TEAM_RED)
			return STR_TEAM_RED;
		else if(Team == TEAM_BLUE)
			return STR_TEAM_BLUE;
	}
	else if(Team == 0)
		return STR_TEAM_GAME;

	return STR_TEAM_SPECTATORS;
}

inline void AppendDecimals(char *pBuf, int Size, int Time, int Precision)
{
	if(Precision > 0)
	{
		char aInvalid[] = ".---";
		char aMSec[] = {
			'.',
			(char)('0' + (Time / 100) % 10),
			(char)('0' + (Time / 10) % 10),
			(char)('0' + Time % 10),
			0
		};
		char *pDecimals = Time < 0 ? aInvalid : aMSec;
		pDecimals[minimum(Precision, 3)+1] = 0;
		str_append(pBuf, pDecimals, Size);
	}
}

void FormatTime(char *pBuf, int Size, int Time, int Precision)
{
	if(Time < 0)
		str_copy(pBuf, "-:--", Size);
	else
		str_format(pBuf, Size, "%02d:%02d", Time / (60 * 1000), (Time / 1000) % 60);
	AppendDecimals(pBuf, Size, Time, Precision);
}

void FormatTimeDiff(char *pBuf, int Size, int Time, int Precision, bool ForceSign)
{
	const char *pPositive = ForceSign ? "+" : "";
	const char *pSign = Time < 0 ? "-" : pPositive;
	Time = absolute(Time);
	str_format(pBuf, Size, "%s%d", pSign, Time / 1000);
	AppendDecimals(pBuf, Size, Time, Precision);
}

CNetConverter::CNetConverter(IServer *pServer, class CConfiguration *pConfig) :
    m_pServer(pServer),
    m_pConfig(pConfig)
{
    m_GameFlags = 0;
    ResetSnapItemsEx();
}

static int PlayerFlags_SixToSeven(int Flags)
{
	int Seven = 0;
	if(Flags & protocol6::PLAYERFLAG_CHATTING)
		Seven |= PLAYERFLAG_CHATTING;
	if(Flags & protocol6::PLAYERFLAG_SCOREBOARD)
		Seven |= PLAYERFLAG_SCOREBOARD;
	return Seven;
}

static inline int MsgFromSevenDown(int Msg, bool System)
{
	if(System)
	{
		if(Msg == NETMSG_INFO)
			;
		else if(Msg >= 14 && Msg <= 19)
			Msg = NETMSG_READY + Msg - 14;
		else if(Msg >= 22 && Msg <= 23)
			Msg = NETMSG_PING + Msg - 22;
		else return -1;
	}

	return Msg;
}

bool CNetConverter::DeepConvertClientMsg6(CMsgUnpacker *pItem, int& Type, bool System, int FromClientID)
{
    if(System)
    {
        Type = MsgFromSevenDown(Type, System);
        if(Type == NETMSG_INPUT)
        {
            int LastAckedSnapshot = pItem->GetInt();
			int IntendedTick = pItem->GetInt();
			int Size = pItem->GetInt();

            if(pItem->Error() || Size / 4 > MAX_INPUT_SIZE)
				return false;

            int Data[MAX_INPUT_SIZE];
			for(int i = 0; i < Size / 4; i++)
				Data[i] = pItem->GetInt();

            CNetObj_PlayerInput *pInput = (CNetObj_PlayerInput *) Data;
            pInput->m_PlayerFlags = PlayerFlags_SixToSeven(pInput->m_PlayerFlags);

            CMsgPacker Msg7(NETMSG_INPUT, true);
            Msg7.AddInt(LastAckedSnapshot);
            Msg7.AddInt(IntendedTick);
            Msg7.AddInt(Size);

            // pack it
            for(int i = 0; i < Size / 4; i++)
                Msg7.AddInt(Data[i]);

            Msg7.AddInt(pItem->GetInt()); // PingCorrection

            pItem->ResetUnpack(Msg7.Data(), Msg7.Size()); // copy
        }
        return (Type != -1);
    }

    if(!GameServer()->m_apPlayers[FromClientID])
        return false;

    // language
    const char* pTempCode = Server()->GetClientLanguage(FromClientID);

    CPlayer *pPlayer = GameServer()->m_apPlayers[FromClientID];

    switch (Type)
    {
        case protocol6::NETMSGTYPE_CL_SAY:
        {
            int Team = pItem->GetInt();
            const char* pMessage = pItem->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);

            if(!pMessage || !pMessage[0])
                return false;

            CMsgPacker Msg7(NETMSGTYPE_CL_SAY, false, false);
            Msg7.AddInt(Team ? CHAT_TEAM : CHAT_ALL);
            Msg7.AddInt(-1);
            Msg7.AddString(pMessage, -1);

            pItem->ResetUnpack(Msg7.Data(), Msg7.Size());
            Type = NETMSGTYPE_CL_SAY;

            return true;
        }
        case protocol6::NETMSGTYPE_CL_SETTEAM:
        {
            int Team = pItem->GetInt();

            {
                CMsgPacker Msg7(NETMSGTYPE_CL_SETTEAM, false, false);
                Msg7.AddInt(Team);

                pItem->ResetUnpack(Msg7.Data(), Msg7.Size());
                Type = NETMSGTYPE_CL_SETTEAM;
            }

            return true;
        }
        case protocol6::NETMSGTYPE_CL_SETSPECTATORMODE:
        {
            int SpectatorID = pItem->GetInt();

            CMsgPacker Msg7(NETMSGTYPE_CL_SETSPECTATORMODE, false, false);
            Msg7.AddInt(SpectatorID == -1 ? SPEC_FREEVIEW : SPEC_PLAYER);
            Msg7.AddInt(SpectatorID);

            pItem->ResetUnpack(Msg7.Data(), Msg7.Size());
            Type = NETMSGTYPE_CL_SETSPECTATORMODE;

            return true;
        }
        case protocol6::NETMSGTYPE_CL_STARTINFO:
        {
            CMsgPacker Msg7(NETMSGTYPE_CL_STARTINFO, false, false);
            // name
            Msg7.AddString(pItem->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), -1);
            // clan
            Msg7.AddString(pItem->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), -1);
            // country
            Msg7.AddInt(pItem->GetInt());

            const char* pSkin = pItem->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
            int UseCustomColor = pItem->GetInt();
            int ColorBody = pItem->GetInt();
            int ColorFeet = pItem->GetInt();

            CTeeInfo TeeInfo(pSkin, UseCustomColor, ColorBody, ColorFeet);
            TeeInfo.FromSix();
            
            for(int i = 0; i < NUM_SKINPARTS; i ++)
			{
                Msg7.AddString(TeeInfo.m_aaSkinPartNames[i], MAX_SKIN_LENGTH);
			}
            for(int i = 0; i < NUM_SKINPARTS; i ++)
			{
                Msg7.AddInt(TeeInfo.m_aUseCustomColors[i]);
			}
            for(int i = 0; i < NUM_SKINPARTS; i ++)
			{
                Msg7.AddInt(TeeInfo.m_aSkinPartColors[i]);
			}

            pPlayer->Acc().m_Skin = TeeInfo;
            
            pItem->ResetUnpack(Msg7.Data(), Msg7.Size());
            Type = NETMSGTYPE_CL_STARTINFO;

            return true;
        }
        case protocol6::NETMSGTYPE_CL_CHANGEINFO:
        {
            const char* pName = pItem->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
            const char* pClan = pItem->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
            int Country = pItem->GetInt();

            // 0.7 couldn't change these things
            if(str_comp(pName, Server()->ClientName(FromClientID)) || 
                str_comp(pClan, Server()->ClientClan(FromClientID)) ||
                Country != Server()->ClientCountry(FromClientID))
            {
                if(m_aChatTick[FromClientID] + 5 * Server()->TickSpeed() > Server()->Tick())
                    return false;

                protocol6::CNetMsg_Sv_Chat Chat;
                Chat.m_ClientID = -1;
                Chat.m_pMessage = Server()->Localization()->Localize(pTempCode, "You must reconnect to change identity.");
                Chat.m_Team = 0;

                m_aChatTick[FromClientID] = Server()->Tick();

                Server()->SendPackMsg(&Chat, MSGFLAG_VITAL | MSGFLAG_NORECORD, FromClientID, -1, false);
                return false;
            }

            const char* pSkin = pItem->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
            int UseCustomColor = pItem->GetInt();
            int ColorBody = pItem->GetInt();
            int ColorFeet = pItem->GetInt();

            CTeeInfo TeeInfo(pSkin, UseCustomColor, ColorBody, ColorFeet);
            TeeInfo.FromSix();

            if(TeeInfo == pPlayer->Acc().m_Skin)
            {
                return false;
            }

            CMsgPacker Msg7(NETMSGTYPE_CL_SKINCHANGE, false, false);
            for(int i = 0; i < NUM_SKINPARTS; i ++)
			{
                Msg7.AddString(TeeInfo.m_aaSkinPartNames[i], MAX_SKIN_LENGTH);
			}
            for(int i = 0; i < NUM_SKINPARTS; i ++)
			{
                Msg7.AddInt(TeeInfo.m_aUseCustomColors[i]);
			}
            for(int i = 0; i < NUM_SKINPARTS; i ++)
			{
                Msg7.AddInt(TeeInfo.m_aSkinPartColors[i]);
			}

            pPlayer->Acc().m_Skin = TeeInfo;
            
            pItem->ResetUnpack(Msg7.Data(), Msg7.Size());
            Type = NETMSGTYPE_CL_SKINCHANGE;

            return true;
        }
        case protocol6::NETMSGTYPE_CL_KILL:
        {
            CMsgPacker Msg7(NETMSGTYPE_CL_KILL, false, false);

            pItem->ResetUnpack(Msg7.Data(), Msg7.Size());
            Type = NETMSGTYPE_CL_KILL;

            return true;
        }
        case protocol6::NETMSGTYPE_CL_EMOTICON:
        case protocol6::NETMSGTYPE_CL_VOTE:
        {
            CMsgPacker Msg7(Msg_SixToSeven(Type), false, false);
            Msg7.AddInt(pItem->GetInt());

            pItem->ResetUnpack(Msg7.Data(), Msg7.Size());
            Type = Msg_SixToSeven(Type);

            return true;
        }
        case protocol6::NETMSGTYPE_CL_CALLVOTE:
        {
            CMsgPacker Msg7(NETMSGTYPE_CL_CALLVOTE, false, false);
            Msg7.AddString(pItem->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), -1); // Type
            Msg7.AddString(pItem->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), -1); // Value
            Msg7.AddString(pItem->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), -1); // Reason
            Msg7.AddInt(0); // Force

            pItem->ResetUnpack(Msg7.Data(), Msg7.Size());
            Type = NETMSGTYPE_CL_CALLVOTE;

            return true;

        } 
    }
    

    return false;
}

bool CNetConverter::PrevConvertClientMsg(CMsgUnpacker *pItem, int& Type, bool System, int FromClientID)
{
    if(Server()->ClientProtocol(FromClientID) == NETPROTOCOL_SEVEN)
        return true;
    
    if(Server()->ClientProtocol(FromClientID) == NETPROTOCOL_SIX)
    {
        SetGameServer(Server()->GameServer(Server()->GetClientWorldID(FromClientID))->GS());
        return DeepConvertClientMsg6(pItem, Type, System, FromClientID);
    }

    return false;
}

int CNetConverter::GetExSnapID(const char *pUuidStr)
{
    CUuid Uuid = CalculateUuid(pUuidStr);
    for(int i = 0; i < m_NumSnapItemsEx; i ++)
    {
        if(m_SnapItemEx[i] == Uuid)
        {
            return MAX_DDNETSNAP_TYPE - i;
        }
    }
    int Index = m_NumSnapItemsEx ++;
    mem_copy(&m_SnapItemEx[Index], &Uuid, sizeof(m_SnapItemEx[Index]));
    return MAX_DDNETSNAP_TYPE - Index;
}

bool CNetConverter::DeepSnapConvert6(void *pItem, int Type, int ID, int Size, int ToClientID)
{
    // TODO: MOVE THESE TO THEIR FUNCTION
    // Type is 0.7, so we need switch check
    switch(Type)
    {
        case NETOBJTYPE_PROJECTILE:
        case NETOBJTYPE_LASER:
        case NETOBJTYPE_FLAG:
        case NETEVENTTYPE_EXPLOSION:
        case NETEVENTTYPE_SPAWN:
        case NETEVENTTYPE_HAMMERHIT:
        case NETEVENTTYPE_DEATH:
        case NETEVENTTYPE_SOUNDWORLD:
        {
            void *pObj = Server()->SnapNewItem(Obj_SevenToSix(Type), ID, Size);
            if(!pObj)
                return false;
            mem_copy(pObj, pItem, Size);
            return true;
        }
        case NETOBJTYPE_PICKUP:
        {
            CNetObj_Pickup *pObj7 = (CNetObj_Pickup *) pItem;
            protocol6::CNetObj_Pickup *pObj6 = static_cast<protocol6::CNetObj_Pickup *>(Server()->SnapNewItem(protocol6::NETOBJTYPE_PICKUP, ID, sizeof(protocol6::CNetObj_Pickup)));
            
            if(!pObj6)
                return false;
            pObj6->m_Type = PickupTypeTo6(pObj7->m_Type);
            pObj6->m_Subtype = PickupSubtypeTo6(pObj7->m_Type);
            pObj6->m_X = pObj7->m_X;
            pObj6->m_Y = pObj7->m_Y;

            return true;
        }
        case NETOBJTYPE_GAMEDATA:
        {
            CNetObj_GameData *pObj7 = (CNetObj_GameData *) pItem;
            protocol6::CNetObj_GameInfo *pObj6 = static_cast<protocol6::CNetObj_GameInfo *>(Server()->SnapNewItem(protocol6::NETOBJTYPE_GAMEINFO, ID, sizeof(protocol6::CNetObj_GameInfo)));
            
            if(!pObj6)
                return false;
            pObj6->m_GameFlags = m_GameFlags;
            pObj6->m_GameStateFlags = 0;
            if(pObj7->m_GameStateFlags&GAMESTATEFLAG_GAMEOVER || pObj7->m_GameStateFlags&GAMESTATEFLAG_ROUNDOVER)
                pObj6->m_GameStateFlags |= protocol6::GAMESTATEFLAG_GAMEOVER;
            if(pObj7->m_GameStateFlags&GAMESTATEFLAG_SUDDENDEATH)
                pObj6->m_GameStateFlags |= protocol6::GAMESTATEFLAG_SUDDENDEATH;
            if(pObj7->m_GameStateFlags&GAMESTATEFLAG_PAUSED && !(pObj7->m_GameStateFlags&GAMESTATEFLAG_STARTCOUNTDOWN)) // don't pause at count down
                pObj6->m_GameStateFlags |= protocol6::GAMESTATEFLAG_PAUSED;
            pObj6->m_RoundStartTick = pObj7->m_GameStartTick;
            
            pObj6->m_WarmupTimer = 0;
            if((pObj7->m_GameStateFlags&GAMESTATEFLAG_WARMUP || pObj7->m_GameStateFlags&GAMESTATEFLAG_STARTCOUNTDOWN) && pObj7->m_GameStateEndTick)
                pObj6->m_WarmupTimer = pObj7->m_GameStateEndTick - Server()->Tick();
            else if((pObj7->m_GameStateFlags&GAMESTATEFLAG_WARMUP || pObj7->m_GameStateFlags&GAMESTATEFLAG_STARTCOUNTDOWN) && (!pObj7->m_GameStateEndTick))
                pObj6->m_WarmupTimer = Server()->TickSpeed() * 999;

            pObj6->m_ScoreLimit = 0;
            pObj6->m_TimeLimit = 0;

            pObj6->m_RoundNum = 0;
            pObj6->m_RoundCurrent = 0;

            // DDNet NETOBJTYPE_GAMEINFOEX "gameinfo@netobj.ddnet.tw"
            protocol6::CNetObj_GameInfoEx *pObjDDNet = static_cast<protocol6::CNetObj_GameInfoEx *>(Server()->SnapNewItem(GetExSnapID("gameinfo@netobj.ddnet.tw"), ID, sizeof(protocol6::CNetObj_GameInfoEx)));
            if(!pObjDDNet)
                return false;
                
            pObjDDNet->m_Flags = 
                protocol6::GAMEINFOFLAG_ENTITIES_VANILLA |
                protocol6::GAMEINFOFLAG_PREDICT_VANILLA |
                protocol6::GAMEINFOFLAG_GAMETYPE_VANILLA |
                protocol6::GAMEINFOFLAG_DONT_MASK_ENTITIES; // if your mod is a race mod, change this
            
            pObjDDNet->m_Flags2 = 
                protocol6::GAMEINFOFLAG2_HUD_HEALTH_ARMOR |
                protocol6::GAMEINFOFLAG2_HUD_AMMO |
                protocol6::GAMEINFOFLAG2_HUD_DDRACE |
                protocol6::GAMEINFOFLAG2_NO_WEAK_HOOK; // if your mod is a race mod, change this
	        pObjDDNet->m_Version = protocol6::GAMEINFO_CURVERSION;
            
            return true;
        };
        case NETOBJTYPE_GAMEDATAFLAG:
        {
            CNetObj_GameDataFlag *pObj7 = (CNetObj_GameDataFlag *) pItem;
            protocol6::CNetObj_GameData *pObj6 = static_cast<protocol6::CNetObj_GameData *>(Server()->GetSnapItemData(protocol6::NETOBJTYPE_GAMEDATA, ID));
            if(!pObj6)
                pObj6 = static_cast<protocol6::CNetObj_GameData *>(Server()->SnapNewItem(protocol6::NETOBJTYPE_GAMEDATA, ID, sizeof(protocol6::CNetObj_GameData)));

            if(!pObj6)
                return false;
            pObj6->m_FlagCarrierRed = pObj7->m_FlagCarrierRed;
            pObj6->m_FlagCarrierBlue = pObj7->m_FlagCarrierBlue;
            pObj6->m_TeamscoreRed = pObj6->m_TeamscoreRed;
            pObj6->m_TeamscoreBlue = pObj6->m_TeamscoreBlue;

            return true;
        };
        case NETOBJTYPE_GAMEDATATEAM:
        {
            CNetObj_GameDataTeam *pObj7 = (CNetObj_GameDataTeam *) pItem;
            protocol6::CNetObj_GameData *pObj6 = static_cast<protocol6::CNetObj_GameData *>(Server()->GetSnapItemData(protocol6::NETOBJTYPE_GAMEDATA, ID));
            bool NewCreate = false;
            if(!pObj6)
            {
                pObj6 = static_cast<protocol6::CNetObj_GameData *>(Server()->SnapNewItem(protocol6::NETOBJTYPE_GAMEDATA, ID, sizeof(protocol6::CNetObj_GameData)));
                NewCreate = true;
            }
            if(!pObj6)
                return false;
            pObj6->m_FlagCarrierRed = NewCreate ? FLAG_ATSTAND : pObj6->m_FlagCarrierRed; // did a fake snap for some mode need flag but no carry
            pObj6->m_FlagCarrierBlue = NewCreate ? FLAG_ATSTAND : pObj6->m_FlagCarrierBlue;
            pObj6->m_TeamscoreRed = pObj7->m_TeamscoreRed;
            pObj6->m_TeamscoreBlue = pObj7->m_TeamscoreBlue;

            return true;
        };
        case NETOBJTYPE_CHARACTER: // not game core, beacuse we don't need snap game core
        {
            CNetObj_Character *pObj7 = (CNetObj_Character *) pItem;
            protocol6::CNetObj_Character *pObj6 = static_cast<protocol6::CNetObj_Character *>(Server()->SnapNewItem(protocol6::NETOBJTYPE_CHARACTER, ID, sizeof(protocol6::CNetObj_Character)));
            
            int ClientID = ID;
            if(!GameServer()->m_apPlayers[ClientID])
                return false;

            if(!pObj6)
                return false;
            pObj6->m_AmmoCount = pObj7->m_Weapon == WEAPON_NINJA ? 0 : pObj7->m_AmmoCount;
            pObj6->m_Angle = pObj7->m_Angle;
            pObj6->m_Armor = pObj7->m_Armor;
            pObj6->m_AttackTick = pObj7->m_AttackTick;
            pObj6->m_Direction = pObj7->m_Direction;
            pObj6->m_Emote = pObj7->m_Emote;
            pObj6->m_Health = pObj7->m_Health;

            // hooks
            pObj6->m_HookDx = pObj7->m_HookDx;
            pObj6->m_HookDy = pObj7->m_HookDy;
            pObj6->m_HookedPlayer = pObj7->m_HookedPlayer;
            pObj6->m_HookState = pObj7->m_HookState;
            pObj6->m_HookTick = pObj7->m_HookTick;
            pObj6->m_HookX = pObj7->m_HookX;
            pObj6->m_HookY = pObj7->m_HookY;

            pObj6->m_Jumped = pObj7->m_Jumped;
            pObj6->m_PlayerFlags = PlayerFlags_SevenToSix(GameServer()->m_apPlayers[ClientID]->m_PlayerFlags);
            pObj6->m_Tick = pObj7->m_Tick;
            pObj6->m_VelX = pObj7->m_VelX;
            pObj6->m_VelY = pObj7->m_VelY;
            pObj6->m_Weapon = pObj7->m_Weapon;
            pObj6->m_X = pObj7->m_X;
            pObj6->m_Y = pObj7->m_Y;

            CCharacter *pFrom = GameServer()->GetPlayerChar(ClientID);

            // DDNet NETOBJTYPE_DDNETCHARACTER "character@netobj.ddnet.tw"
            protocol6::CNetObj_DDNetCharacter *pObjDDNet = static_cast<protocol6::CNetObj_DDNetCharacter *>(Server()->SnapNewItem(GetExSnapID("character@netobj.ddnet.tw"), ID, sizeof(protocol6::CNetObj_DDNetCharacter)));
            if(!pObjDDNet)
                return false;
            
            pObjDDNet->m_Flags = 0;
            if(pFrom->WeaponStat(WEAPON_HAMMER)->m_Got)
                pObjDDNet->m_Flags |= protocol6::CHARACTERFLAG_WEAPON_HAMMER;
            if(pFrom->WeaponStat(WEAPON_GUN)->m_Got)
                pObjDDNet->m_Flags |= protocol6::CHARACTERFLAG_WEAPON_GUN;
            if(pFrom->WeaponStat(WEAPON_SHOTGUN)->m_Got)
                pObjDDNet->m_Flags |= protocol6::CHARACTERFLAG_WEAPON_SHOTGUN;
            if(pFrom->WeaponStat(WEAPON_GRENADE)->m_Got)
                pObjDDNet->m_Flags |= protocol6::CHARACTERFLAG_WEAPON_GRENADE;
            if(pFrom->WeaponStat(WEAPON_LASER)->m_Got)
                pObjDDNet->m_Flags |= protocol6::CHARACTERFLAG_WEAPON_LASER;
            if(pFrom->WeaponStat(WEAPON_NINJA)->m_Got)
                pObjDDNet->m_Flags |= protocol6::CHARACTERFLAG_WEAPON_NINJA;
            if(!GameServer()->Tuning()->m_PlayerCollision)
                pObjDDNet->m_Flags |= protocol6::CHARACTERFLAG_COLLISION_DISABLED;
            if(!GameServer()->Tuning()->m_PlayerHooking)
                pObjDDNet->m_Flags |= protocol6::CHARACTERFLAG_HOOK_HIT_DISABLED;
            pObjDDNet->m_FreezeStart = -1;
            pObjDDNet->m_FreezeEnd = 0;
            pObjDDNet->m_Jumps = 2;
            pObjDDNet->m_TeleCheckpoint = -1; // if your mod is race mod, please change this
            pObjDDNet->m_StrongWeakID = 0;
            pObjDDNet->m_NinjaActivationTick = 0;
            pObjDDNet->m_JumpedTotal = pFrom->Core()->m_JumpCounter;
            pObjDDNet->m_TargetX = pFrom->Core()->m_Input.m_TargetX;
            pObjDDNet->m_TargetY = pFrom->Core()->m_Input.m_TargetY;

            return true;
        }
        case NETOBJTYPE_PLAYERINFO:
        {
            protocol6::CNetObj_ClientInfo *pClientInfo = static_cast<protocol6::CNetObj_ClientInfo *>(Server()->SnapNewItem(protocol6::NETOBJTYPE_CLIENTINFO, ID, sizeof(protocol6::CNetObj_ClientInfo)));
            
            int ClientID = ID; // use id map next update.
            if(!GameServer()->m_apPlayers[ClientID])
                return false;

            CTeeInfo TeeInfos = GameServer()->m_apPlayers[ClientID]->Acc().m_Skin;

            if(!pClientInfo)
                return false;
            StrToInts(&pClientInfo->m_Name0, 4, Server()->ClientName(ClientID));
            StrToInts(&pClientInfo->m_Clan0, 3, Server()->ClientClan(ClientID));
            pClientInfo->m_Country = Server()->ClientCountry(ClientID);
            StrToInts(&pClientInfo->m_Skin0, 6, TeeInfos.m_aSkinName);
            pClientInfo->m_UseCustomColor = TeeInfos.m_UseCustomColor;
            pClientInfo->m_ColorBody = TeeInfos.m_ColorBody;
            pClientInfo->m_ColorFeet = TeeInfos.m_ColorFeet;

            CNetObj_PlayerInfo *pObj7 = (CNetObj_PlayerInfo *) pItem;
            protocol6::CNetObj_PlayerInfo *pObj6 = static_cast<protocol6::CNetObj_PlayerInfo *>(Server()->SnapNewItem(protocol6::NETOBJTYPE_PLAYERINFO, ID, sizeof(protocol6::CNetObj_PlayerInfo)));
            
            if(!pObj6)
                return false;

            pObj6->m_ClientID = ClientID;
            pObj6->m_Latency = pObj7->m_Latency;
            pObj6->m_Local = (ClientID == ToClientID) ? 1 : 0;
            pObj6->m_Score = pObj7->m_Score;
            pObj6->m_Team = GameServer()->m_apPlayers[ClientID]->GetTeam();

            // DDNet NETOBJTYPE_DDNETPLAYER "player@netobj.ddnet.tw"
            protocol6::CNetObj_DDNetPlayer *pObjDDNet = static_cast<protocol6::CNetObj_DDNetPlayer *>(Server()->SnapNewItem(GetExSnapID("player@netobj.ddnet.tw"), ID, sizeof(protocol6::CNetObj_DDNetPlayer)));
            if(!pObjDDNet)
                return false;
            
            pObjDDNet->m_AuthLevel = pObj7->m_PlayerFlags&PLAYERFLAG_ADMIN ? 2 : 0;
            pObjDDNet->m_Flags = 0;

            return true;
        }
        case NETOBJTYPE_SPECTATORINFO:
        {
            CNetObj_SpectatorInfo *pObj7 = (CNetObj_SpectatorInfo *) pItem;
            protocol6::CNetObj_SpectatorInfo *pObj6 = static_cast<protocol6::CNetObj_SpectatorInfo *>(Server()->SnapNewItem(protocol6::NETOBJTYPE_SPECTATORINFO, ID, sizeof(protocol6::CNetObj_SpectatorInfo)));
            
            if(!pObj6)
                return false;
            pObj6->m_SpectatorID = pObj7->m_SpecMode == SPEC_FREEVIEW ? protocol6::SPEC_FREEVIEW : pObj7->m_SpectatorID;
            pObj6->m_X = pObj7->m_X;
            pObj6->m_Y = pObj7->m_Y;

            return true;
        }
        // event
        case NETEVENTTYPE_DAMAGE:
        {
            CNetEvent_Damage *pEvent7 = (CNetEvent_Damage*) pItem;
            float Angle = pEvent7->m_Angle / 256.0f;
	        float a = 3 * 3.14159f / 2 + Angle;
            float s = a-pi/3;
            float e = a+pi/3;
            for(int i = 0; i < pEvent7->m_ArmorAmount + pEvent7->m_HealthAmount; i++)
            {
                float f = mix(s, e, float(i+1)/float(pEvent7->m_ArmorAmount + pEvent7->m_HealthAmount+2));
                protocol6::CNetEvent_DamageInd *pEvent6 = static_cast<protocol6::CNetEvent_DamageInd *>(Server()->SnapNewItem(protocol6::NETEVENTTYPE_DAMAGEIND, m_EventID, sizeof(protocol6::CNetEvent_DamageInd)));
                pEvent6->m_X = pEvent7->m_X;
                pEvent6->m_Y = pEvent7->m_Y;
                pEvent6->m_Angle = (int)(f*256.0f);
                
                m_EventID ++;
		    }
            return true;
        }
    }
    return false;
}

bool CNetConverter::SnapItemConvert(void *pItem, int Type, int ID, int Size, int ToClientID)
{
    m_GameFlags = GameServer()->m_pController->GameFlags();
    
    void *pObj = nullptr; // Snap Obj
    if(Server()->ClientProtocol(ToClientID) == NETPROTOCOL_SEVEN) // do nothing
    {
        pObj = Server()->SnapNewItem(Type, ID, Size);
        if(!pObj)
            return false;
        mem_copy(pObj, pItem, Size);
    }

    if(Server()->ClientProtocol(ToClientID) == NETPROTOCOL_SIX)
    {
        SetGameServer(Server()->GameServer(Server()->GetClientWorldID(ToClientID))->GS());
        return DeepSnapConvert6(pItem, Type, ID, Size, ToClientID);
    }

    return true;
}

void CNetConverter::RebuildSnapshot(class CSnapshotBuilder *pSnapshotBuilder, int ClientID)
{
    if(Server()->ClientProtocol(ClientID) == NETPROTOCOL_SEVEN)
        return; // pass

    CSnapshotBuilder TempBuilder;
    mem_copy(&TempBuilder, pSnapshotBuilder, sizeof(CSnapshotBuilder));

    pSnapshotBuilder->Init();
    for(int i = 0; i < TempBuilder.NumItems(); i ++)
    {
        CSnapshotItem *pItem = TempBuilder.GetItem(i);
        int SnapID = pItem->ID();
        int SnapType = pItem->Type();
        int Size = i == TempBuilder.NumItems() - 1 ? TempBuilder.GetDataSize() - TempBuilder.GetOffest(i) : 
            TempBuilder.GetOffest(i + 1) - TempBuilder.GetOffest(i);
        Size -= sizeof(CSnapshotItem);

        SnapItemConvert(pItem->Data(), SnapType, SnapID, Size, ClientID);
    }
}

int CNetConverter::DeepMsgConvert6(CMsgPacker *pMsg, int Flags, int ToClientID)
{
    CUnpacker Unpacker;
    Unpacker.Reset(pMsg->Data(), pMsg->Size());

    if(Unpacker.Error())
        return -1;

    // language
    const char* pTempCode = Server()->GetClientLanguage(ToClientID);

    switch (pMsg->Type())
    {
        case NETMSGTYPE_SV_MOTD:
        case NETMSGTYPE_SV_BROADCAST:
        case NETMSGTYPE_SV_KILLMSG:
        case NETMSGTYPE_SV_READYTOENTER:
        case NETMSGTYPE_SV_WEAPONPICKUP:
        case NETMSGTYPE_SV_EMOTICON:
        case NETMSGTYPE_SV_VOTECLEAROPTIONS:
        case NETMSGTYPE_SV_VOTEOPTIONLISTADD:
        case NETMSGTYPE_SV_VOTEOPTIONADD:
        case NETMSGTYPE_SV_VOTEOPTIONREMOVE:
        case NETMSGTYPE_SV_VOTESTATUS:
        {
            CMsgPacker Msg6(Msg_SevenToSix(pMsg->Type()), false, false);
            Msg6.AddRaw(pMsg->Data(), pMsg->Size());

            return Server()->SendMsg(&Msg6, Flags, ToClientID);
        }
        case NETMSGTYPE_SV_CHAT:
        {
            CMsgPacker Msg6(protocol6::NETMSGTYPE_SV_CHAT, false, false);

            int Mode = Unpacker.GetInt(); // Mode
            int ChatterClientID = Unpacker.GetInt();
            int TargetID = Unpacker.GetInt();
            const char* pMessage = Unpacker.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);

            int Team = 0;
            if(ChatterClientID != -1 && Mode == CHAT_WHISPER)
            {
                char aChat[512];
                str_format(aChat, sizeof(aChat), "(%s): %s", "Whisper", pMessage);
                // 3 = recv, 2 = send
                Msg6.AddInt(TargetID == ToClientID ? 3 : 2); // Team
                Msg6.AddInt(TargetID == ToClientID ? ChatterClientID : TargetID);
                Msg6.AddString(aChat, -1);
            }
            else
            {
                if(Mode == CHAT_TEAM)
                    Team = 1;
                Msg6.AddInt(Team);
                Msg6.AddInt(ChatterClientID);
                Msg6.AddString(pMessage, -1);
            }

            return Server()->SendMsg(&Msg6, Flags, ToClientID);
        }
        case NETMSGTYPE_SV_TEAM:
        {
            int ClientID = Unpacker.GetInt();
            int Team = Unpacker.GetInt();
            int Silent = Unpacker.GetInt();
            Unpacker.GetInt(); // cool down tick, nothing to do

            if(Silent)
                return 0;
            
            // send chat
	        dynamic_string Buffer;
            Buffer.clear();
            switch(GetStrTeam(Team, m_GameFlags&GAMEFLAG_TEAMS))
            {
                case STR_TEAM_GAME: Server()->Localization()->Format_L(Buffer, pTempCode, "'{STR}' joined the game", Server()->ClientName(ClientID), nullptr); break;
                case STR_TEAM_RED: Server()->Localization()->Format_L(Buffer, pTempCode, "'{STR}' joined the red team", Server()->ClientName(ClientID), nullptr); break;
                case STR_TEAM_BLUE: Server()->Localization()->Format_L(Buffer, pTempCode, "'{STR}' joined the blue team", Server()->ClientName(ClientID), nullptr); break;
                case STR_TEAM_SPECTATORS: Server()->Localization()->Format_L(Buffer, pTempCode, "'{STR}' joined the spectators", Server()->ClientName(ClientID), nullptr); break;
            }
            protocol6::CNetMsg_Sv_Chat Chat;
            Chat.m_ClientID = -1;
            Chat.m_pMessage = Buffer.buffer();
            Chat.m_Team = 0;

            return Server()->SendPackMsg(&Chat, Flags, ToClientID, -1, false);
        }
        case NETMSGTYPE_SV_TUNEPARAMS:
        {  
            CMsgPacker Msg6(protocol6::NETMSGTYPE_SV_TUNEPARAMS, false, false);

            int Pos = -1;
            do
            {
                Pos ++;
                int Int = Unpacker.GetInt();
                if(!Unpacker.Error())
                    Msg6.AddInt(Int);
                if(Pos == 29)
                    Msg6.AddInt(g_pData->m_Weapons.m_aId[WEAPON_LASER].m_Damage);
            }while(Unpacker.Error());

            return Server()->SendMsg(&Msg6, Flags, ToClientID);
        }
        case NETMSGTYPE_SV_VOTESET:
        {
            int ClientID = Unpacker.GetInt(); // ClientID
            int Type = Unpacker.GetInt(); // Type
            int Timeout = Unpacker.GetInt();
            const char* pDesc = Unpacker.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
            const char* pReason = Unpacker.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
            
            if(Timeout)
            {
                if(ClientID != -1)
                {
                    dynamic_string Buffer;
                    Buffer.clear();
                    switch(Type)
                    {
                        case VOTE_START_OP:
                            {
                                Server()->Localization()->Format_L(Buffer, pTempCode, "'{STR}' called vote to change server option '{STR}' ({STR})", Server()->ClientName(ClientID), pDesc, pReason, nullptr);
                                break;
                            }
                        case VOTE_START_KICK:
                            {
                                Server()->Localization()->Format_L(Buffer, pTempCode, "'{STR}' called for vote to kick '{STR}' ({STR})", Server()->ClientName(ClientID), pDesc, pReason, nullptr);
                                break;
                            }
                        case VOTE_START_SPEC:
                            {
                                Server()->Localization()->Format_L(Buffer, pTempCode, "'{STR}' called for vote to move '{STR}' to spectators ({STR})", Server()->ClientName(ClientID), pDesc, pReason, nullptr);
                            }
                    }

                    protocol6::CNetMsg_Sv_Chat Chat;
                    Chat.m_ClientID = -1;
                    Chat.m_pMessage = Buffer.buffer();
                    Chat.m_Team = 0;

                    Server()->SendPackMsg(&Chat, Flags, ToClientID, -1, false);
                }
            }
            else
            {
                dynamic_string Buffer;
                Buffer.clear();
                switch(Type)
                {
                    case VOTE_START_OP:
                        Server()->Localization()->Format_L(Buffer, pTempCode, "Admin forced server option '{STR}' ({STR})", pDesc, pReason, nullptr);
                        break;
                    case VOTE_START_SPEC:
                        Server()->Localization()->Format_L(Buffer, pTempCode, "Admin moved '{STR}' to spectator ({STR})", pDesc, pReason, nullptr);
                        break;
                    case VOTE_END_ABORT:
                        Buffer.copy(Server()->Localization()->Localize(pTempCode, "Vote aborted"));
                        break;
                    case VOTE_END_PASS:
                        Buffer.copy(Server()->Localization()->Localize(pTempCode, ClientID == -1 ? "Admin forced vote yes" : "Vote passed"));
                        break;
                    case VOTE_END_FAIL:
                        Buffer.copy(Server()->Localization()->Localize(pTempCode, ClientID == -1 ? "Admin forced vote no" : "Vote failed"));
                        break;
                }

                protocol6::CNetMsg_Sv_Chat Chat;
                Chat.m_ClientID = -1;
                Chat.m_pMessage = Buffer.buffer();
                Chat.m_Team = 0;

                Server()->SendPackMsg(&Chat, Flags, ToClientID, -1, false);
            }

            CMsgPacker Msg6(protocol6::NETMSGTYPE_SV_VOTESET, false, false);
            
            Msg6.AddInt(Timeout); // Timeout
            Msg6.AddString(pDesc, -1); // Desc
            Msg6.AddString(pReason, -1); // Reason

            return Server()->SendMsg(&Msg6, Flags, ToClientID);
        }
        case NETMSGTYPE_SV_SERVERSETTINGS:
        {
            // TODO: add message "Team were lock"
            break;
        }
        case NETMSGTYPE_SV_CLIENTINFO:
        {
            Unpacker.GetInt(); // ClientID
            if(Unpacker.GetInt()) // Local
                return 0; // do not send
            int Team = Unpacker.GetInt();
            const char* pName = Unpacker.GetString(); // Name
            Unpacker.GetString(); // Clan
            Unpacker.GetInt(); // Country

            for(int i = 0; i < 6; i ++)
                Unpacker.GetString(); // see src/generated/protocol.h or protocol.cpp
            for(int i = 0; i < 12; i ++)
                Unpacker.GetInt(); // see src/generated/protocol.h or protocol.cpp

            int Silent = Unpacker.GetInt();
            if(Silent)
                return 0;

            dynamic_string Buffer;
            Buffer.clear();
            switch(GetStrTeam(Team, m_GameFlags&GAMEFLAG_TEAMS))
            {
                case STR_TEAM_GAME: Server()->Localization()->Format_L(Buffer, pTempCode, "'{STR}' entered and joined the game", pName, nullptr); break;
                case STR_TEAM_RED: Server()->Localization()->Format_L(Buffer, pTempCode, "'{STR}' entered and joined the red team", pName, nullptr); break;
                case STR_TEAM_BLUE: Server()->Localization()->Format_L(Buffer, pTempCode, "'{STR}' entered and joined the blue team", pName, nullptr); break;
                case STR_TEAM_SPECTATORS: Server()->Localization()->Format_L(Buffer, pTempCode, "'{STR}' entered and joined the spectators", pName, nullptr); break;
            }

            protocol6::CNetMsg_Sv_Chat Chat;
            Chat.m_ClientID = -1;
            Chat.m_pMessage = Buffer.buffer();
            Chat.m_Team = 0;

            return Server()->SendPackMsg(&Chat, Flags, ToClientID, -1, false);
        }
        case NETMSGTYPE_SV_CLIENTDROP:
        {
            int ClientID = Unpacker.GetInt();
            const char* pReason = Unpacker.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
            int Silent = Unpacker.GetInt();

            if(Silent)
                return 0;

            dynamic_string Buffer;
            Buffer.clear();
            if(pReason[0])
                Server()->Localization()->Format_L(Buffer, pTempCode, "'{STR}' has left the game ({STR})", Server()->ClientName(ClientID), pReason, nullptr);
            else
                Server()->Localization()->Format_L(Buffer, pTempCode, "'{STR}' has left the game", Server()->ClientName(ClientID), nullptr);
            
            protocol6::CNetMsg_Sv_Chat Chat;
            Chat.m_ClientID = -1;
            Chat.m_pMessage = Buffer.buffer();
            Chat.m_Team = 0;

            return Server()->SendPackMsg(&Chat, Flags, ToClientID, -1, false);
        }
        case NETMSGTYPE_SV_GAMEMSG:
        {
            return DeepGameMsgConvert6(pMsg, Flags, ToClientID);
        }
        case NETMSGTYPE_SV_RACEFINISH:
        {
            return -1;
        }
        case NETMSGTYPE_SV_COMMANDINFO:
        {
            // DDNet message NETMSGTYPE_SV_COMMANDINFO
            CMsgPacker MsgDDNet(0, false, false);
            CUuid Uuid = CalculateUuid("commandinfo@netmsg.ddnet.org");
            MsgDDNet.AddRaw(&Uuid, sizeof(Uuid));
            int Size = Unpacker.Size();
            MsgDDNet.AddRaw(Unpacker.GetRaw(Size), Size);

            return Server()->SendMsg(&MsgDDNet, MSGFLAG_VITAL | MSGFLAG_NORECORD, ToClientID);
        }
        case NETMSGTYPE_SV_COMMANDINFOREMOVE:
        {
            // DDNet message NETMSGTYPE_SV_COMMANDINFOREMOVE
            CMsgPacker MsgDDNet(0, false, false);
            CUuid Uuid = CalculateUuid("commandinfo-remove@netmsg.ddnet.org");
            MsgDDNet.AddRaw(&Uuid, sizeof(Uuid));
            int Size = Unpacker.Size();
            MsgDDNet.AddRaw(Unpacker.GetRaw(Size), Size);

            return Server()->SendMsg(&MsgDDNet, MSGFLAG_VITAL | MSGFLAG_NORECORD, ToClientID);
        }
    }
    return -1;
}

int CNetConverter::DeepGameMsgConvert6(CMsgPacker *pMsg, int Flags, int ToClientID)
{
    CUnpacker Unpacker;
    Unpacker.Reset(pMsg->Data(), pMsg->Size());

    // language
    const char *pTempCode = Server()->GetClientLanguage(ToClientID);

    int GameMsgID = Unpacker.GetInt();
    switch (GameMsgID)
    {
        case GAMEMSG_TEAM_SWAP:
        {
            protocol6::CNetMsg_Sv_Chat Chat;
            Chat.m_ClientID = -1;
            Chat.m_pMessage = Server()->Localization()->Localize(pTempCode, "Teams were swapped");
            Chat.m_Team = 0;
            
            return Server()->SendPackMsg(&Chat, Flags, ToClientID, -1, false);
        }
        case GAMEMSG_SPEC_INVALIDID:
        {
            protocol6::CNetMsg_Sv_Chat Chat;
            Chat.m_ClientID = -1;
            Chat.m_pMessage = Server()->Localization()->Localize(pTempCode, "Invalid spectator id used");
            Chat.m_Team = 0;
            
            return Server()->SendPackMsg(&Chat, Flags, ToClientID, -1, false);
        }
        case GAMEMSG_TEAM_SHUFFLE:
        {
            protocol6::CNetMsg_Sv_Chat Chat;
            Chat.m_ClientID = -1;
            Chat.m_pMessage = Server()->Localization()->Localize(pTempCode, "Teams were shuffled");
            Chat.m_Team = 0;
            
            return Server()->SendPackMsg(&Chat, Flags, ToClientID, -1, false);
        }
        case GAMEMSG_TEAM_BALANCE:
        {
            protocol6::CNetMsg_Sv_Chat Chat;
            Chat.m_ClientID = -1;
            Chat.m_pMessage = Server()->Localization()->Localize(pTempCode, "Teams have been balanced");
            Chat.m_Team = 0;
            
            return Server()->SendPackMsg(&Chat, Flags, ToClientID, -1, false);
        }
        case GAMEMSG_CTF_DROP:
        {
            protocol6::CNetMsg_Sv_SoundGlobal Sound;
            Sound.m_SoundID = SOUND_CTF_DROP;
            
            return Server()->SendPackMsg(&Sound, Flags, ToClientID, -1, false);
        }
        case GAMEMSG_CTF_RETURN:
        {
            protocol6::CNetMsg_Sv_SoundGlobal Sound;
            Sound.m_SoundID = SOUND_CTF_RETURN;
            
            return Server()->SendPackMsg(&Sound, Flags, ToClientID, -1, false);
        }
        case GAMEMSG_TEAM_ALL:
        {
            const char *pMsg = "";
            switch(GetStrTeam(Unpacker.GetInt(), m_GameFlags&GAMEFLAG_TEAMS))
            {
                case STR_TEAM_GAME: pMsg = Server()->Localization()->Localize(pTempCode, "All players were moved to the game"); break;
                case STR_TEAM_RED: pMsg = Server()->Localization()->Localize(pTempCode, "All players were moved to the red team"); break;
                case STR_TEAM_BLUE: pMsg = Server()->Localization()->Localize(pTempCode, "All players were moved to the blue team"); break;
                case STR_TEAM_SPECTATORS: pMsg = Server()->Localization()->Localize(pTempCode, "All players were moved to the spectators"); break;
            }

            protocol6::CNetMsg_Sv_Chat Chat;
            Chat.m_ClientID = -1;
            Chat.m_pMessage = pMsg;
            Chat.m_Team = 0;
            
            return Server()->SendPackMsg(&Chat, Flags, ToClientID, -1, false);
        }
    }
    return -1;
}

int CNetConverter::SendMsgConvert(CMsgPacker *pMsg, int Flags, int ToClientID, int Depth)
{
    // send a msg for record
    if(Depth == 0)
    {
        CMsgPacker Msg7(pMsg->Type(), false, false);
        Msg7.AddRaw(pMsg->Data(), pMsg->Size());

        Server()->SendMsg(&Msg7, MSGFLAG_NOSEND | Flags, -1);
    }

    if(ToClientID == -1)
    {
        for(int i = 0 ; i < MAX_CLIENTS; i ++)
        {
            SendMsgConvert(pMsg, Flags, i, Depth + 1);
        }
        return 0;
    }
    else if(Server()->ClientProtocol(ToClientID) == NETPROTOCOL_SEVEN)
    {
        CMsgPacker Msg7(pMsg->Type(), false, false);
        Msg7.AddRaw(pMsg->Data(), pMsg->Size());

        return Server()->SendMsg(&Msg7, Flags | MSGFLAG_NORECORD, ToClientID); // no record
    }
    else if(Server()->ClientProtocol(ToClientID) == NETPROTOCOL_SIX)
    {
        SetGameServer(Server()->GameServer(Server()->GetClientWorldID(ToClientID))->GS());
        return DeepMsgConvert6(pMsg, Flags | MSGFLAG_NORECORD, ToClientID); // no record
    }
    return -1;
}

static int SystemMsg7To6(int Msg)
{
    switch (Msg)
    {
        case NETMSG_MAP_DATA: return protocol6::NETMSG_MAP_DATA;
        case NETMSG_CON_READY: return protocol6::NETMSG_CON_READY;
        case NETMSG_SNAP: return protocol6::NETMSG_SNAP;
        case NETMSG_SNAPEMPTY: return protocol6::NETMSG_SNAPEMPTY;
        case NETMSG_SNAPSINGLE: return protocol6::NETMSG_SNAPSINGLE;
        case NETMSG_SNAPSMALL: return protocol6::NETMSG_SNAPSMALL;
        case NETMSG_INPUTTIMING: return protocol6::NETMSG_INPUTTIMING;
        case NETMSG_RCON_LINE: return protocol6::NETMSG_RCON_LINE;
        case NETMSG_RCON_CMD_ADD: return protocol6::NETMSG_RCON_CMD_ADD;
        case NETMSG_RCON_CMD_REM: return protocol6::NETMSG_RCON_CMD_REM;
        case NETMSG_PING: return protocol6::NETMSG_PING;
        case NETMSG_PING_REPLY: return protocol6::NETMSG_PING_REPLY;
    }
    return -1;
}

int CNetConverter::DeepSystemMsgConvert6(CMsgPacker *pMsg, int Flags, int ToClientID)
{
    CUnpacker Unpacker;
    Unpacker.Reset(pMsg->Data(), pMsg->Size());

    if(Unpacker.Error())
        return -1;

    switch (pMsg->Type())
    {
        case NETMSG_MAP_DATA: // see server.cpp : NETMSG_REQUEST_MAP_DATA
        case NETMSG_CON_READY:
        case NETMSG_SNAP:
        case NETMSG_SNAPEMPTY:
        case NETMSG_SNAPSINGLE:
        case NETMSG_SNAPSMALL:
        case NETMSG_INPUTTIMING:
        case NETMSG_RCON_LINE:
        case NETMSG_RCON_CMD_ADD:
        case NETMSG_RCON_CMD_REM:
        case NETMSG_PING:
        case NETMSG_PING_REPLY:
        {
            CMsgPacker Msg6(SystemMsg7To6(pMsg->Type()), true, false);
            Msg6.AddRaw(pMsg->Data(), pMsg->Size());

            return Server()->SendMsg(&Msg6, Flags | MSGFLAG_NORECORD, ToClientID); // no record
        }
        case NETMSG_MAP_CHANGE:
        {
            CMsgPacker Msg6(protocol6::NETMSG_MAP_CHANGE, true, false);
            Msg6.AddString(Unpacker.GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), -1); // Map name
            Msg6.AddInt(Unpacker.GetInt()); // Crc
            Msg6.AddInt(Unpacker.GetInt()); // Size

            return Server()->SendMsg(&Msg6, Flags, ToClientID); // no record
        }
        case NETMSG_SERVERINFO:
        {
            Server()->SendServerInfo(ToClientID);
            return 0;
        }
        case NETMSG_RCON_AUTH_ON:
        {
            CMsgPacker Msg6(protocol6::NETMSG_RCON_AUTH_STATUS, true, false);
            Msg6.AddInt(1); // Authed
            Msg6.AddInt(1); // Cmdlist

            return Server()->SendMsg(&Msg6, Flags, ToClientID); // no record
        }
        case NETMSG_RCON_AUTH_OFF:
        {
            CMsgPacker Msg6(protocol6::NETMSG_RCON_AUTH_STATUS, true, false);
            Msg6.AddInt(0); // Authed
            Msg6.AddInt(0); // Cmdlist

            return Server()->SendMsg(&Msg6, Flags, ToClientID); // no record
        }
    }
    return -1;
}

int CNetConverter::SendSystemMsgConvert(CMsgPacker *pMsg, int Flags, int ToClientID, int Depth)
{
    dbg_assert(((CMsgPacker *)pMsg)->Convert(), "SendSystemMsgConvert::Msg needn't convert");
    dbg_assert(((CMsgPacker *)pMsg)->System(), "SendSystemMsgConvert::Msg isn't system msg");

    // send a msg for record
    if(Depth == 0)
    {
        CMsgPacker Msg7(pMsg->Type(), false, false);
        Msg7.AddRaw(pMsg->Data(), pMsg->Size());

        Server()->SendMsg(&Msg7, MSGFLAG_NOSEND | Flags, -1);
    }

    if(ToClientID == -1)
    {
        for(int i = 0 ; i < MAX_CLIENTS; i ++)
        {
            SendSystemMsgConvert(pMsg, Flags, i, Depth + 1);
        }
        return 0;
    }
    else if(Server()->ClientProtocol(ToClientID) == NETPROTOCOL_SEVEN)
    {
        CMsgPacker Msg7(pMsg->Type(), true, false);
        Msg7.AddRaw(pMsg->Data(), pMsg->Size());

        return Server()->SendMsg(&Msg7, Flags | MSGFLAG_NORECORD, ToClientID); // no record
    }
    else if(Server()->ClientProtocol(ToClientID) == NETPROTOCOL_SIX)
    {
        SetGameServer(Server()->GameServer(Server()->GetClientWorldID(ToClientID))->GS());
        return DeepSystemMsgConvert6(pMsg, Flags | MSGFLAG_NORECORD, ToClientID); // no record
    }

    return -1;
}

void CNetConverter::ResetChatTick()
{
    mem_zero(m_aChatTick, sizeof(m_aChatTick));
}

void CNetConverter::ResetSnapItemsEx()
{
    m_NumSnapItemsEx = 0;
    mem_zero(m_SnapItemEx, sizeof(m_SnapItemEx));
}

void CNetConverter::ResetEventID()
{
    m_EventID = MAX_EVENTS;
}

void CNetConverter::SnapItemUuid(int ClientID)
{
    for(int i = 0; i < m_NumSnapItemsEx; i ++)
    {
        int *pUuidItem = (int *)Server()->SnapNewItem(0, MAX_DDNETSNAP_TYPE - i, sizeof(m_SnapItemEx[i])); // NETOBJTYPE_EX
        if(pUuidItem)
        {
            for(size_t j = 0; j < sizeof(CUuid) / sizeof(int32_t); j ++)
                pUuidItem[j] = bytes_be_to_uint(&m_SnapItemEx[i].m_aData[j * sizeof(int32_t)]);
        }
    }
}

INetConverter *CreateNetConverter(IServer *pServer, class CConfiguration *pConfig) { return new CNetConverter(pServer, pConfig); }