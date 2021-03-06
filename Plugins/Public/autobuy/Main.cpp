// Autobuy for FLHookPlugin
// December 2015 by BestDiscoveryHookDevs2015
//
// This is based on the original autobuy available in FLHook. However this one was hardly extensible and lacking features.
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Includes
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <windows.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <list>
#include <map>
#include <algorithm>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>
#include "Main.h"
#include <sstream>
#include <iostream>
#include <hookext_exports.h>

#define ADDR_COMMON_VFTABLE_MINE 0x139C64
#define ADDR_COMMON_VFTABLE_CM 0x139C90
#define ADDR_COMMON_VFTABLE_GUN 0x139C38

static int set_iPluginDebug = 0;

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

void LoadSettings();

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	srand((uint)time(0));
	// If we're being loaded from the command line while FLHook is running then
	// set_scCfgFile will not be empty so load the settings as FLHook only
	// calls load settings on FLHook startup and .rehash.
	if(fdwReason == DLL_PROCESS_ATTACH)
	{
		if (set_scCfgFile.length()>0)
			LoadSettings();
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
	}
	return true;
}

/// Hook will call this function after calling a plugin function to see if we the
/// processing to continue
EXPORT PLUGIN_RETURNCODE Get_PluginReturnCode()
{
	return returncode;
}

// For ships, we go the easy way and map each ship belonging to each base
static map <uint, int> mapAmmolimits;

// Autobuy data for players
struct AUTOBUY_PLAYERINFO
{
	bool bAutoBuyMissiles;
	bool bAutoBuyMines;
	bool bAutoBuyTorps;
	bool bAutoBuyCD;
	bool bAutoBuyCM;
	bool bAutobuyBB;
	bool bAutobuyCloak;
	bool bAutobuyMunition;
};

static map <uint, AUTOBUY_PLAYERINFO> mapAutobuyPlayerInfo;
static map <uint, uint> mapAutobuyFLHookExtras;

static map <uint, int> mapStackableItems;

uint iNanobotsID;
uint iShieldBatsID;

bool bPluginEnabled = true;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Loading Settings
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;

	//Load ammo limit data from FL
	string File_Misc = "..\\data\\equipment\\misc_equip.ini";
	string File_Weapon = "..\\data\\equipment\\weapon_equip.ini";
	string File_FLHook = "..\\exe\\flhook_plugins\\autobuy.cfg";
	int iLoaded = 0;
	int iLoaded2 = 0;
	int iLoadedStackables = 0;

	INI_Reader ini;
	if (ini.open(File_Misc.c_str(), false))
	{
		while (ini.read_header())
			{
				if (ini.is_header("CounterMeasure"))
				{
					uint itemname;
					int itemlimit;
					bool valid = false;

					while (ini.read_value())
					{
						if (ini.is_value("nickname"))
						{
							itemname = CreateID(ini.get_value_string(0));
						}
						else if (ini.is_value("ammo_limit"))
						{
							valid = true;
							itemlimit = ini.get_value_int(0);
						}
					}	

					if (valid == true)
					{
						mapAmmolimits[itemname] = itemlimit;
						++iLoaded;
					}
				}
			}
		ini.close();
	}
	if (ini.open(File_Weapon.c_str(), false))
	{
		while (ini.read_header())
		{
			
			if (ini.is_header("Munition"))
			{
				uint itemname;
				int itemlimit;
				bool valid = false;

				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						itemname = CreateID(ini.get_value_string(0));
					}
					else if (ini.is_value("ammo_limit"))
					{
						valid = true;
						itemlimit = ini.get_value_int(0);
					}
				}

				if (valid == true)
				{
					mapAmmolimits[itemname] = itemlimit;
					++iLoaded;
				}
			}
			else if (ini.is_header("Mine"))
			{
				uint itemname;
				int itemlimit;
				bool valid = false;

				while (ini.read_value())
				{
					if (ini.is_value("nickname"))
					{
						itemname = CreateID(ini.get_value_string(0));
					}
					else if (ini.is_value("ammo_limit"))
					{
						valid = true;
						itemlimit = ini.get_value_int(0);
					}
				}

				if (valid == true)
				{
					mapAmmolimits[itemname] = itemlimit;
					++iLoaded;
				}
			}
		}
		ini.close();
	}

	if (ini.open(File_FLHook.c_str(), false))
	{
		while (ini.read_header())
			{
				if (ini.is_header("config"))
				{
					while (ini.read_value())
					{
						if (ini.is_value("enabled"))
						{
							bPluginEnabled = ini.get_value_bool(0);
						}
					}				
				}
				else if (ini.is_header("extra"))
				{
					while (ini.read_value())
					{
						if (ini.is_value("item"))
						{
							mapAutobuyFLHookExtras[CreateID(ini.get_value_string(0))] = CreateID(ini.get_value_string(1));
							++iLoaded2;
						}
					}
				}
				else if (ini.is_header("stackable"))
				{
					while (ini.read_value())
					{
						if (ini.is_value("weapon"))
						{
							mapStackableItems[CreateID(ini.get_value_string(0))] = ini.get_value_int(1);
							++iLoadedStackables;
						}
					}
				}
			}
		ini.close();
	}


	ConPrint(L"AUTOBUY: Loaded %u ammo limit entries\n", iLoaded);
	ConPrint(L"AUTOBUY: Loaded %u FLHook extra items\n", iLoaded2);
	ConPrint(L"AUTOBUY: Loaded %u stackable launchers\n", iLoadedStackables);

	pub::GetGoodID(iNanobotsID, "ge_s_repair_01");
	pub::GetGoodID(iShieldBatsID, "ge_s_battery_01");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct AUTOBUY_CARTITEM
{
	uint iArchID;
	int iCount;
	wstring wscDescription;
};

