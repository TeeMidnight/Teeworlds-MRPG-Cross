/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/hash_ctxt.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/console.h>

#include "netban.h"
#include "network.h"

#include "protocol6.h"

static TOKEN ToSecurityToken(const unsigned char *pData)
{
	return (int)pData[0] | (pData[1] << 8) | (pData[2] << 16) | (pData[3] << 24);
}

TOKEN CNetServer::GetGlobalToken() const
{
	static NETADDR NullAddr = {0};
	SHA256_CTX Sha256;
	sha256_init(&Sha256);
	sha256_update(&Sha256, (unsigned char *)m_aSecurityTokenSeed, sizeof(m_aSecurityTokenSeed));
	sha256_update(&Sha256, (unsigned char *)&NullAddr, 20); // omit port, bad idea!

	TOKEN SecurityToken = ToSecurityToken(sha256_finish(&Sha256).data);

	return SecurityToken;
}

bool CNetServer::Open(NETADDR BindAddr, CConfiguration *pConfig, IConsole *pConsole, IEngine *pEngine, CNetBan *pNetBan,
	int MaxClients, int MaxClientsPerIP, NETFUNC_NEWCLIENT pfnNewClient, NETFUNC_DELCLIENT pfnDelClient, void *pUser)
{
	// zero out the whole structure
	mem_zero(this, sizeof(*this));

	// open socket
	NETSOCKET Socket = net_udp_create(BindAddr, 0);
	if(!Socket.type)
		return false;

	// init
	m_pNetBan = pNetBan;
	Init(Socket, pConfig, pConsole, pEngine);

	m_TokenManager.Init(this);
	m_TokenCache.Init(this, &m_TokenManager);

	m_NumClients = 0;
	SetMaxClients(MaxClients);
	SetMaxClientsPerIP(MaxClientsPerIP);

	secure_random_fill(m_aSecurityTokenSeed, sizeof(m_aSecurityTokenSeed));

	for(int i = 0; i < NET_MAX_CLIENTS; i++)
		m_aSlots[i].m_Connection.Init(this, true);

	m_pfnNewClient = pfnNewClient;
	m_pfnDelClient = pfnDelClient;
	m_UserPtr = pUser;

	return true;
}

void CNetServer::Close(const char *pReason)
{
	for(int i = 0; i < NET_MAX_CLIENTS; i++)
		Drop(i, pReason);

	Shutdown();
}

void CNetServer::Drop(int ClientID, const char *pReason)
{
	if(ClientID < 0 || ClientID >= NET_MAX_CLIENTS || m_aSlots[ClientID].m_Connection.State() == NET_CONNSTATE_OFFLINE)
		return;

	if(m_pfnDelClient)
		m_pfnDelClient(ClientID, pReason, m_UserPtr);

	m_aSlots[ClientID].m_Connection.Disconnect(pReason);
	m_NumClients--;
}

int CNetServer::Update()
{
	int64 Now = time_get();
	for(int i = 0; i < NET_MAX_CLIENTS; i++)
	{
		if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
			continue;

		m_aSlots[i].m_Connection.Update();
		if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_ERROR)
		{
			if(Now - m_aSlots[i].m_Connection.ConnectTime() < time_freq() && NetBan())
			{
				if(NetBan()->BanAddr(ClientAddr(i), 60, "Stressing network") == -1)
					Drop(i, m_aSlots[i].m_Connection.ErrorString());
			}
			else
				Drop(i, m_aSlots[i].m_Connection.ErrorString());
		}
	}

	m_TokenManager.Update();
	m_TokenCache.Update();

	return 0;
}

