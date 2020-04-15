/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "dungeon.h"

#include <game/server/gamecontext.h>

#include "../entities/npcwall.h"
#include "../logicworld/logicwall.h"
#include "../entities/jobitems.h"

CGameControllerDungeon::CGameControllerDungeon(class CGS *pGS) : IGameController(pGS)
{
	m_pGameType = "MmoTee";
	m_DungeonID = GS()->DungeonID();
	m_GameFlags = GAMEFLAG_RACE;

	// создание двери
	vec2 PosDoor = vec2(CGS::Dungeon[m_DungeonID].DoorX, CGS::Dungeon[m_DungeonID].DoorY);
	m_DungeonDoor = new DungeonDoor(&GS()->m_World, PosDoor);
	ChangeState(DUNGEON_WAITING);
}

void CGameControllerDungeon::KillAllPlayers()
{
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (GS()->m_apPlayers[i] && GS()->m_apPlayers[i]->GetCharacter())
			GS()->m_apPlayers[i]->GetCharacter()->Die(i, WEAPON_SELF);
	}
}

void CGameControllerDungeon::ChangeState(int State)
{
	m_StateDungeon = State;

	// - - - - - - - - - - - - - - - - - - - - - -
	// Используется при смене статуса в Ожидание данжа
	if (State == DUNGEON_WAITING)
	{	
		// элемент RACE
		for (int i = 0; i < MAX_PLAYERS; i++)
		{
			if (!GS()->m_apPlayers[i])
				continue;

			GS()->m_apPlayers[i]->Acc().TimeDungeon = 0;
		}
		m_MaximumTick = 0;
		m_FinishedTick = 0;
		m_StartingTick = 0;
		m_SafeTick = 0;
		SetMobsSpawn(false);
	}

	// - - - - - - - - - - - - - - - - - - - - - -
	// Используется при смене статуса в Ожидание данжа
	else if (State == DUNGEON_WAITING_START)
	{
		m_StartingTick = Server()->TickSpeed() * 60;
		SetMobsSpawn(false);
	}

	// - - - - - - - - - - - - - - - - - - - - - -
	// Используется при смене статуса в Начало данжа
	else if (State == DUNGEON_STARTED)
	{
		KillAllPlayers();
		m_MaximumTick = Server()->TickSpeed() * 600;
		m_SafeTick = Server()->TickSpeed() * 30;
		GS()->ChatWorldID(GS()->GetWorldID(), "[Dungeon]", "The security timer is enabled for 30 seconds!");
		GS()->ChatWorldID(GS()->GetWorldID(), "[Dungeon]", "You are given 10 minutes to complete of dungeon!");
		GS()->BroadcastWorldID(GS()->GetWorldID(), 99999, 500, "Dungeon started!");
		SetMobsSpawn(true);
	}

	// - - - - - - - - - - - - - - - - - - - - - -
	// Используется при смене статуса в Ожидание данжа
	else if (State == DUNGEON_WAITING_FINISH)
	{
		m_SafeTick = 0;
		m_FinishedTick = Server()->TickSpeed() * 10;
		SetMobsSpawn(false);
	}

	// - - - - - - - - - - - - - - - - - - - - - -
	// Используется при смене статуса в Завершение данжа
	else if (State == DUNGEON_FINISHED)
	{
		// элемент RACE
		for (int i = 0; i < MAX_PLAYERS; i++)
		{
			if (!GS()->m_apPlayers[i])
				continue;

			int Seconds = GS()->m_apPlayers[i]->Acc().TimeDungeon / Server()->TickSpeed();
			GS()->Mmo()->SaveDungeonRecord(GS()->m_apPlayers[i], m_DungeonID, Seconds);
			GS()->m_apPlayers[i]->Acc().TimeDungeon = 0;
		}

		KillAllPlayers();
	}

	// установить статус дверям
	m_DungeonDoor->SetState(State);
}

