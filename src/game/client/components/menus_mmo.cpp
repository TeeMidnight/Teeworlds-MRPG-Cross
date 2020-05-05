#include <base/color.h>

#include <engine/storage.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>
#include <engine/graphics.h>

#include <game/version.h>
#include <game/client/ui.h>
#include <game/client/render.h>
#include <game/client/gameclient.h>

#include <generated/client_data.h>

#include <game/client/components/binds.h>
#include <game/client/components/menus.h>
#include <game/client/components/items.h>
#include <game/client/components/skins.h>

void CMenus::RenderRgbSliders(CUIRect* pMainView, CUIRect* pButton, int &r, int &g, int &b, bool Enabled)
{
	const char *pLabels[] = {"R.", "G.", "B."};
	int *pColorSlider[3] = {&r, &g, &b};
	for(int i=0; i<3; i++)
	{
		CUIRect Text;
		pMainView->HSplitTop(19.0f, pButton, pMainView);
		pButton->VMargin(15.0f, pButton);
		pButton->VSplitLeft(30.0f, &Text, pButton);
		pButton->VSplitRight(5.0f, pButton, 0);
		pButton->HSplitTop(4.0f, 0, pButton);

		if(Enabled)
		{
			float k = (*pColorSlider[i]) / 255.0f;
			k = DoScrollbarH(pColorSlider[i], pButton, k);
			*pColorSlider[i] = (int)(k*255.0f);
		}
		else
			DoScrollbarH(pColorSlider[i], pButton, 0);
		UI()->DoLabel(&Text, pLabels[i], 15.0f, CUI::ALIGN_LEFT);
	}
}

void CMenus::RenderSettingsMmo(CUIRect MainView)
{
	CUIRect Label, Button, Tabbar;
	static int s_SettingsPage = 0;
	MainView.HSplitTop(20.0f, 0, &MainView);
	MainView.HSplitTop(20.0f, &Tabbar, &MainView);

	// рисуем меню вкладок
	const int TAB_SIZE = 4;
	const char* Tabs[TAB_SIZE] = { "Visual", "General", "Gamer", "Credits" };
	const int Corner[TAB_SIZE] = { CUI::CORNER_TL, 0, 0, CUI::CORNER_TR };
	for (int i = 0; i < TAB_SIZE; i++)
	{
		Tabbar.VSplitLeft(182.5f, &Button, &Tabbar);

		static CButtonContainer s_Buttons[3];
		if (DoButton_MenuTabTop(&s_Buttons[i], Tabs[i], Client()->State() == IClient::STATE_OFFLINE && s_SettingsPage == i, &Button, s_SettingsPage == i ? 1.0f : 1.5f, 1.0f, Corner[i], 6.0f))
			s_SettingsPage = i;
	}

	// рисуем текст информации
	MainView.HSplitTop(400.0f, &MainView, &Label);
	const char* Information[TAB_SIZE] = { "Setting up the visual part of the client",
											"Setting up the general part of the client",
											"Features of the Gamer(Dune)",
											"Information & Credits" };
	UI()->DoLabel(&Label, Information[s_SettingsPage], 14.0f, CUI::ALIGN_CENTER);
	RenderSettingsMmoGeneral(MainView, s_SettingsPage);
}

