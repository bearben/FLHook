// Player Control plugin for FLHookPlugin
// Feb 2010 by Cannon
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

#include <windows.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <float.h>
#include <FLHook.h>
#include <plugin.h>
#include <math.h>
#include <list>
#include <set>

#include <PluginUtilities.h>
#include "Main.h"

#include <FLCoreServer.h>
#include <FLCoreCommon.h>


namespace GiveCash
{
	// The minimum transfer ammount.
	static int set_iMinTransfer = 0;

	// Transfers are not allowed to/from chars in this system.
	static uint set_iBlockedSystem = 0;

	// Enable in dock cash cheat detection
	static bool set_bCheatDetection = false;

	// Prohibit transfers if the character has not been online for at least this time
	static int set_iMinTime = 0;

	/**
	It checks character's givecash history and prints out any received cash messages.
	Also fixes the money fix list, we can do this because this plugin is called
	before the money fix list is accessed.
	*/
	static void CheckTransferLog(uint iClientID)
	{
		wstring wscCharname = ToLower((const wchar_t*) Players.GetActiveCharacterName(iClientID));

		string logFile;
		if (!GetUserFilePath(logFile, wscCharname, "-givecashlog.txt"))
			return;

		FILE *f = fopen(logFile.c_str(), "r");
		if (!f)
			return;

		// A fixed length buffer be a little dangerous, but char name lengths are fixed 
		// to about 30ish characters so this should be okay, and in the worst case 
		// we will catch the exception.
		try {
			char buf[1000];
			while (fgets(buf, 1000, f)!=NULL)
			{
				string scValue = buf;
				wstring msg = L"";
				uint lHiByte;
				uint lLoByte;
				while(scValue.length()>3 && sscanf(scValue.c_str(), "%02X%02X", &lHiByte, &lLoByte) == 2)
				{
					scValue = scValue.substr(4);
					msg.append(1, (wchar_t)((lHiByte << 8) | lLoByte));
				}
				PrintUserCmdText(iClientID, L"%s", msg.c_str());
			}
		} catch (...) {}
		// Always close the file and remove the givecash log.
		fclose(f);
		remove(logFile.c_str());
	}

	/**
	Save a transfer to disk so that we can inform the receiving character
	when they log in. The log is recorded in ascii hex to support wide
	char sets.
	*/
	static void LogTransfer(wstring wscToCharname, wstring msg)
	{
		string logFile;
		if (!GetUserFilePath(logFile, wscToCharname, "-givecashlog.txt"))
			return;
		FILE *f = fopen(logFile.c_str(), "at");
		if (!f)
			return;

		try
		{
			for(uint i = 0; (i < msg.length()); i++)
			{
				char cHiByte = msg[i] >> 8;
				char cLoByte = msg[i] & 0xFF;
				fprintf(f, "%02X%02X", ((uint)cHiByte) & 0xFF, ((uint)cLoByte) & 0xFF);
			}
			fprintf(f,"\n");
		}
		catch (...) {}
		fclose(f);
		return;
	}

	/** Return return if this account is banned */
	static bool IsBannedAccount(CAccount *acc)
	{
		wstring wscDir;
		HkGetAccountDirName(acc, wscDir);

		char szDataPath[MAX_PATH];
		GetUserDataPath(szDataPath);

		string path = string(szDataPath) + "\\Accts\\MultiPlayer\\" + wstos(wscDir) + "\\banned";

		FILE *file = fopen(path.c_str(), "r");
		if (file) {
			fclose(file);
			return true;
		}
		return false;
	}

	/** Return return if this char is in the blocked system */
	static bool InBlockedSystem(const wstring &wscCharname)
	{
		// An optimisation if we have no blocked systems.
		if (set_iBlockedSystem==0)
			return false;

		// If the char is logged in we can check in memory.
		uint iClientID=HkGetClientIdFromCharname(wscCharname);
		if (iClientID!=-1)
		{
			uint iSystem = 0;
			pub::Player::GetSystem(iClientID, iSystem);
			if (iSystem==set_iBlockedSystem)
				return true;
			return false;
		}

		// Have to check the charfile.
		wstring wscSystemNick;
		if (HkFLIniGet(wscCharname, L"system", wscSystemNick)!=HKE_OK)
			return false;

		uint iSystem = 0;
		pub::GetSystemID(iSystem, wstos(wscSystemNick).c_str());
		if (iSystem==set_iBlockedSystem)
			return true;
		return false;
	}