int HkPlayerAutoBuyGetCount(list<CARGO_INFO> &lstCargo, uint iItemArchID)
{
	foreach(lstCargo, CARGO_INFO, it)
	{
		if ((*it).iArchID == iItemArchID)
			return (*it).iCount;
	}

	return 0;
}

#define ADD_EQUIP_TO_CART(desc)	{ aci.iArchID = ((Archetype::Launcher*)eq)->iProjectileArchID; \
								aci.iCount = mapAmmolimits[aci.iArchID] - HkPlayerAutoBuyGetCount(lstCargo, aci.iArchID); \
								aci.wscDescription = desc; \
								lstCart.push_back(aci); }

#define ADD_EQUIP_TO_CART_STACKABLE(desc)	{ aci.iArchID = ((Archetype::Launcher*)eq)->iProjectileArchID; \
								aci.iCount = (mapAmmolimits[aci.iArchID] * tempmap[eq->iArchID])  - HkPlayerAutoBuyGetCount(lstCargo, aci.iArchID); \
								aci.wscDescription = desc; \
								lstCart.push_back(aci); }

#define ADD_EQUIP_TO_CART_FLHOOK(desc)	{ aci.iArchID = mapAutobuyFLHookExtras[eq->iArchID]; \
								aci.iCount = mapAmmolimits[aci.iArchID] - HkPlayerAutoBuyGetCount(lstCargo, aci.iArchID); \
								aci.wscDescription = desc; \
								lstCart.push_back(aci); }

void AutobuyInfo(uint iClientID)
{
	PrintUserCmdText(iClientID, L"错误：参数不合法");
	PrintUserCmdText(iClientID, L"使用方法：/autobuy <选项> [<on/off>]");
	PrintUserCmdText(iClientID, L"<选项>:");
	PrintUserCmdText(iClientID, L"   info - 显示当前自动购买的设置");
	PrintUserCmdText(iClientID, L"   missiles - 开启/关闭 自动购买导弹");
	PrintUserCmdText(iClientID, L"   torps - 开启/关闭 自动购买鱼雷");
	PrintUserCmdText(iClientID, L"   mines - 开启/关闭 自动购买地雷");
	PrintUserCmdText(iClientID, L"   cd - 开启/关闭 自动购买CD弹药");
	PrintUserCmdText(iClientID, L"   cm - 开启/关闭 自动购买反制弹药");
	PrintUserCmdText(iClientID, L"   reload - 开启/关闭 自动购买维修和电池补给");
	PrintUserCmdText(iClientID, L"   all: 开启/关闭 自动购买全部");
	PrintUserCmdText(iClientID, L"例如:");
	PrintUserCmdText(iClientID, L"\"/autobuy missiles on\" 开启自动购买导弹");
	PrintUserCmdText(iClientID, L"\"/autobuy all off\" 关闭所有的自动购买");
	PrintUserCmdText(iClientID, L"\"/autobuy info\" 显示自动购买信息");
}