void CGameControllerDungeon::StateTick()
{
	int Players = PlayersNum();

	// сбросить данж
	if (Players < 1 && m_StateDungeon != DUNGEON_WAITING)
		ChangeState(DUNGEON_WAITING);

	// обновлять информацию каждую секунду
	if (Server()->Tick() % Server()->TickSpeed() == 0)
	{
		CGS::Dungeon[m_DungeonID].Players = Players;
		CGS::Dungeon[m_DungeonID].State = m_StateDungeon;
	}

	// - - - - - - - - - - - - - - - - - - - - - -
	// Используется в тике когда Ожидание данжа
	if (m_StateDungeon == DUNGEON_WAITING)
	{
		// пишем всем игрокам что ждем 2 игроков
		if (Players == 1)
			GS()->BroadcastWorldID(GS()->GetWorldID(), 99999, 10, "Dungeon '{STR}' Waiting 2 players!", CGS::Dungeon[m_DungeonID].Name);
		// начинаем данж если равно 2 игрока или больше
		else if (Players > 1)
			ChangeState(DUNGEON_WAITING_START);
	}

	// - - - - - - - - - - - - - - - - - - - - - -
	// Используется в тике когда Отчет начала данжа
	else if (m_StateDungeon == DUNGEON_WAITING_START)
	{
		if (Players < 2)
			ChangeState(DUNGEON_WAITING);
		else if (m_StartingTick)
		{
			int Time = m_StartingTick / Server()->TickSpeed();
			GS()->BroadcastWorldID(GS()->GetWorldID(), 99999, 500, "Dungeon waiting {INT} sec!", &Time);

			m_StartingTick--;
			if (!m_StartingTick)
				ChangeState(DUNGEON_STARTED);
		}
	}

	// - - - - - - - - - - - - - - - - - - - - - -
	// Используется в тике когда Данж начат
	else if (m_StateDungeon == DUNGEON_STARTED)
	{
		// элемент RACE
		for (int i = 0; i < MAX_PLAYERS; i++)
		{
			if (!GS()->m_apPlayers[i])
				continue;

			GS()->m_apPlayers[i]->Acc().TimeDungeon++;
		}

		// тик безопасности в течении какого времени игрок не вернется в старый мир
		if (m_SafeTick)
		{
			m_SafeTick--;
			if (!m_SafeTick)
				GS()->ChatWorldID(GS()->GetWorldID(), "[Dungeon]", "The security timer is over, be careful!");
		}

		// завершаем данж когда успешно данж завершен
		if (LeftMobsToWin() <= 0)
			ChangeState(DUNGEON_WAITING_FINISH);
	}

	// - - - - - - - - - - - - - - - - - - - - - -
	// Используется в тике когда Отчет начала данжа
	else if (m_StateDungeon == DUNGEON_WAITING_FINISH)
	{
		if (m_FinishedTick)
		{
			int Time = m_FinishedTick / Server()->TickSpeed();
			GS()->BroadcastWorldID(GS()->GetWorldID(), 99999, 500, "Dungeon ended {INT} sec!", &Time);

			m_FinishedTick--;
		}
		if (!m_FinishedTick)
			ChangeState(DUNGEON_FINISHED);
	}
}

int CGameControllerDungeon::OnCharacterDeath(CCharacter* pVictim, CPlayer* pKiller, int Weapon)
{
	if (!pKiller || !pVictim || !pVictim->GetPlayer())
		return 0;

	int KillerID = pKiller->GetCID();
	if (pVictim->GetPlayer()->IsBot() && pVictim->GetPlayer()->GetSpawnBot() == SPAWNMOBS)
	{
		int Progress = 100 - (int)kurosio::translate_to_procent(CountMobs(), LeftMobsToWin());
		CGS::Dungeon[m_DungeonID].Progress = Progress;
		GS()->ChatWorldID(GS()->GetWorldID(), "[Dungeon]", "The dungeon is completed on [{INT}%]", &Progress);
	}
	return 0;
}

void CGameControllerDungeon::OnCharacterSpawn(CCharacter* pChr)
{
	IGameController::OnCharacterSpawn(pChr);
	if (pChr && pChr->GetPlayer() && m_StateDungeon >= DUNGEON_STARTED && !m_SafeTick)
	{
		int ClientID = pChr->GetPlayer()->GetCID();
		GS()->Server()->ChangeWorld(ClientID, pChr->GetPlayer()->Acc().LastWorldID);
		GS()->Chat(ClientID, "You were thrown out of dungeon!");
	}
}

int CGameControllerDungeon::CountMobs() const
{
	int countMobs = 0;
	for (int i = MAX_PLAYERS; i < MAX_CLIENTS; i++)
	{
		if (GS()->m_apPlayers[i] && GS()->m_apPlayers[i]->GetSpawnBot() == SPAWNMOBS 
			&& GS()->CheckPlayerMessageWorldID(i) == GS()->GetWorldID())
			countMobs++;
	}
	return countMobs;
}

int CGameControllerDungeon::PlayersNum() const
{
	int playIt = 0;
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (Server()->GetWorldID(i) == GS()->GetWorldID())
			playIt++;
	}
	return playIt;
}