	void GiveCash::LoadSettings(const string &scPluginCfgFile)
	{
		set_iMinTransfer = IniGetI(scPluginCfgFile, "GiveCash", "MinTransfer", 1);
		set_iMinTime = IniGetI(scPluginCfgFile, "GiveCash", "MinTime", 0);
		set_bCheatDetection = IniGetB(scPluginCfgFile, "GiveCash", "CheatDetection", true);
		set_iBlockedSystem = CreateID(IniGetS(scPluginCfgFile, "GiveCash", "BlockedSystem", "").c_str());
	}

	/// Check for cash transfer while this char was offline whenever they
	/// enter or leave a base.
	void GiveCash::PlayerLaunch(uint iShip, unsigned int iClientID)
	{	
		CheckTransferLog(iClientID);
	}

	/// Check for cash transfer while this char was offline whenever they
	/// enter or leave a base. */
	void GiveCash::BaseEnter(uint iBaseID, uint iClientID)
	{
		CheckTransferLog(iClientID);
	}

	bool GiveCash::UserCmd_GiveCashTarget(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage) 
	{
		uint iShip, iTargetShip;
		pub::Player::GetShip(iClientID, iShip);
		pub::SpaceObj::GetTarget(iShip, iTargetShip);
		if (!iTargetShip)
		{
			PrintUserCmdText(iClientID, L"错误：目标不是飞船。");
			return true;
		}
		wstring wscCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientID);
		uint iClientIDTarget = HkGetClientIDByShip(iTargetShip);
		if (!iClientIDTarget)
		{
			PrintUserCmdText(iClientID, L"错误：目标不是玩家飞船。");
			return true;
		}
		wstring wscTargetCharname = (const wchar_t*)Players.GetActiveCharacterName(iClientIDTarget);

		wstring wscCash = GetParam(wscParam, L' ', 0);
		wstring wscAnon = GetParam(wscParam, L' ', 1);
		wscCash = ReplaceStr(wscCash, L".", L"");
		wscCash = ReplaceStr(wscCash, L",", L"");
		wscCash = ReplaceStr(wscCash, L"$", L"");
		int cash = ToInt(wscCash);
		if ((!wscTargetCharname.length() || cash <= 0) || (wscAnon.size() && wscAnon != L"anon"))
		{
			PrintUserCmdText(iClientID, L"错误：参数不合法");
			PrintUserCmdText(iClientID, usage);
			return true;
		}

		bool bAnon = false;
		if (wscAnon == L"anon")
			bAnon = true;