bool  UserCmd_AutoBuy(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	if (!bPluginEnabled)
	{
		PrintUserCmdText(iClientID, L"自动购买已关闭。");
		return true;
	}

	wstring wscType = ToLower(GetParam(wscParam, ' ', 0));
	wstring wscSwitch = ToLower(GetParam(wscParam, ' ', 1));

	if (!wscType.compare(L"info"))
	{
		PrintUserCmdText(iClientID, L"导弹：%s", mapAutobuyPlayerInfo[iClientID].bAutoBuyMissiles ? L"On" : L"Off");
		PrintUserCmdText(iClientID, L"地雷：%s", mapAutobuyPlayerInfo[iClientID].bAutoBuyMines ? L"On" : L"Off");
		PrintUserCmdText(iClientID, L"鱼雷：%s", mapAutobuyPlayerInfo[iClientID].bAutoBuyTorps ? L"On" : L"Off");
		PrintUserCmdText(iClientID, L"CD：%s", mapAutobuyPlayerInfo[iClientID].bAutoBuyCD ? L"On" : L"Off");
		PrintUserCmdText(iClientID, L"反制：%s", mapAutobuyPlayerInfo[iClientID].bAutoBuyCM ? L"On" : L"Off");
		PrintUserCmdText(iClientID, L"弹药：%s", mapAutobuyPlayerInfo[iClientID].bAutobuyMunition ? L"On" : L"Off");
		PrintUserCmdText(iClientID, L"隐身电池：%s", mapAutobuyPlayerInfo[iClientID].bAutobuyCloak ? L"On" : L"Off");
		PrintUserCmdText(iClientID, L"维修/护盾电池：%s", mapAutobuyPlayerInfo[iClientID].bAutobuyBB ? L"On" : L"Off");
		return true;
	}

	if (!wscType.length() || !wscSwitch.length() || ((wscSwitch.compare(L"on") != 0) && (wscSwitch.compare(L"off") != 0)))
	{
		AutobuyInfo(iClientID);
		return true;
	}
		

	bool bEnable = !wscSwitch.compare(L"on") ? true : false;
	if (!wscType.compare(L"all")) {

		mapAutobuyPlayerInfo[iClientID].bAutobuyBB = bEnable;
		mapAutobuyPlayerInfo[iClientID].bAutoBuyCD = bEnable;
		mapAutobuyPlayerInfo[iClientID].bAutobuyCloak = bEnable;
		mapAutobuyPlayerInfo[iClientID].bAutoBuyCM = bEnable;
		mapAutobuyPlayerInfo[iClientID].bAutoBuyMines = bEnable;
		mapAutobuyPlayerInfo[iClientID].bAutoBuyMissiles = bEnable;	
		mapAutobuyPlayerInfo[iClientID].bAutobuyMunition = bEnable;
		mapAutobuyPlayerInfo[iClientID].bAutoBuyTorps = bEnable;
		
		HookExt::IniSetB(iClientID, "autobuy.bb", bEnable ? true : false);
		HookExt::IniSetB(iClientID, "autobuy.cd", bEnable ? true : false);
		HookExt::IniSetB(iClientID, "autobuy.cloak", bEnable ? true : false);
		HookExt::IniSetB(iClientID, "autobuy.cm", bEnable ? true : false);
		HookExt::IniSetB(iClientID, "autobuy.mines", bEnable ? true : false);
		HookExt::IniSetB(iClientID, "autobuy.missiles", bEnable ? true : false);
		HookExt::IniSetB(iClientID, "autobuy.munition", bEnable ? true : false);
		HookExt::IniSetB(iClientID, "autobuy.torps", bEnable ? true : false);
	}
	else if (!wscType.compare(L"missiles")) {
		mapAutobuyPlayerInfo[iClientID].bAutoBuyMissiles = bEnable;
		HookExt::IniSetB(iClientID, "autobuy.missiles", bEnable);
	}
	else if (!wscType.compare(L"mines")) {
		mapAutobuyPlayerInfo[iClientID].bAutoBuyMines = bEnable;
		HookExt::IniSetB(iClientID, "autobuy.mines", bEnable);
	}
	else if (!wscType.compare(L"torps")) {
		mapAutobuyPlayerInfo[iClientID].bAutoBuyTorps = bEnable;
		HookExt::IniSetB(iClientID, "autobuy.torps", bEnable);
	}
	else if (!wscType.compare(L"cd")) {
		mapAutobuyPlayerInfo[iClientID].bAutoBuyCD = bEnable;
		HookExt::IniSetB(iClientID, "autobuy.cd", bEnable);
	}
	else if (!wscType.compare(L"cm")) {
		mapAutobuyPlayerInfo[iClientID].bAutoBuyCM = bEnable;
		HookExt::IniSetB(iClientID, "autobuy.cm", bEnable);
	}
	else if (!wscType.compare(L"bb")) {
		mapAutobuyPlayerInfo[iClientID].bAutobuyBB = bEnable;
		HookExt::IniSetB(iClientID, "autobuy.bb", bEnable);
	}
	else if (!wscType.compare(L"munition")) {
		mapAutobuyPlayerInfo[iClientID].bAutobuyMunition = bEnable;
		HookExt::IniSetB(iClientID, "autobuy.munition", bEnable);
	}
	else if (!wscType.compare(L"cloak")) {
		mapAutobuyPlayerInfo[iClientID].bAutobuyCloak = bEnable;
		HookExt::IniSetB(iClientID, "autobuy.cloak", bEnable);
	}
	else
		AutobuyInfo(iClientID);

	PrintUserCmdText(iClientID, L"OK");
	return true;
}

