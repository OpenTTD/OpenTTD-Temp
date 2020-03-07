/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file ai_changelog.hpp Lists all changes / additions to the API.
 *
 * Only new / renamed / deleted api functions will be listed here. A list of
 * bug fixes can be found in the normal changelog. Note that removed API
 * functions may still be available if you return an older API version
 * in GetAPIVersion() in info.nut.
 *
 * \b 1.12.0
 *
 * API additions:
 * \li AIMarine::BT_LOCK
 * \li AIMarine::BT_CANAL
 * \li AITile::IsSeaTile
 * \li AITile::IsRiverTile
 * \li AITile::BT_CLEAR_WATER
 *
 * This version is not yet released. The following changes are not set in stone yet.
 *
 * \b 1.11.0
 *
 * API additions:
 * \li AICargo::GetName
 * \li AIPriorityQueue
 *
 * Other changes:
 * \li AIVehicle::CloneVehicle now correctly returns estimate when short on cash
 *
 * \b 1.10.0
 *
 * API additions:
 * \li AIGroup::SetPrimaryColour
 * \li AIGroup::SetSecondaryColour
 * \li AIGroup::GetPrimaryColour
 * \li AIGroup::GetSecondaryColour
 * \li AIVehicle::BuildVehicleWithRefit
 * \li AIVehicle::GetBuildWithRefitCapacity
 * \li AIRoad::GetName
 * \li AIRoad::RoadVehCanRunOnRoad
 * \li AIRoad::RoadVehHasPowerOnRoad
 * \li AIRoad::ConvertRoadType
 * \li AIRoad::GetMaxSpeed
 * \li AIEngine::CanRunOnRoad
 * \li AIEngine::HasPowerOnRoad
 * \li AIRoadTypeList::RoadTypeList
 * \li AIEventVehicleAutoReplaced
 *
 * Other changes:
 * \li AITile::DemolishTile works without a selected company
 *
 * \b 1.9.0
 *
 * API additions:
 * \li AIAirport::GetMonthlyMaintenanceCost
 * \li AIGroup::SetParent
 * \li AIGroup::GetParent
 * \li AICompany::SetPrimaryLiveryColour
 * \li AICompany::SetSecondaryLiveryColour
 * \li AICompany::GetPrimaryLiveryColour
 * \li AICompany::GetSecondaryLiveryColour
 *
 * Other changes:
 * \li AIBridge::GetName takes one extra parameter to refer the vehicle type
 * \li AIGroup::CreateGroup gains parent_group_id parameter
 *
 * \b 1.8.0
 *
 * No changes
 *
 * API additions:
 * \li AIRoad::ERR_ROADTYPE_DISALLOWS_CROSSING
 *
 * \b 1.7.0 - 1.7.2
 *
 * No changes
 *
 * \b 1.6.1 - 1.6.0
 *
 * No changes
 *
 * \b 1.5.3 - 1.5.1
 *
 * No changes
 *
 * \b 1.5.0
 *
 * API additions:
 * \li AIList::SwapList
 * \li AIStation::GetCargoPlanned
 * \li AIStation::GetCargoPlannedFrom
 * \li AIStation::GetCargoPlannedFromVia
 * \li AIStation::GetCargoPlannedVia
 * \li AIStation::GetCargoWaitingFromVia
 * \li AIStationList_CargoPlannedByFrom
 * \li AIStationList_CargoPlannedByVia
 * \li AIStationList_CargoPlannedFromByVia
 * \li AIStationList_CargoPlannedViaByFrom
 * \li AIStationList_CargoWaitingByFrom
 * \li AIStationList_CargoWaitingByVia
 * \li AIStationList_CargoWaitingFromByVia
 * \li AIStationList_CargoWaitingViaByFrom
 *
 * \b 1.4.4 - 1.4.1
 * No changes
 *
 * \b 1.4.0
 *
 * API additions:
 * \li AICargo::GetDistributionType
 * \li AIDate::DATE_INVALID
 * \li AIDate::IsValidDate
 * \li AIStation::HasCargoRating
 * \li AIStation::GetCargoWaitingFrom
 * \li AIStation::GetCargoWaitingVia
 * \li AITile::GetTerrainType
 * \li AITown::FoundTown
 * \li AITown::GetFundBuildingsDuration
 * \li AITown::TOWN_GROWTH_NONE
 *
 * Other changes:
 * \li AIStation::GetCargoRating does return -1 for cargo-station combinations that
 *     do not have a rating yet instead of returning 69.
 *
 * \b 1.3.3 - 1.3.2
 *
 * No changes
 *
 * \b 1.3.1
 *
 * API additions:
 * \li AITile::GetTerrainType
 *
 * \b 1.3.0
 *
 * API additions:
 * \li AIEventExclusiveTransportRights
 * \li AIEventRoadReconstruction
 * \li AIIndustryType::IsProcessingIndustry
 * \li AIStation::IsAirportClosed
 * \li AIStation::OpenCloseAirport
 * \li AIController::Break
 *
 * \b 1.2.3 - 1.2.1
 *
 * No changes
 *
 * \b 1.2.0
 *
 * API additions:
 *
 * \li AIAirport::GetMaintenanceCostFactor
 * \li AICargo::CT_AUTO_REFIT
 * \li AICargo::CT_NO_REFIT
 * \li AICargo::IsValidTownEffect
 * \li AICargoList_StationAccepting
 * \li AICompany::GetQuarterlyIncome
 * \li AICompany::GetQuarterlyExpenses
 * \li AICompany::GetQuarterlyCargoDelivered
 * \li AICompany::GetQuarterlyPerformanceRating
 * \li AICompany::GetQuarterlyCompanyValue
 * \li AIController::GetOpsTillSuspend
 * \li AIEngine::GetMaximumOrderDistance
 * \li AIEventAircraftDestTooFar
 * \li AIInfo::CONFIG_DEVELOPER
 * \li AIInfrastructure
 * \li AIOrder::ERR_ORDER_AIRCRAFT_NOT_ENOUGH_RANGE
 * \li AIOrder::GetOrderDistance
 * \li AIOrder::GetOrderRefit
 * \li AIOrder::IsRefitOrder
 * \li AIOrder::SetOrderRefit
 * \li AIRail::GetMaintenanceCostFactor
 * \li AIRoad::GetMaintenanceCostFactor
 * \li AITile::GetTownAuthority
 * \li AITown::GetCargoGoal
 * \li AITown::GetGrowthRate
 * \li AITown::GetLastMonthReceived
 * \li AITownEffectList (to walk over all available town effects)
 * \li AIVehicle::ERR_VEHICLE_TOO_LONG in case vehicle length limit is reached
 * \li AIVehicle::GetMaximumOrderDistance
 *
 * API renames:
 * \li AITown::GetLastMonthTransported to AITown::GetLastMonthSupplied to better
 *     reflect what it does.
 * \li AIInfo has all its configure settings renamed from AICONFIG to just CONFIG
 *     like CONFIG_RANDOM.
 * \li AIEvent has all its types renamed from AI_ET_ prefix to just ET_ prefix,
 *     like ET_SUBSIDY_OFFER.
 * \li AIOrder has all its types renamed from AIOF_ prefix to just OF_ prefix.
 *
 * API removals:
 * \li AICompany::GetCompanyValue, use AICompany::GetQuarterlyCompanyValue instead.
 *
 * Other changes:
 * \li AITown::GetLastMonthProduction no longer has prerequisites based on town
 *     effects.
 * \li AITown::GetLastMonthTransported resp. AITown::GetLastMonthSupplied no longer has prerequisites based on
 *     town effects.
 * \li AITown::GetLastMonthTransportedPercentage no longer has prerequisites
 *     based on town effects.
 *
 * \b 1.1.5
 *
 * No changes
 *
 * \b 1.1.4
 *
 * API additions:
 * \li AIVehicle::ERR_VEHICLE_TOO_LONG in case vehicle length limit is reached.
 *
 * \b 1.1.3 - 1.1.1
 *
 * No changes
 *
 * \b 1.1.0
 *
 * API additions:
 * \li IsEnd for all lists.
 * \li AIEventTownFounded
 * \li AIIndustry::GetIndustryID
 * \li AIIndustryType::INDUSTRYTYPE_TOWN
 * \li AIIndustryType::INDUSTRYTYPE_UNKNOWN
 * \li AIOrder::IsVoidOrder
 * \li AIRail::GetName
 * \li AITown::IsCity
 *
 * API removals:
 * \li HasNext for all lists.
 * \li AIAbstractList, use AIList instead.
 * \li AIList::ChangeItem, use AIList::SetValue instead.
 * \li AIRail::ERR_NONUNIFORM_STATIONS_DISABLED, that error is never returned anymore.
 *
 * Other changes:
 * \li AIEngine::GetMaxTractiveEffort can be used for road vehicles.
 * \li AIEngine::GetPower can be used for road vehicles.
 * \li AIEngine::GetWeight can be used for road vehicles.
 * \li AIIndustry::IsCargoAccepted now returns CargoAcceptState instead of a boolean.
 * \li AIOrder::GetOrderFlags returns AIOrder::AIOF_INVALID for void orders as well.
 * \li AIRoad::BuildDriveThroughRoadStation now allows overbuilding.
 * \li AIRoad::BuildRoadStation now allows overbuilding.
 *
 * \b 1.0.5 - 1.0.4
 *
 * No changes
 *
 * \b 1.0.3
 *
 * API additions:
 * \li AIRail::ERR_RAILTYPE_DISALLOWS_CROSSING
 *
 * \b 1.0.2
 *
 * Other changes:
 * \li AIBridge::GetPrice now returns the price of the bridge without the cost for the rail or road.
 *
 * \b 1.0.1
 *
 * API additions:
 * \li AIRail::GetMaxSpeed
 *
 * \b 1.0.0
 *
 * API additions:
 * \li AIBaseStation
 * \li AIEngine::IsBuildable
 * \li AIEventCompanyAskMerger
 * \li AIIndustry::GetLastMonthTransportedPercentage
 * \li AIInfo::AICONFIG_INGAME
 * \li AIMarine::GetBuildCost
 * \li AIOrder::AIOF_GOTO_NEAREST_DEPOT
 * \li AIOrder::GetStopLocation
 * \li AIOrder::SetStopLocation
 * \li AIRail::RemoveRailStationTileRectangle
 * \li AIRail::RemoveRailWaypointTileRectangle
 * \li AIRail::GetBuildCost
 * \li AIRoad::GetBuildCost
 * \li AISubsidy::SubsidyParticipantType
 * \li AISubsidy::GetSourceType
 * \li AISubsidy::GetSourceIndex
 * \li AISubsidy::GetDestinationType
 * \li AISubsidy::GetDestinationIndex
 * \li AITile::GetBuildCost
 * \li AITown::GetLastMonthTransportedPercentage
 * \li AIVehicleList_Depot
 * \li AIWaypoint::WaypointType
 * \li AIWaypoint::HasWaypointType
 * \li Some error messages to AIWaypoint
 *
 * API removals:
 * \li AIOrder::ChangeOrder, use AIOrder::SetOrderFlags instead
 * \li AIRail::RemoveRailStationTileRect, use AIRail::RemoveRailStationTileRectangle instead
 * \li AIRail::RemoveRailWaypoint, use AIRail::RemoveRailWaypointTileRectangle instead
 * \li AISign::GetMaxSignID, use AISignList instead
 * \li AIStation::ERR_STATION_TOO_LARGE, use AIError::ERR_STATION_TOO_SPREAD_OUT instead
 * \li AISubsidy::SourceIsTown, use AISubsidy::GetSourceType instead
 * \li AISubsidy::GetSource, use AISubsidy::GetSourceIndex instead
 * \li AISubsidy::DestinationIsTown, use AISubsidy::GetDestinationType instead
 * \li AISubsidy::GetDestination, use AISubsidy::GetDestinationIndex instead
 * \li AITile::GetHeight, use AITile::GetMinHeight/GetMaxHeight/GetCornerHeight instead
 * \li AITown::GetMaxProduction, use AITown::GetLastMonthProduction instead
 * \li AIVehicle::SkipToVehicleOrder, use AIOrder::SkipToOrder instead
 * \li AIWaypoint::WAYPOINT_INVALID, use AIBaseStation::STATION_INVALID instead
 *
 * Other changes:
 * \li The GetName / SetName / GetLocation functions were moved from AIStation
 *     and AIWaypoint to AIBaseStation, but you can still use AIStation.GetName
 *     as before
 * \li The GetConstructionDate function was moved from AIStation to
 *     AIBaseStation, but can still be used as AIStation.GetConstructionDate
 * \li WaypointID was replaced by StationID. All WaypointIDs from previous
 *     savegames are invalid. Use STATION_INVALID instead of WAYPOINT_INVALID
 * \li AIWaypointList constructor now needs a WaypointType similar to AIStationList,
 *     it can also handle buoys.
 * \li AIVehicleList_Station now also works for waypoints
 * \li Stations can be build over rail without signals that is in the right
 *     direction for the to-be built station. It will also convert the rail if
 *     the station's rail type supports the old type.
 * \li GetAPIVersion() was added as function to info.nut. If it does not exist
 *     API version 0.7 is assumed. This function should return the major and
 *     minor number of the stable version of the API the AI is written against.
 *     For 0.7.2 that would be 0.7, for 1.1.3 it would be 1.1, etc.
 * \li The subsidy logic has changed. Subsidy is now awarded when cargo
 *     originating from subsidy source is delivered to station that has subsidy
 *     destination it its catchment area. One industry tile or one town house
 *     is enough as long as station accepts the cargo. Awarded subsidies are no
 *     longer bound to stations used for first delivery, any station can be
 *     used for loading and unloading as long as cargo is transferred from
 *     source to destination.
 * \li Make AIEngine:CanRefitCargo() not report refittability to mail by
 *     default for aircraft. It is not necessarily true. This means that even
 *     if the aircraft can carry mail (as secondary cargo) it does not return
 *     true if the aircraft cannot carry it as its only cargo.
 * \li Improve behaviour of AIEngine::GetCargoType(), AIEventEnginePreview::GetCargoType()
 *     and AIEngine::CanRefitCargo() for articulated vehicles. For
 *     CanRefitCargo true is returned if at least one part can be refitted.
 *     For GetCargoType the first most used cargo type is returned.
 * \li AIIndustryType::GetConstructionCost() now returns -1 if the industry is
 *     neither buildable nor prospectable.
 * \li AIEngine::IsValidEngine will now return true if you have at least one
 *     vehicle of that type in your company, regardless if it's still buildable
 *     or not. AIEngine::IsBuildable returns only true when you can actually
 *     build an engine.
 * \li AITile::GetCargoProduction will now return the number of producers,
 *     including houses instead the number of producing tiles. This means that
 *     also industries that do not have a tile within the radius, but where
 *     the search bounding box and the industry's bounding box intersect, are
 *     counted. Previously these industries (and their cargoes), although they
 *     produced cargo for a station at the given location, were not returned.
 * \li AIRail::BuildRail will now fail completely if there is an obstacle
 *     between the begin and end, instead of building up to the obstacle and
 *     returning that everything went okay.
 * \li Orders for buoys are now waypoint orders, i.e. instead of using the
 *     station orders for buoys one has to use waypoint orders.
 * \li Autoreplaces can now also be set for the default group via AIGroup.
 *
 * \b 0.7.5 - 0.7.4
 *
 * No changes
 *
 * \b 0.7.3
 *
 * API additions:
 * \li AIAbstractList::SORT_ASCENDING
 * \li AIAbstractList::SORT_DESCENDING
 * \li AIAirport::IsAirportInformationAvailable
 * \li AICompany::GetPresidentGender
 * \li AICompany::SetPresidentGender
 * \li AIEngine::GetDesignDate
 * \li AIStation::GetConstructionDate
 *
 * Other changes:
 * \li AIs are now killed when they execute a DoCommand or Sleep at a time
 *     they are not allowed to do so.
 * \li When the API requests a string as parameter you can give every squirrel
 *     type and it will be converted to a string
 * \li AIs can create subclasses of API classes and use API constants as part
 *     of their own constants
 *
 * \b 0.7.2
 *
 * API additions:
 * \li AIVehicle::GetReliability
 *
 * Other changes:
 * \li DoCommands and sleeps in call, acall, pcall and valuators are disallowed
 *
 * \b 0.7.1
 *
 * API additions:
 * \li AIAirport::GetPrice
 * \li AIController::GetVersion
 * \li AIOrder::AIOF_DEPOT_FLAGS
 * \li AIOrder::AIOF_STOP_IN_DEPOT
 * \li AIOrder::IsCurrentOrderPartOfOrderList
 * \li AIOrder::IsGotoDepotOrder
 * \li AIOrder::IsGotoStationOrder
 * \li AIOrder::IsGotoWaypointOrder
 * \li AISignList
 * \li AITile::CORNER_[WSEN]
 * \li AITile::ERR_AREA_ALREADY_FLAT
 * \li AITile::ERR_EXCAVATION_WOULD_DAMAGE
 * \li AITile::GetCornerHeight
 * \li AITile::GetMaxHeight
 * \li AITile::GetMinHeight
 * \li AIVehicle::SendVehicleToDepotForServicing
 *
 * Other changes:
 * \li GetURL() was added as optional function to info.nut
 * \li UseAsRandomAI() was added as optional function to info.nut
 * \li A limit was introduced on the time the AI spends in the constructor and Load function
 *
 * \b 0.7.0
 * \li First stable release with the NoAI framework.
 */