		GiveCash::GiveCashCombined(iClientID, cash, wscTargetCharname, wscCharname, bAnon);
		return true;
	}

	/** Process a give cash command */
	bool GiveCash::UserCmd_GiveCash(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage) 
	{
		// Get the current character name
		wstring wscCharname = (const wchar_t*) Players.GetActiveCharacterName(iClientID);

		// Get the parameters from the user command.
		wstring wscTargetCharname = GetParam(wscParam, L' ', 0);
		wstring wscCash = GetParam(wscParam, L' ', 1);
		wstring wscAnon = GetParam(wscParam, L' ', 2);
		wscCash = ReplaceStr(wscCash, L".", L"");
		wscCash = ReplaceStr(wscCash, L",", L"");
		wscCash = ReplaceStr(wscCash, L"$", L"");
		int cash = ToInt(wscCash);
		if ((!wscTargetCharname.length() || cash<=0) || (wscAnon.size() && wscAnon!=L"anon"))
		{
			PrintUserCmdText(iClientID, L"错误：参数不合法");
			PrintUserCmdText(iClientID, usage);
			return true;
		}

		bool bAnon = false;
		if (wscAnon==L"anon")
			bAnon = true;

		GiveCash::GiveCashCombined(iClientID, cash, wscTargetCharname, wscCharname, bAnon);
		return true;
	}

	bool GiveCash::GiveCashCombined(uint iClientID, const int &cash, const wstring &wscTargetCharname, const wstring &wscCharname, const bool &bAnon)
	{
		HK_ERROR err;

		if (HkGetAccountByCharname(wscTargetCharname) == 0)
		{
			PrintUserCmdText(iClientID, L"错误：角色不存在");
			return true;
		}

		int secs = 0;
		if ((err = HkGetOnLineTime(wscCharname, secs)) != HKE_OK) {
			PrintUserCmdText(iClientID, L"错误：" + HkErrGetText(err));
			return true;
		}
		if (secs<set_iMinTime)
		{
			PrintUserCmdText(iClientID, L"错误：在线时间不足");
			return true;
		}

		if (InBlockedSystem(wscCharname) || InBlockedSystem(wscTargetCharname))
		{
			PrintUserCmdText(iClientID, L"错误：转账被禁止");
			return true;
		}

		// Read the current number of credits for the player
		// and check that the character has enough cash.
		int iCash = 0;
		if ((err = HkGetCash(wscCharname, iCash)) != HKE_OK) {
			PrintUserCmdText(iClientID, L"错误：" + HkErrGetText(err));
			return true;
		}
		if (cash<set_iMinTransfer || cash<0) {
			PrintUserCmdText(iClientID, L"错误：转账金额过小，最低额度为 " + ToMoneyStr(set_iMinTransfer));
			return true;
		}
		if (iCash<cash)
		{
			PrintUserCmdText(iClientID, L"错误：现金不足");
			return true;
		}

		// Prevent target ship from becoming corrupt.
		float fTargetValue = 0.0f;
		if ((err = HKGetShipValue(wscTargetCharname, fTargetValue)) != HKE_OK)
		{
			PrintUserCmdText(iClientID, L"错误：" + HkErrGetText(err));
			return true;
		}
		if ((fTargetValue + cash) > 2000000000.0f)
		{
			PrintUserCmdText(iClientID, L"错误：转账金额超出上限");
			return true;
		}

		// Calculate the new cash
		int iExpectedCash = 0;
		if ((err = HkGetCash(wscTargetCharname, iExpectedCash)) != HKE_OK)
		{
			PrintUserCmdText(iClientID, L"错误：获取金额失败 err=" + HkErrGetText(err));
			return true;
		}
		iExpectedCash += cash;

		// Do an anticheat check on the receiving character first.
		uint targetClientId = HkGetClientIdFromCharname(wscTargetCharname);
		if (targetClientId != -1 && !HkIsInCharSelectMenu(targetClientId))
		{
			if (HkAntiCheat(targetClientId) != HKE_OK)
			{
				PrintUserCmdText(iClientID, L"错误：转账失败");
				AddLog("NOTICE: Possible cheating when sending %s credits from %s (%s) to %s (%s)",
					wstos(ToMoneyStr(cash)).c_str(),
					wstos(wscCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscCharname))).c_str(),
					wstos(wscTargetCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscTargetCharname))).c_str());
				return true;
			}
			HkSaveChar(targetClientId);
		}

		if (targetClientId != -1)
		{
			if (ClientInfo[iClientID].iTradePartner || ClientInfo[targetClientId].iTradePartner)
			{
				PrintUserCmdText(iClientID, L"错误：请关闭交易窗口");
				AddLog("NOTICE: Trade window open when sending %s credits from %s (%s) to %s (%s) %u %u",
					wstos(ToMoneyStr(cash)).c_str(),
					wstos(wscCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscCharname))).c_str(),
					wstos(wscTargetCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscTargetCharname))).c_str(),
					iClientID, targetClientId);
				return true;
			}
		}

		// Remove cash from current character and save it checking that the
		// save completes before allowing the cash to be added to the target ship.
		if ((err = HkAddCash(wscCharname, 0 - cash)) != HKE_OK)
		{
			PrintUserCmdText(iClientID, L"错误：移除现金失败 err=" + HkErrGetText(err));
			return true;
		}

		if (HkAntiCheat(iClientID) != HKE_OK)
		{
			PrintUserCmdText(iClientID, L"错误：转账失败");
			AddLog("NOTICE: Possible cheating when sending %s credits from %s (%s) to %s (%s)",
				wstos(ToMoneyStr(cash)).c_str(),
				wstos(wscCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscCharname))).c_str(),
				wstos(wscTargetCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscTargetCharname))).c_str());
			return true;
		}
		HkSaveChar(iClientID);

		// Add cash to target character
		if ((err = HkAddCash(wscTargetCharname, cash)) != HKE_OK)
		{
			PrintUserCmdText(iClientID, L"错误：添加金额失败 err=" + HkErrGetText(err));
			return true;
		}

		targetClientId = HkGetClientIdFromCharname(wscTargetCharname);
		if (targetClientId != -1 && !HkIsInCharSelectMenu(targetClientId))
		{
			if (HkAntiCheat(targetClientId) != HKE_OK)
			{
				PrintUserCmdText(iClientID, L"错误：转账失败");
				AddLog("NOTICE: Possible cheating when sending %s credits from %s (%s) to %s (%s)",
					wstos(ToMoneyStr(cash)).c_str(),
					wstos(wscCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscCharname))).c_str(),
					wstos(wscTargetCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscTargetCharname))).c_str());
				return true;
			}
			HkSaveChar(targetClientId);
		}


		// Check that receiving character has the correct ammount of cash.
		int iCurrCash;
		if ((err = HkGetCash(wscTargetCharname, iCurrCash)) != HKE_OK
			|| iCurrCash != iExpectedCash)
		{
			AddLog("ERROR: Cash transfer error when sending %s credits from %s (%s) to %s (%s) current %s credits expected %s credits ",
				wstos(ToMoneyStr(cash)).c_str(),
				wstos(wscCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscCharname))).c_str(),
				wstos(wscTargetCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscTargetCharname))).c_str(),
				wstos(ToMoneyStr(iCurrCash)).c_str(), wstos(ToMoneyStr(iExpectedCash)).c_str());
			PrintUserCmdText(iClientID, L"错误：转账失败");
			return true;
		}

		// If the target player is online then send them a message saying
		// telling them that they've received the cash.
		wstring msg;
		if (bAnon)
			msg += L"您收到匿名的 " + ToMoneyStr(cash) + L" 转账";
		else
			msg = L"您收到来自 " + wscCharname + L" 的 " + ToMoneyStr(cash) + L" 转账";
		if (targetClientId != -1 && !HkIsInCharSelectMenu(targetClientId))
		{
			PrintUserCmdText(targetClientId, L"%s", msg.c_str());
		}
		// Otherwise we assume that the character is offline so we record an entry
		// in the character's givecash.ini. When they come online we inform them
		// of the transfer. The ini is cleared when ever the character logs in.
		else
		{
			wstring msg;
			if (bAnon)
				msg += L"您收到匿名的 " + ToMoneyStr(cash) + L" 转账";
			else
				msg = L"您收到来自 " + wscCharname + L" 的 " + ToMoneyStr(cash) + L" 转账";
			LogTransfer(wscTargetCharname, msg);
		}

		AddLog("NOTICE: Send %s credits from %s (%s) to %s (%s)",
			wstos(ToMoneyStr(cash)).c_str(),
			wstos(wscCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscCharname))).c_str(),
			wstos(wscTargetCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscTargetCharname))).c_str());

		// A friendly message explaining the transfer.
		msg = L"您成功将 " + ToMoneyStr(cash) + L" 现金";
		if (bAnon)
			msg += L"匿名地";
		msg = msg + L"转账给 " + wscTargetCharname; 
		PrintUserCmdText(iClientID, L"%s", msg.c_str());
		return true;
	}

	/** Process a set cash code command */
	bool GiveCash::UserCmd_SetCashCode(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage) 
	{
		wstring wscCharname = (const wchar_t*) Players.GetActiveCharacterName(iClientID);
		string scFile;
		if (!GetUserFilePath(scFile, wscCharname, "-givecash.ini"))
			return true;

		wstring wscCode = GetParam(wscParam, L' ', 0);

		if (!wscCode.size())
		{
			PrintUserCmdText(iClientID, L"错误：参数不合法");
			PrintUserCmdText(iClientID, usage);
		}
		else if (wscCode==L"none")
		{
			IniWriteW(scFile, "Settings", "Code", L"");
			PrintUserCmdText(iClientID, L"取钱密码清空");
		}
		else
		{
			IniWriteW(scFile, "Settings", "Code", wscCode);
			PrintUserCmdText(iClientID, L"取钱密码设置为 "+wscCode);
		}
		return true;
	}

	/** Process a show cash command **/
	bool GiveCash::UserCmd_ShowCash(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage) 
	{
		// The last error.
		HK_ERROR err;

		// Get the current character name
		wstring wscCharname = (const wchar_t*) Players.GetActiveCharacterName(iClientID);

		// Get the parameters from the user command.
		wstring wscTargetCharname = GetParam(wscParam, L' ', 0);
		wstring wscCode = GetParam(wscParam, L' ', 1);

		if (!wscTargetCharname.length() || !wscCode.length())
		{
			PrintUserCmdText(iClientID, L"错误：参数不合法");
			PrintUserCmdText(iClientID, usage);
			return true;
		}

		CAccount *acc=HkGetAccountByCharname(wscTargetCharname);
		if (acc==0)
		{
			PrintUserCmdText(iClientID, L"错误：角色不存在");
			return true;	
		}

		string scFile;
		if (!GetUserFilePath(scFile, wscTargetCharname, "-givecash.ini"))
			return true;

		wstring wscTargetCode = IniGetWS(scFile, "Settings", "Code", L"");
		if (!wscTargetCode.length() || wscTargetCode!=wscCode)
		{
			PrintUserCmdText(iClientID, L"错误：密码错误");
			return true;
		}

		int iCash = 0;
		if ((err = HkGetCash(wscTargetCharname, iCash)) != HKE_OK)
		{
			PrintUserCmdText(iClientID, L"错误： "+HkErrGetText(err));
			return true;
		}

		PrintUserCmdText(iClientID, L"角色 "+wscTargetCharname+L" 的账户有 " + ToMoneyStr(iCash) + L" 现金");
		return true;
	}


	/** Process a draw cash command **/
	bool GiveCash::UserCmd_DrawCash(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage) 
	{
		// The last error.
		HK_ERROR err;

		// Get the current character name
		wstring wscCharname = (const wchar_t*) Players.GetActiveCharacterName(iClientID);

		// Get the parameters from the user command.
		wstring wscTargetCharname = GetParam(wscParam, L' ', 0);
		wstring wscCode = GetParam(wscParam, L' ', 1);
		wstring wscCash = GetParam(wscParam, L' ', 2);
		wscCash = ReplaceStr(wscCash, L".", L"");
		wscCash = ReplaceStr(wscCash, L",", L"");
		wscCash = ReplaceStr(wscCash, L"$", L"");
		int cash = ToInt(wscCash);
		if (!wscTargetCharname.length() || !wscCode.length() || cash<=0)
		{
			PrintUserCmdText(iClientID, L"错误：参数不合法");
			PrintUserCmdText(iClientID, usage);
			return true;
		}

		CAccount *iTargetAcc=HkGetAccountByCharname(wscTargetCharname);
		if (iTargetAcc==0)
		{
			PrintUserCmdText(iClientID, L"错误：角色不存在");
			return true;	
		}

		int secs = 0;
		if ((err = HkGetOnLineTime(wscTargetCharname, secs)) != HKE_OK) {
			PrintUserCmdText(iClientID, L"错误：" + HkErrGetText(err));
			return true;
		}
		if (secs<set_iMinTime)
		{
			PrintUserCmdText(iClientID, L"错误：在线时间不足");
			return true;
		}

		if (InBlockedSystem(wscCharname) || InBlockedSystem(wscTargetCharname) || IsBannedAccount(iTargetAcc))
		{
			PrintUserCmdText(iClientID, L"错误：转账被禁止");
			return true;
		}

		string scFile;
		if (!GetUserFilePath(scFile, wscTargetCharname, "-givecash.ini"))
			return true;

		wstring wscTargetCode = IniGetWS(scFile, "Settings", "Code", L"");
		if (!wscTargetCode.length() || wscTargetCode!=wscCode)
		{
			PrintUserCmdText(iClientID, L"错误：取钱密码错误");
			return true;
		}

		if (cash<set_iMinTransfer || cash<0) {
			PrintUserCmdText(iClientID, L"错误：转账金额过小，最低额度为 "+ToMoneyStr(set_iMinTransfer));
			return true;
		}

		int tCash = 0;
		if ((err = HkGetCash(wscTargetCharname, tCash)) != HKE_OK)
		{
			PrintUserCmdText(iClientID, L"ERR "+HkErrGetText(err));
			return true;
		}
		if (tCash<cash)
		{
			PrintUserCmdText(iClientID, L"现金不足");
			return true;
		}

		// Check the adding this cash to this player will not
		// exceed the maximum ship value.
		float fTargetValue = 0.0f;
		if ((err = HKGetShipValue(wscCharname, fTargetValue)) != HKE_OK)
		{
			PrintUserCmdText(iClientID, L"错误："+HkErrGetText(err));
			return true;
		}
		if ((fTargetValue + cash) > 2000000000.0f)
		{
			PrintUserCmdText(iClientID, L"错误：转账超出上限");
			return true;
		}

		// Calculate the new cash
		int iExpectedCash = 0;
		if ((err = HkGetCash(wscCharname, iExpectedCash)) != HKE_OK)
		{
			PrintUserCmdText(iClientID, L"错误："+HkErrGetText(err));
			return true;
		}
		iExpectedCash += cash;

		// Do an anticheat check on the receiving ship first.
		if (HkAntiCheat(iClientID) != HKE_OK)
		{
			PrintUserCmdText(iClientID, L"错误：转账失败");	
			AddLog("NOTICE: Possible cheating when drawing %s credits from %s (%s) to %s (%s)",
				wstos(ToMoneyStr(cash)).c_str(),
				wstos(wscTargetCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscTargetCharname))).c_str(),
				wstos(wscCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscCharname))).c_str());				
			return true;
		}
		HkSaveChar(iClientID);


		uint targetClientId = HkGetClientIdFromCharname(wscTargetCharname);
		if (targetClientId != -1)
		{
			if (ClientInfo[iClientID].iTradePartner || ClientInfo[targetClientId].iTradePartner)
			{
				PrintUserCmdText(iClientID, L"错误：请关闭交易窗口");
				AddLog("NOTICE: Trade window open when drawing %s credits from %s (%s) to %s (%s) %u %u",
					wstos(ToMoneyStr(cash)).c_str(),
					wstos(wscTargetCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscTargetCharname))).c_str(),
					wstos(wscCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscCharname))).c_str(),
					iClientID, targetClientId);			
				return true;
			}
		}

		// Remove cash from target character
		if ((err = HkAddCash(wscTargetCharname, 0-cash)) != HKE_OK)
		{
			PrintUserCmdText(iClientID, L"ERR "+HkErrGetText(err));
			return true;
		}

		if (targetClientId!=-1 && !HkIsInCharSelectMenu(targetClientId))
		{		
			if (HkAntiCheat(targetClientId) != HKE_OK)
			{
				PrintUserCmdText(iClientID, L"错误：转账失败");						
				AddLog("NOTICE: Possible cheating when drawing %s credits from %s (%s) to %s (%s)",
					wstos(ToMoneyStr(cash)).c_str(),
					wstos(wscTargetCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscTargetCharname))).c_str(),
					wstos(wscCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscCharname))).c_str());				
				return true;
			}
			HkSaveChar(targetClientId);
		}

		// Add cash to this player
		if ((err = HkAddCash(wscCharname, cash)) != HKE_OK)
		{
			PrintUserCmdText(iClientID, L"错误："+HkErrGetText(err));
			return true;
		}

		if (HkAntiCheat(iClientID) != HKE_OK)
		{
			PrintUserCmdText(iClientID, L"错误：转账失败");			
			AddLog("NOTICE: Possible cheating when drawing %s credits from %s (%s) to %s (%s)",
				wstos(ToMoneyStr(cash)).c_str(),
				wstos(wscTargetCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscTargetCharname))).c_str(),
				wstos(wscCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscCharname))).c_str());				
			return true;
		}
		HkSaveChar(iClientID);
		
		// Check that receiving player has the correct ammount of cash.
		int iCurrCash;
		if ((err = HkGetCash(wscCharname, iCurrCash)) != HKE_OK
			|| iCurrCash != iExpectedCash)
		{
			AddLog("ERROR: Cash transfer error when drawing %s credits from %s (%s) to %s (%s) current %s credits expected %s credits ",
					wstos(ToMoneyStr(cash)).c_str(),
					wstos(wscTargetCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscTargetCharname))).c_str(),
					wstos(wscCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscCharname))).c_str(),
					wstos(ToMoneyStr(iCurrCash)).c_str(), wstos(ToMoneyStr(iExpectedCash)).c_str());
			PrintUserCmdText(iClientID, L"错误：转账失败");
		}

		// If the target player is online then send them a message saying
		// telling them that they've received transfered cash.
		wstring msg = L"您成功将 " + ToMoneyStr(cash) + L" 现金转账给 " + wscCharname;
		if (targetClientId!=-1 && !HkIsInCharSelectMenu(targetClientId))
		{
			PrintUserCmdText(targetClientId, L"%s", msg.c_str());
		}
		// Otherwise we assume that the character is offline so we record an entry
		// in the character's givecash.ini. When they come online we inform them
		// of the transfer. The ini is cleared when ever the character logs in.
		else
		{
			LogTransfer(wscTargetCharname, msg);
		}

		AddLog("NOTICE: Draw %s credits from %s (%s) to %s (%s)",
			wstos(ToMoneyStr(cash)).c_str(),
			wstos(wscTargetCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscTargetCharname))).c_str(),
			wstos(wscCharname).c_str(), wstos(HkGetAccountID(HkGetAccountByCharname(wscCharname))).c_str());

		// A friendly message explaining the transfer.
		msg = GetTimeString(set_bLocalTime) + L": 您从 " + wscTargetCharname + L" 取走了 " + ToMoneyStr(cash) + L" 现金";
		PrintUserCmdText(iClientID, L"%s", msg.c_str());
		return true;
	}
}