void CheckforStackables(uint iClientID)
{
	map<uint, uint> tempmap;

	// player cargo
	int iRemHoldSize;
	list<CARGO_INFO> lstCargo;
	HkEnumCargo(ARG_CLIENTID(iClientID), lstCargo, iRemHoldSize);

	foreach(lstCargo, CARGO_INFO, it)
	{
		if (!(*it).bMounted)
			continue;

		if (mapStackableItems.find(it->iArchID) != mapStackableItems.end())
		{
			tempmap[it->iArchID] += 1;
		}
	}

	//now that we have identified the stackables, retrieve the current ammo count for stackables
	for (map<uint, uint>::iterator ita = tempmap.begin(); ita != tempmap.end(); ita++)
	{
		Archetype::Equipment *eq = Archetype::GetEquipment(ita->first);
		uint ammo = ((Archetype::Launcher*)eq)->iProjectileArchID;

		for (list<EquipDesc>::iterator item = Players[iClientID].equipDescList.equip.begin(); item != Players[iClientID].equipDescList.equip.end(); item++)
		{
			if (item->iArchID == ammo)
			{
				if (item->iCount > (mapAmmolimits[ammo] * tempmap[ita->first]))
				{
					wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientID);
					//ConPrint(L"DEBUG: player %s, iCount %d, ammo %d, tempmap %d \n", wscCharname.c_str(), item->iCount, mapAmmolimits[ammo], tempmap[ita->first]);
					PrintUserCmdText(iClientID, L"您的弹药超出了上限，一部分被移除。");

					pub::Player::RemoveCargo(iClientID, item->sID, (item->iCount - (mapAmmolimits[ammo] * tempmap[ita->first])) );
				}
			}
		}
	}

	

	foreach(lstCargo, CARGO_INFO, it)
	{
		if (!(*it).bMounted)
			continue;

		if (mapStackableItems.find(it->iArchID) != mapStackableItems.end())
		{
			tempmap[it->iArchID] += 1;
		}
	}


}


