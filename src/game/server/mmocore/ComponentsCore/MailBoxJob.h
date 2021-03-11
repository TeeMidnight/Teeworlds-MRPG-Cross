/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_MAILBOXJOB_H
#define GAME_SERVER_MAILBOXJOB_H

#include "../MmoComponent.h"

class MailBoxJob : public MmoComponent
{
	bool OnHandleVoteCommands(CPlayer* pPlayer, const char* CMD, const int VoteID, const int VoteID2, int Get, const char* GetText) override;
	void OnMessage(int MsgID, void* pRawMsg, int ClientID) override;

public:
	int GetActiveInbox(CPlayer* pPlayer);
	void GetInformationInbox(CPlayer *pPlayer);
	void SendInbox(int AccountID, const char* pName, const char* pDesc, int ItemID = -1, int Count = -1, int Enchant = -1);
	bool SendInbox(const char* pNickname, const char* pName, const char* pDesc, int ItemID = -1, int Count = -1, int Enchant = -1);

private:
	void ReceiveInbox(CPlayer* pPlayer, int InboxID);
	void SendClientMailList(CPlayer* pPlayer);
};

#endif