void CMenus::RenderSettingsMmoGeneral(CUIRect MainView, int Page)
{
	CUIRect Button;
	RenderTools()->DrawUIRect4(&MainView, vec4(0.0f, 0.0f, 0.0f, g_Config.m_ClMenuAlpha / 50.0f), vec4(0.0f, 0.0f, 0.0f, g_Config.m_ClMenuAlpha / 50.0f), vec4(0.0f, 0.0f, 0.0f, 0.0f), vec4(0.0f, 0.0f, 0.0f, 0.0f), 0, 5.0f);

	// visual
	float ButtonHeight = 20.0f;
	float Spacing = 2.0f;
	if(Page == 0)
	{
		RenderMmoSettingsTexture(MainView, MainView);
	}

	// general
	else if (Page == 1)
	{
		CUIRect BasicLeft, BasicRight;
		PreparationLeftRightSide("Basic settings", MainView, &BasicLeft, &BasicRight, Spacing, ButtonHeight);

		// ---------------------- LEFT SIDE --------------------------
		UI()->DoLabel(&BasicLeft, "General Mmo Settings", 12.0f, CUI::ALIGN_CENTER);
		BasicLeft.HSplitTop(14.0f, &Button, &BasicLeft);

		// vanilla damage ind
		BasicLeft.HSplitTop(Spacing, 0, &BasicLeft);
		BasicLeft.HSplitTop(ButtonHeight, &Button, &BasicLeft);
		static int s_ButtonDmgInd = 0;
		if (DoButton_CheckBox(&s_ButtonDmgInd, Localize("Mmo Damage Ind (Vanilla)"), g_Config.m_ClMmoDamageInd, &Button))
			g_Config.m_ClMmoDamageInd ^= 1;

		// show colored
		BasicLeft.HSplitTop(Spacing, 0, &BasicLeft);
		BasicLeft.HSplitTop(ButtonHeight, &Button, &BasicLeft);
		static int s_ButtonColorVote = 0;
		if (DoButton_CheckBox(&s_ButtonColorVote, Localize("Show Colored Vote (MmoTee)"), g_Config.m_ClShowColoreVote, &Button))
			g_Config.m_ClShowColoreVote ^= 1;

		// --------------------- RIGHT SIDE --------------------------
		UI()->DoLabel(&BasicRight, "Customize", 12.0f, CUI::ALIGN_CENTER);
		BasicRight.HSplitTop(14.0f, &Button, &BasicRight);

		// background
		CUIRect BackgroundExpBar = Button;
		BasicRight.HSplitTop(ButtonHeight * 4.2f, &BackgroundExpBar, 0);
		RenderTools()->DrawUIRect(&BackgroundExpBar, vec4(0.0f, 0.0f, 0.0f, g_Config.m_ClMenuAlpha / 80.0f), CUI::CORNER_ALL, 5.0f);
		
		// expbar
		CUIRect ExpBar;
		BasicRight.VMargin(10.0f, &ExpBar);
		ExpBar.HMargin(5.0f, &ExpBar), ExpBar.h = 20.0f;
		vec4 ProgressColor((g_Config.m_HdColorProgress >> 16) / 255.0f,
			((g_Config.m_HdColorProgress >> 8) & 0xff) / 255.0f, (g_Config.m_HdColorProgress & 0xff) / 255.0f, 0.8f);
		RenderTools()->DrawUIBar(TextRender(), ExpBar, ProgressColor, 50, 100, "Experience Bar", 20, 5.0f, 2.0f);
		BasicRight.HSplitTop(10.0f, &ExpBar, &BasicRight);

		int hri, hgi, hbi;
		hri = g_Config.m_HdColorProgress >> 16;
		hgi = (g_Config.m_HdColorProgress >> 8) & 0xff;
		hbi = g_Config.m_HdColorProgress & 0xff;
		BasicRight.HSplitTop(15.0f, &Button, &BasicRight);
		RenderRgbSliders(&BasicRight, &Button, hri, hgi, hbi, true);
		g_Config.m_HdColorProgress = (hri<<16) + (hgi<<8) + hbi;

		// эффекты
		BasicRight.HSplitTop(ButtonHeight, &Button, &BasicRight);
		const char* Name[4] = { "All Effects", "Only Enchant Effects", "Only Item Effects", "Off Effects" };
		UI()->DoLabel(&BasicRight, Name[g_Config.m_ClShowMEffects], 12.0f, CUI::ALIGN_CENTER);
		BasicRight.HSplitTop(14.0f, 0, &BasicRight);
	
		BasicRight.HSplitTop(ButtonHeight, &Button, &BasicRight);
		DoScrollbarOption(&g_Config.m_ClShowMEffects, &g_Config.m_ClShowMEffects, &Button, "Effects (MRPG)", 0, 3);

		BasicRight.HSplitTop(ButtonHeight, &Button, &BasicRight);
		DoScrollbarOption(&g_Config.m_ClDialogsSpeedNPC, &g_Config.m_ClDialogsSpeedNPC, &Button, Localize("Dialogs speed with NPC (MRPG)"), 50, 100, &LogarithmicScrollbarScale);
	}
	// gamer dune
	else if(Page == 2)
	{
		CUIRect BasicLeft, BasicRight;
		PreparationLeftRightSide("Gamer features (Dune)", MainView, &BasicLeft, &BasicRight, Spacing, ButtonHeight);

		// ---------------------- LEFT SIDE --------------------------
		BasicLeft.HSplitTop(Spacing, 0, &BasicLeft);
		BasicLeft.HSplitTop(ButtonHeight, &Button, &BasicLeft);
		static int s_ButtonAddaptivePickupInd = 0;
		if(DoButton_CheckBox(&s_ButtonAddaptivePickupInd, Localize("Make pickups grey when you don't need them"), g_Config.m_ClAdaptivePickups, &Button))
			g_Config.m_ClAdaptivePickups ^= 1;

	}
	// information
	else if (Page == 3)
	{
		UI()->DoLabel(&MainView, "The client uses open source client codes:\n*Teeworlds by (teeworlds team)\n*DDRaceNetwork Client by (DDNet team)\n*Gamer Client by (Dune)\n*MmoTee Client by (Kurosio)", 16.0f, CUI::ALIGN_CENTER);
	}
}