/*
	TODO: chopp up this function into smaller working parts
*/
int CNetServer::Recv(CNetChunk *pChunk, TOKEN *pResponseToken)
{
	while(1)
	{
		// check for a chunk
		if(m_RecvUnpacker.IsActive() && m_RecvUnpacker.FetchChunk(pChunk))
			return 1;

		// TODO: empty the recvinfo
		NETADDR Addr;
		int Protocol = NETPROTOCOL_UNKNOWN;
		int Size = 0;
		int Result = UnpackPacket(&Addr, m_RecvUnpacker.m_aBuffer, &m_RecvUnpacker.m_Data, Protocol, &Size);
		// no more packets for now
		if(Result > 0)
			break;
		
		if(!Result)
		{
			// check for bans
			char aBuf[128];
			int LastInfoQuery;
			if(NetBan() && NetBan()->IsBanned(&Addr, aBuf, sizeof(aBuf), &LastInfoQuery))
			{
				// banned, reply with a message (5 second cooldown)
				int Time = time_timestamp();
				if(LastInfoQuery + 5 < Time)
				{
					SendControlMsg(&Addr, m_RecvUnpacker.m_Data.m_ResponseToken, 0, NET_CTRLMSG_CLOSE, aBuf, str_length(aBuf) + 1, Protocol);
				}
				continue;
			}

			bool Found = false;
			// try to find matching slot
			for(int i = 0; i < NET_MAX_CLIENTS; i++)
			{
				if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
					continue;

				if(net_addr_comp(m_aSlots[i].m_Connection.PeerAddress(), &Addr, true) == 0)
				{
					if(Protocol != m_aSlots[i].m_Connection.Protocol())
					{
						Protocol = m_aSlots[i].m_Connection.Protocol();
						if(UnpackPacket(m_RecvUnpacker.m_aBuffer, Size, &m_RecvUnpacker.m_Data, Protocol) != 0)
							break;
					}

					if(m_aSlots[i].m_Connection.Feed(&m_RecvUnpacker.m_Data, &Addr))
					{
						if(m_RecvUnpacker.m_Data.m_DataSize)
						{
							if(!(m_RecvUnpacker.m_Data.m_Flags&NET_PACKETFLAG_CONNLESS))
								m_RecvUnpacker.Start(&Addr, &m_aSlots[i].m_Connection, i);
							else
							{
								pChunk->m_Flags = NETSENDFLAG_CONNLESS;
								pChunk->m_Address = *m_aSlots[i].m_Connection.PeerAddress();
								pChunk->m_ClientID = i;
								pChunk->m_DataSize = m_RecvUnpacker.m_Data.m_DataSize;
								pChunk->m_pData = m_RecvUnpacker.m_Data.m_aChunkData;
								if(pResponseToken)
									*pResponseToken = NET_TOKEN_NONE;
								return 1;
							}
						}
					}
					Found = true;
				}
			}

			if(Found)
				continue;

			int Accept = m_TokenManager.ProcessMessage(&Addr, &m_RecvUnpacker.m_Data);
			if((Protocol == NETPROTOCOL_SEVEN) && Accept <= 0)
				continue;

			if(m_RecvUnpacker.m_Data.m_Flags&NET_PACKETFLAG_CONTROL)
			{
				if(m_RecvUnpacker.m_Data.m_aChunkData[0] == NET_CTRLMSG_CONNECT)
				{
					// check if there are free slots
					if(m_NumClients >= m_MaxClients)
					{
						const char FullMsg[] = "This server is full";
						SendControlMsg(&Addr, m_RecvUnpacker.m_Data.m_ResponseToken, 0, NET_CTRLMSG_CLOSE, FullMsg, sizeof(FullMsg), Protocol);
						continue;
					}

					// only allow a specific number of players with the same ip
					int FoundAddr = 1;
					
					bool Continue = false;
					for(int i = 0; i < NET_MAX_CLIENTS; i++)
					{
						if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
							continue;

						if(!net_addr_comp(&Addr, m_aSlots[i].m_Connection.PeerAddress(), false))
						{
							if(FoundAddr++ >= m_MaxClientsPerIP)
							{
								char aBuf[128];
								str_format(aBuf, sizeof(aBuf), "Only %d players with the same IP are allowed", m_MaxClientsPerIP);
								SendControlMsg(&Addr, m_RecvUnpacker.m_Data.m_ResponseToken, 0, NET_CTRLMSG_CLOSE, aBuf, str_length(aBuf) + 1, Protocol);
								Continue = true;
								break;
							}
						}
					}

					if(Continue)
						continue;

					for(int i = 0; i < NET_MAX_CLIENTS; i++)
					{
						if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
						{
							m_NumClients++;
							
							m_aSlots[i].m_Connection.SetToken(m_RecvUnpacker.m_Data.m_Token);
							m_aSlots[i].m_Connection.SetProtocol(Protocol);
							m_aSlots[i].m_Connection.Feed(&m_RecvUnpacker.m_Data, &Addr);
							if(m_pfnNewClient)
								m_pfnNewClient(i, m_UserPtr, Protocol);
							break;
						}
					}
				}
				else if(m_RecvUnpacker.m_Data.m_aChunkData[0] == NET_CTRLMSG_TOKEN)
					m_TokenCache.AddToken(&Addr, m_RecvUnpacker.m_Data.m_ResponseToken, NET_TOKENFLAG_RESPONSEONLY);
			}
			else if(m_RecvUnpacker.m_Data.m_Flags&NET_PACKETFLAG_CONNLESS)
			{
				pChunk->m_Flags = NETSENDFLAG_CONNLESS;
				if(Protocol == NETPROTOCOL_SIX)
					pChunk->m_Flags |= NETSENDFLAG_SIX;
				pChunk->m_ClientID = -1;
				pChunk->m_Address = Addr;
				pChunk->m_DataSize = m_RecvUnpacker.m_Data.m_DataSize;
				pChunk->m_pData = m_RecvUnpacker.m_Data.m_aChunkData;
				
				if(m_RecvUnpacker.m_Data.m_Flags & NET_PACKETFLAG_EXTENDED)
				{
					pChunk->m_Flags |= NETSENDFLAG_EXTENDED;
					mem_copy(pChunk->m_aExtraData, m_RecvUnpacker.m_Data.m_aExtraData, sizeof(pChunk->m_aExtraData));
				}
				if(pResponseToken)
					*pResponseToken = m_RecvUnpacker.m_Data.m_ResponseToken;
				return 1;
			}
		}
	}
	return 0;
}