void PlayerAutobuy(uint iClientID, uint iBaseID)
{
	map<uint, int> tempmap;

	// player cargo
	int iRemHoldSize;
	list<CARGO_INFO> lstCargo;
	HkEnumCargo(ARG_CLIENTID(iClientID), lstCargo, iRemHoldSize);

	// shopping cart
	list<AUTOBUY_CARTITEM> lstCart;

	if (mapAutobuyPlayerInfo[iClientID].bAutobuyBB)
	{ // shield bats & nanobots
		Archetype::Ship *ship = Archetype::GetShip(Players[iClientID].iShipArchetype);
		
		uint iRemNanobots = ship->iMaxNanobots;
		uint iRemShieldBats = ship->iMaxShieldBats;
		bool bNanobotsFound = false;
		bool bShieldBattsFound = false;
		foreach(lstCargo, CARGO_INFO, it)
		{
			AUTOBUY_CARTITEM aci;
			if ((*it).iArchID == iNanobotsID) {
				aci.iArchID = iNanobotsID;
				aci.iCount = ship->iMaxNanobots - (*it).iCount;
				aci.wscDescription = L"维修机器人";
				lstCart.push_back(aci);
				bNanobotsFound = true;
			}
			else if ((*it).iArchID == iShieldBatsID) {
				aci.iArchID = iShieldBatsID;
				aci.iCount = ship->iMaxShieldBats - (*it).iCount;
				aci.wscDescription = L"护盾电池";
				lstCart.push_back(aci);
				bShieldBattsFound = true;
			}
		}

		if (!bNanobotsFound)
		{ // no nanos found -> add all
			AUTOBUY_CARTITEM aci;
			aci.iArchID = iNanobotsID;
			aci.iCount = ship->iMaxNanobots;
			aci.wscDescription = L"维修机器人";
			lstCart.push_back(aci);
		}

		if (!bShieldBattsFound)
		{ // no batts found -> add all
			AUTOBUY_CARTITEM aci;
			aci.iArchID = iShieldBatsID;
			aci.iCount = ship->iMaxShieldBats;
			aci.wscDescription = L"护盾电池";
			lstCart.push_back(aci);
		}
	}

	if (mapAutobuyPlayerInfo[iClientID].bAutoBuyCD || mapAutobuyPlayerInfo[iClientID].bAutoBuyCM || mapAutobuyPlayerInfo[iClientID].bAutoBuyMines ||
		mapAutobuyPlayerInfo[iClientID].bAutoBuyMissiles || mapAutobuyPlayerInfo[iClientID].bAutobuyMunition || mapAutobuyPlayerInfo[iClientID].bAutoBuyTorps)
	{
		// add mounted equip to a new list and eliminate double equipment(such as 2x lancer etc)
		list<CARGO_INFO> lstMounted;
		foreach(lstCargo, CARGO_INFO, it)
		{
			if (!(*it).bMounted)
				continue;

			if (mapStackableItems.find(it->iArchID) != mapStackableItems.end())
			{
				if (tempmap[it->iArchID] < mapStackableItems[it->iArchID])
					tempmap[it->iArchID] += 1;
			}

			bool bFound = false;
			foreach(lstMounted, CARGO_INFO, it2)
			{				
				if ((*it2).iArchID == (*it).iArchID)
				{
					bFound = true;
					break;
				}
			}

			if (!bFound)
				lstMounted.push_back(*it);
		}

		uint iVFTableMines = (uint)hModCommon + ADDR_COMMON_VFTABLE_MINE;
		uint iVFTableCM = (uint)hModCommon + ADDR_COMMON_VFTABLE_CM;
		uint iVFTableGun = (uint)hModCommon + ADDR_COMMON_VFTABLE_GUN;

		// check mounted equip
		foreach(lstMounted, CARGO_INFO, it2)
		{
			uint i = (*it2).iArchID;
			AUTOBUY_CARTITEM aci;
			Archetype::Equipment *eq = Archetype::GetEquipment(it2->iArchID);
			EQ_TYPE eq_type = HkGetEqType(eq);
			if (eq_type == ET_MINE)
			{
				if (mapAutobuyPlayerInfo[iClientID].bAutoBuyMines)
					ADD_EQUIP_TO_CART(L"地雷")
			}
			else if (eq_type == ET_CM)
			{
				if (mapAutobuyPlayerInfo[iClientID].bAutoBuyCM)
					ADD_EQUIP_TO_CART(L"反制")
			}
			else if (eq_type == ET_TORPEDO)
			{
				if (mapAutobuyPlayerInfo[iClientID].bAutoBuyTorps)
				{
					if (mapStackableItems.find(eq->get_id()) != mapStackableItems.end())
					{
						ADD_EQUIP_TO_CART_STACKABLE(L"鱼雷")
					}
					else
					{
						ADD_EQUIP_TO_CART(L"鱼雷")
					}
				}
			}
			else if (eq_type == ET_CD)
			{
				if (mapAutobuyPlayerInfo[iClientID].bAutoBuyCD)
					ADD_EQUIP_TO_CART(L"CD")
			}
			else if (eq_type == ET_MISSILE)
			{
				if (mapAutobuyPlayerInfo[iClientID].bAutoBuyMissiles)
				{
					if (mapStackableItems.find(eq->get_id()) != mapStackableItems.end())
					{
						ADD_EQUIP_TO_CART_STACKABLE(L"导弹")
					}
					else
					{
						ADD_EQUIP_TO_CART(L"导弹")
					}
				}
			}
			else if (eq_type == ET_GUN)
			{
				if (mapAutobuyPlayerInfo[iClientID].bAutobuyMunition)
				{
					if (mapStackableItems.find(eq->get_id()) != mapStackableItems.end())
					{
						ADD_EQUIP_TO_CART_STACKABLE(L"弹药")
					}
					else
					{
						ADD_EQUIP_TO_CART(L"弹药")
					}
				}
			}

			//FLHook handling
			if (mapAutobuyFLHookExtras.find(eq->iArchID) != mapAutobuyFLHookExtras.end())
			{
				//Cloak Fuel
				if (mapAutobuyPlayerInfo[iClientID].bAutobuyCloak)
					ADD_EQUIP_TO_CART_FLHOOK(L"隐身电池")
			}
		}
	}

	// search base in base-info list
	BASE_INFO *bi = 0;
	foreach(lstBases, BASE_INFO, it3)
	{
		if (it3->iBaseID == iBaseID)
		{
			bi = &(*it3);
			break;
		}
	}

	if (!bi)
		return; // base not found

	int iCash;
	HkGetCash(ARG_CLIENTID(iClientID), iCash);

	foreach(lstCart, AUTOBUY_CARTITEM, it4)
	{
		if (!(*it4).iCount || !Arch2Good((*it4).iArchID))
			continue;

		// check if good is available and if player has the neccessary rep
		bool bGoodAvailable = false;
		foreach(bi->lstMarketMisc, DATA_MARKETITEM, itmi)
		{
			if (itmi->iArchID == it4->iArchID)
			{
				// get base rep
				int iSolarRep;
				pub::SpaceObj::GetSolarRep(bi->iObjectID, iSolarRep);
				uint iBaseRep;
				pub::Reputation::GetAffiliation(iSolarRep, iBaseRep);
				if (iBaseRep == -1)
					continue; // rep can't be determined yet(space object not created yet?)

							  // get player rep
				int iRepID;
				pub::Player::GetRep(iClientID, iRepID);

				// check if rep is sufficient
				float fPlayerRep;
				pub::Reputation::GetGroupFeelingsTowards(iRepID, iBaseRep, fPlayerRep);
				if (fPlayerRep < itmi->fRep)
					break; // bad rep, not allowed to buy
				bGoodAvailable = true;
				break;
			}
		}

		if (!bGoodAvailable)
			continue; // base does not sell this item or bad rep

		float fPrice;
		if (pub::Market::GetPrice(iBaseID, (*it4).iArchID, fPrice) == -1)
			continue; // good not available

		Archetype::Equipment *eq = Archetype::GetEquipment((*it4).iArchID);
		if (iRemHoldSize < (eq->fVolume * (*it4).iCount))
		{
			uint iNewCount = (uint)(iRemHoldSize / eq->fVolume);
			if (!iNewCount) {
				//				PrintUserCmdText(iClientID, L"Auto-Buy(%s): FAILED! Insufficient cargo space", (*it4).wscDescription.c_str());
				continue;
			}
			else
				(*it4).iCount = iNewCount;
		}

		int iCost = ((int)fPrice * (*it4).iCount);
		if (iCash < iCost)
			PrintUserCmdText(iClientID, L"自动购买（%s）：失败！现金不足", (*it4).wscDescription.c_str());
		else {
			HkAddCash(ARG_CLIENTID(iClientID), -iCost);
			iCash -= iCost;
			iRemHoldSize -= ((int)eq->fVolume * (*it4).iCount);


			//Turns out we need to use HkAddCargo due to anticheat problems
			HkAddCargo(ARG_CLIENTID(iClientID), (*it4).iArchID, (*it4).iCount, false);

			// add the item, dont use hkaddcargo for performance/bug reasons
			// assume we only mount multicount goods (missiles, ammo, bots)
			//pub::Player::AddCargo(iClientID, (*it4).iArchID, (*it4).iCount, 1, false);

			if ((*it4).iCount != 0)
			{
				PrintUserCmdText(iClientID, L"自动购买（%s）：购买了 %d 单位，价值：%s 星币", (*it4).wscDescription.c_str(), (*it4).iCount, ToMoneyStr(iCost).c_str());
			}
		}
	}


}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Actual Code
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Clean up when a client disconnects */
void ClearClientInfo(uint iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	mapAutobuyPlayerInfo.erase(iClientID);
}

