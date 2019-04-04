#include "Main.h"

bool UserCommands::UserCmd_Deploy(uint iClientID, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{

	//Verify that the user is in space
	uint playerShip;
	pub::Player::GetShip(iClientID, playerShip);
	if(!playerShip)
	{
		PrintUserCmdText(iClientID, L"���󣺲���̫����");
		return true;
	}

	// Check that the user has a valid bay mounted, if so, get the struct associated with it
	BayArch bayArch;
	bool foundBay = false;
	for (auto& item : Players[iClientID].equipDescList.equip)
	{
		if (item.bMounted) 
		{
			if(availableDroneBays.find(item.iArchID) != availableDroneBays.end())
			{
				foundBay = true;
				bayArch = availableDroneBays[item.iArchID];
				break;
			}
		}
	}

	if (!foundBay)
	{
		PrintUserCmdText(iClientID, L"δ�ҵ����˻����豸");
		return true;
	}

	clientDroneInfo[iClientID].droneBay = bayArch;

	// Verify that the user doesn't already have a drone in space
	if(clientDroneInfo[iClientID].deployedInfo.deployedDroneObj != 0)
	{
		PrintUserCmdText(iClientID, L"ͬһʱ����ֻ��ʹ��һ�����˻�");
		return true;
	}

	// Verify that the user isn't already building a drone
	if(clientDroneInfo[iClientID].buildState != STATE_DRONE_OFF)
	{
		PrintUserCmdText(iClientID, L"���Ѿ���һ�����˻�׼�����");
		return true;
	}

	// Verify that the client isn't cruising or in a tradelane
	const ENGINE_STATE engineState = HkGetEngineState(iClientID);
	if(engineState == ES_TRADELANE || engineState == ES_CRUISE)
	{
		PrintUserCmdText(iClientID, L"Ѳ����ͨ�������޷��������˻���������ȡ��");
		return true;
	}

	//Get the drone type argument - We don't care about any garbage after the first space 
	const string reqDroneType = wstos(GetParam(wscParam, L' ', 0));

	// Verify that the requested drone type is a member of the bay's available drones
	if(find(bayArch.availableDrones.begin(), bayArch.availableDrones.end(), reqDroneType) == bayArch.availableDrones.end())
	{
		PrintUserCmdText(iClientID, L"�������˻��ⲻ֧�ִ������˻���");
		PrintUserCmdText(iClientID, L"--- ���Է�������˻� ---");
		for (const auto& bay : bayArch.availableDrones)
		{
			PrintUserCmdText(iClientID, stows(bay));
		}
		PrintUserCmdText(iClientID, L"------------------------");
		return true;
	}

	const DroneArch requestedDrone = availableDroneArch[reqDroneType];

	// All of the required information is present! Build the timer struct and add it to the list
	DroneBuildTimerWrapper wrapper;
	wrapper.buildTimeRequired = clientDroneInfo[iClientID].droneBay.iDroneBuildTime;
	wrapper.reqDrone = requestedDrone;
	wrapper.startBuildTime = timeInMS();

	buildTimerMap[iClientID] = wrapper;

	// Set the buildstate, and alert the user
	clientDroneInfo[iClientID].buildState = STATE_DRONE_BUILDING;
	PrintUserCmdText(iClientID, L"���˻��Ѿ����÷���׼�� :: ����ʱ %i ��", clientDroneInfo[iClientID].droneBay.iDroneBuildTime);

	// Save the carrier shipObj to the client struct
	clientDroneInfo[iClientID].carrierShipobj = playerShip;
	return true;
}

bool UserCommands::UserCmd_AttackTarget(uint iClientID, const wstring& wscCmd, const wstring& wscParam,
	const wchar_t* usage)
{
	// Verify that the user is in space
	uint iShipObj;
	pub::Player::GetShip(iClientID, iShipObj);
	if(!iShipObj)
	{
		PrintUserCmdText(iClientID, L"����Ҫ��̫����ʹ�ô�ָ��");
		return true;
	}

	// Verify that the user has a drone currently deployed
	if(clientDroneInfo[iClientID].deployedInfo.deployedDroneObj == 0)
	{
		PrintUserCmdText(iClientID, L"����Ҫ��һ�ܻ�����˻���ִ�д�����");
		return true;
	}

	// Get the players current target
	uint iTargetObj;
	pub::SpaceObj::GetTarget(iShipObj, iTargetObj);

	if(!iTargetObj)
	{
		PrintUserCmdText(iClientID, L"��ѡ�����˻��Ĺ���Ŀ��");
		return true;
	}

	//Only allow the drone to target the targets specified in the configuration
	const BayArch& clientBayArch = clientDroneInfo[iClientID].droneBay;
	uint targetArchetype;
	pub::SpaceObj::GetSolarArchetypeID(iTargetObj, targetArchetype);
	Archetype::Ship* targetShiparch = Archetype::GetShip(targetArchetype);
	if (!targetShiparch)
	{
		PrintUserCmdText(iClientID, L"Ŀ�겻�Ϸ�������һ�ҷɴ�");
		return true;
	}

	//Validate that we're only engaging a shipclass that we're allowed to engage
	const auto it = find(clientBayArch.validShipclassTargets.begin(), clientBayArch.validShipclassTargets.end(), targetShiparch->iShipClass);
	if(it == clientBayArch.validShipclassTargets.end())
	{
		PrintUserCmdText(iClientID, L"Ŀ�겻�Ϸ������˻�����Ӧ�Դ˷ɴ�");
		return true;
	}

	PrintUserCmdText(iClientID, L"");

	const uint droneObj = clientDroneInfo[iClientID].deployedInfo.deployedDroneObj;

	// Set the old target to neutral reputation, and the hostile one hostile.
	Utility::SetRepNeutral(droneObj, clientDroneInfo[iClientID].deployedInfo.lastShipObjTarget);
	Utility::SetRepHostile(droneObj, iTargetObj);

	// Set the target hostile to the drone as well only if it isn't another existing drone 
	bool isTargetDrone = false;
	for (const auto& currClient : clientDroneInfo)
	{
		if (iTargetObj == currClient.second.deployedInfo.deployedDroneObj)
		{
			isTargetDrone = true;
			break;
		}
	}

	if (!isTargetDrone)
	{
		Utility::SetRepHostile(iTargetObj, droneObj);
	}

	clientDroneInfo[iClientID].deployedInfo.lastShipObjTarget = iTargetObj;
	PrintUserCmdText(iClientID, L"���˻�����ѡ�зɴ�\n");

	// If the last shipObj the drone was targeting is a player, log the event
	if (clientDroneInfo[iClientID].deployedInfo.lastShipObjTarget != 0)
	{
		const wstring charname = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(iClientID));
		const uint targetid = HkGetClientIDByShip(clientDroneInfo[iClientID].deployedInfo.lastShipObjTarget);
		const wstring targetname = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(targetid));

		// Only bother logging if we weren't engaging a NPC
		if (!targetname.empty())
		{
			wstring logString = L"Player %s has ordered its drone to target %t";
			logString = ReplaceStr(logString, L"%s", charname);
			logString = ReplaceStr(logString, L"%t", targetname);
			Utility::LogEvent(wstos(logString).c_str());

			// Since we know this was a real player, alert them that they're being engaged
			PrintUserCmdText(targetid, L"��� %s �����˻������㹥����", charname);
		}
	}

	return true;
}


