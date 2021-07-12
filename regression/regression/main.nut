class Regression extends AIController {
	function Start();
};



function Regression::TestInit()
{
	print("");
	print("--TestInit--");
	print(" Ops:      " + this.GetOpsTillSuspend());
	print(" TickTest: " + this.GetTick());
	this.Sleep(1);
	print(" TickTest: " + this.GetTick());
	print(" Ops:      " + this.GetOpsTillSuspend());
	print(" SetCommandDelay: " + AIController.SetCommandDelay(1));
	print(" IsValid(vehicle.plane_speed): " + AIGameSettings.IsValid("vehicle.plane_speed"));
	print(" vehicle.plane_speed: " + AIGameSettings.GetValue("vehicle.plane_speed"));
	require("require.nut");
	print(" min(6, 3): " + min(6, 3));
	print(" min(3, 6): " + min(3, 6));
	print(" max(6, 3): " + max(6, 3));
	print(" max(3, 6): " + max(3, 6));

	print(" AIList Consistency Tests");
	print("");
	print(" Value Descending");
	local list = AIList();
	list.AddItem( 5, 10);
	list.AddItem(10, 10);
	list.AddItem(15, 20);
	list.AddItem(20, 20);
	list.AddItem(25, 30);
	list.AddItem(30, 30);
	list.AddItem(35, 40);
	list.AddItem(40, 40);

	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		list.RemoveItem(i - 10);
		list.RemoveItem(i - 5);
		list.RemoveItem(i);
		print("   " + i);
	}

	list.AddItem(10, 10);
	list.AddItem(20, 20);
	list.AddItem(30, 30);
	list.AddItem(40, 40);

	print("");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		list.SetValue(i, 2);
		print("   " + i);
	}
	print("");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("   " + i);
	}

	list = AIList();
	list.Sort(AIList.SORT_BY_VALUE, AIList.SORT_ASCENDING);
	print("");
	print(" Value Ascending");
	list.AddItem( 5, 10);
	list.AddItem(10, 10);
	list.AddItem(15, 20);
	list.AddItem(20, 20);
	list.AddItem(25, 30);
	list.AddItem(30, 30);
	list.AddItem(35, 40);
	list.AddItem(40, 40);

	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		list.RemoveItem(i + 10);
		list.RemoveItem(i + 5);
		list.RemoveItem(i);
		print("   " + i);
	}

	list.AddItem(10, 10);
	list.AddItem(20, 20);
	list.AddItem(30, 30);
	list.AddItem(40, 40);

	print("");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		list.SetValue(i, 50);
		print("   " + i);
	}
	print("");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("   " + i);
	}

	list = AIList();
	list.Sort(AIList.SORT_BY_ITEM, AIList.SORT_DESCENDING);
	print("");
	print(" Item Descending");
	list.AddItem( 5, 10);
	list.AddItem(10, 10);
	list.AddItem(15, 20);
	list.AddItem(20, 20);
	list.AddItem(25, 30);
	list.AddItem(30, 30);
	list.AddItem(35, 40);
	list.AddItem(40, 40);

	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		list.RemoveItem(i - 10);
		list.RemoveItem(i - 5);
		list.RemoveItem(i);
		print("   " + i);
	}

	list.AddItem(10, 10);
	list.AddItem(20, 20);
	list.AddItem(30, 30);
	list.AddItem(40, 40);

	print("");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		list.SetValue(i, 2);
		print("   " + i);
	}
	print("");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("   " + i);
	}

	list = AIList();
	list.Sort(AIList.SORT_BY_ITEM, AIList.SORT_ASCENDING);
	print("");
	print(" Item Ascending");
	list.AddItem( 5, 10);
	list.AddItem(10, 10);
	list.AddItem(15, 20);
	list.AddItem(20, 20);
	list.AddItem(25, 30);
	list.AddItem(30, 30);
	list.AddItem(35, 40);
	list.AddItem(40, 40);

	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		list.RemoveItem(i + 10);
		list.RemoveItem(i + 5);
		list.RemoveItem(i);
		print("   " + i);
	}

	list.AddItem(10, 10);
	list.AddItem(20, 20);
	list.AddItem(30, 30);
	list.AddItem(40, 40);

	print("");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		list.SetValue(i, 50);
		print("   " + i);
	}
	print("");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("   " + i);
	}

	list.Clear();
	foreach (idx, val in list) {
		print("   " + idx);
	}

	print(" Ops:      " + this.GetOpsTillSuspend());
}

function Regression::Std()
{
	print("");
	print("--Std--");
	print(" abs(-21): " + abs(-21));
	print(" abs( 21): " + abs(21));
}

function Regression::Base()
{
	print("");
	print("--AIBase--");
	print("  Rand():       " + AIBase.Rand());
	print("  Rand():       " + AIBase.Rand());
	print("  Rand():       " + AIBase.Rand());
	print("  RandRange(0): " + AIBase.RandRange(0));
	print("  RandRange(0): " + AIBase.RandRange(0));
	print("  RandRange(0): " + AIBase.RandRange(0));
	print("  RandRange(1): " + AIBase.RandRange(1));
	print("  RandRange(1): " + AIBase.RandRange(1));
	print("  RandRange(1): " + AIBase.RandRange(1));
	print("  RandRange(2): " + AIBase.RandRange(2));
	print("  RandRange(2): " + AIBase.RandRange(2));
	print("  RandRange(2): " + AIBase.RandRange(2));
	print("  RandRange(1000000): " + AIBase.RandRange(1000000)); // 32 bit tests
	print("  RandRange(1000000): " + AIBase.RandRange(1000000));
	print("  RandRange(1000000): " + AIBase.RandRange(1000000));
	print("  Chance(1, 2): " + AIBase.Chance(1, 2));
	print("  Chance(1, 2): " + AIBase.Chance(1, 2));
	print("  Chance(1, 2): " + AIBase.Chance(1, 2));

	AIRoad.SetCurrentRoadType(AIRoad.ROADTYPE_ROAD);
}

function Regression::Airport()
{
	print("");
	print("--AIAirport--");

	print("  IsHangarTile():       " + AIAirport.IsHangarTile(32116));
	print("  IsAirportTile():      " + AIAirport.IsAirportTile(32116));
	print("  GetHangarOfAirport(): " + AIAirport.GetHangarOfAirport(32116));
	print("  GetAirportType():     " + AIAirport.GetAirportType(32116));

	for (local i = -1; i < 10; i++) {
		print("  IsAirportInformationAvailable(" + i + "): " + AIAirport.IsAirportInformationAvailable(i));
		print("  IsValidAirportType(" + i + "):            " + AIAirport.IsValidAirportType(i));
		print("  GetAirportWidth(" + i + "):               " + AIAirport.GetAirportWidth(i));
		print("  GetAirportHeight(" + i + "):              " + AIAirport.GetAirportHeight(i));
		print("  GetAirportCoverageRadius(" + i + "):      " + AIAirport.GetAirportCoverageRadius(i));
	}

	print("  GetBankBalance():     " + AICompany.GetBankBalance(AICompany.COMPANY_SELF));
	print("  GetPrice():           " + AIAirport.GetPrice(0));
	print("  BuildAirport():       " + AIAirport.BuildAirport(32116, 0, AIStation.STATION_JOIN_ADJACENT));
	print("  IsHangarTile():       " + AIAirport.IsHangarTile(32116));
	print("  IsAirportTile():      " + AIAirport.IsAirportTile(32116));
	print("  GetAirportType():     " + AIAirport.GetAirportType(32119));
	print("  GetHangarOfAirport(): " + AIAirport.GetHangarOfAirport(32116));
	print("  IsHangarTile():       " + AIAirport.IsHangarTile(32119));
	print("  IsAirportTile():      " + AIAirport.IsAirportTile(32119));
	print("  GetAirportType():     " + AIAirport.GetAirportType(32119));
	print("  GetBankBalance():     " + AICompany.GetBankBalance(AICompany.COMPANY_SELF));

	print("  RemoveAirport():      " + AIAirport.RemoveAirport(32118));
	print("  IsHangarTile():       " + AIAirport.IsHangarTile(32119));
	print("  IsAirportTile():      " + AIAirport.IsAirportTile(32119));
	print("  GetBankBalance():     " + AICompany.GetBankBalance(AICompany.COMPANY_SELF));
	print("  BuildAirport():       " + AIAirport.BuildAirport(32116, 0, AIStation.STATION_JOIN_ADJACENT));
}

function Regression::Bridge()
{
	local j = 0;

	print("");
	print("--Bridge--");
	for (local i = -1; i < 14; i++) {
		if (AIBridge.IsValidBridge(i)) j++;
		print("  Bridge " + i);
		print("    IsValidBridge():    " + AIBridge.IsValidBridge(i));
		print("    GetName():");
		print("     VT_RAIL:           " + AIBridge.GetName(i, AIVehicle.VT_RAIL));
		print("     VT_ROAD:           " + AIBridge.GetName(i, AIVehicle.VT_ROAD));
		print("     VT_WATER:          " + AIBridge.GetName(i, AIVehicle.VT_WATER));
		print("     VT_AIR:            " + AIBridge.GetName(i, AIVehicle.VT_AIR));
		print("    GetMaxSpeed():      " + AIBridge.GetMaxSpeed(i));
		print("    GetPrice():         " + AIBridge.GetPrice(i, 5));
		print("    GetMaxLength():     " + AIBridge.GetMaxLength(i));
		print("    GetMinLength():     " + AIBridge.GetMinLength(i));
	}
	print("  Valid Bridges:        " + j);

	print("  IsBridgeTile():       " + AIBridge.IsBridgeTile(33160));
	print("  GetBridgeID():        " + AIBridge.GetBridgeID(33160));
	print("  RemoveBridge():       " + AIBridge.RemoveBridge(33155));
	print("  GetLastErrorString(): " + AIError.GetLastErrorString());
	print("  GetOtherBridgeEnd():  " + AIBridge.GetOtherBridgeEnd(33160));
	print("  BuildBridge():        " + AIBridge.BuildBridge(AIVehicle.VT_ROAD, 5, 33160, 33155));
	print("  IsBridgeTile():       " + AIBridge.IsBridgeTile(33160));
	print("  GetBridgeID():        " + AIBridge.GetBridgeID(33160));
	print("  IsBridgeTile():       " + AIBridge.IsBridgeTile(33155));
	print("  GetBridgeID():        " + AIBridge.GetBridgeID(33155));
	print("  GetOtherBridgeEnd():  " + AIBridge.GetOtherBridgeEnd(33160));
	print("  BuildBridge():        " + AIBridge.BuildBridge(AIVehicle.VT_ROAD, 5, 33160, 33155));
	print("  GetLastErrorString(): " + AIError.GetLastErrorString());
	print("  RemoveBridge():       " + AIBridge.RemoveBridge(33155));
	print("  IsBridgeTile():       " + AIBridge.IsBridgeTile(33160));
}

