/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifdef CONF_DISCORD
#include <base/math.h>
#include "discord_main.h"
#include "discord_commands.h"

#include <engine/shared/config.h>
#include <engine/shared/console.h>
#include <game/server/gamecontext.h>

std::vector < DiscordCommands::Command > DiscordCommands::m_aCommands;

void DiscordCommands::InitCommands()
{
	// commands important
	DiscordCommands::RegisterCommand("!mhelp", "", "get help on commands.", ComHelp, CMD_IMPORTANT);
	DiscordCommands::RegisterCommand("!mconnect", "", "help for connect your discord to account in game.", ComConnect, CMD_IMPORTANT);

	// commands game server
	DiscordCommands::RegisterCommand("!monline", "", "show a list of players on the server.", ComOnline, CMD_GAME);
	DiscordCommands::RegisterCommand("!mstats", "r[nick]", "searching for players and displaying their personal MRPG cards.", ComStats, CMD_GAME);
	DiscordCommands::RegisterCommand("!mranking", "", "show the ranking of players by level.", ComRanking, CMD_GAME);
	DiscordCommands::RegisterCommand("!mgoldranking", "", "show the ranking of players by gold.", ComRanking, CMD_GAME);

	// commands fun
	DiscordCommands::RegisterCommand("!mavatar", "?s[mention]", "show user avatars.", ComAvatar, CMD_FUN);

	// commands admin
}

/************************************************************************/
/*  Important commands                                                  */
/************************************************************************/
void DiscordCommands::ComHelp(IConsole::IResult* pResult, DiscordJob *pDiscord, SleepyDiscord::Message message)
{
	std::string ImportantCmd("__**Important commands:**__");
	std::string RelatedGameServerCmd("\n\n__**Related game server commands:**__");
	std::string EntertainmentFunCmd("\n\n__**Entertainment / Fun**__");

	for(auto& pCommand : DiscordCommands::m_aCommands)
	{
		char aArgsDesc[256];
		CConsole::ParseArgsDescription(pCommand.m_aCommandArgs, aArgsDesc, sizeof(aArgsDesc));
		std::string ArgsStr(aArgsDesc);
		if(pCommand.m_TypeFlags & CMD_IMPORTANT)
		{
			ImportantCmd += "\n- **" + std::string(pCommand.m_aCommand) + (ArgsStr.empty() ? "" : " " + ArgsStr) + "** - " + std::string(pCommand.m_aCommandDesc);
		}
		if(pCommand.m_TypeFlags & CMD_GAME)
		{
			RelatedGameServerCmd += "\n- **" + std::string(pCommand.m_aCommand) + (ArgsStr.empty() ? "" : " " + ArgsStr) + "** - " + std::string(pCommand.m_aCommandDesc);
		}
		if(pCommand.m_TypeFlags & CMD_FUN)
		{
			EntertainmentFunCmd += "\n- **" + std::string(pCommand.m_aCommand) + (ArgsStr.empty() ? "" : " " + ArgsStr) + "** - " + std::string(pCommand.m_aCommandDesc);
		}
	}
	
	std::string Description(ImportantCmd + RelatedGameServerCmd + EntertainmentFunCmd +
		"\n\n__**The argument text can be placed in quotation marks \"\".**__"
		"\n__**[argument] - means that it is optional argument.**__");

	SleepyDiscord::Embed EmbedHelp;
	EmbedHelp.title = "Commands / Information";
	EmbedHelp.description = Description;
	EmbedHelp.color = string_to_number(DC_DISCORD_INFO, 0, 1410065407);
	pDiscord->sendMessage(message.channelID, "\0", EmbedHelp);
}

