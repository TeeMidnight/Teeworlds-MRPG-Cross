#ifndef ENGINE_SERVER_NETCONVERTER_H
#define ENGINE_SERVER_NETCONVERTER_H

#include <engine/netconverter.h>

class CNetConverter : public INetConverter
{
    IServer *m_pServer;
    class CGS *m_pGameServer;
    class CConfiguration *m_pConfig;

private:
    IServer *Server() { return m_pServer; }
    class CGS *GameServer() { return m_pGameServer; }
    class CConfiguration *Config() { return m_pConfig; }

    int GetExSnapID(const char *pUuidStr);

    bool SnapItemConvert(void *pItem, int Type, int ID, int Size, int ToClientID);
    bool DeepSnapConvert6(void *pItem, int Type, int ID, int Size, int ToClientID);
    bool DeepConvertClientMsg6(CMsgUnpacker *pItem, int& Type, bool System, int FromClientID);
    int DeepMsgConvert6(CMsgPacker *pMsg, int Flags, int ToClientID);
    int DeepGameMsgConvert6(CMsgPacker *pMsg, int Flags, int ToClientID);
    int DeepSystemMsgConvert6(CMsgPacker *pMsg, int Flags, int ToClientID);

    int m_GameFlags;
    int64 m_aChatTick[MAX_CLIENTS];
    CUuid m_SnapItemEx[64];
    int m_NumSnapItemsEx;
    int m_EventID;
    bool m_aClientLocal[MAX_CLIENTS];

public:
    CNetConverter(IServer *pServer, class CConfiguration *pConfig);
    void SetGameServer(class CGS *pGameServer) override 
    {
        m_pGameServer = pGameServer;
    }

    void RebuildSnapshot(class CSnapshotBuilder *pSnapshotBuilder, int ClientID) override;
    bool PrevConvertClientMsg(CMsgUnpacker *pItem, int& Type, bool System, int FromClientID) override;
	int SendMsgConvert(CMsgPacker *pMsg, int Flags, int ToClientID, int Depth = 0) override;
	int SendSystemMsgConvert(CMsgPacker *pMsg, int Flags, int ToClientID, int Depth = 0) override;
    void ResetChatTick() override;
    void ResetSnapItemsEx() override;
    void ResetEventID() override;
    void SnapItemUuid(int ClientID) override;
};

#endif