bool UserCommands::UserCmd_RecallDrone(uint iClientID, const wstring& wscCmd, const wstring& wscParam,
	const wchar_t* usage)
{
	// Verify that the user is in space
	uint iShipObj;
	pub::Player::GetShip(iClientID, iShipObj);
	if (!iShipObj)
	{
		PrintUserCmdText(iClientID, L"����Ҫ��̫����ʹ�ô�ָ��");
		return true;
	}

	// Verify that the user has a drone currently deployed
	if (clientDroneInfo[iClientID].deployedInfo.deployedDroneObj == 0)
	{
		PrintUserCmdText(iClientID, L"����Ҫ��һ�ܻ�����˻���ִ�д�����");
		return true;
	}

	// Set the NPC to fly to your current position
	pub::AI::DirectiveGotoOp gotoOp;
	
	// Type zero says to fly to a spaceObj
	gotoOp.iGotoType = 0;
	gotoOp.iTargetID = iShipObj;
	gotoOp.fRange = 10.0;
	gotoOp.fThrust = 100;
	gotoOp.goto_cruise = true;

	pub::AI::SubmitDirective(clientDroneInfo[iClientID].deployedInfo.deployedDroneObj, &gotoOp);

	//Create the timer entry to keep watch on this docking operation
	DroneDespawnWrapper wrapper;
	wrapper.droneObj = clientDroneInfo[iClientID].deployedInfo.deployedDroneObj;
	wrapper.parentObj = iShipObj;
	
	droneDespawnMap[iClientID] = wrapper;

	PrintUserCmdText(iClientID, L"�ѷ������˻�����ָ��");

	return true;
}

bool UserCommands::UserCmd_DroneStop(uint iClientID, const wstring& wscCmd, const wstring& wscParam,
	const wchar_t* usage)
{
	// Verify that the user is in space
	uint iShipObj;
	pub::Player::GetShip(iClientID, iShipObj);
	if (!iShipObj)
	{
		PrintUserCmdText(iClientID, L"����Ҫ��̫����ʹ�ô�ָ��");
		return true;
	}

	// Verify that the user has a drone currently deployed
	if (clientDroneInfo[iClientID].deployedInfo.deployedDroneObj == 0)
	{
		PrintUserCmdText(iClientID, L"����Ҫ��һ�ܻ�����˻���ִ�д�����");
		return true;
	}

	const uint droneObj = clientDroneInfo[iClientID].deployedInfo.deployedDroneObj;

	// Set the drone reputation to neutral with who it was last attacking
	Utility::SetRepNeutral(droneObj, clientDroneInfo[iClientID].deployedInfo.lastShipObjTarget);

	// Send a stop directive to the drone
	pub::AI::DirectiveCancelOp cancelOp;
	pub::AI::SubmitDirective(droneObj, &cancelOp);

	PrintUserCmdText(iClientID, L"���˻�����ȡ��");

	// Log event
	const wstring charname = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(iClientID));
	wstring logString = L"Player %s halted drone operations";
	logString = ReplaceStr(logString, L"%s", charname);
	Utility::LogEvent(wstos(logString).c_str());

	return true;
}