void DiscordCommands::ComConnect(IConsole::IResult* pResult, DiscordJob* pDiscord, SleepyDiscord::Message message)
{
	sqlstr::CSqlString<64> DiscordID = sqlstr::CSqlString<64>(std::string(message.author.ID).c_str());
	ResultPtr pRes = SJK.SD("Nick", "tw_accounts_data", "WHERE DiscordID = '%s'", DiscordID.cstr());
	if(!pRes->rowsCount())
	{
		SleepyDiscord::Embed EmbedConnectInfo;
		EmbedConnectInfo.title = "Information on how to connect your account";
		EmbedConnectInfo.description = "_**Warning:**_"
			"\nDo not connect other people's discord accounts to your in-game account. This is similar to hacking your account in the game."
			"\n\n_**How to connect:**_"
			"\nYou need to enter in the game the command \"__/discord_connect <did>__\"."
			"\nYour personal Discord ID: " + message.author.ID;
		EmbedConnectInfo.color = string_to_number(DC_DISCORD_INFO, 0, 1410065407);
		pDiscord->sendMessage(message.channelID, "\0", EmbedConnectInfo);

		pDiscord->SendWarningMessage(message.channelID, "Your account is not connected to the game account.\nSee the information in !mconnect, to connect your account to Discord.");
		return;
	}

	if(pRes->next())
	{
		std::string Nick(pRes->getString("Nick").c_str());
		pDiscord->SendSuccesfulMessage(message.channelID, "Your account is connected to the game nickname [" + Nick + "].");
	}
}


/************************************************************************/
/*  Game server commands                                                */
/************************************************************************/
void DiscordCommands::ComOnline(IConsole::IResult* pResult, DiscordJob* pDiscord, SleepyDiscord::Message message)
{
	std::string Onlines = "";
	CGS* pGS = (CGS*)pDiscord->Server()->GameServer(MAIN_WORLD_ID);
	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		CPlayer* pPlayer = pGS->GetPlayer(i);
		if(pPlayer)
		{
			std::string Nickname(pDiscord->Server()->ClientName(i));
			std::string Team(pPlayer->GetTeam() != TEAM_SPECTATORS ? "Ingame" : "Spectator");
			Onlines += "**" + Nickname + "** - " + Team + "\n";
		}
	}

	SleepyDiscord::Embed EmbedOnlines;
	EmbedOnlines.title = "Online Server";
	EmbedOnlines.description = Onlines.empty() ? "Server is empty" : Onlines;
	EmbedOnlines.color = string_to_number(DC_DISCORD_BOT, 0, 1410065407);
	pDiscord->sendMessage(message.channelID, "\0", EmbedOnlines);
}

void DiscordCommands::ComStats(IConsole::IResult* pResult, DiscordJob* pDiscord, SleepyDiscord::Message message)
{
	IConsole::IResult* pArgs = (IConsole::IResult*)pResult;
	const char* pSearchNick = pArgs->GetString(0);
	bool Found = pDiscord->SendGenerateMessage(message.author, message.channelID, "Discord MRPG Card", pSearchNick);
	if(!Found)
		pDiscord->SendWarningMessage(message.channelID, "Accounts containing [" + std::string(pSearchNick) + "] among nicknames were not found on the server.");
}

void DiscordCommands::ComRanking(IConsole::IResult* pResult, DiscordJob* pDiscord, SleepyDiscord::Message message)
{
	SleepyDiscord::Embed EmbedRanking;
	EmbedRanking.title = message.startsWith("!mgoldranking") ? "Ranking by Gold" : "Ranking by Level";
	EmbedRanking.color = 422353;

	std::string Names = "";
	std::string Levels = "";
	std::string GoldValues = "";
	ResultPtr pRes = SJK.SD("a.ID, ad.Nick AS `Nick`, ad.Level AS `Level`, g.Count AS `Gold`", "tw_accounts a", "JOIN tw_accounts_data ad ON a.ID = ad.ID LEFT JOIN tw_accounts_items g ON a.ID = g.OwnerID AND g.ItemID = 1 ORDER BY %s LIMIT 10", (message.startsWith("!mgoldranking") ? "g.Count DESC, ad.Level DESC" : "ad.Level DESC, g.Count DESC"));
	while(pRes->next())
	{
		Names += std::to_string((int)pRes->getRow()) + ". **" + pRes->getString("Nick").c_str() + "**\n";
		Levels += std::to_string(pRes->getInt("Level")) + "\n";
		GoldValues += std::to_string(pRes->getInt("Gold")) + "\n";
	}

	EmbedRanking.fields.push_back(SleepyDiscord::EmbedField("Name", Names, true));
	EmbedRanking.fields.push_back(SleepyDiscord::EmbedField("Level", Levels, true));
	EmbedRanking.fields.push_back(SleepyDiscord::EmbedField("Gold", GoldValues, true));
	pDiscord->sendMessage(message.channelID, "\0", EmbedRanking);
}