int CNetServer::Send(CNetChunk *pChunk, TOKEN Token)
{
	if(pChunk->m_Flags&NETSENDFLAG_CONNLESS)
	{
		if(pChunk->m_DataSize >= NET_MAX_PAYLOAD)
		{
			dbg_msg("netserver", "packet payload too big. %d. dropping packet", pChunk->m_DataSize);
			return -1;
		}

		if(pChunk->m_ClientID == -1)
		{
			for(int i = 0; i < NET_MAX_CLIENTS; i++)
			{
				if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
					continue;

				if(net_addr_comp(&pChunk->m_Address, m_aSlots[i].m_Connection.PeerAddress(), true) == 0)
				{
					// upgrade the packet, now that we know its recipent
					pChunk->m_ClientID = i;
					break;
				}
			}
		}

		if(Token != NET_TOKEN_NONE)
		{
			SendPacketConnless(&pChunk->m_Address, Token, m_TokenManager.GenerateToken(&pChunk->m_Address), pChunk->m_pData, pChunk->m_DataSize);
		}
		else
		{
			if(pChunk->m_ClientID == -1)
			{
				m_TokenCache.SendPacketConnless(&pChunk->m_Address, pChunk->m_pData, pChunk->m_DataSize);
			}
			else
			{
				dbg_assert(pChunk->m_ClientID >= 0, "errornous client id");
				dbg_assert(pChunk->m_ClientID < NET_MAX_CLIENTS, "errornous client id");
				dbg_assert(m_aSlots[pChunk->m_ClientID].m_Connection.State() != NET_CONNSTATE_OFFLINE, "errornous client id");

				m_aSlots[pChunk->m_ClientID].m_Connection.SendPacketConnless((const char *)pChunk->m_pData, pChunk->m_DataSize);
			}
		}
	}
	else
	{
		if(pChunk->m_DataSize+NET_MAX_CHUNKHEADERSIZE >= NET_MAX_PAYLOAD)
		{
			dbg_msg("netclient", "chunk payload too big. %d. dropping chunk", pChunk->m_DataSize);
			return -1;
		}

		int Flags = 0;
		dbg_assert(pChunk->m_ClientID >= 0, "errornous client id");
		dbg_assert(pChunk->m_ClientID < NET_MAX_CLIENTS, "errornous client id");
		dbg_assert(m_aSlots[pChunk->m_ClientID].m_Connection.State() != NET_CONNSTATE_OFFLINE, "errornous client id");

		if(pChunk->m_Flags&NETSENDFLAG_VITAL)
			Flags = NET_CHUNKFLAG_VITAL;

		if(m_aSlots[pChunk->m_ClientID].m_Connection.QueueChunk(Flags, pChunk->m_DataSize, pChunk->m_pData) == 0)
		{
			if(pChunk->m_Flags&NETSENDFLAG_FLUSH)
				m_aSlots[pChunk->m_ClientID].m_Connection.Flush();
		}
		else
		{
			Drop(pChunk->m_ClientID, "Error sending data");
		}
	}
	return 0;
}

static const unsigned char NET_HEADER_EXTENDED[] = {'x', 'e'};
int CNetServer::SendConnlessSevenDown(CNetChunk *pChunk)
{
	if(pChunk->m_DataSize >= NET_MAX_PACKETSIZE - 6)
		return -1;

	unsigned char aBuffer[NET_MAX_PACKETSIZE];
	const int DATA_OFFSET = 6;
	if(pChunk->m_Flags&NETSENDFLAG_EXTENDED)
	{
		mem_copy(aBuffer, NET_HEADER_EXTENDED, sizeof(NET_HEADER_EXTENDED));
		mem_copy(aBuffer + sizeof(NET_HEADER_EXTENDED), pChunk->m_aExtraData, 4);
	}
	else
	{
		for(int i = 0; i < DATA_OFFSET; i++)
			aBuffer[i] = 0xff;
	}
	mem_copy(aBuffer + DATA_OFFSET, pChunk->m_pData, pChunk->m_DataSize);
	net_udp_send(*Socket(), &pChunk->m_Address, aBuffer, pChunk->m_DataSize + DATA_OFFSET);
	return 0;
}

void CNetServer::SetMaxClients(int MaxClients)
{
	m_MaxClients = clamp(MaxClients, 1, int(NET_MAX_CLIENTS));
}

void CNetServer::SetMaxClientsPerIP(int MaxClientsPerIP)
{
	m_MaxClientsPerIP = clamp(MaxClientsPerIP, 1, int(NET_MAX_CLIENTS));
}