function Regression::BridgeList()
{
	local list = AIBridgeList();

	print("");
	print("--BridgeList--");
	print("  Count():             " + list.Count());
	list.Valuate(AIBridge.GetMaxSpeed);
	print("  MaxSpeed ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIBridge.GetPrice, 5);
	print("  Price ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIBridge.GetMaxLength);
	print("  MaxLength ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIBridge.GetMinLength);
	print("  MinLength ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list = AIBridgeList_Length(14);

	print("");
	print("--BridgeList_Length--");
	print("  Count():             " + list.Count());
	list.Valuate(AIBridge.GetMaxSpeed);
	print("  MaxSpeed ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIBridge.GetPrice, 14);
	print("  Price ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
}

function Regression::Cargo()
{
	print("");
	print("--AICargo--");
	for (local i = -1; i < 15; i++) {
		print("  Cargo " + i);
		print("    IsValidCargo():          " + AICargo.IsValidCargo(i));
		print("    GetName():               '" + AICargo.GetName(i) + "'");
		print("    GetCargoLabel():         '" + AICargo.GetCargoLabel(i) + "'");
		print("    IsFreight():             " + AICargo.IsFreight(i));
		print("    HasCargoClass():         " + AICargo.HasCargoClass(i, AICargo.CC_PASSENGERS));
		print("    GetTownEffect():         " + AICargo.GetTownEffect(i));
		print("    GetCargoIncome(0, 0):    " + AICargo.GetCargoIncome(i, 0, 0));
		print("    GetCargoIncome(10, 10):  " + AICargo.GetCargoIncome(i, 10, 10));
		print("    GetCargoIncome(100, 10): " + AICargo.GetCargoIncome(i, 100, 10));
		print("    GetCargoIncome(10, 100): " + AICargo.GetCargoIncome(i, 10, 100));
		print("    GetRoadVehicleTypeForCargo(): " + AIRoad.GetRoadVehicleTypeForCargo(i));
	}
}

function Regression::CargoList()
{
	local list = AICargoList();

	print("");
	print("--CargoList--");
	print("  Count():            " + list.Count());
	list.Valuate(AICargo.IsFreight);
	print("  IsFreight ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list.Valuate(AICargo.GetCargoIncome, 100, 100);
	print("  CargoIncomes(100, 100) ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list = AICargoList_IndustryAccepting(8);
	print("");
	print("--CargoList_IndustryAccepting--");
	print("  Count():            " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i);
	}

	list = AICargoList_IndustryProducing(4);
	print("");
	print("--CargoList_IndustryProducing--");
	print("  Count():            " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i);
	}
}

function Regression::Company()
{
	print("");
	print("--Company--");

	/* Test AIXXXMode() in scopes */
	{
		local test = AITestMode();
		print("  SetName():            " + AICompany.SetName("Regression"));
		print("  SetName():            " + AICompany.SetName("Regression"));
		{
			local exec = AIExecMode();
			print("  SetName():            " + AICompany.SetName("Regression"));
			print("  SetName():            " + AICompany.SetName("Regression"));
			print("  GetLastErrorString(): " + AIError.GetLastErrorString());
		}
	}

	print("  GetName():                         " + AICompany.GetName(AICompany.COMPANY_SELF));
	print("  GetPresidentName():                " + AICompany.GetPresidentName(AICompany.COMPANY_SELF));
	print("  SetPresidentName():                " + AICompany.SetPresidentName("Regression AI"));
	print("  GetPresidentName():                " + AICompany.GetPresidentName(AICompany.COMPANY_SELF));
	print("  GetBankBalance():                  " + AICompany.GetBankBalance(AICompany.COMPANY_SELF));
	print("  GetName():                         " + AICompany.GetName(240));
	print("  GetLoanAmount():                   " + AICompany.GetLoanAmount());
	print("  GetMaxLoanAmount():                " + AICompany.GetMaxLoanAmount());
	print("  GetLoanInterval():                 " + AICompany.GetLoanInterval());
	print("  SetLoanAmount(1):                  " + AICompany.SetLoanAmount(1));
	print("  SetLoanAmount(100):                " + AICompany.SetLoanAmount(100));
	print("  SetLoanAmount(10000):              " + AICompany.SetLoanAmount(10000));
	print("  GetLastErrorString():              " + AIError.GetLastErrorString());
	print("  GetBankBalance():                  " + AICompany.GetBankBalance(AICompany.COMPANY_SELF));
	print("  GetLoanAmount():                   " + AICompany.GetLoanAmount());
	print("  SetMinimumLoanAmount(31337):       " + AICompany.SetMinimumLoanAmount(31337));
	print("  GetBankBalance():                  " + AICompany.GetBankBalance(AICompany.COMPANY_SELF));
	print("  GetLoanAmount():                   " + AICompany.GetLoanAmount());
	print("  SetLoanAmount(10000):              " + AICompany.SetLoanAmount(AICompany.GetMaxLoanAmount()));
	print("  GetBankBalance():                  " + AICompany.GetBankBalance(AICompany.COMPANY_SELF));
	print("  GetLoanAmount():                   " + AICompany.GetLoanAmount());
	print("  GetCompanyHQ():                    " + AICompany.GetCompanyHQ(AICompany.COMPANY_SELF));
	print("  BuildCompanyHQ():                  " + AICompany.BuildCompanyHQ(AIMap.GetTileIndex(127, 129)));
	print("  GetCompanyHQ():                    " + AICompany.GetCompanyHQ(AICompany.COMPANY_SELF));
	print("  BuildCompanyHQ():                  " + AICompany.BuildCompanyHQ(AIMap.GetTileIndex(129, 129)));
	print("  GetCompanyHQ():                    " + AICompany.GetCompanyHQ(AICompany.COMPANY_SELF));
	print("  BuildCompanyHQ():                  " + AICompany.BuildCompanyHQ(AIMap.GetTileIndex(239, 76)));
	print("  GetLastErrorString():              " + AIError.GetLastErrorString());
	print("  GetAutoRenewStatus():              " + AICompany.GetAutoRenewStatus(AICompany.COMPANY_SELF));
	print("  SetAutoRenewStatus(true):          " + AICompany.SetAutoRenewStatus(true));
	print("  GetAutoRenewStatus():              " + AICompany.GetAutoRenewStatus(AICompany.COMPANY_SELF));
	print("  SetAutoRenewStatus(true):          " + AICompany.SetAutoRenewStatus(true));
	print("  SetAutoRenewStatus(false):         " + AICompany.SetAutoRenewStatus(false));
	print("  GetAutoRenewStatus():              " + AICompany.GetAutoRenewStatus(AICompany.COMPANY_SELF));
	print("  GetAutoRenewMonths():              " + AICompany.GetAutoRenewMonths(AICompany.COMPANY_SELF));
	print("  SetAutoRenewMonths(-12):           " + AICompany.SetAutoRenewMonths(-12));
	print("  GetAutoRenewMonths():              " + AICompany.GetAutoRenewMonths(AICompany.COMPANY_SELF));
	print("  SetAutoRenewMonths(-12):           " + AICompany.SetAutoRenewMonths(-12));
	print("  SetAutoRenewMonths(6):             " + AICompany.SetAutoRenewMonths(6));
	print("  GetAutoRenewMoney():               " + AICompany.GetAutoRenewMoney(AICompany.COMPANY_SELF));
	print("  SetAutoRenewMoney(200000):         " + AICompany.SetAutoRenewMoney(200000));
	print("  GetAutoRenewMoney():               " + AICompany.GetAutoRenewMoney(AICompany.COMPANY_SELF));
	print("  SetAutoRenewMoney(200000):         " + AICompany.SetAutoRenewMoney(200000));
	print("  SetAutoRenewMoney(100000):         " + AICompany.SetAutoRenewMoney(100000));
	for (local i = -1; i <= AICompany.EARLIEST_QUARTER; i++) {
		print("  Quarter: " + i);
		print("    GetQuarterlyIncome():            " + AICompany.GetQuarterlyIncome(AICompany.COMPANY_SELF, i));
		print("    GetQuarterlyExpenses():          " + AICompany.GetQuarterlyExpenses(AICompany.COMPANY_SELF, i));
		print("    GetQuarterlyCargoDelivered():    " + AICompany.GetQuarterlyCargoDelivered(AICompany.COMPANY_SELF, i));
		print("    GetQuarterlyPerformanceRating(): " + AICompany.GetQuarterlyPerformanceRating(AICompany.COMPANY_SELF, i));
		print("    GetQuarterlyCompanyValue():      " + AICompany.GetQuarterlyCompanyValue(AICompany.COMPANY_SELF, i));
	}
}

function Regression::Engine()
{
	local j = 0;

	print("");
	print("--Engine--");
	for (local i = -1; i < 257; i++) {
		if (AIEngine.IsValidEngine(i)) j++;
		print("  Engine " + i);
		print("    IsValidEngine():    " + AIEngine.IsValidEngine(i));
		print("    GetName():          " + AIEngine.GetName(i));
		print("    GetCargoType():     " + AIEngine.GetCargoType(i));
		print("    CanRefitCargo():    " + AIEngine.CanRefitCargo(i, 1));
		print("    GetCapacity():      " + AIEngine.GetCapacity(i));
		print("    GetReliability():   " + AIEngine.GetReliability(i));
		print("    GetMaxSpeed():      " + AIEngine.GetMaxSpeed(i));
		print("    GetPrice():         " + AIEngine.GetPrice(i));
		print("    GetMaxAge():        " + AIEngine.GetMaxAge(i));
		print("    GetRunningCost():   " + AIEngine.GetRunningCost(i));
		print("    GetPower():         " + AIEngine.GetPower(i));
		print("    GetWeight():        " + AIEngine.GetWeight(i));
		print("    GetMaxTractiveEffort(): " + AIEngine.GetMaxTractiveEffort(i));
		print("    GetVehicleType():   " + AIEngine.GetVehicleType(i));
		print("    GetRailType():      " + AIEngine.GetRailType(i));
		print("    GetRoadType():      " + AIEngine.GetRoadType(i));
		print("    GetPlaneType():     " + AIEngine.GetPlaneType(i));
	}
	print("  Valid Engines:        " + j);
}

function Regression::EngineList()
{
	local list = AIEngineList(AIVehicle.VT_ROAD);

	print("");
	print("--EngineList--");
	print("  Count():             " + list.Count());
	list.Valuate(AIEngine.GetCargoType);
	print("  CargoType ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIEngine.GetCapacity);
	print("  Capacity ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIEngine.GetReliability);
	print("  Reliability ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIEngine.GetMaxSpeed);
	print("  MaxSpeed ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIEngine.GetPrice);
	print("  Price ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
}

function Regression::Prices()
{
	print("");
	print("--Prices--");
	print(" -Rail-");
	print("  0,BT_TRACK:    " + AIRail.GetBuildCost(0, AIRail.BT_TRACK));
	print("  0,BT_SIGNAL:   " + AIRail.GetBuildCost(0, AIRail.BT_SIGNAL));
	print("  0,BT_DEPOT:    " + AIRail.GetBuildCost(0, AIRail.BT_DEPOT));
	print("  0,BT_STATION:  " + AIRail.GetBuildCost(0, AIRail.BT_STATION));
	print("  0,BT_WAYPOINT: " + AIRail.GetBuildCost(0, AIRail.BT_WAYPOINT));
	print("  1,BT_TRACK:    " + AIRail.GetBuildCost(1, AIRail.BT_TRACK));
	print("  1,BT_SIGNAL:   " + AIRail.GetBuildCost(1, AIRail.BT_SIGNAL));
	print("  1,BT_DEPOT:    " + AIRail.GetBuildCost(1, AIRail.BT_DEPOT));
	print("  1,BT_STATION:  " + AIRail.GetBuildCost(1, AIRail.BT_STATION));
	print("  1,BT_WAYPOINT: " + AIRail.GetBuildCost(1, AIRail.BT_WAYPOINT));
	print(" -Road-");
	print("  ROADTYPE_ROAD,BT_ROAD:       " + AIRoad.GetBuildCost(AIRoad.ROADTYPE_ROAD, AIRoad.BT_ROAD));
	print("  ROADTYPE_ROAD,BT_DEPOT:      " + AIRoad.GetBuildCost(AIRoad.ROADTYPE_ROAD, AIRoad.BT_DEPOT));
	print("  ROADTYPE_ROAD,BT_BUS_STOP:   " + AIRoad.GetBuildCost(AIRoad.ROADTYPE_ROAD, AIRoad.BT_BUS_STOP));
	print("  ROADTYPE_ROAD,BT_TRUCK_STOP: " + AIRoad.GetBuildCost(AIRoad.ROADTYPE_ROAD, AIRoad.BT_TRUCK_STOP));
	print("  ROADTYPE_TRAM,BT_ROAD:       " + AIRoad.GetBuildCost(AIRoad.ROADTYPE_TRAM, AIRoad.BT_ROAD));
	print("  ROADTYPE_TRAM,BT_DEPOT:      " + AIRoad.GetBuildCost(AIRoad.ROADTYPE_TRAM, AIRoad.BT_DEPOT));
	print("  ROADTYPE_TRAM,BT_BUS_STOP:   " + AIRoad.GetBuildCost(AIRoad.ROADTYPE_TRAM, AIRoad.BT_BUS_STOP));
	print("  ROADTYPE_TRAM,BT_TRUCK_STOP: " + AIRoad.GetBuildCost(AIRoad.ROADTYPE_TRAM, AIRoad.BT_TRUCK_STOP));
	print(" -Water-");
	print("  BT_DOCK:  " + AIMarine.GetBuildCost(AIMarine.BT_DOCK));
	print("  BT_DEPOT: " + AIMarine.GetBuildCost(AIMarine.BT_DEPOT));
	print("  BT_BUOY:  " + AIMarine.GetBuildCost(AIMarine.BT_BUOY));
	print("  BT_LOCK:  " + AIMarine.GetBuildCost(AIMarine.BT_LOCK));
	print("  BT_CANAL: " + AIMarine.GetBuildCost(AIMarine.BT_CANAL));
	print(" -Tile-");
	print("  BT_FOUNDATION:   " + AITile.GetBuildCost(AITile.BT_FOUNDATION));
	print("  BT_TERRAFORM:    " + AITile.GetBuildCost(AITile.BT_TERRAFORM));
	print("  BT_BUILD_TREES:  " + AITile.GetBuildCost(AITile.BT_BUILD_TREES));
	print("  BT_CLEAR_GRASS:  " + AITile.GetBuildCost(AITile.BT_CLEAR_GRASS));
	print("  BT_CLEAR_ROUGH:  " + AITile.GetBuildCost(AITile.BT_CLEAR_ROUGH));
	print("  BT_CLEAR_ROCKY:  " + AITile.GetBuildCost(AITile.BT_CLEAR_ROCKY));
	print("  BT_CLEAR_FIELDS: " + AITile.GetBuildCost(AITile.BT_CLEAR_FIELDS));
	print("  BT_CLEAR_HOUSE:  " + AITile.GetBuildCost(AITile.BT_CLEAR_HOUSE));
	print("  BT_CLEAR_WATER:  " + AITile.GetBuildCost(AITile.BT_CLEAR_WATER));
}

function cost_callback(old_path, new_tile, new_direction, self) { if (old_path == null) return 0; return old_path.GetCost() + 1; }
function estimate_callback(tile, direction, goals, self) { return goals[0] - tile; }
function neighbours_callback(path, cur_tile, self) { return [[cur_tile + 1, 1]]; }
function check_direction_callback(tile, existing_direction, new_direction, self) { return false; }

function Regression::Group()
{
	print ("");
	print("--Group--");
	print("  SetAutoReplace():         " + AIGroup.SetAutoReplace(AIGroup.GROUP_ALL, 116, 117));
	print("  GetEngineReplacement():   " + AIGroup.GetEngineReplacement(AIGroup.GROUP_ALL, 116));
	print("  GetNumEngines():          " + AIGroup.GetNumEngines(AIGroup.GROUP_ALL, 116));
	print("  AIRoad.BuildRoadDepot():  " + AIRoad.BuildRoadDepot(10000, 10001));
	local vehicle = AIVehicle.BuildVehicle(10000, 116);
	print("  AIVehicle.BuildVehicle(): " + vehicle);
	print("  GetNumEngines():          " + AIGroup.GetNumEngines(AIGroup.GROUP_ALL, 116));
	local group = AIGroup.CreateGroup(AIVehicle.VT_ROAD, AIGroup.GROUP_INVALID);
	print("  CreateGroup():            " + group);
	print("  MoveVehicle():            " + AIGroup.MoveVehicle(group, vehicle));
	print("  GetNumEngines():          " + AIGroup.GetNumEngines(group, 116));
	print("  GetNumEngines():          " + AIGroup.GetNumEngines(AIGroup.GROUP_ALL, 116));
	print("  GetNumEngines():          " + AIGroup.GetNumEngines(AIGroup.GROUP_DEFAULT, 116));
	print("  GetName():                " + AIGroup.GetName(0));
	print("  GetName():                " + AIGroup.GetName(1));
	print("  AIVehicle.SellVehicle():  " + AIVehicle.SellVehicle(vehicle));
	print("  AITile.DemolishTile():    " + AITile.DemolishTile(10000));
	print("  HasWagonRemoval():        " + AIGroup.HasWagonRemoval());
	print("  EnableWagonRemoval():     " + AIGroup.EnableWagonRemoval(true));
	print("  HasWagonRemoval():        " + AIGroup.HasWagonRemoval());
	print("  EnableWagonRemoval():     " + AIGroup.EnableWagonRemoval(false));
	print("  EnableWagonRemoval():     " + AIGroup.EnableWagonRemoval(false));
	print("  HasWagonRemoval():        " + AIGroup.HasWagonRemoval());
}

function Regression::Industry()
{
	local j = 0;

	print("");
	print("--Industry--");
	print("  GetIndustryCount():  " + AIIndustry.GetIndustryCount());
	local list = AIIndustryList();
	list.Sort(AIList.SORT_BY_ITEM, AIList.SORT_ASCENDING);
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		if (AIIndustry.IsValidIndustry(i)) j++;
		print("  Industry " + i);
		print("    IsValidIndustry(): " + AIIndustry.IsValidIndustry(i));
		print("    GetName():         " + AIIndustry.GetName(i));
		print("    GetLocation():     " + AIIndustry.GetLocation(i));
		print("    IsCargoAccepted(): " + AIIndustry.IsCargoAccepted(i, 1));

		local cargo_list = AICargoList();
		for (local j = cargo_list.Begin(); !cargo_list.IsEnd(); j = cargo_list.Next()) {
			if (AIIndustry.IsCargoAccepted(i, j) || AIIndustry.GetLastMonthProduction(i,j) >= 0) {
				print("	   GetLastMonthProduction():  " + AIIndustry.GetLastMonthProduction(i, j));
				print("	   GetLastMonthTransported(): " + AIIndustry.GetLastMonthTransported(i, j));
				print("	   GetStockpiledCargo():      " + AIIndustry.GetStockpiledCargo(i, j));
			}
		}
	}
	print("  Valid Industries:    " + j);
	print("  GetIndustryCount():  " + AIIndustry.GetIndustryCount());
	print("  GetIndustryID():     " + AIIndustry.GetIndustryID(19694));
	print("  GetIndustryID():     " + AIIndustry.GetIndustryID(19695));
}

function Regression::IndustryList()
{
	local list = AIIndustryList();

	print("");
	print("--IndustryList--");
	print("  Count():             " + list.Count());
	list.Valuate(AIIndustry.GetLocation);
	print("  Location ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIIndustry.GetDistanceManhattanToTile, 30000);
	print("  DistanceManhattanToTile(30000) ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIIndustry.GetDistanceSquareToTile, 30000);
	print("  DistanceSquareToTile(30000) ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIIndustry.GetAmountOfStationsAround);
	print("  GetAmountOfStationsAround(30000) ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIIndustry.IsCargoAccepted, 1);
	print("  CargoAccepted(1) ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list = AIIndustryList_CargoAccepting(1);
	print("--IndustryList_CargoAccepting--");
	print("  Count():             " + list.Count());
	list.Valuate(AIIndustry.GetLocation);
	print("  Location ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list = AIIndustryList_CargoProducing(1);
	print("--IndustryList_CargoProducing--");
	print("  Count():             " + list.Count());
	list.Valuate(AIIndustry.GetLocation);
	print("  Location ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
}

function Regression::IndustryTypeList()
{
	local list = AIIndustryTypeList();

	print("");
	print("--IndustryTypeList--");
	print("  Count():             " + list.Count());
	list.Valuate(AIIndustry.GetLocation);
	print("  Location ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    Id:                      " + i);
		print("    IsRawIndustry():         " + AIIndustryType.IsRawIndustry(i));
		print("    ProductionCanIncrease(): " + AIIndustryType.ProductionCanIncrease(i));
		print("    GetConstructionCost():   " + AIIndustryType.GetConstructionCost(i));
		print("    GetName():               " + AIIndustryType.GetName(i));
		print("    CanBuildIndustry():      " + AIIndustryType.CanBuildIndustry(i));
		print("    CanProspectIndustry():   " + AIIndustryType.CanProspectIndustry(i));
		print("    IsBuiltOnWater():        " + AIIndustryType.IsBuiltOnWater(i));
		print("    HasHeliport():           " + AIIndustryType.HasHeliport(i));
		print("    HasDock():               " + AIIndustryType.HasDock(i));
	}
}

function CustomValuator(list_id)
{
	return list_id * 4343;
}

function Regression::List()
{
	local list = AIList();

	print("");
	print("--List--");

	print("  IsEmpty():     " + list.IsEmpty());
	list.AddItem(1, 1);
	list.AddItem(2, 2);
	for (local i = 1000; i < 1100; i++) {
		list.AddItem(i, i);
	}
	list.RemoveItem(1050);
	list.RemoveItem(1150);
	list.SetValue(1051, 12);
	print("  Count():       " + list.Count());
	print("  HasItem(1050): " + list.HasItem(1050));
	print("  HasItem(1051): " + list.HasItem(1051));
	print("  IsEmpty():     " + list.IsEmpty());
	list.Sort(AIList.SORT_BY_ITEM, AIList.SORT_ASCENDING);
	print("  List Dump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(CustomValuator);
	print("  Custom ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(function (a) { return a * 42; });
	print("  Custom ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIBase.RandItem);
	print("  Randomize ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list.KeepTop(10);
	print("  KeepTop(10):");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.KeepBottom(8);
	print("  KeepBottom(8):");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.RemoveBottom(2);
	print("  RemoveBottom(2):");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.RemoveTop(2);
	print("  RemoveTop(2):");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	local list2 = AIList();
	list2.AddItem(1003, 0);
	list2.AddItem(1004, 0);
	list.RemoveList(list2);
	print("  RemoveList({1003, 1004}):");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list2.AddItem(1005, 0);
	list.KeepList(list2);
	print("  KeepList({1003, 1004, 1005}):");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list2.Clear();
	for (local i = 4000; i < 4003; i++) {
		list2.AddItem(i, i * 2);
	}
	list2.AddItem(1005, 1005);
	list.AddList(list2);
	print("  AddList({1005, 4000, 4001, 4002}):");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list[4000] = 50;
	list[4006] = 12;

	print("  foreach():");
	foreach (idx, val in list) {
		print("    " + idx + " => " + val);
	}
	print("  []:");
	print("    4000 => " + list[4000]);

	list.Clear();
	print("  IsEmpty():     " + list.IsEmpty());

	for (local i = 0; i < 10; i++) {
		list.AddItem(i, 5 + i / 2);
	}

	local it = list.Begin();
	print("    " + it + " => " + list.GetValue(it) + "  (" + !list.IsEnd() + ")");
	list.Sort(list.SORT_BY_VALUE, list.SORT_ASCENDING);
	it = list.Next();
	print("    " + it + " => " + list.GetValue(it) + "  (" + !list.IsEnd() + ")");

	it = list.Begin();
	print("    " + it + " => " + list.GetValue(it) + "  (" + !list.IsEnd() + ")");

	list.SetValue(it + 1, -5);
	it = list.Next();
	print("    " + it + " => " + list.GetValue(it) + "  (" + !list.IsEnd() + ")");

	list.RemoveValue(list.GetValue(it) + 1);
	it = list.Next();
	print("    " + it + " => " + list.GetValue(it) + "  (" + !list.IsEnd() + ")");

	list.RemoveAboveValue(list.GetValue(it));
	it = list.Next();
	print("    " + it + " => " + list.GetValue(it) + "  (" + !list.IsEnd() + ")");

	while (!list.IsEnd()) {
		it = list.Next();
		print("    " + it + " => " + list.GetValue(it));
	}
}

function Regression::Map()
{
	print("");
	print("--Map--");
	print("  GetMapSize():     " + AIMap.GetMapSize());
	print("  GetMapSizeX():    " + AIMap.GetMapSizeX());
	print("  GetMapSizeY():    " + AIMap.GetMapSizeY());
	print("  GetTileX(123):    " + AIMap.GetTileX(123));
	print("  GetTileY(123):    " + AIMap.GetTileY(123));
	print("  GetTileIndex():   " + AIMap.GetTileIndex(123, 0));
	print("  GetTileIndex():   " + AIMap.GetTileIndex(0, 123));
	print("  GetTileIndex():   " + AIMap.GetTileIndex(0, 0));
	print("  GetTileIndex():   " + AIMap.GetTileIndex(-1, -1));
	print("  GetTileIndex():   " + AIMap.GetTileIndex(10000, 10000));
	print("  IsValidTile(123): " + AIMap.IsValidTile(123));
	print("  GetTileX(124):    " + AIMap.GetTileX(124));
	print("  GetTileY(124):    " + AIMap.GetTileY(124));
	print("  IsValidTile(124): " + AIMap.IsValidTile(124));
	print("  IsValidTile(0):   " + AIMap.IsValidTile(0));
	print("  IsValidTile(-1):  " + AIMap.IsValidTile(-1));
	print("  IsValidTile():    " + AIMap.IsValidTile(AIMap.GetMapSize()));
	print("  IsValidTile():    " + AIMap.IsValidTile(AIMap.GetMapSize() - AIMap.GetMapSizeX() - 2));
	print("  DemolishTile():   " + AITile.DemolishTile(19592));
	print("  DemolishTile():   " + AITile.DemolishTile(19335));
	print("  Distance");
	print("    DistanceManhattan(): " + AIMap.DistanceManhattan(1, 10000));
	print("    DistanceMax():       " + AIMap.DistanceMax(1, 10000));
	print("    DistanceSquare():    " + AIMap.DistanceSquare(1, 10000));
	print("    DistanceFromEdge():  " + AIMap.DistanceFromEdge(10000));
}

function Regression::Marine()
{
	print("");
	print("--AIMarine--");

	print("  IsWaterDepotTile():   " + AIMarine.IsWaterDepotTile(32116));
	print("  IsDockTile():         " + AIMarine.IsDockTile(32116));
	print("  IsBuoyTile():         " + AIMarine.IsBuoyTile(32116));
	print("  IsLockTile():         " + AIMarine.IsLockTile(32116));
	print("  IsCanalTile():        " + AIMarine.IsCanalTile(32116));

	print("  GetBankBalance():     " + AICompany.GetBankBalance(AICompany.COMPANY_SELF));
	print("  BuildWaterDepot():    " + AIMarine.BuildWaterDepot(28479, 28478));
	print("  BuildDock():          " + AIMarine.BuildDock(29253, AIStation.STATION_JOIN_ADJACENT));
	print("  BuildBuoy():          " + AIMarine.BuildBuoy(28481));
	print("  BuildLock():          " + AIMarine.BuildLock(28487));
	print("  HasTransportType():   " + AITile.HasTransportType(32127, AITile.TRANSPORT_WATER));
	print("  BuildCanal():         " + AIMarine.BuildCanal(32127));
	print("  HasTransportType():   " + AITile.HasTransportType(32127, AITile.TRANSPORT_WATER));
	print("  IsWaterDepotTile():   " + AIMarine.IsWaterDepotTile(28479));
	print("  IsDockTile():         " + AIMarine.IsDockTile(29253));
	print("  IsBuoyTile():         " + AIMarine.IsBuoyTile(28481));
	print("  IsLockTile():         " + AIMarine.IsLockTile(28487));
	print("  IsCanalTile():        " + AIMarine.IsCanalTile(32127));
	print("  GetBankBalance():     " + AICompany.GetBankBalance(AICompany.COMPANY_SELF));

	local list = AIWaypointList(AIWaypoint.WAYPOINT_BUOY);
	print("");
	print("--AIWaypointList(BUOY)--");
	print("  Count():             " + list.Count());
	print("  Location ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + AIWaypoint.GetLocation(i));
	}
	print("  HasWaypointType:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + AIWaypoint.HasWaypointType(i, AIWaypoint.WAYPOINT_RAIL) + "  " + AIWaypoint.HasWaypointType(i, AIWaypoint.WAYPOINT_BUOY) + "  " + AIWaypoint.HasWaypointType(i, AIWaypoint.WAYPOINT_ANY));
	}
	print("");

	print("  RemoveWaterDepot():   " + AIMarine.RemoveWaterDepot(28479));
	print("  RemoveDock():         " + AIMarine.RemoveDock(29253));
	print("  RemoveBuoy():         " + AIMarine.RemoveBuoy(28481));
	print("  RemoveLock():         " + AIMarine.RemoveLock(28487));
	print("  RemoveCanal():        " + AIMarine.RemoveCanal(32127));
	print("  IsWaterDepotTile():   " + AIMarine.IsWaterDepotTile(28479));
	print("  IsDockTile():         " + AIMarine.IsDockTile(29253));
	print("  IsBuoyTile():         " + AIMarine.IsBuoyTile(28481));
	print("  IsLockTile():         " + AIMarine.IsLockTile(28487));
	print("  IsCanalTile():        " + AIMarine.IsCanalTile(32127));
	print("  GetBankBalance():     " + AICompany.GetBankBalance(AICompany.COMPANY_SELF));

	print("  BuildWaterDepot():    " + AIMarine.BuildWaterDepot(28479, 28480));
	print("  BuildDock():          " + AIMarine.BuildDock(29253, AIStation.STATION_JOIN_ADJACENT));
	print("  BuildBuoy():          " + AIMarine.BuildBuoy(28481));
	print("  BuildLock():          " + AIMarine.BuildLock(28487));
	print("  BuildCanal():         " + AIMarine.BuildCanal(28744));
}

function Regression::Order()
{
	print("");
	print("--Order--");
	print("  GetOrderCount():       " + AIOrder.GetOrderCount(12));
	print("  GetOrderDestination(): " + AIOrder.GetOrderDestination(12, 1));
	print("  AreOrderFlagsValid():  " + AIOrder.AreOrderFlagsValid(33416, AIOrder.OF_TRANSFER));
	print("  AreOrderFlagsValid():  " + AIOrder.AreOrderFlagsValid(33416, AIOrder.OF_TRANSFER | AIOrder.OF_UNLOAD));
	print("  AreOrderFlagsValid():  " + AIOrder.AreOrderFlagsValid(33416, AIOrder.OF_TRANSFER | AIOrder.OF_FULL_LOAD));
	print("  AreOrderFlagsValid():  " + AIOrder.AreOrderFlagsValid(33417, AIOrder.OF_SERVICE_IF_NEEDED));
	print("  AreOrderFlagsValid():  " + AIOrder.AreOrderFlagsValid(33417, AIOrder.OF_STOP_IN_DEPOT));
	print("  AreOrderFlagsValid():  " + AIOrder.AreOrderFlagsValid(0, AIOrder.OF_SERVICE_IF_NEEDED | AIOrder.OF_GOTO_NEAREST_DEPOT));
	print("  IsValidConditionalOrder(): " + AIOrder.IsValidConditionalOrder(AIOrder.OC_LOAD_PERCENTAGE, AIOrder.CF_EQUALS));
	print("  IsValidConditionalOrder(): " + AIOrder.IsValidConditionalOrder(AIOrder.OC_RELIABILITY, AIOrder.CF_IS_TRUE));
	print("  IsValidConditionalOrder(): " + AIOrder.IsValidConditionalOrder(AIOrder.OC_REQUIRES_SERVICE, AIOrder.CF_IS_FALSE));
	print("  IsValidConditionalOrder(): " + AIOrder.IsValidConditionalOrder(AIOrder.OC_AGE, AIOrder.CF_INVALID));
	print("  IsValidVehicleOrder(): " + AIOrder.IsValidVehicleOrder(12, 1));
	print("  IsGotoStationOrder():  " + AIOrder.IsGotoStationOrder(12, 1));
	print("  IsGotoDepotOrder():    " + AIOrder.IsGotoDepotOrder(12, 1));
	print("  IsGotoWaypointOrder(): " + AIOrder.IsGotoWaypointOrder(12, 1));
	print("  IsConditionalOrder():  " + AIOrder.IsConditionalOrder(12, 1));
	print("  IsCurrentOrderPartOfOrderList(): " + AIOrder.IsCurrentOrderPartOfOrderList(12));
	print("  GetOrderFlags():       " + AIOrder.GetOrderFlags(12, 1));
	print("  AppendOrder():         " + AIOrder.AppendOrder(12, 33416, AIOrder.OF_TRANSFER));
	print("  InsertOrder():         " + AIOrder.InsertOrder(12, 0, 33416, AIOrder.OF_TRANSFER));
	print("  GetOrderCount():       " + AIOrder.GetOrderCount(12));
	print("  IsValidVehicleOrder(): " + AIOrder.IsValidVehicleOrder(12, 1));
	print("  IsGotoStationOrder():  " + AIOrder.IsGotoStationOrder(12, 1));
	print("  IsGotoDepotOrder():    " + AIOrder.IsGotoDepotOrder(12, 1));
	print("  IsGotoWaypointOrder(): " + AIOrder.IsGotoWaypointOrder(12, 1));
	print("  IsConditionalOrder():  " + AIOrder.IsConditionalOrder(12, 1));
	print("  IsCurrentOrderPartOfOrderList(): " + AIOrder.IsCurrentOrderPartOfOrderList(12));
	print("  GetOrderFlags():       " + AIOrder.GetOrderFlags(12, 0));
	print("  GetOrderFlags():       " + AIOrder.GetOrderFlags(12, 1));
	print("  GetOrderJumpTo():      " + AIOrder.GetOrderJumpTo(12, 1));
	print("  RemoveOrder():         " + AIOrder.RemoveOrder(12, 0));
	print("  SetOrderFlags():       " + AIOrder.SetOrderFlags(12, 0, AIOrder.OF_FULL_LOAD));
	print("  GetOrderFlags():       " + AIOrder.GetOrderFlags(12, 0));
	print("  GetOrderDestination(): " + AIOrder.GetOrderDestination(12, 0));
	print("  CopyOrders():          " + AIOrder.CopyOrders(12, 1));
	print("  CopyOrders():          " + AIOrder.CopyOrders(13, 12));
	print("  ShareOrders():         " + AIOrder.ShareOrders(13, 1));
	print("  ShareOrders():         " + AIOrder.ShareOrders(13, 12));
	print("  UnshareOrders():       " + AIOrder.UnshareOrders(13));
	print("  AppendOrder():         " + AIOrder.AppendOrder(12, 33421, AIOrder.OF_NONE));

	print("  GetStopLocation():     " + AIOrder.GetStopLocation(13, 0));
	print("  BuildVehicle():        " + AIVehicle.BuildVehicle(23596, 8));
	print("  BuildRailStation():    " + AIRail.BuildRailStation(7958, AIRail.RAILTRACK_NE_SW, 1, 1, AIStation.STATION_NEW));
	print("  AppendOrder():         " + AIOrder.AppendOrder(20, 7958, AIOrder.OF_NONE));
	print("  GetOrderCount():       " + AIOrder.GetOrderCount(20));
	print("  GetStopLocation():     " + AIOrder.GetStopLocation(20, 0));
	print("  SetStopLocation():     " + AIOrder.SetStopLocation(20, 0, AIOrder.STOPLOCATION_MIDDLE));
	print("  GetStopLocation():     " + AIOrder.GetStopLocation(20, 0));

	local list = AIVehicleList_Station(3);

	print("");
	print("--VehicleList_Station--");
	print("  Count():             " + list.Count());
	list.Valuate(AIVehicle.GetLocation);
	print("  Location ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	print("  foreach():");
	foreach (idx, val in list) {
		print("    " + idx + " => " + val);
	}
}

function Regression::RailTypeList()
{
	local list = AIRailTypeList();

	print("");
	print("--RailTypeList--");
	print("  Count():             " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    RailType:                " + i);
		print("    GetName():               " + AIRail.GetName(i));
		print("    IsRailTypeAvailable():   " + AIRail.IsRailTypeAvailable(i));
		print("    GetMaxSpeed():           " + AIRail.GetMaxSpeed(i));
	}
}

function Regression::Rail()
{
	AIRail.SetCurrentRailType(0);

	print("");
	print("--Rail--");
	print("    IsRailTile():                  " + AIRail.IsRailTile(10002));
	print("    BuildRailTrack():              " + AIRail.BuildRailTrack(10002, AIRail.RAILTRACK_NW_SE));
	print("    BuildSignal():                 " + AIRail.BuildSignal(10002, 10258, AIRail.SIGNALTYPE_PBS));
	print("    RemoveRailTrack():             " + AIRail.RemoveRailTrack(10002, AIRail.RAILTRACK_NW_NE));
	print("    RemoveRailTrack():             " + AIRail.RemoveRailTrack(10002, AIRail.RAILTRACK_NW_SE));
	print("    BuildRail():                   " + AIRail.BuildRail(10002, 10003, 10006));
	print("    HasTransportType():            " + AITile.HasTransportType(10005, AITile.TRANSPORT_RAIL));
	print("    HasTransportType():            " + AITile.HasTransportType(10006, AITile.TRANSPORT_RAIL));
	print("    RemoveRail():                  " + AIRail.RemoveRail(10006, 10005, 10002));
	print("    HasTransportType():            " + AITile.HasTransportType(10004, AITile.TRANSPORT_RAIL));
	print("    HasTransportType():            " + AITile.HasTransportType(10005, AITile.TRANSPORT_RAIL));
	print("    BuildRailTrack():              " + AIRail.BuildRailTrack(6200, AIRail.RAILTRACK_NE_SW));
	print("    RemoveRailTrack():             " + AIRail.RemoveRailTrack(6200, AIRail.RAILTRACK_NW_NE));
	print("    RemoveRailTrack():             " + AIRail.RemoveRailTrack(6200, AIRail.RAILTRACK_NE_SW));
	print("    BuildRail():                   " + AIRail.BuildRail(6200, 6200 + 256, 6200 + (256 * 4)));
	print("    HasTransportType():            " + AITile.HasTransportType(6200 + (256 * 3), AITile.TRANSPORT_RAIL));
	print("    HasTransportType():            " + AITile.HasTransportType(6200 + (256 * 4), AITile.TRANSPORT_RAIL));
	print("    RemoveRail():                  " + AIRail.RemoveRail(6200 + (256 * 3), 6200 + (256 * 2), 6200 - 256));
	print("    HasTransportType():            " + AITile.HasTransportType(6200 + (256 * 3), AITile.TRANSPORT_RAIL));
	print("    HasTransportType():            " + AITile.HasTransportType(6200 + (256 * 4), AITile.TRANSPORT_RAIL));
	print("    BuildRailTrack():              " + AIRail.BuildRail(14706, 14705, 12907));
	print("    HasTransportType():            " + AITile.HasTransportType(13421, AITile.TRANSPORT_RAIL));
	print("    HasTransportType():            " + AITile.HasTransportType(14191, AITile.TRANSPORT_RAIL));
	print("    RemoveRail():                  " + AIRail.RemoveRail(12907, 13163, 14706));
	print("    HasTransportType():            " + AITile.HasTransportType(13421, AITile.TRANSPORT_RAIL));
	print("    HasTransportType():            " + AITile.HasTransportType(14191, AITile.TRANSPORT_RAIL));
	print("    BuildRailTrack():              " + AIRail.BuildRailTrack(61533, AIRail.RAILTRACK_NW_SW));
	print("    BuildRailTrack():              " + AIRail.BuildRailTrack(61533, AIRail.RAILTRACK_NE_SE));
	print("    BuildRailTrack():              " + AIRail.BuildRailTrack(61533, AIRail.RAILTRACK_NW_NE));
	print("    BuildRailTrack():              " + AIRail.BuildRailTrack(61533, AIRail.RAILTRACK_SW_SE));
	print("    BuildRailTrack():              " + AIRail.BuildRailTrack(61533, AIRail.RAILTRACK_NE_SW));
	print("    DemolishTile():                " + AITile.DemolishTile(61533));
	print("    BuildRailTrack():              " + AIRail.BuildRailTrack(61533, AIRail.RAILTRACK_NE_SW));
	print("    BuildRailTrack():              " + AIRail.BuildRailTrack(61533, AIRail.RAILTRACK_NW_SE));
	print("    BuildRailTrack():              " + AIRail.BuildRailTrack(61533, AIRail.RAILTRACK_NW_NE));
	print("    BuildRailTrack():              " + AIRail.BuildRailTrack(61533, AIRail.RAILTRACK_SW_SE));
	print("    DemolishTile():                " + AITile.DemolishTile(61533));
	print("    BuildRailTrack():              " + AIRail.BuildRailTrack(61533, AIRail.RAILTRACK_NW_SE));

	print("  Depot");
	print("    IsRailTile():                  " + AIRail.IsRailTile(33411));
	print("    BuildRailDepot():              " + AIRail.BuildRailDepot(0, 1));
	print("    BuildRailDepot():              " + AIRail.BuildRailDepot(33411, 33411));
	print("    BuildRailDepot():              " + AIRail.BuildRailDepot(33411, 33414));
	print("    BuildRailDepot():              " + AIRail.BuildRailDepot(33411, 33412));
	print("    GetRailDepotFrontTile():       " + AIRail.GetRailDepotFrontTile(33411));
	print("    IsBuildable():                 " + AITile.IsBuildable(33411));
	local list = AIDepotList(AITile.TRANSPORT_RAIL);
	print("    DepotList");
	print("      Count():                     " + list.Count());
	list.Valuate(AITile.GetDistanceManhattanToTile, 0);
	print("      Depot distance from (0,0) ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("        " + i + " => " + list.GetValue(i));
	}
	print("    RemoveDepot():                 " + AITile.DemolishTile(33411));
	print("    BuildRailDepot():              " + AIRail.BuildRailDepot(23596, 23597));

	print("  Station");
	print("    BuildRailStation():            " + AIRail.BuildRailStation(0, AIRail.RAILTRACK_NE_SW, 1, 1, AIStation.STATION_NEW));
	print("    BuildRailStation():            " + AIRail.BuildRailStation(7958, AIRail.RAILTRACK_NE_SW, 4, 5, AIStation.STATION_NEW));
	print("    IsRailStationTile():           " + AIRail.IsRailStationTile(7957));
	print("    IsRailStationTile():           " + AIRail.IsRailStationTile(7958));
	print("    IsRailStationTile():           " + AIRail.IsRailStationTile(7959));
	print("    RemoveRailStationTileRectangle():" + AIRail.RemoveRailStationTileRectangle(7959, 7959, false));
	print("    IsRailStationTile():           " + AIRail.IsRailStationTile(7957));
	print("    IsRailStationTile():           " + AIRail.IsRailStationTile(7958));
	print("    IsRailStationTile():           " + AIRail.IsRailStationTile(7959));
	print("    DemolishTile():                " + AITile.DemolishTile(7960));
	print("    IsRailStationTile():           " + AIRail.IsRailStationTile(7957));
	print("    IsRailStationTile():           " + AIRail.IsRailStationTile(7958));
	print("    IsRailStationTile():           " + AIRail.IsRailStationTile(7959));

	print("  Waypoint");
	print("    BuildRailTrack():              " + AIRail.BuildRailTrack(12646, AIRail.RAILTRACK_NW_SE));
	print("    BuildRailTrack():              " + AIRail.BuildRailTrack(12648, AIRail.RAILTRACK_NE_SW));
	print("    BuildRailTrack():              " + AIRail.BuildRailTrack(12650, AIRail.RAILTRACK_NW_NE));
	print("    BuildRailWaypoint():           " + AIRail.BuildRailWaypoint(12644));
	print("    BuildRailWaypoint():           " + AIRail.BuildRailWaypoint(12646));
	print("    BuildRailWaypoint():           " + AIRail.BuildRailWaypoint(12648));
	print("    BuildRailWaypoint():           " + AIRail.BuildRailWaypoint(12650));
	print("    IsRailWaypointTile():          " + AIRail.IsRailWaypointTile(12644));
	print("    IsRailWaypointTile():          " + AIRail.IsRailWaypointTile(12646));
	print("    IsRailWaypointTile():          " + AIRail.IsRailWaypointTile(12648));
	print("    IsRailWaypointTile():          " + AIRail.IsRailWaypointTile(12650));
	print("    RemoveRailWaypointTileRectangle():" + AIRail.RemoveRailWaypointTileRectangle(12644, 12646, false));
	print("    RemoveRailWaypointTileRectangle():" + AIRail.RemoveRailWaypointTileRectangle(12648, 12650, true));
	print("    IsRailWaypointTile():          " + AIRail.IsRailWaypointTile(12644));
	print("    IsRailWaypointTile():          " + AIRail.IsRailWaypointTile(12646));
	print("    IsRailWaypointTile():          " + AIRail.IsRailWaypointTile(12648));
	print("    IsRailWaypointTile():          " + AIRail.IsRailWaypointTile(12650));
	print("    HasTransportType():            " + AITile.HasTransportType(12644, AITile.TRANSPORT_RAIL));
	print("    HasTransportType():            " + AITile.HasTransportType(12646, AITile.TRANSPORT_RAIL));
	print("    HasTransportType():            " + AITile.HasTransportType(12648, AITile.TRANSPORT_RAIL));
	print("    HasTransportType():            " + AITile.HasTransportType(12650, AITile.TRANSPORT_RAIL));
	print("    DemolishTile():                " + AITile.DemolishTile(12648));
	print("    DemolishTile():                " + AITile.DemolishTile(12650));
}

function Regression::Road()
{
	print("");
	print("--Road--");
	print("  Road");
	print("    IsRoadTile():                  " + AIRoad.IsRoadTile(33411));
	print("    BuildRoad():                   " + AIRoad.BuildRoad(0, 1));
	print("    BuildRoad():                   " + AIRoad.BuildRoad(33411, 33411));
	print("    HasTransportType():            " + AITile.HasTransportType(33413, AITile.TRANSPORT_ROAD));
	print("    BuildRoad():                   " + AIRoad.BuildRoad(33411, 33414));
	print("    HasTransportType():            " + AITile.HasTransportType(33413, AITile.TRANSPORT_ROAD));
	print("    AreRoadTilesConnected():       " + AIRoad.AreRoadTilesConnected(33412, 33413));
	print("    IsRoadTile():                  " + AIRoad.IsRoadTile(33411));
	print("    HasRoadType(Road):             " + AIRoad.HasRoadType(33411, AIRoad.ROADTYPE_ROAD));
	print("    HasRoadType(Tram):             " + AIRoad.HasRoadType(33411, AIRoad.ROADTYPE_TRAM));
	print("    GetNeighbourRoadCount():       " + AIRoad.GetNeighbourRoadCount(33412));
	print("    RemoveRoad():                  " + AIRoad.RemoveRoad(33411, 33411));
	print("    RemoveRoad():                  " + AIRoad.RemoveRoad(33411, 33412));
	print("    RemoveRoad():                  " + AIRoad.RemoveRoad(19590, 19590));
	print("    RemoveRoad():                  " + AIRoad.RemoveRoad(33411, 33414));
	print("    BuildOneWayRoad():             " + AIRoad.BuildOneWayRoad(33411, 33414));
	print("    AreRoadTilesConnected():       " + AIRoad.AreRoadTilesConnected(33412, 33413));
	print("    AreRoadTilesConnected():       " + AIRoad.AreRoadTilesConnected(33413, 33412));
	print("    BuildOneWayRoad():             " + AIRoad.BuildOneWayRoad(33413, 33412));
	print("    AreRoadTilesConnected():       " + AIRoad.AreRoadTilesConnected(33412, 33413));
	print("    AreRoadTilesConnected():       " + AIRoad.AreRoadTilesConnected(33413, 33412));
	print("    BuildOneWayRoad():             " + AIRoad.BuildOneWayRoad(33412, 33413));
	print("    BuildOneWayRoad():             " + AIRoad.BuildOneWayRoad(33413, 33412));
	print("    AreRoadTilesConnected():       " + AIRoad.AreRoadTilesConnected(33412, 33413));
	print("    AreRoadTilesConnected():       " + AIRoad.AreRoadTilesConnected(33413, 33412));
	print("    RemoveRoad():                  " + AIRoad.RemoveRoad(33411, 33412));
	print("    IsRoadTypeAvailable(Road):     " + AIRoad.IsRoadTypeAvailable(AIRoad.ROADTYPE_ROAD));
	print("    IsRoadTypeAvailable(Tram):     " + AIRoad.IsRoadTypeAvailable(AIRoad.ROADTYPE_TRAM));
	print("    SetCurrentRoadType(Tram):      " + AIRoad.SetCurrentRoadType(AIRoad.ROADTYPE_TRAM));
	print("    GetCurrentRoadType():          " + AIRoad.GetCurrentRoadType());

	print("  Depot");
	print("    IsRoadTile():                  " + AIRoad.IsRoadTile(33411));
	print("    BuildRoadDepot():              " + AIRoad.BuildRoadDepot(0, 1));
	print("    BuildRoadDepot():              " + AIRoad.BuildRoadDepot(33411, 33411));
	print("    BuildRoadDepot():              " + AIRoad.BuildRoadDepot(33411, 33414));
	print("    BuildRoadDepot():              " + AIRoad.BuildRoadDepot(33411, 33412));
	print("    HasRoadType(Road):             " + AIRoad.HasRoadType(33411, AIRoad.ROADTYPE_ROAD));
	print("    HasRoadType(Tram):             " + AIRoad.HasRoadType(33411, AIRoad.ROADTYPE_TRAM));
	print("    GetLastError():                " + AIError.GetLastError());
	print("    GetLastErrorString():          " + AIError.GetLastErrorString());
	print("    GetErrorCategory():            " + AIError.GetErrorCategory());
	print("    IsRoadTile():                  " + AIRoad.IsRoadTile(33411));
	print("    GetRoadDepotFrontTile():       " + AIRoad.GetRoadDepotFrontTile(33411));
	print("    IsRoadDepotTile():             " + AIRoad.IsRoadDepotTile(33411));
	print("    IsBuildable():                 " + AITile.IsBuildable(33411));
	local list = AIDepotList(AITile.TRANSPORT_ROAD);
	print("    DepotList");
	print("      Count():                     " + list.Count());
	list.Valuate(AITile.GetDistanceManhattanToTile, 0);
	print("      Depot distance from (0,0) ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("        " + i + " => " + list.GetValue(i));
	}
	print("    RemoveRoadDepot():             " + AIRoad.RemoveRoadDepot(33411));
	print("    RemoveRoadDepot():             " + AIRoad.RemoveRoadDepot(33411));

	print("  Station");
	print("    IsRoadTile():                  " + AIRoad.IsRoadTile(33411));
	print("    BuildRoadStation():            " + AIRoad.BuildRoadStation(0, 1, AIRoad.ROADVEHTYPE_BUS, AIStation.STATION_JOIN_ADJACENT));
	print("    BuildRoadStation():            " + AIRoad.BuildRoadStation(33411, 33411, AIRoad.ROADVEHTYPE_BUS, AIStation.STATION_JOIN_ADJACENT));
	print("    BuildRoadStation():            " + AIRoad.BuildRoadStation(33411, 33414, AIRoad.ROADVEHTYPE_BUS, AIStation.STATION_JOIN_ADJACENT));
	print("    BuildRoadStation():            " + AIRoad.BuildRoadStation(33411, 33412, AIRoad.ROADVEHTYPE_BUS, AIStation.STATION_JOIN_ADJACENT));
	print("    IsStationTile():               " + AITile.IsStationTile(33411));
	print("    IsStationTile():               " + AITile.IsStationTile(33412));
	print("    HasRoadType(Road):             " + AIRoad.HasRoadType(33411, AIRoad.ROADTYPE_ROAD));
	print("    HasRoadType(Tram):             " + AIRoad.HasRoadType(33411, AIRoad.ROADTYPE_TRAM));
	print("    IsRoadTile():                  " + AIRoad.IsRoadTile(33411));
	print("    GetDriveThroughBackTile():     " + AIRoad.GetDriveThroughBackTile(33411));
	print("    GetRoadStationFrontTile():     " + AIRoad.GetRoadStationFrontTile(33411));
	print("    IsRoadStationTile():           " + AIRoad.IsRoadStationTile(33411));
	print("    IsDriveThroughRoadStationTile: " + AIRoad.IsDriveThroughRoadStationTile(33411));
	print("    RemoveRoadStation():           " + AIRoad.RemoveRoadStation(33411));
	print("    RemoveRoadStation():           " + AIRoad.RemoveRoadStation(33411));

	print("  Station Types");
	print("    BuildRoadStation(bus):         " + AIRoad.BuildRoadStation(33411, 33410, AIRoad.ROADVEHTYPE_BUS, AIStation.STATION_JOIN_ADJACENT));
	print("    BuildRoadStation(truck):       " + AIRoad.BuildRoadStation(33421, 33422, AIRoad.ROADVEHTYPE_TRUCK, AIStation.STATION_JOIN_ADJACENT));
	print("    BuildRoadStation(truck):       " + AIRoad.BuildRoadStation(33412, 33413, AIRoad.ROADVEHTYPE_TRUCK, AIStation.STATION_JOIN_ADJACENT));
	print("    BuildRoadStation(bus):         " + AIRoad.BuildRoadStation(33411 + 256, 33411, AIRoad.ROADVEHTYPE_BUS, AIStation.STATION_JOIN_ADJACENT));
	print("    BuildRoadStation(truck):       " + AIRoad.BuildRoadStation(33412 + 256, 33412 + 256 + 256, AIRoad.ROADVEHTYPE_TRUCK, AIStation.STATION_JOIN_ADJACENT));
	print("    BuildDriveThroughRoadStation(bus-drive):   " + AIRoad.BuildDriveThroughRoadStation(33413, 33412, AIRoad.ROADVEHTYPE_BUS, AIStation.STATION_JOIN_ADJACENT));
	print("    BuildDriveThroughRoadStation(truck-drive): " + AIRoad.BuildDriveThroughRoadStation(33414, 33413, AIRoad.ROADVEHTYPE_TRUCK, AIStation.STATION_JOIN_ADJACENT));
	print("    BuildDriveThroughRoadStation(bus-drive):   " + AIRoad.BuildDriveThroughRoadStation(33415, 33414, AIRoad.ROADVEHTYPE_BUS, AIStation.STATION_JOIN_ADJACENT));
	print("    BuildDriveThroughRoadStation(truck-drive): " + AIRoad.BuildDriveThroughRoadStation(33416, 33415, AIRoad.ROADVEHTYPE_TRUCK, AIStation.STATION_JOIN_ADJACENT));
	print("    BuildRoadDepot():              " + AIRoad.BuildRoadDepot(33417, 33418));
	print("    GetRoadStationFrontTile():     " + AIRoad.GetRoadStationFrontTile(33411 + 256));
	print("    GetRoadStationFrontTile():     " + AIRoad.GetRoadStationFrontTile(33412 + 256));
	print("    IsDriveThroughRoadStationTile: " + AIRoad.IsDriveThroughRoadStationTile(33415));
	print("    IsBuildable():                 " + AITile.IsBuildable(33415));
	print("    GetDriveThroughBackTile():     " + AIRoad.GetDriveThroughBackTile(33415));
	print("    GetRoadStationFrontTile():     " + AIRoad.GetRoadStationFrontTile(33415));
	print("    IsRoadTile():                  " + AIRoad.IsRoadTile(33415));
}

function Regression::Sign()
{
	local j = 0;

	print("");
	print("--Sign--");
	print("  BuildSign(33410, 'Some Sign'):       " + AISign.BuildSign(33410, "Some Sign"));
	print("  BuildSign(33411, 'Test'):            " + AISign.BuildSign(33411, "Test"));
	print("  SetName(1, 'Test2'):                 " + AISign.SetName(1, "Test2"));
	local sign_id = AISign.BuildSign(33409, "Some other Sign");
	print("  BuildSign(33409, 'Some other Sign'): " + sign_id);
	print("  RemoveSign(" + sign_id + "):                       " + AISign.RemoveSign(sign_id));
	print("");
	local list = AISignList();
	list.Sort(AIList.SORT_BY_ITEM, AIList.SORT_ASCENDING);
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		j++;
		print("  Sign " + i);
		print("    IsValidSign():   " + AISign.IsValidSign(i));
		print("    GetName():       " + AISign.GetName(i));
		print("    GetLocation():   " + AISign.GetLocation(i));
	}
	print("  Valid Signs:       " + j);
}

function Regression::Station()
{
	print("");
	print("--Station--");
	print("  IsValidStation(0):        " + AIStation.IsValidStation(0));
	print("  IsValidStation(1000):     " + AIStation.IsValidStation(1000));
	print("  GetName(0):               " + AIStation.GetName(0));
	print("  SetName(0):               " + AIStation.SetName(0, "Look, a station"));
	print("  GetName(0):               " + AIStation.GetName(0));
	print("  GetLocation(1):           " + AIStation.GetLocation(1));
	print("  GetLocation(1000):        " + AIStation.GetLocation(1000));
	print("  GetStationID(33411):      " + AIStation.GetStationID(33411));
	print("  GetStationID(34411):      " + AIStation.GetStationID(34411));
	print("  GetStationID(33411):      " + AIStation.GetStationID(33411));
	print("  HasRoadType(3, TRAM):     " + AIStation.HasRoadType(3, AIRoad.ROADTYPE_TRAM));
	print("  HasRoadType(3, ROAD):     " + AIStation.HasRoadType(3, AIRoad.ROADTYPE_ROAD));
	print("  HasRoadType(33411, TRAM): " + AIRoad.HasRoadType(33411, AIRoad.ROADTYPE_TRAM));
	print("  HasRoadType(33411, ROAD): " + AIRoad.HasRoadType(33411, AIRoad.ROADTYPE_ROAD));
	print("  HasStationType(3, BUS):   " + AIStation.HasStationType(3, AIStation.STATION_BUS_STOP));
	print("  HasStationType(3, TRAIN): " + AIStation.HasStationType(3, AIStation.STATION_TRAIN));

	print("  GetCoverageRadius(BUS):   " + AIStation.GetCoverageRadius(AIStation.STATION_BUS_STOP));
	print("  GetCoverageRadius(TRUCK): " + AIStation.GetCoverageRadius(AIStation.STATION_TRUCK_STOP));
	print("  GetCoverageRadius(TRAIN): " + AIStation.GetCoverageRadius(AIStation.STATION_TRAIN));

	print("  GetNearestTown():         " + AIStation.GetNearestTown(0));
	print("  GetNearestTown():         " + AIStation.GetNearestTown(10000));
	print("  GetNearestTown():         " + AIStation.GetNearestTown(3));

	print("");
	print("--CargoWaiting--");
	for (local cargo = 0; cargo <= 1000; cargo += 1000) {
		for (local station0 = 0; station0 <= 1000; station0 += 1000) {
			print("  GetCargoWaiting(" + station0 + ", " + cargo + "): " +
					AIStation.GetCargoWaiting(station0, cargo));
			for (local station1 = 0; station1 <= 1000; station1 += 1000) {
				print("  GetCargoWaitingFrom(" + station0 + ", " + station1 + ", " + cargo + "): " +
						AIStation.GetCargoWaitingFrom(station0, station1, cargo));
				print("  GetCargoWaitingVia(" + station0 + ", " + station1 + ", " + cargo + "): " +
						AIStation.GetCargoWaitingFrom(station0, station1, cargo));
				for (local station2 = 0; station2 <= 1000; station2 += 1000) {
					print("  GetCargoWaitingFromVia(" + station0 + ", " + station1 + ", " + station2 + ", " + cargo + "): " +
							AIStation.GetCargoWaitingFromVia(station0, station1, station2, cargo));
				}
			}
		}
	}

	print("");
	print("--CargoPlanned--");
	for (local cargo = 0; cargo <= 1000; cargo += 1000) {
		for (local station0 = 0; station0 <= 1000; station0 += 1000) {
			print("  GetCargoPlanned(" + station0 + ", " + cargo + "): " +
					AIStation.GetCargoPlanned(station0, cargo));
			for (local station1 = 0; station1 <= 1000; station1 += 1000) {
				print("  GetCargoPlannedFrom(" + station0 + ", " + station1 + ", " + cargo + "): " +
						AIStation.GetCargoPlannedFrom(station0, station1, cargo));
				print("  GetCargoPlannedVia(" + station0 + ", " + station1 + ", " + cargo + "): " +
						AIStation.GetCargoPlannedFrom(station0, station1, cargo));
				for (local station2 = 0; station2 <= 1000; station2 += 1000) {
					print("  GetCargoPlannedFromVia(" + station0 + ", " + station1 + ", " + station2 + ", " + cargo + "): " +
							AIStation.GetCargoPlannedFromVia(station0, station1, station2, cargo));
				}
			}
		}
	}
}

function Regression::Tile()
{
	print("");
	print("--Tile--");
	print("  HasTreeOnTile():      " + AITile.HasTreeOnTile(33148));
	print("  IsFarmTile():         " + AITile.IsFarmTile(32892));
	print("  IsRockTile():         " + AITile.IsRockTile(31606));
	print("  IsRoughTile():        " + AITile.IsRoughTile(33674));
	print("  HasTreeOnTile():      " + AITile.HasTreeOnTile(33404));
	print("  IsFarmTile():         " + AITile.IsFarmTile(33404));
	print("  IsRockTile():         " + AITile.IsRockTile(33404));
	print("  IsRoughTile():        " + AITile.IsRoughTile(33404));
	print("  IsSnowTile():         " + AITile.IsSnowTile(33404));
	print("  IsDesertTile():       " + AITile.IsDesertTile(33404));
	print("  PlantTree():          " + AITile.PlantTree(33404));
	print("  HasTreeOnTile():      " + AITile.HasTreeOnTile(33404));
	print("  PlantTree():          " + AITile.PlantTree(33404));
	print("  HasTreeOnTile():      " + AITile.HasTreeOnTile(33661));
	print("  PlantTreeRectangle(): " + AITile.PlantTreeRectangle(33404, 2, 2));
	print("  HasTreeOnTile():      " + AITile.HasTreeOnTile(33661));
}

function Regression::TileList()
{
	local list = AITileList();

	print("");
	print("--TileList--");
	print("  Count():             " + list.Count());
	list.AddRectangle(27631 - 256 * 1, 256 * 1 + 27631 + 2);
	print("  Count():             " + list.Count());

	list.Valuate(AITile.GetSlope);
	print("  Slope():             done");
	print("  Count():             " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
		print("    " + i + " => " + AITile.GetComplementSlope(list.GetValue(i)));
		print("    " + i + " => " + AITile.IsSteepSlope(list.GetValue(i)));
		print("    " + i + " => " + AITile.IsHalftileSlope(list.GetValue(i)));
	}
	list.Clear();

	print("");
	print("--TileList--");
	print("  Count():             " + list.Count());
	list.AddRectangle(34436, 256 * 2 + 34436 + 8);
	print("  Count():             " + list.Count());

	list.Valuate(AITile.GetCornerHeight, AITile.CORNER_N);
	print("  Height():            done");
	print("  Count():             " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list.Valuate(AITile.GetCornerHeight, AITile.CORNER_N);
	print("  CornerHeight(North): done");
	print("  Count():             " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list.Valuate(AITile.GetMinHeight);
	print("  MinHeight():         done");
	print("  Count():             " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list.Valuate(AITile.GetMaxHeight);
	print("  MaxHeight():         done");
	print("  Count():             " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list.Valuate(AITile.GetSlope);
	list.KeepValue(0);
	print("  Slope():             done");
	print("  KeepValue(0):        done");
	print("  Count():             " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list.Clear();
	list.AddRectangle(41895 - 256 * 2, 256 * 2 + 41895 + 8);
	list.Valuate(AITile.IsBuildable);
	list.KeepValue(1);
	print("  Buildable():         done");
	print("  KeepValue(1):        done");
	print("  Count():             " + list.Count());

	list.Valuate(AITile.IsBuildableRectangle, 3, 3);
	print("  BuildableRectangle(3, 3) ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AITile.GetDistanceManhattanToTile, 30000);
	print("  DistanceManhattanToTile(30000) ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AITile.GetDistanceSquareToTile, 30000);
	print("  DistanceSquareToTile(30000) ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list.AddRectangle(31895 - 256 * 5, 256 * 5 + 31895 + 8);

	list.Valuate(AITile.GetOwner);
	print("  GetOwner() ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AITile.GetTownAuthority);
	print("  GetTownAuthority() ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AITile.GetClosestTown);
	print("  GetClosestTown() ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list.Valuate(AITile.GetCargoAcceptance, 0, 1, 1, 3);
	list.KeepAboveValue(10);
	print("  CargoAcceptance():   done");
	print("  KeepAboveValue(10):  done");
	print("  Count():             " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list.Valuate(AIRoad.IsRoadTile);
	list.KeepValue(1);
	print("  RoadTile():          done");
	print("  KeepValue(1):        done");
	print("  Count():             " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list.Valuate(AIRoad.GetNeighbourRoadCount);
	list.KeepValue(1);
	print("  NeighbourRoadCount():done");
	print("  KeepValue(1):        done");
	print("  Count():             " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list.AddRectangle(0x6F3F, 0x7248);
	list.Valuate(AITile.IsWaterTile);
	print("  IsWaterTile():       done");
	print("  Count():             " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list.Valuate(AITile.IsSeaTile);
	print("  IsSeaTile():         done");
	print("  Count():             " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list.Valuate(AITile.IsRiverTile);
	print("  IsRiverTile()        done");
	print("  Count():             " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list.Valuate(AIMarine.IsCanalTile);
	print("  IsCanalTile()        done");
	print("  Count():             " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list.Valuate(AITile.IsCoastTile);
	print("  IsCoastTile()        done");
	print("  Count():             " + list.Count());
	print("  ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list = AITileList_IndustryAccepting(0, 3);
	print("");
	print("--TileList_IndustryAccepting--");
	print("  Count():             " + list.Count());
	list.Valuate(AITile.GetCargoAcceptance, 3, 1, 1, 3);
	print("  Location ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list = AITileList_IndustryProducing(1, 3);
	print("");
	print("--TileList_IndustryProducing--");
	print("  Count():             " + list.Count());
	list.Valuate(AITile.GetCargoProduction, 7, 1, 1, 3);
	print("  Location ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	list = AITileList_StationType(6, AIStation.STATION_BUS_STOP);
	print("");
	print("--TileList_StationType--");
	print("  Count():             " + list.Count());
	print("  Location ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
}

function Regression::Town()
{
	local j = 0;

	print("");
	print("--Town--");
	print("  GetTownCount():    " + AITown.GetTownCount());
	local list = AITownList();
	list.Sort(AIList.SORT_BY_ITEM, AIList.SORT_ASCENDING);
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		if (AITown.IsValidTown(i)) j++;
		print("  Town " + i);
		print("    IsValidTown():   " + AITown.IsValidTown(i));
		print("    GetName():       " + AITown.GetName(i));
		print("    GetPopulation(): " + AITown.GetPopulation(i));
		print("    GetLocation():   " + AITown.GetLocation(i));
		print("    GetHouseCount(): " + AITown.GetHouseCount(i));
		print("    GetRating():     " + AITown.GetRating(i, AICompany.COMPANY_SELF));
		print("    IsCity():        " + AITown.IsCity(i));
	}
	print("  Valid Towns:       " + j);
	print("  GetTownCount():    " + AITown.GetTownCount());
}

function Regression::TownList()
{
	local list = AITownList();

	print("");
	print("--TownList--");
	print("  Count():             " + list.Count());
	list.Valuate(AITown.GetLocation);
	print("  Location ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AITown.GetDistanceManhattanToTile, 30000);
	print("  DistanceManhattanToTile(30000) ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AITown.GetDistanceSquareToTile, 30000);
	print("  DistanceSquareToTile(30000) ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AITown.IsWithinTownInfluence, AITown.GetLocation(0));
	print("  IsWithinTownInfluence(" + AITown.GetLocation(0) + ") ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AITown.GetAllowedNoise);
	print("  GetAllowedNoise() ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AITown.GetPopulation);
	list.KeepAboveValue(500);
	print("  KeepAboveValue(500): done");
	print("  Count():             " + list.Count());
	print("  Population ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}

	print("  HasStatue():                     " + AITown.HasStatue(list.Begin()));
	print("  GetRoadReworkDuration():         " + AITown.GetRoadReworkDuration(list.Begin()));
	print("  GetExclusiveRightsCompany():     " + AITown.GetExclusiveRightsCompany(list.Begin()));
	print("  GetExclusiveRightsDuration():    " + AITown.GetExclusiveRightsDuration(list.Begin()));
	print("  IsActionAvailable(BUILD_STATUE): " + AITown.IsActionAvailable(list.Begin(), AITown.TOWN_ACTION_BUILD_STATUE));
	print("  PerformTownAction(BUILD_STATUE): " + AITown.PerformTownAction(list.Begin(), AITown.TOWN_ACTION_BUILD_STATUE));
	print("  IsActionAvailable(BUILD_STATUE): " + AITown.IsActionAvailable(list.Begin(), AITown.TOWN_ACTION_BUILD_STATUE));
	print("  HasStatue():                     " + AITown.HasStatue(list.Begin()));
}

function Regression::Tunnel()
{
	print("");
	print("--Tunnel--");
	print("  IsTunnelTile():       " + AITunnel.IsTunnelTile(29050));
	print("  RemoveTunnel():       " + AITunnel.RemoveTunnel(29050));
	print("  GetOtherTunnelEnd():  " + AITunnel.GetOtherTunnelEnd(29050));
	print("  BuildTunnel():        " + AITunnel.BuildTunnel(AIVehicle.VT_ROAD, 29050));
	print("  GetOtherTunnelEnd():  " + AITunnel.GetOtherTunnelEnd(29050));
	print("  IsTunnelTile():       " + AITunnel.IsTunnelTile(29050));
	print("  IsTunnelTile():       " + AITunnel.IsTunnelTile(28026));
	print("  RemoveTunnel():       " + AITunnel.RemoveTunnel(29050));
	print("  IsTunnelTile():       " + AITunnel.IsTunnelTile(29050));

	print("  --Errors--");
	print("  BuildTunnel():        " + AITunnel.BuildTunnel(AIVehicle.VT_ROAD, 7529));
	print("  BuildTunnel():        " + AITunnel.BuildTunnel(AIVehicle.VT_ROAD, 8043));
	print("  GetLastErrorString(): " + AIError.GetLastErrorString());
	print("  RemoveTunnel():       " + AITunnel.RemoveTunnel(7529));
}

function Regression::Vehicle()
{
	local accounting = AIAccounting();

	print("");
	print("--Vehicle--");
	print("  IsValidVehicle(-1):   " + AIVehicle.IsValidVehicle(-1));
	print("  IsValidVehicle(0):    " + AIVehicle.IsValidVehicle(0));
	print("  IsValidVehicle(12):   " + AIVehicle.IsValidVehicle(12));
	print("  ISValidVehicle(9999): " + AIVehicle.IsValidVehicle(9999));

	local bank = AICompany.GetBankBalance(AICompany.COMPANY_SELF);

	print("  BuildVehicle():       " + AIVehicle.BuildVehicle(33417, 153));
	print("  IsValidVehicle(12):   " + AIVehicle.IsValidVehicle(12));
	print("  CloneVehicle():       " + AIVehicle.CloneVehicle(33417, 12, true));

	local bank_after = AICompany.GetBankBalance(AICompany.COMPANY_SELF);

	print("  --Accounting--");
	print("    GetCosts():         " + accounting.GetCosts());
	print("    Should be:          " + (bank - bank_after));
	print("    ResetCosts():       " + accounting.ResetCosts());

	bank = AICompany.GetBankBalance(AICompany.COMPANY_SELF);

	print("  SellVehicle(13):      " + AIVehicle.SellVehicle(13));
	print("  IsInDepot():          " + AIVehicle.IsInDepot(12));
	print("  IsStoppedInDepot():   " + AIVehicle.IsStoppedInDepot(12));
	print("  StartStopVehicle():   " + AIVehicle.StartStopVehicle(12));
	print("  IsInDepot():          " + AIVehicle.IsInDepot(12));
	print("  IsStoppedInDepot():   " + AIVehicle.IsStoppedInDepot(12));
	print("  SendVehicleToDepot(): " + AIVehicle.SendVehicleToDepot(12));
	print("  IsInDepot():          " + AIVehicle.IsInDepot(12));
	print("  IsStoppedInDepot():   " + AIVehicle.IsStoppedInDepot(12));

	bank_after = AICompany.GetBankBalance(AICompany.COMPANY_SELF);

	print("  --Accounting--");
	print("    GetCosts():         " + accounting.GetCosts());
	print("    Should be:          " + (bank - bank_after));

	print("  GetName():            " + AIVehicle.GetName(12));
	print("  SetName():            " + AIVehicle.SetName(12, "MyVehicleName"));
	print("  GetName():            " + AIVehicle.GetName(12));
	print("  CloneVehicle():       " + AIVehicle.CloneVehicle(33417, 12, true));

	print("  --VehicleData--");
	print("    GetLocation():       " + AIVehicle.GetLocation(12));
	print("    GetEngineType():     " + AIVehicle.GetEngineType(12));
	print("    GetUnitNumber():     " + AIVehicle.GetUnitNumber(12));
	print("    GetAge():            " + AIVehicle.GetAge(12));
	print("    GetMaxAge():         " + AIVehicle.GetMaxAge(12));
	print("    GetAgeLeft():        " + AIVehicle.GetAgeLeft(12));
	print("    GetCurrentSpeed():   " + AIVehicle.GetCurrentSpeed(12));
	print("    GetRunningCost():    " + AIVehicle.GetRunningCost(12));
	print("    GetProfitThisYear(): " + AIVehicle.GetProfitThisYear(12));
	print("    GetProfitLastYear(): " + AIVehicle.GetProfitLastYear(12));
	print("    GetCurrentValue():   " + AIVehicle.GetCurrentValue(12));
	print("    GetVehicleType():    " + AIVehicle.GetVehicleType(12));
	print("    GetRoadType():       " + AIVehicle.GetRoadType(12));
	print("    GetCapacity():       " + AIVehicle.GetCapacity(12, 10));
	print("    GetCargoLoad():      " + AIVehicle.GetCargoLoad(12, 10));
	print("    IsInDepot():         " + AIVehicle.IsInDepot(12));
	print("    GetNumWagons():      " + AIVehicle.GetNumWagons(12));
	print("    GetWagonEngineType(): " + AIVehicle.GetWagonEngineType(12, 0));
	print("    GetWagonAge():       " + AIVehicle.GetWagonAge(12, 0));
	print("    GetLength():         " + AIVehicle.GetLength(12));

	print("  GetOwner():           " + AITile.GetOwner(32119));
	print("  BuildVehicle():       " + AIVehicle.BuildVehicle(32119, 219));
	print("  IsValidVehicle(14):   " + AIVehicle.IsValidVehicle(14));
	print("  IsInDepot(14):        " + AIVehicle.IsInDepot(14));
	print("  IsStoppedInDepot(14): " + AIVehicle.IsStoppedInDepot(14));
	print("  IsValidVehicle(15):   " + AIVehicle.IsValidVehicle(15));
	print("  IsInDepot(15):        " + AIVehicle.IsInDepot(15));
	print("  IsStoppedInDepot(15): " + AIVehicle.IsStoppedInDepot(15));

	print("  BuildVehicle():       " + AIVehicle.BuildVehicle(28479, 204));
	print("  IsValidVehicle(16):   " + AIVehicle.IsValidVehicle(16));
	print("  IsInDepot(16):        " + AIVehicle.IsInDepot(16));
	print("  IsStoppedInDepot(16): " + AIVehicle.IsStoppedInDepot(16));

	print("  BuildRailDepot():     " + AIRail.BuildRailDepot(10008, 10000));
	print("  BuildVehicle():       " + AIVehicle.BuildVehicle(10008, 9));
	print("  BuildVehicle():       " + AIVehicle.BuildVehicle(10008, 27));
	print("  BuildVehicle():       " + AIVehicle.BuildVehicle(10008, 27));
	print("  IsValidVehicle(17):   " + AIVehicle.IsValidVehicle(17));
	print("  IsValidVehicle(18):   " + AIVehicle.IsValidVehicle(18));
	print("  IsValidVehicle(19):   " + AIVehicle.IsValidVehicle(19)); // 19 is immediately joined to 18
	print("  MoveWagonChain():     " + AIVehicle.MoveWagonChain(18, 0, 17, 0));
	print("  GetNumWagons():       " + AIVehicle.GetNumWagons(17));
	print("  GetLength():          " + AIVehicle.GetLength(17));
	print("  GetWagonEngineType(): " + AIVehicle.GetWagonEngineType(17, 0));
	print("  GetWagonAge():        " + AIVehicle.GetWagonAge(17, 0));
	print("  GetWagonEngineType(): " + AIVehicle.GetWagonEngineType(17, 1));
	print("  GetWagonAge():        " + AIVehicle.GetWagonAge(17, 1));
	print("  GetWagonEngineType(): " + AIVehicle.GetWagonEngineType(17 2));
	print("  GetWagonAge():        " + AIVehicle.GetWagonAge(17, 2));
	print("  GetWagonEngineType(): " + AIVehicle.GetWagonEngineType(17 3));
	print("  GetWagonAge():        " + AIVehicle.GetWagonAge(17, 3));

	print("  --Refit--");
	print("    GetBuildWithRefitCapacity(): " + AIVehicle.GetBuildWithRefitCapacity(28479, 211, 255));
	print("    GetBuildWithRefitCapacity(): " + AIVehicle.GetBuildWithRefitCapacity(28479, 211, 0));
	print("    GetBuildWithRefitCapacity(): " + AIVehicle.GetBuildWithRefitCapacity(28479, 211, 9));
	print("    BuildVehicleWithRefit():     " + AIVehicle.BuildVehicleWithRefit(28479, 211, 9));
	print("    GetCapacity():               " + AIVehicle.GetCapacity(20, 9));
	print("    GetCapacity():               " + AIVehicle.GetCapacity(20, 5));
	print("    GetRefitCapacity():          " + AIVehicle.GetRefitCapacity(20, 5));
	print("    RefitVehicle():              " + AIVehicle.RefitVehicle(20, 5));
	print("    GetCapacity():               " + AIVehicle.GetCapacity(20, 9));
	print("    GetCapacity():               " + AIVehicle.GetCapacity(20, 5));
	print("    SellVehicle():               " + AIVehicle.SellVehicle(20));

	print("  --Errors--");
	print("    RefitVehicle():        " + AIVehicle.RefitVehicle(12, 0));
	print("    GetLastErrorString():  " + AIError.GetLastErrorString());
	print("    SellVehicle():         " + AIVehicle.SellVehicle(12));
	print("    GetLastErrorString():  " + AIError.GetLastErrorString());
	print("    SendVehicleToDepot():  " + AIVehicle.SendVehicleToDepot(13));
	print("    GetLastErrorString():  " + AIError.GetLastErrorString());

	local list = AIVehicleList();

	print("");
	print("--VehicleList--");
	print("  Count():             " + list.Count());
	list.Valuate(AIVehicle.GetLocation);
	print("  Location ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIVehicle.GetEngineType);
	print("  EngineType ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIVehicle.GetUnitNumber);
	print("  UnitNumber ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIVehicle.GetAge);
	print("  Age ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIVehicle.GetMaxAge);
	print("  MaxAge ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIVehicle.GetAgeLeft);
	print("  AgeLeft ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIVehicle.GetCurrentSpeed);
	print("  CurrentSpeed ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIVehicle.GetRunningCost);
	print("  RunningCost ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIVehicle.GetProfitThisYear);
	print("  ProfitThisYear ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIVehicle.GetProfitLastYear);
	print("  ProfitLastYear ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIVehicle.GetCurrentValue);
	print("  CurrentValue ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIVehicle.GetVehicleType);
	print("  VehicleType ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIVehicle.GetRoadType);
	print("  RoadType ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIVehicle.GetCapacity, 10);
	print("  VehicleType ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
	list.Valuate(AIVehicle.GetCargoLoad, 10);
	print("  VehicleType ListDump:");
	for (local i = list.Begin(); !list.IsEnd(); i = list.Next()) {
		print("    " + i + " => " + list.GetValue(i));
	}
}

function Regression::PrintSubsidy(subsidy_id)
{
	print("      --Subsidy (" + subsidy_id + ") --");
	print("        IsValidSubsidy():      " + AISubsidy.IsValidSubsidy(subsidy_id));
	print("        IsAwarded():           " + AISubsidy.IsAwarded(subsidy_id));
	print("        GetAwardedTo():        " + AISubsidy.GetAwardedTo(subsidy_id));
	print("        GetExpireDate():       " + AISubsidy.GetExpireDate(subsidy_id));
	print("        GetSourceType():       " + AISubsidy.GetSourceType(subsidy_id));
	print("        GetSourceIndex():      " + AISubsidy.GetSourceIndex(subsidy_id));
	print("        GetDestinationType():  " + AISubsidy.GetDestinationType(subsidy_id));
	print("        GetDestinationIndex(): " + AISubsidy.GetDestinationIndex(subsidy_id));
	print("        GetCargoType():        " + AISubsidy.GetCargoType(subsidy_id));
}

function Regression::Math()
{
	print("");
	print("--Math--");
	print("  -2147483648 < -2147483647:   " + (-2147483648 < -2147483647));
	print("  -2147483648 < -1         :   " + (-2147483648 < -1         ));
	print("  -2147483648 <  0         :   " + (-2147483648 <  0         ));
	print("  -2147483648 <  1         :   " + (-2147483648 <  1         ));
	print("  -2147483648 <  2147483647:   " + (-2147483648 <  2147483647));

	print("  -2147483647 < -2147483648:   " + (-2147483647 < -2147483648));
	print("  -1          < -2147483648:   " + (-1          < -2147483648));
	print("   0          < -2147483648:   " + ( 0          < -2147483648));
	print("   1          < -2147483648:   " + ( 1          < -2147483648));
	print("   2147483647 < -2147483648:   " + ( 2147483647 < -2147483648));

	print("  -1          >  2147483647:   " + (-1          >  2147483647));
	print("  -1          >  1         :   " + (-1          >  1         ));
	print("  -1          >  0         :   " + (-1          >  0         ));
	print("  -1          > -1         :   " + (-1          > -1         ));
	print("  -1          > -2147483648:   " + (-1          > -2147483648));

	print("   1          >  2147483647:   " + ( 1          >  2147483647));
	print("   1          >  1         :   " + ( 1          >  1         ));
	print("   1          >  0         :   " + ( 1          >  0         ));
	print("   1          > -1         :   " + ( 1          > -1         ));
	print("   1          > -2147483648:   " + ( 1          > -2147483648));

	print("   2147483647 >  2147483646:   " + ( 2147483647 >  2147483646));
	print("   2147483647 >  1         :   " + ( 2147483647 >  1         ));
	print("   2147483647 >  0         :   " + ( 2147483647 >  0         ));
	print("   2147483647 > -1         :   " + ( 2147483647 > -1         ));
	print("   2147483647 > -2147483648:   " + ( 2147483647 > -2147483648));

	print("   2147483646 >  2147483647:   " + ( 2147483646 >  2147483647));
	print("   1          >  2147483647:   " + ( 1          >  2147483647));
	print("   0          >  2147483647:   " + ( 0          >  2147483647));
	print("  -1          >  2147483647:   " + (-1          >  2147483647));
	print("  -2147483648 >  2147483647:   " + (-2147483648 >  2147483647));

	print("   13725      > -2147483648:   " + ( 13725      > -2147483648));
}

function Regression::Start()
{
	this.TestInit();
	this.Std();
	this.Base();
	this.List();

	/* Do this first as it gains maximum loan (which is faked to quite a lot). */
	this.Company();

	this.Airport();
	this.Bridge();
	this.BridgeList();
	this.Cargo();
	this.CargoList();
	this.Engine();
	this.EngineList();
	this.Group();
	this.Industry();
	this.IndustryList();
	this.IndustryTypeList();
	this.Map();
	this.Marine();
	this.Prices();
	this.Rail();
	this.RailTypeList();
	this.Road();
	this.Sign();
	this.Station();
	this.Tile();
	this.TileList();
	this.Town();
	this.TownList();
	this.Tunnel();
	this.Vehicle();
	/* Order has to be after Vehicle */
	this.Order();
	print("");
	print("  First Subsidy Test");
	PrintSubsidy(0);

	while (AIEventController.IsEventWaiting()) {
		local e = AIEventController.GetNextEvent();
		print("  GetNextEvent:          " + (e == null ? "null" : "instance"));
		print("    GetEventType:        " + e.GetEventType());
		switch (e.GetEventType()) {
			case AIEvent.ET_SUBSIDY_OFFER: {
				local c = AIEventSubsidyOffer.Convert(e);
				print("      EventName:         SubsidyOffer");
				PrintSubsidy(c.GetSubsidyID());
			} break;

			case AIEvent.ET_VEHICLE_WAITING_IN_DEPOT: {
				local c = AIEventVehicleWaitingInDepot.Convert(e);
				print("      EventName:         VehicleWaitingInDepot");
				print("      VehicleID:         " + c.GetVehicleID());
			} break;

			default:
				print("      Unknown Event");
				break;
		}
	}
	print("  IsEventWaiting:        false");

	this.Math();
}