/************************************************************************/
/*  Fun commands                                                        */
/************************************************************************/
void DiscordCommands::ComAvatar(IConsole::IResult* pResult, DiscordJob* pDiscord, SleepyDiscord::Message message)
{
	if(message.mentions.empty())
	{
		pDiscord->sendMessage(message.channelID, message.author.avatarUrl());
		return;
	}
	else if(message.mentions.size() > 1)
	{
		pDiscord->SendWarningMessage(message.channelID, "Prohibited to use this command with more than 1 people.");
		return;
	}
	pDiscord->sendMessage(message.channelID, message.mentions[0].avatarUrl());
}


/************************************************************************/
/*  Admin commands                                                      */
/************************************************************************/


/************************************************************************/
/*  Engine commands                                                     */
/************************************************************************/
void DiscordCommands::RegisterCommand(const char* pName, const char* pArgs, const char* pDesc, CommandCallback pCallback, int FlagsType, int Flags)
{
	DiscordCommands::Command NewCommand;
	str_copy(NewCommand.m_aCommand, pName, sizeof(NewCommand.m_aCommand));
	str_copy(NewCommand.m_aCommandArgs, pArgs, sizeof(NewCommand.m_aCommandArgs));
	str_copy(NewCommand.m_aCommandDesc, pDesc, sizeof(NewCommand.m_aCommandDesc));
	NewCommand.m_TypeFlags = FlagsType;
	NewCommand.m_AccessFlags = Flags;
	NewCommand.m_pCallback = pCallback;
	DiscordCommands::m_aCommands.push_back(NewCommand);
}

bool DiscordCommands::ExecuteCommand(DiscordJob* pDiscord, SleepyDiscord::Message message)
{
	for(auto& pCommand : DiscordCommands::m_aCommands)
	{
		if(message.startsWith(pCommand.m_aCommand))
		{
			if(!(pCommand.m_AccessFlags & ACCESS_EVERYONE))
			{
				if(!(pCommand.m_AccessFlags & ACCESS_OWNER))
				{
					if(pCommand.m_AccessFlags & ACCESS_ADMIN)
					{
						std::vector<SleepyDiscord::Role> UserRoles = pDiscord->getUserRoles(message.serverID, message.author);
						auto pRole = std::find_if(UserRoles.begin(), UserRoles.end(), [](SleepyDiscord::Role& pItem) { return pItem.permissions & SleepyDiscord::Permission::ADMINISTRATOR; });
						if(pRole == UserRoles.end())
						{
							pDiscord->SendWarningMessage(message.channelID,
								"You do not have enough rights to execute this command."
								"\nOnly a user with administrator rights can use this command.");
							return true;
						}
					}

					if((pCommand.m_AccessFlags & ACCESS_VERIFIED && !message.author.verified))
					{
						pDiscord->SendWarningMessage(message.channelID,
							"You do not have enough rights to execute this command."
							"\nOnly the one verified in Discord can use this command.");
						return true;
					}
				}
				else
				{
					SleepyDiscord::Server Server = pDiscord->getServer(message.serverID).cast();
					if(message.author.ID != Server.ownerID)
					{
						pDiscord->SendWarningMessage(message.channelID,
							"You do not have enough rights to execute this command."
							"\nOnly the owner of the server can use this command.");
						return true;
					}
				}
			}

			CConsole::CResult Result;
			const int CommandLength = str_length(pCommand.m_aCommand);
			if(CommandLength <= (int)message.content.size())
			{
				std::string ArgumentsLine = message.content.substr(CommandLength);
				str_copy(Result.m_aStringStorage, ArgumentsLine.c_str(), sizeof(Result.m_aStringStorage));
				Result.m_pArgsStart = Result.m_aStringStorage;

				int Error = CConsole::ParseArgs(&Result, pCommand.m_aCommandArgs);
				if(Error)
				{
					char aBufCommandDesc[256];
					CConsole::ParseArgsDescription(pCommand.m_aCommandArgs, aBufCommandDesc, sizeof(aBufCommandDesc));
					pDiscord->SendWarningMessage(message.channelID,
						"The command works like this **" + std::string(pCommand.m_aCommand) + " " + std::string(aBufCommandDesc) + "**."
						"\nString argument can be placed in quotation marks **\"\"**.");
					return true;
				}
			}
			pCommand.m_pCallback(&Result, pDiscord, message);
			return true;
		}
	}
	return false;
}

#endif