void __stdcall CharacterSelect_AFTER(struct CHARACTER_ID const &charId, unsigned int iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	ClearClientInfo(iClientID);

	mapAutobuyPlayerInfo[iClientID].bAutobuyBB = HookExt::IniGetB(iClientID, "autobuy.bb");
	mapAutobuyPlayerInfo[iClientID].bAutoBuyCD = HookExt::IniGetB(iClientID, "autobuy.cd");
	mapAutobuyPlayerInfo[iClientID].bAutobuyCloak = HookExt::IniGetB(iClientID, "autobuy.cloak");
	mapAutobuyPlayerInfo[iClientID].bAutoBuyCM = HookExt::IniGetB(iClientID, "autobuy.cm");
	mapAutobuyPlayerInfo[iClientID].bAutoBuyMines = HookExt::IniGetB(iClientID, "autobuy.mines");
	mapAutobuyPlayerInfo[iClientID].bAutoBuyMissiles = HookExt::IniGetB(iClientID, "autobuy.missiles");
	mapAutobuyPlayerInfo[iClientID].bAutobuyMunition = HookExt::IniGetB(iClientID, "autobuy.munition");
	mapAutobuyPlayerInfo[iClientID].bAutoBuyTorps = HookExt::IniGetB(iClientID, "autobuy.torps");
	
}