bool UserCommands::UserCmd_DroneCome(uint iClientID, const wstring& wscCmd, const wstring& wscParam,
	const wchar_t* usage)
{
	// Verify that the user is in space
	uint iShipObj;
	pub::Player::GetShip(iClientID, iShipObj);
	if (!iShipObj)
	{
		PrintUserCmdText(iClientID, L"����Ҫ��̫����ʹ�ô�ָ��");
		return true;
	}

	// Verify that the user has a drone currently deployed
	if (clientDroneInfo[iClientID].deployedInfo.deployedDroneObj == 0)
	{
		PrintUserCmdText(iClientID, L"����Ҫ��һ�ܻ�����˻���ִ�д�����");
		return true;
	}

	const uint droneObj = clientDroneInfo[iClientID].deployedInfo.deployedDroneObj;

	// Set the drone reputation to neutral with who it was last attacking
	Utility::SetRepNeutral(droneObj, clientDroneInfo[iClientID].deployedInfo.lastShipObjTarget);

	// Set the NPC to fly to your current position
	pub::AI::DirectiveGotoOp gotoOp;

	// Type zero says to fly to a spaceObj
	gotoOp.iGotoType = 0;
	gotoOp.iTargetID = iShipObj;
	gotoOp.fRange = 10.0;
	gotoOp.fThrust = 100;
	gotoOp.goto_cruise = true;

	pub::AI::SubmitDirective(clientDroneInfo[iClientID].deployedInfo.deployedDroneObj, &gotoOp);

	PrintUserCmdText(iClientID, L"���˻�����ս��������������λ��");

	// If the last shipObj the drone was targeting is a player, log the event
	if (clientDroneInfo[iClientID].deployedInfo.lastShipObjTarget != 0)
	{
		const wstring charname = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(iClientID));
		const wstring targetname = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(HkGetClientIDByShip(clientDroneInfo[iClientID].deployedInfo.lastShipObjTarget)));

		// Only bother logging if we weren't engaging a NPC
		if (!targetname.empty())
		{
			wstring logString = L"Player %s disengaged from target %t";
			logString = ReplaceStr(logString, L"%s", charname);
			logString = ReplaceStr(logString, L"%t", targetname);
			Utility::LogEvent(wstos(logString).c_str());
		}
	}

	return true;
}

bool UserCommands::UserCmd_DroneBayAvailability(uint iClientID, const wstring& wscCmd, const wstring& wscParam,
	const wchar_t* usage)
{
	// Check that the user has a valid bay mounted, if so, get the struct associated with it
	BayArch bayArch;
	bool foundBay = false;
	for (auto& item : Players[iClientID].equipDescList.equip)
	{
		if (item.bMounted)
		{
			if (availableDroneBays.find(item.iArchID) != availableDroneBays.end())
			{
				foundBay = true;
				bayArch = availableDroneBays[item.iArchID];
				break;
			}
		}
	}

	if (!foundBay)
	{
		PrintUserCmdText(iClientID, L"δ�ҵ����˻����豸");
		return true;
	}

	clientDroneInfo[iClientID].droneBay = bayArch;

	// Print out each available drone type for this bay
	PrintUserCmdText(iClientID, L"--- ���Է�������˻� ---");
	for (const auto& bay : bayArch.availableDrones)
	{
		PrintUserCmdText(iClientID, stows(bay));
	}
	PrintUserCmdText(iClientID, L"------------------------");

	return true;
}



bool UserCommands::UserCmd_Debug(uint iClientID, const wstring& wscCmd, const wstring& wscParam, const wchar_t* usage)
{
	// For debugging, list the contents of the users dronemap
	ClientDroneInfo info = clientDroneInfo[iClientID];
	PrintUserCmdText(iClientID, L"��ǰ�����˻�IDΪ %u", info.deployedInfo.deployedDroneObj);
	PrintUserCmdText(iClientID, L"��ǰ״̬ %u", info.buildState);
	return true;
}

bool UserCommands::UserCmd_DroneHelp(uint iClientID, const wstring& wscCmd, const wstring& wscParam,
	const wchar_t* usage)
{
	PrintUserCmdText(iClientID, L"���˻�ʹ�÷���");
	PrintUserCmdText(iClientID, L"/dronetypes - �г����Ļ���֧�ֵ����˻�����");
	PrintUserCmdText(iClientID, L"/dronedeploy [���˻�����] - ����ָ�����͵����˻�");
	PrintUserCmdText(iClientID, L"/dronetarget - �������˻�����ѡ�е�Ŀ��");
	PrintUserCmdText(iClientID, L"/dronestop - �������˻�ֹͣ����");
	PrintUserCmdText(iClientID, L"/dronerecall - �������˻�������ͣ��");
	PrintUserCmdText(iClientID, L"/dronecome - �������˻�����ս��������������λ��");

	return true;
}