void CMenus::RenderSettingsMmoChangerGeneric(CUIRect MainView, CCSkinChanger::CTextureEntity* pEntities, char* pConfigStr, const char* pLabel, int ItemsPerRow, float Ratio)
{
	char aBuf[512];
	static CListBoxState s_ListBoxState;
	int OldSelected = -1;
	str_format(aBuf, sizeof(aBuf), "%s: %s", pLabel, pConfigStr[0] ? pConfigStr : "default");
	UiDoListboxHeader(&s_ListBoxState, &MainView, aBuf, 20.0f, 2.0f);

	const int Num = pEntities->Num();
	UiDoListboxStart(&s_ListBoxState, &s_ListBoxState, MainView.w / (float)ItemsPerRow / Ratio, 0, Num, ItemsPerRow, OldSelected);

	for (int i = 0; i < Num + 1; ++i) // first is default
	{
		if (i == 0)
		{
			if (pConfigStr[0] == '\0')
				OldSelected = i;
		}
		else if (str_comp(pEntities->GetName(i - 1), pConfigStr) == 0)
			OldSelected = i;
		static int s_DefaultEntitiyId;
		CListboxItem Item = UiDoListboxNextItem(&s_ListBoxState, i > 0 ? (void*)pEntities->GetName(i - 1) : (void*)&s_DefaultEntitiyId, OldSelected == i);
		if (Item.m_Visible)
		{
			CUIRect Pos;
			Item.m_Rect.Margin(5.0f, &Item.m_Rect);
			Item.m_Rect.HSplitBottom(10.0f, &Item.m_Rect, &Pos);

			Item.m_Rect.h = Item.m_Rect.w / Ratio;

			Graphics()->BlendNormal();
			if (i == 0) Graphics()->TextureSet(pEntities->GetDefault());
			else Graphics()->TextureSet(pEntities->Get(i - 1));
			
			Graphics()->QuadsBegin();
			IGraphics::CQuadItem QuadItem(Item.m_Rect.x, Item.m_Rect.y, Item.m_Rect.w, Item.m_Rect.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}
	}

	const int NewSelected = UiDoListboxEnd(&s_ListBoxState, 0);
	if (OldSelected != NewSelected)
	{
		if (NewSelected == 0)
			pConfigStr[0] = '\0';
		else
			str_copy(pConfigStr, pEntities->GetName(NewSelected - 1), 255);
		pEntities->Reload(NewSelected - 1); // -1 is default
	}
}

void CMenus::RenderMmoSettingsTexture(CUIRect MainView, CUIRect Background)
{
	CUIRect Button, TabBar;

	static int TextureMenu = 0;
	{ // меню выбора скинов
		MainView.HSplitTop(20.0f, &TabBar, &MainView);

		TabBar.VSplitLeft(TabBar.w / 6, &Button, &TabBar);
		static CButtonContainer s_ButtonGameSkins;
		if (DoButton_MenuTab(&s_ButtonGameSkins, Localize("Gameskin"), TextureMenu == 0, &Button, 0))
			TextureMenu = 0;

		TabBar.VSplitLeft(TabBar.w / 5, &Button, &TabBar);
		static CButtonContainer s_ButtonEmoticons;
		if (DoButton_MenuTab(&s_ButtonEmoticons, Localize("Emoticons"), TextureMenu == 1, &Button, 0))
			TextureMenu = 1;

		TabBar.VSplitLeft(TabBar.w / 4, &Button, &TabBar);
		static CButtonContainer s_ButtonCursors;
		if (DoButton_MenuTab(&s_ButtonCursors, Localize("Cursor"), TextureMenu == 2, &Button, 0))
			TextureMenu = 2;

		TabBar.VSplitLeft(TabBar.w / 3, &Button, &TabBar);
		static CButtonContainer s_ButtonParticles;
		if (DoButton_MenuTab(&s_ButtonParticles, Localize("Particles"), TextureMenu == 3, &Button, 0))
			TextureMenu = 3;

		TabBar.VSplitLeft(TabBar.w / 2, &Button, &TabBar);
		static CButtonContainer s_ButtonEntities;
		if (DoButton_MenuTab(&s_ButtonEntities, Localize("Entities"), TextureMenu == 4, &Button, 0))
			TextureMenu = 4;

		static CButtonContainer s_ButtonFonts;
		if (DoButton_MenuTab(&s_ButtonFonts, Localize("Fonts"), TextureMenu == 5, &TabBar, 0))
			TextureMenu = 5;
	}

	// замена скинов game.png
	if (TextureMenu == 0)
	{
		if (!m_pClient->m_pSkinChanger->m_GameSkins.IsLoaded())
			m_pClient->m_pSkinChanger->m_GameSkins.LoadEntities();

		RenderSettingsMmoChangerGeneric(MainView, &m_pClient->m_pSkinChanger->m_GameSkins, g_Config.m_GameTexture, "Gameskins", 3, 2.0f);
	}

	// замена скинов emoticion
	else if (TextureMenu == 1)
	{
		if (!m_pClient->m_pSkinChanger->m_Emoticons.IsLoaded())
			m_pClient->m_pSkinChanger->m_Emoticons.LoadEntities();

		RenderSettingsMmoChangerGeneric(MainView, &m_pClient->m_pSkinChanger->m_Emoticons, g_Config.m_GameEmoticons, "Emoticons", 5, 1.0f);
	}

	// замена скинов cursors
	else if (TextureMenu == 2)
	{
		if (!m_pClient->m_pSkinChanger->m_Cursors.IsLoaded())
			m_pClient->m_pSkinChanger->m_Cursors.LoadEntities();

		RenderSettingsMmoChangerGeneric(MainView, &m_pClient->m_pSkinChanger->m_Cursors, g_Config.m_GameCursor, "Cursors", 16, 1.0f);
	}

	// замена скинов particles
	else if (TextureMenu == 3)
	{
		if (!m_pClient->m_pSkinChanger->m_Particles.IsLoaded())
			m_pClient->m_pSkinChanger->m_Particles.LoadEntities();

		RenderSettingsMmoChangerGeneric(MainView, &m_pClient->m_pSkinChanger->m_Particles, g_Config.m_GameParticles, "Particles", 5, 1.0f);
	}

	// замена скинов entities
	else if (TextureMenu == 4)
	{
		if (!m_pClient->m_pSkinChanger->m_Entities.IsLoaded())
			m_pClient->m_pSkinChanger->m_Entities.LoadEntities();

		RenderSettingsMmoChangerGeneric(MainView, &m_pClient->m_pSkinChanger->m_Entities, g_Config.m_GameEntities, "Entities", 5, 1.0f);
	}
	else if (TextureMenu == 5)
	{
		RenderFontSelection(MainView);
	}
}

class CFontFile
{
public:
	CFontFile() {}
	CFontFile(const char *n, const char *f) : m_Name(n), m_FileName(f) {}

	string m_Name;
	string m_FileName;
	bool operator<(const CFontFile &Other) { return m_Name < Other.m_Name; }
};

int GatherFonts(const char *pFileName, int IsDir, int Type, void *pUser)
{
	const char *pSuffix = str_endswith(pFileName, ".ttf");
	if (IsDir || !pSuffix) return 0;

	sorted_array<CFontFile> &Fonts = *((sorted_array<CFontFile> *)pUser);
	char aFontName[128];
	str_truncate(aFontName, sizeof(aFontName), pFileName, pSuffix - pFileName);
	for (int i = 0; i < Fonts.size(); i++)
	{
		if (!str_comp(Fonts[i].m_Name, aFontName))
			return 0;
	}
	Fonts.add(CFontFile(aFontName, pFileName));
	return 0;
}

void CMenus::RenderFontSelection(CUIRect MainView)
{
	static CListBoxState s_FontList;
	static int s_SelectedFont = 0;
	static sorted_array<CFontFile> s_Fonts;

	if (s_Fonts.size() == 0)
	{
		Storage()->ListDirectory(IStorage::TYPE_ALL, "fonts", GatherFonts, &s_Fonts);
		for (int i = 0; i < s_Fonts.size(); i++)
		{
			if (str_comp(s_Fonts[i].m_FileName, g_Config.m_ClFontfile) == 0)
			{
				s_SelectedFont = i;
				break;
			}
		}
	}

	int OldSelectedFont = s_SelectedFont;
	static float s_Fade[2] = { 0 };
	UiDoListboxHeader(&s_FontList, &MainView, Localize(""), 20.0f, 2.0f);
	UiDoListboxStart(&s_FontList, &s_Fade[0], 20.0f, Localize("Fonts"), s_Fonts.size(), 1, s_SelectedFont);
	for (sorted_array<CFontFile>::range r = s_Fonts.all(); !r.empty(); r.pop_front())
	{
		CListboxItem Item = UiDoListboxNextItem(&s_FontList, &r.front());
		if (Item.m_Visible)
		{
			Item.m_Rect.VMargin(5.0f, &Item.m_Rect);
			Item.m_Rect.y += 2.0f;
			UI()->DoLabel(&Item.m_Rect, r.front().m_Name, Item.m_Rect.h*ms_FontmodHeight*0.8f, CUI::ALIGN_LEFT);
		}
	}

	s_SelectedFont = UiDoListboxEnd(&s_FontList, 0);
	if (OldSelectedFont != s_SelectedFont)
	{
		str_copy(g_Config.m_ClFontfile, s_Fonts[s_SelectedFont].m_FileName, sizeof(g_Config.m_ClFontfile));
		char aRelFontPath[512];

		str_format(aRelFontPath, sizeof(aRelFontPath), "fonts/%s", g_Config.m_ClFontfile);
		char aFontPath[512];

		IOHANDLE File = Storage()->OpenFile(aRelFontPath, IOFLAG_READ, IStorage::TYPE_ALL, aFontPath, sizeof(aFontPath));
		if (File)
			io_close(File);

		TextRender()->SetDefaultFont(TextRender()->LoadFont(aFontPath));
	}
}

void CMenus::PreparationLeftRightSide(const char* pName, CUIRect MainView, CUIRect *LeftSide, CUIRect *RightSide, const float Spacing, const float ButtonHeight)
{
	UI()->DoLabel(&MainView, pName, 14.0f, CUI::ALIGN_CENTER);
	MainView.HSplitTop(ButtonHeight, 0, &MainView);
	MainView.VSplitMid(LeftSide, RightSide);

	{ // left
		CUIRect BackLeft;
		LeftSide->VMargin(Spacing * 2.0f, &BackLeft);
		BackLeft.VMargin(2.0f, LeftSide);
		RenderTools()->DrawUIRect(&BackLeft, vec4(0.0f, 0.0f, 0.0f, g_Config.m_ClMenuAlpha / 50.0f), CUI::CORNER_ALL, 5.0f);
	}
	{ // right
		CUIRect BackRight;
		RightSide->VMargin(Spacing * 2.0f, &BackRight);
		BackRight.VMargin(2.0f, RightSide);
		RenderTools()->DrawUIRect(&BackRight, vec4(0.0f, 0.0f, 0.0f, g_Config.m_ClMenuAlpha / 50.0f), CUI::CORNER_ALL, 5.0f);
	}
	LeftSide->VSplitRight(Spacing * 0.5f, LeftSide, 0);
	RightSide->VSplitLeft(Spacing * 0.5f, 0, RightSide);
}