int CGameControllerDungeon::LeftMobsToWin() const
{
	int leftMobs = 0;
	for (int i = MAX_PLAYERS; i < MAX_CLIENTS; i++)
	{
		if (GS()->m_apPlayers[i] && GS()->m_apPlayers[i]->GetSpawnBot() == SPAWNMOBS && GS()->m_apPlayers[i]->GetCharacter())
			leftMobs++;
	}
	return leftMobs;
}

void CGameControllerDungeon::SetMobsSpawn(bool AllowedSpawn)
{
	for (int i = MAX_PLAYERS; i < MAX_CLIENTS; i++)
	{
		CPlayerBot* BotPlayer = static_cast<CPlayerBot*>(GS()->m_apPlayers[i]);
		if (BotPlayer && BotPlayer->GetSpawnBot() == SPAWNMOBS 
			&& GS()->CheckPlayerMessageWorldID(i) == GS()->GetWorldID())
		{
			BotPlayer->SetDungeonAllowedSpawn(AllowedSpawn);
			if (!AllowedSpawn && BotPlayer->GetCharacter())
				BotPlayer->GetCharacter()->Die(i, WEAPON_SELF);
		}
	}
}

void CGameControllerDungeon::Tick()
{
	// тик максимально местонахождения там
	if (m_MaximumTick)
	{
		m_MaximumTick--;
		if (!m_MaximumTick)
			ChangeState(DUNGEON_FINISHED);
	}

	StateTick();
	IGameController::Tick();
}

void CGameControllerDungeon::CreateLogic(int Type, int Mode, vec2 Pos, int ParseInt)
{
	if(Type == 1)
	{
		new CLogicWall(&GS()->m_World, Pos);
	}
	if(Type == 2)
	{
		new CLogicWallWall(&GS()->m_World, Pos, Mode, ParseInt);
	}
	if(Type == 3)
	{
		new CLogicDoorKey(&GS()->m_World, Pos, ParseInt, Mode);
	}
}

bool CGameControllerDungeon::OnEntity(int Index, vec2 Pos)
{
	if(IGameController::OnEntity(Index, Pos))
		return true;

	if(Index == ENTITY_NPC_WALLUP)
	{
		new CNPCWall(&GS()->m_World, Pos, false);
		return true;
	}
	if(Index == ENTITY_NPC_WALLLEFT)
	{
		new CNPCWall(&GS()->m_World, Pos, true);
		return true;
	}
	return false;
}

// Двери
DungeonDoor::DungeonDoor(CGameWorld *pGameWorld, vec2 Pos)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_DUNGEONDOOR, Pos)
{
	m_To = vec2(Pos.x, Pos.y-140);
	m_Pos.y += 30;

	m_State = DUNGEON_WAITING;
	GameWorld()->InsertEntity(this);
}

void DungeonDoor::Tick()
{
	if(m_State != DUNGEON_STARTED)
	{
		for(CCharacter *pChar = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); pChar; pChar = (CCharacter *)pChar->TypeNext())
		{
			vec2 IntersectPos = closest_point_on_line(m_Pos, m_To, pChar->m_Core.m_Pos);
			float Distance = distance(IntersectPos, pChar->m_Core.m_Pos);

			// снижаем скокрость
			if (Distance < 64.0f && length(pChar->m_Core.m_Vel) >= 64.0)
				pChar->m_Core.m_Vel = vec2(0, 0);

			// проверяем дистанцию
			if (Distance < 30.0f && pChar->IsAlive())
			{
				vec2 Dir = normalize(pChar->m_Core.m_Pos - IntersectPos);
				float a = (30.0f * 1.45f - Distance);
				float Velocity = 0.5f;
				if (length(pChar->m_Core.m_Vel) > 0.0001)
					Velocity = 1 - (dot(normalize(pChar->m_Core.m_Vel), Dir) + 1) / 4;

				pChar->Die(pChar->GetPlayer()->GetCID(), WEAPON_SELF);
			}		
		}
	}
}

void DungeonDoor::Snap(int SnappingClient)
{
	if (m_State == DUNGEON_STARTED || NetworkClipped(SnappingClient))
		return;
	
	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
	if (!pObj)
		return;

	pObj->m_X = int(m_Pos.x);
	pObj->m_Y = int(m_Pos.y);
	pObj->m_FromX = int(m_To.x);
	pObj->m_FromY = int(m_To.y);
	pObj->m_StartTick = Server()->Tick()-2;
}