void __stdcall BaseEnter_AFTER(unsigned int iBaseID, unsigned int iClientID)
{
	pub::Player::GetBase(iClientID, iBaseID);
	PlayerAutobuy(iClientID, iBaseID);
}

void __stdcall PlayerLaunch_AFTER(unsigned int iShip, unsigned int iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	CheckforStackables(iClientID);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Client command processing
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef bool(*_UserCmdProc)(uint, const wstring &, const wstring &, const wchar_t*);

struct USERCMD
{
	wchar_t *wszCmd;
	_UserCmdProc proc;
	wchar_t *usage;
};

USERCMD UserCmds[] =
{
	{ L"/autobuy", UserCmd_AutoBuy, L"Usage: /autobuy" },
	{ L"/autobuy*", UserCmd_AutoBuy, L"Usage: /autobuy" },
};

/**
This function is called by FLHook when a user types a chat string. We look at the
string they've typed and see if it starts with one of the above commands. If it
does we try to process it.
*/
bool UserCmd_Process(uint iClientID, const wstring &wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	wstring wscCmdLineLower = ToLower(wscCmd);

	// If the chat string does not match the USER_CMD then we do not handle the
	// command, so let other plugins or FLHook kick in. We require an exact match
	for (uint i = 0; (i < sizeof(UserCmds) / sizeof(USERCMD)); i++)
	{

		if (wscCmdLineLower.find(UserCmds[i].wszCmd) == 0)
		{
			// Extract the parameters string from the chat string. It should
			// be immediately after the command and a space.
			wstring wscParam = L"";
			if (wscCmd.length() > wcslen(UserCmds[i].wszCmd))
			{
				if (wscCmd[wcslen(UserCmds[i].wszCmd)] != ' ')
					continue;
				wscParam = wscCmd.substr(wcslen(UserCmds[i].wszCmd) + 1);
			}

			// Dispatch the command to the appropriate processing function.
			if (UserCmds[i].proc(iClientID, wscCmd, wscParam, UserCmds[i].usage))
			{
				// We handled the command tell FL hook to stop processing this
				// chat string.
				returncode = SKIPPLUGINS_NOFUNCTIONCALL; // we handled the command, return immediatly
				return true;
			}
		}
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions to hook
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Autobuy by Discovery Development Team";
	p_PI->sShortName = "autobuy";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;
	
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&BaseEnter_AFTER, PLUGIN_HkIServerImpl_BaseEnter_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&CharacterSelect_AFTER, PLUGIN_HkIServerImpl_CharacterSelect_AFTER, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ClearClientInfo, PLUGIN_ClearClientInfo, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&PlayerLaunch_AFTER, PLUGIN_HkIServerImpl_PlayerLaunch_AFTER, 0));

	return p_PI;
}
