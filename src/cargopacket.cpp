/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargopacket.cpp Implementation of the cargo packets. */

#include "stdafx.h"
#include "station_base.h"
#include "core/pool_func.hpp"
#include "core/random_func.hpp"
#include "economy_base.h"
#include "cargoaction.h"
#include "order_type.h"

#include "safeguards.h"

/* Initialize the cargopacket-pool */
CargoPacketPool _cargopacket_pool("CargoPacket");
INSTANTIATE_POOL_METHODS(CargoPacket)

/**
 * Create a new packet for savegame loading.
 */
CargoPacket::CargoPacket()
{
	this->source_type = ST_INDUSTRY;
	this->source_id   = INVALID_SOURCE;
}

/**
 * Creates a new cargo packet.
 * @param source      Source station of the packet.
 * @param source_xy   Source location of the packet.
 * @param count       Number of cargo entities to put in this packet.
 * @param source_type 'Type' of source the packet comes from (for subsidies).
 * @param source_id   Actual source of the packet (for subsidies).
 * @pre count != 0
 * @note We have to zero memory ourselves here because we are using a 'new'
 * that, in contrary to all other pools, does not memset to 0.
 */
CargoPacket::CargoPacket(StationID source, TileIndex source_xy, uint16 count, SourceType source_type, SourceID source_id) :
	feeder_share(0),
	count(count),
	days_in_transit(0),
	source_id(source_id),
	source(source),
	source_xy(source_xy),
	loaded_at_xy(0)
{
	assert(count != 0);
	this->source_type  = source_type;
}

/**
 * Creates a new cargo packet. Initializes the fields that cannot be changed later.
 * Used when loading or splitting packets.
 * @param count           Number of cargo entities to put in this packet.
 * @param days_in_transit Number of days the cargo has been in transit.
 * @param source          Station the cargo was initially loaded.
 * @param source_xy       Station location the cargo was initially loaded.
 * @param loaded_at_xy    Location the cargo was loaded last.
 * @param feeder_share    Feeder share the packet has already accumulated.
 * @param source_type     'Type' of source the packet comes from (for subsidies).
 * @param source_id       Actual source of the packet (for subsidies).
 * @note We have to zero memory ourselves here because we are using a 'new'
 * that, in contrary to all other pools, does not memset to 0.
 */
CargoPacket::CargoPacket(uint16 count, byte days_in_transit, StationID source, TileIndex source_xy, TileIndex loaded_at_xy, Money feeder_share, SourceType source_type, SourceID source_id) :
		feeder_share(feeder_share),
		count(count),
		days_in_transit(days_in_transit),
		source_id(source_id),
		source(source),
		source_xy(source_xy),
		loaded_at_xy(loaded_at_xy)
{
	assert(count != 0);
	this->source_type = source_type;
}

/**
 * Split this packet in two and return the split off part.
 * @param new_size Size of the split part.
 * @return Split off part, or nullptr if no packet could be allocated!
 */
CargoPacket *CargoPacket::Split(uint new_size)
{
	if (!CargoPacket::CanAllocateItem()) return nullptr;

	Money fs = this->FeederShare(new_size);
	CargoPacket *cp_new = new CargoPacket(new_size, this->days_in_transit, this->source, this->source_xy, this->loaded_at_xy, fs, this->source_type, this->source_id);
	this->feeder_share -= fs;
	this->count -= new_size;
	return cp_new;
}

/**
 * Merge another packet into this one.
 * @param cp Packet to be merged in.
 */
void CargoPacket::Merge(CargoPacket *cp)
{
	this->count += cp->count;
	this->feeder_share += cp->feeder_share;
	delete cp;
}

/**
 * Reduce the packet by the given amount and remove the feeder share.
 * @param count Amount to be removed.
 */
void CargoPacket::Reduce(uint count)
{
	assert(count < this->count);
	this->feeder_share -= this->FeederShare(count);
	this->count -= count;
}

/**
 * Invalidates (sets source_id to INVALID_SOURCE) all cargo packets from given source.
 * @param src_type Type of source.
 * @param src Index of source.
 */
/* static */ void CargoPacket::InvalidateAllFrom(SourceType src_type, SourceID src)
{
	for (CargoPacket *cp : CargoPacket::Iterate()) {
		if (cp->source_type == src_type && cp->source_id == src) cp->source_id = INVALID_SOURCE;
	}
}

/**
 * Invalidates (sets source to INVALID_STATION) all cargo packets from given station.
 * @param sid Station that gets removed.
 */
/* static */ void CargoPacket::InvalidateAllFrom(StationID sid)
{
	for (CargoPacket *cp : CargoPacket::Iterate()) {
		if (cp->source == sid) cp->source = INVALID_STATION;
	}
}

/*
 *
 * Cargo list implementation
 *
 */

/**
 * Destroy the cargolist ("frees" all cargo packets).
 */
template <class Tinst, class Tcont>
CargoList<Tinst, Tcont>::~CargoList()
{
	for (Iterator it(this->packets.begin()); it != this->packets.end(); ++it) {
		delete *it;
	}
}

/**
 * Empty the cargo list, but don't free the cargo packets;
 * the cargo packets are cleaned by CargoPacket's CleanPool.
 */
template <class Tinst, class Tcont>
void CargoList<Tinst, Tcont>::OnCleanPool()
{
	this->packets.clear();
}

/**
 * Update the cached values to reflect the removal of this packet or part of it.
 * Decreases count and days_in_transit.
 * @param cp Packet to be removed from cache.
 * @param count Amount of cargo from the given packet to be removed.
 */
template <class Tinst, class Tcont>
void CargoList<Tinst, Tcont>::RemoveFromCache(const CargoPacket *cp, uint count)
{
	assert(count <= cp->count);
	this->count                 -= count;
	this->cargo_days_in_transit -= cp->days_in_transit * count;
}

/**
 * Update the cache to reflect adding of this packet.
 * Increases count and days_in_transit.
 * @param cp New packet to be inserted.
 */
template <class Tinst, class Tcont>
void CargoList<Tinst, Tcont>::AddToCache(const CargoPacket *cp)
{
	this->count                 += cp->count;
	this->cargo_days_in_transit += cp->days_in_transit * cp->count;
}

/** Invalidates the cached data and rebuilds it. */
template <class Tinst, class Tcont>
void CargoList<Tinst, Tcont>::InvalidateCache()
{
	this->count = 0;
	this->cargo_days_in_transit = 0;

	for (ConstIterator it(this->packets.begin()); it != this->packets.end(); it++) {
		static_cast<Tinst *>(this)->AddToCache(*it);
	}
}

/**
 * Tries to merge the second packet into the first and return if that was
 * successful.
 * @param icp Packet to be merged into.
 * @param cp Packet to be eliminated.
 * @return If the packets could be merged.
 */
template <class Tinst, class Tcont>
/* static */ bool CargoList<Tinst, Tcont>::TryMerge(CargoPacket *icp, CargoPacket *cp)
{
	if (Tinst::AreMergable(icp, cp) &&
			icp->count + cp->count <= CargoPacket::MAX_COUNT) {
		icp->Merge(cp);
		return true;
	} else {
		return false;
	}
}

/*
 *
 * Vehicle cargo list implementation.
 *
 */

/**
 * Appends the given cargo packet. Tries to merge it with another one in the
 * packets list. If no fitting packet is found, appends it. You can only append
 * packets to the ranges of packets designated for keeping or loading.
 * Furthermore if there are already packets reserved for loading you cannot
 * directly add packets to the "keep" list. You first have to load the reserved
 * ones.
 * @warning After appending this packet may not exist anymore!
 * @note Do not use the cargo packet anymore after it has been appended to this CargoList!
 * @param cp Cargo packet to add.
 * @param action Either MTA_KEEP if you want to add the packet directly or MTA_LOAD
 * if you want to reserve it first.
 * @pre cp != nullptr
 * @pre action == MTA_LOAD || (action == MTA_KEEP && this->designation_counts[MTA_LOAD] == 0)
 */
void VehicleCargoList::Append(CargoPacket *cp, MoveToAction action)
{
	assert(cp != nullptr);
	assert(action == MTA_LOAD ||
			(action == MTA_KEEP && this->action_counts[MTA_LOAD] == 0));
	this->AddToMeta(cp, action);

	if (this->count == cp->count) {
		this->packets.push_back(cp);
		return;
	}

	uint sum = cp->count;
	for (ReverseIterator it(this->packets.rbegin()); it != this->packets.rend(); it++) {
		CargoPacket *icp = *it;
		if (VehicleCargoList::TryMerge(icp, cp)) return;
		sum += icp->count;
		if (sum >= this->action_counts[action]) {
			this->packets.push_back(cp);
			return;
		}
	}

	NOT_REACHED();
}

/**
 * Shifts cargo from the front of the packet list and applies some action to it.
 * @tparam Taction Action class or function to be used. It should define
 *                 "bool operator()(CargoPacket *)". If true is returned the
 *                 cargo packet will be removed from the list. Otherwise it
 *                 will be kept and the loop will be aborted.
 * @param action Action instance to be applied.
 */
template<class Taction>
void VehicleCargoList::ShiftCargo(Taction action)
{
	Iterator it(this->packets.begin());
	while (it != this->packets.end() && action.MaxMove() > 0) {
		CargoPacket *cp = *it;
		if (action(cp)) {
			it = this->packets.erase(it);
		} else {
			break;
		}
	}
}

/**
 * Pops cargo from the back of the packet list and applies some action to it.
 * @tparam Taction Action class or function to be used. It should define
 *                 "bool operator()(CargoPacket *)". If true is returned the
 *                 cargo packet will be removed from the list. Otherwise it
 *                 will be kept and the loop will be aborted.
 * @param action Action instance to be applied.
 */
template<class Taction>
void VehicleCargoList::PopCargo(Taction action)
{
	if (this->packets.empty()) return;
	Iterator it(--(this->packets.end()));
	Iterator begin(this->packets.begin());
	while (action.MaxMove() > 0) {
		CargoPacket *cp = *it;
		if (action(cp)) {
			if (it != begin) {
				this->packets.erase(it--);
			} else {
				this->packets.erase(it);
				break;
			}
		} else {
			break;
		}
	}
}

/**
 * Update the cached values to reflect the removal of this packet or part of it.
 * Decreases count, feeder share and days_in_transit.
 * @param cp Packet to be removed from cache.
 * @param count Amount of cargo from the given packet to be removed.
 */
void VehicleCargoList::RemoveFromCache(const CargoPacket *cp, uint count)
{
	this->feeder_share -= cp->FeederShare(count);
	this->Parent::RemoveFromCache(cp, count);
}

/**
 * Update the cache to reflect adding of this packet.
 * Increases count, feeder share and days_in_transit.
 * @param cp New packet to be inserted.
 */
void VehicleCargoList::AddToCache(const CargoPacket *cp)
{
	this->feeder_share += cp->feeder_share;
	this->Parent::AddToCache(cp);
}

/**
 * Removes a packet or part of it from the metadata.
 * @param cp Packet to be removed.
 * @param action MoveToAction of the packet (for updating the counts).
 * @param count Amount of cargo to be removed.
 */
void VehicleCargoList::RemoveFromMeta(const CargoPacket *cp, MoveToAction action, uint count)
{
	assert(count <= this->action_counts[action]);
	this->AssertCountConsistency();
	this->RemoveFromCache(cp, count);
	this->action_counts[action] -= count;
	this->AssertCountConsistency();
}

/**
 * Adds a packet to the metadata.
 * @param cp Packet to be added.
 * @param action MoveToAction of the packet.
 */
void VehicleCargoList::AddToMeta(const CargoPacket *cp, MoveToAction action)
{
	this->AssertCountConsistency();
	this->AddToCache(cp);
	this->action_counts[action] += cp->count;
	this->AssertCountConsistency();
}

/**
 * Ages the all cargo in this list.
 */
void VehicleCargoList::AgeCargo()
{
	for (ConstIterator it(this->packets.begin()); it != this->packets.end(); it++) {
		CargoPacket *cp = *it;
		/* If we're at the maximum, then we can't increase no more. */
		if (cp->days_in_transit == UINT16_MAX) continue;

		cp->days_in_transit++;
		this->cargo_days_in_transit += cp->count;
	}
}

/**
 * Sets loaded_at_xy to the current station for all cargo to be transferred.
 * This is done when stopping or skipping while the vehicle is unloading. In
 * that case the vehicle will get part of its transfer credits early and it may
 * get more transfer credits than it's entitled to.
 * @param xy New loaded_at_xy for the cargo.
 */
void VehicleCargoList::SetTransferLoadPlace(TileIndex xy)
{
	uint sum = 0;
	for (Iterator it = this->packets.begin(); sum < this->action_counts[MTA_TRANSFER]; ++it) {
		CargoPacket *cp = *it;
		cp->loaded_at_xy = xy;
		sum += cp->count;
	}
}

/**
 * Choose action to be performed with the given cargo packet.
 * @param cp The packet.
 * @param cargo_next Next hop the cargo wants to pass.
 * @param current_station Current station of the vehicle carrying the cargo.
 * @param accepted If the cargo is accepted at the current station.
 * @param next_station Next station(s) the vehicle may stop at.
 * @return MoveToAction to be performed.
 */
/* static */ VehicleCargoList::MoveToAction VehicleCargoList::ChooseAction(const CargoPacket *cp, StationID cargo_next,
		StationID current_station, bool accepted, StationIDStack next_station)
{
	if (cargo_next == INVALID_STATION) {
		return (accepted && cp->source != current_station) ? MTA_DELIVER : MTA_KEEP;
	} else if (cargo_next == current_station) {
		return MTA_DELIVER;
	} else if (next_station.Contains(cargo_next)) {
		return MTA_KEEP;
	} else {
		return MTA_TRANSFER;
	}
}

/**
 * Stages cargo for unloading. The cargo is sorted so that packets to be
 * transferred, delivered or kept are in consecutive chunks in the list. At the
 * same time the designation_counts are updated to reflect the size of those
 * chunks.
 * @param accepted If the cargo will be accepted at the station.
 * @param current_station ID of the station.
 * @param next_station ID of the station the vehicle will go to next.
 * @param order_flags OrderUnloadFlags that will apply to the unload operation.
 * @param ge GoodsEntry for getting the flows.
 * @param payment Payment object for registering transfers.
 * return If any cargo will be unloaded.
 */
bool VehicleCargoList::Stage(bool accepted, StationID current_station, StationIDStack next_station, uint8 order_flags, const GoodsEntry *ge, CargoPayment *payment)
{
	this->AssertCountConsistency();
	assert(this->action_counts[MTA_LOAD] == 0);
	this->action_counts[MTA_TRANSFER] = this->action_counts[MTA_DELIVER] = this->action_counts[MTA_KEEP] = 0;
	Iterator deliver = this->packets.end();
	Iterator it = this->packets.begin();
	uint sum = 0;

	bool force_keep = (order_flags & OUFB_NO_UNLOAD) != 0;
	bool force_unload = (order_flags & OUFB_UNLOAD) != 0;
	bool force_transfer = (order_flags & (OUFB_TRANSFER | OUFB_UNLOAD)) != 0;
	assert(this->count > 0 || it == this->packets.end());
	while (sum < this->count) {
		CargoPacket *cp = *it;

		this->packets.erase(it++);
		StationID cargo_next = INVALID_STATION;
		MoveToAction action = MTA_LOAD;
		if (force_keep) {
			action = MTA_KEEP;
		} else if (force_unload && accepted && cp->source != current_station) {
			action = MTA_DELIVER;
		} else if (force_transfer) {
			action = MTA_TRANSFER;
			/* We cannot send the cargo to any of the possible next hops and
			 * also not to the current station. */
			FlowStatMap::const_iterator flow_it(ge->flows.find(cp->source));
			if (flow_it == ge->flows.end()) {
				cargo_next = INVALID_STATION;
			} else {
				FlowStat new_shares = flow_it->second;
				new_shares.ChangeShare(current_station, INT_MIN);
				StationIDStack excluded = next_station;
				while (!excluded.IsEmpty() && !new_shares.GetShares()->empty()) {
					new_shares.ChangeShare(excluded.Pop(), INT_MIN);
				}
				if (new_shares.GetShares()->empty()) {
					cargo_next = INVALID_STATION;
				} else {
					cargo_next = new_shares.GetVia();
				}
			}
		} else {
			/* Rewrite an invalid source station to some random other one to
			 * avoid keeping the cargo in the vehicle forever. */
			if (cp->source == INVALID_STATION && !ge->flows.empty()) {
				cp->source = ge->flows.begin()->first;
			}
			bool restricted = false;
			FlowStatMap::const_iterator flow_it(ge->flows.find(cp->source));
			if (flow_it == ge->flows.end()) {
				cargo_next = INVALID_STATION;
			} else {
				cargo_next = flow_it->second.GetViaWithRestricted(restricted);
			}
			action = VehicleCargoList::ChooseAction(cp, cargo_next, current_station, accepted, next_station);
			if (restricted && action == MTA_TRANSFER) {
				/* If the flow is restricted we can't transfer to it. Choose an
				 * unrestricted one instead. */
				cargo_next = flow_it->second.GetVia();
				action = VehicleCargoList::ChooseAction(cp, cargo_next, current_station, accepted, next_station);
			}
		}
		Money share;
		switch (action) {
			case MTA_KEEP:
				this->packets.push_back(cp);
				if (deliver == this->packets.end()) --deliver;
				break;
			case MTA_DELIVER:
				this->packets.insert(deliver, cp);
				break;
			case MTA_TRANSFER:
				this->packets.push_front(cp);
				/* Add feeder share here to allow reusing field for next station. */
				share = payment->PayTransfer(cp, cp->count);
				cp->AddFeederShare(share);
				this->feeder_share += share;
				cp->next_station = cargo_next;
				break;
			default:
				NOT_REACHED();
		}
		this->action_counts[action] += cp->count;
		sum += cp->count;
	}
	this->AssertCountConsistency();
	return this->action_counts[MTA_DELIVER] > 0 || this->action_counts[MTA_TRANSFER] > 0;
}

/** Invalidates the cached data and rebuild it. */
void VehicleCargoList::InvalidateCache()
{
	this->feeder_share = 0;
	this->Parent::InvalidateCache();
}

/**
 * Moves some cargo from one designation to another. You can only move
 * between adjacent designations. E.g. you can keep cargo that was previously
 * reserved (MTA_LOAD), but you can't reserve cargo that's marked as to be
 * delivered. Furthermore, as this method doesn't change the actual packets,
 * you cannot move cargo from or to MTA_TRANSFER. You need a specialized
 * template method for that.
 * @tparam from Previous designation of cargo.
 * @tparam to New designation of cargo.
 * @param max_move Maximum amount of cargo to reassign.
 * @return Amount of cargo actually reassigned.
 */
template<VehicleCargoList::MoveToAction Tfrom, VehicleCargoList::MoveToAction Tto>
uint VehicleCargoList::Reassign(uint max_move, TileOrStationID)
{
	static_assert(Tfrom != MTA_TRANSFER && Tto != MTA_TRANSFER);
	static_assert(Tfrom - Tto == 1 || Tto - Tfrom == 1);
	max_move = std::min(this->action_counts[Tfrom], max_move);
	this->action_counts[Tfrom] -= max_move;
	this->action_counts[Tto] += max_move;
	return max_move;
}

/**
 * Reassign cargo from MTA_DELIVER to MTA_TRANSFER and take care of the next
 * station the cargo wants to visit.
 * @param max_move Maximum amount of cargo to reassign.
 * @param next_station Station to record as next hop in the reassigned packets.
 * @return Amount of cargo actually reassigned.
 */
template<>
uint VehicleCargoList::Reassign<VehicleCargoList::MTA_DELIVER, VehicleCargoList::MTA_TRANSFER>(uint max_move, TileOrStationID next_station)
{
	max_move = std::min(this->action_counts[MTA_DELIVER], max_move);

	uint sum = 0;
	for (Iterator it(this->packets.begin()); sum < this->action_counts[MTA_TRANSFER] + max_move;) {
		CargoPacket *cp = *it++;
		sum += cp->Count();
		if (sum <= this->action_counts[MTA_TRANSFER]) continue;
		if (sum > this->action_counts[MTA_TRANSFER] + max_move) {
			CargoPacket *cp_split = cp->Split(sum - this->action_counts[MTA_TRANSFER] + max_move);
			sum -= cp_split->Count();
			this->packets.insert(it, cp_split);
		}
		cp->next_station = next_station;
	}

	this->action_counts[MTA_DELIVER] -= max_move;
	this->action_counts[MTA_TRANSFER] += max_move;
	return max_move;
}

/**
 * Returns reserved cargo to the station and removes it from the cache.
 * @param max_move Maximum amount of cargo to move.
 * @param dest Station the cargo is returned to.
 * @param next ID of the next station the cargo wants to go to.
 * @return Amount of cargo actually returned.
 */
uint VehicleCargoList::Return(uint max_move, StationCargoList *dest, StationID next)
{
	max_move = std::min(this->action_counts[MTA_LOAD], max_move);
	this->PopCargo(CargoReturn(this, dest, max_move, next));
	return max_move;
}

/**
 * Shifts cargo between two vehicles.
 * @param dest Other vehicle's cargo list.
 * @param max_move Maximum amount of cargo to be moved.
 * @return Amount of cargo actually moved.
 */
uint VehicleCargoList::Shift(uint max_move, VehicleCargoList *dest)
{
	max_move = std::min(this->count, max_move);
	this->PopCargo(CargoShift(this, dest, max_move));
	return max_move;
}

/**
 * Unloads cargo at the given station. Deliver or transfer, depending on the
 * ranges defined by designation_counts.
 * @param dest StationCargoList to add transferred cargo to.
 * @param max_move Maximum amount of cargo to move.
 * @param payment Payment object to register payments in.
 * @return Amount of cargo actually unloaded.
 */
uint VehicleCargoList::Unload(uint max_move, StationCargoList *dest, CargoPayment *payment)
{
	uint moved = 0;
	if (this->action_counts[MTA_TRANSFER] > 0) {
		uint move = std::min(this->action_counts[MTA_TRANSFER], max_move);
		this->ShiftCargo(CargoTransfer(this, dest, move));
		moved += move;
	}
	if (this->action_counts[MTA_TRANSFER] == 0 && this->action_counts[MTA_DELIVER] > 0 && moved < max_move) {
		uint move = std::min(this->action_counts[MTA_DELIVER], max_move - moved);
		this->ShiftCargo(CargoDelivery(this, move, payment));
		moved += move;
	}
	return moved;
}

/**
 * Truncates the cargo in this list to the given amount. It leaves the
 * first cargo entities and removes max_move from the back of the list.
 * @param max_move Maximum amount of entities to be removed from the list.
 * @return Amount of entities actually moved.
 */
uint VehicleCargoList::Truncate(uint max_move)
{
	max_move = std::min(this->count, max_move);
	this->PopCargo(CargoRemoval<VehicleCargoList>(this, max_move));
	return max_move;
}

/**
 * Routes packets with station "avoid" as next hop to a different place.
 * @param max_move Maximum amount of cargo to move.
 * @param dest List to prepend the cargo to.
 * @param avoid Station to exclude from routing and current next hop of packets to reroute.
 * @param avoid2 Additional station to exclude from routing.
 * @param ge GoodsEntry to get the routing info from.
 */
uint VehicleCargoList::Reroute(uint max_move, VehicleCargoList *dest, StationID avoid, StationID avoid2, const GoodsEntry *ge)
{
	max_move = std::min(this->action_counts[MTA_TRANSFER], max_move);
	this->ShiftCargo(VehicleCargoReroute(this, dest, max_move, avoid, avoid2, ge));
	return max_move;
}

/*
 *
 * Station cargo list implementation.
 *
 */

/**
 * Appends the given cargo packet to the range of packets with the same next station
 * @warning After appending this packet may not exist anymore!
 * @note Do not use the cargo packet anymore after it has been appended to this CargoList!
 * @param next the next hop
 * @param cp the cargo packet to add
 * @pre cp != nullptr
 */
void StationCargoList::Append(CargoPacket *cp, StationID next)
{
	assert(cp != nullptr);
	this->AddToCache(cp);

	StationCargoPacketMap::List &list = this->packets[next];
	for (StationCargoPacketMap::List::reverse_iterator it(list.rbegin());
			it != list.rend(); it++) {
		if (StationCargoList::TryMerge(*it, cp)) return;
	}

	/* The packet could not be merged with another one */
	list.push_back(cp);
}

/**
 * Shifts cargo from the front of the packet list for a specific station and
 * applies some action to it.
 * @tparam Taction Action class or function to be used. It should define
 *                 "bool operator()(CargoPacket *)". If true is returned the
 *                 cargo packet will be removed from the list. Otherwise it
 *                 will be kept and the loop will be aborted.
 * @param action Action instance to be applied.
 * @param next Next hop the cargo wants to visit.
 * @return True if all packets with the given next hop have been removed,
 *         False otherwise.
 */
template <class Taction>
bool StationCargoList::ShiftCargo(Taction &action, StationID next)
{
	std::pair<Iterator, Iterator> range(this->packets.equal_range(next));
	for (Iterator it(range.first); it != range.second && it.GetKey() == next;) {
		if (action.MaxMove() == 0) return false;
		CargoPacket *cp = *it;
		if (action(cp)) {
			it = this->packets.erase(it);
		} else {
			return false;
		}
	}
	return true;
}

/**
 * Shifts cargo from the front of the packet list for a specific station and
 * and optional also from the list for "any station", then applies some action
 * to it.
 * @tparam Taction Action class or function to be used. It should define
 *                 "bool operator()(CargoPacket *)". If true is returned the
 *                 cargo packet will be removed from the list. Otherwise it
 *                 will be kept and the loop will be aborted.
 * @param action Action instance to be applied.
 * @param next Next hop the cargo wants to visit.
 * @param include_invalid If cargo from the INVALID_STATION list should be
 *                        used if necessary.
 * @return Amount of cargo actually moved.
 */
template <class Taction>
uint StationCargoList::ShiftCargo(Taction action, StationIDStack next, bool include_invalid)
{
	uint max_move = action.MaxMove();
	while (!next.IsEmpty()) {
		this->ShiftCargo(action, next.Pop());
		if (action.MaxMove() == 0) break;
	}
	if (include_invalid && action.MaxMove() > 0) {
		this->ShiftCargo(action, INVALID_STATION);
	}
	return max_move - action.MaxMove();
}

/**
 * Truncates where each destination loses roughly the same percentage of its
 * cargo. This is done by randomizing the selection of packets to be removed.
 * Optionally count the cargo by origin station.
 * @param max_move Maximum amount of cargo to remove.
 * @param cargo_per_source Container for counting the cargo by origin.
 * @return Amount of cargo actually moved.
 */
uint StationCargoList::Truncate(uint max_move, StationCargoAmountMap *cargo_per_source)
{
	max_move = std::min(max_move, this->count);
	uint prev_count = this->count;
	uint moved = 0;
	uint loop = 0;
	bool do_count = cargo_per_source != nullptr;
	while (max_move > moved) {
		for (Iterator it(this->packets.begin()); it != this->packets.end();) {
			CargoPacket *cp = *it;
			if (prev_count > max_move && RandomRange(prev_count) < prev_count - max_move) {
				if (do_count && loop == 0) {
					(*cargo_per_source)[cp->source] += cp->count;
				}
				++it;
				continue;
			}
			uint diff = max_move - moved;
			if (cp->count > diff) {
				if (diff > 0) {
					this->RemoveFromCache(cp, diff);
					cp->Reduce(diff);
					moved += diff;
				}
				if (loop > 0) {
					if (do_count) (*cargo_per_source)[cp->source] -= diff;
					return moved;
				} else {
					if (do_count) (*cargo_per_source)[cp->source] += cp->count;
					++it;
				}
			} else {
				it = this->packets.erase(it);
				if (do_count && loop > 0) {
					(*cargo_per_source)[cp->source] -= cp->count;
				}
				moved += cp->count;
				this->RemoveFromCache(cp, cp->count);
				delete cp;
			}
		}
		loop++;
	}
	return moved;
}

/**
 * Reserves cargo for loading onto the vehicle.
 * @param max_move Maximum amount of cargo to reserve.
 * @param dest VehicleCargoList to reserve for.
 * @param load_place Tile index of the current station.
 * @param next_station Next station(s) the loading vehicle will visit.
 * @return Amount of cargo actually reserved.
 */
uint StationCargoList::Reserve(uint max_move, VehicleCargoList *dest, TileIndex load_place, StationIDStack next_station)
{
	return this->ShiftCargo(CargoReservation(this, dest, max_move, load_place), next_station, true);
}

/**
 * Loads cargo onto a vehicle. If the vehicle has reserved cargo load that.
 * Otherwise load cargo from the station.
 * @param max_move Amount of cargo to load.
 * @param dest Vehicle cargo list where the cargo resides.
 * @param load_place The new loaded_at_xy to be assigned to packets being moved.
 * @param next_station Next station(s) the loading vehicle will visit.
 * @return Amount of cargo actually loaded.
 * @note Vehicles may or may not reserve, depending on their orders. The two
 *       modes of loading are exclusive, though. If cargo is reserved we don't
 *       need to load unreserved cargo.
 */
uint StationCargoList::Load(uint max_move, VehicleCargoList *dest, TileIndex load_place, StationIDStack next_station)
{
	uint move = std::min(dest->ActionCount(VehicleCargoList::MTA_LOAD), max_move);
	if (move > 0) {
		this->reserved_count -= move;
		dest->Reassign<VehicleCargoList::MTA_LOAD, VehicleCargoList::MTA_KEEP>(move);
		return move;
	} else {
		return this->ShiftCargo(CargoLoad(this, dest, max_move, load_place), next_station, true);
	}
}

/**
 * Routes packets with station "avoid" as next hop to a different place.
 * @param max_move Maximum amount of cargo to move.
 * @param dest List to append the cargo to.
 * @param avoid Station to exclude from routing and current next hop of packets to reroute.
 * @param avoid2 Additional station to exclude from routing.
 * @param ge GoodsEntry to get the routing info from.
 */
uint StationCargoList::Reroute(uint max_move, StationCargoList *dest, StationID avoid, StationID avoid2, const GoodsEntry *ge)
{
	return this->ShiftCargo(StationCargoReroute(this, dest, max_move, avoid, avoid2, ge), avoid, false);
}

/**
 * Ages all in-flight cargo in this list.
 */
void StationCargoList::AgeCargo()
{
	for (ConstIterator it(this->packets.begin()); it != this->packets.end(); it++) {
		CargoPacket *cp = *it;
		/* Only age cargo that is already in transit. Also, make sure to now overflow. */
		if (cp->days_in_transit == 0 || cp->days_in_transit == UINT16_MAX) continue;

		cp->days_in_transit++;
		this->cargo_days_in_transit += cp->count;
	}
}

/*
 * We have to instantiate everything we want to be usable.
 */
template class CargoList<VehicleCargoList, CargoPacketList>;
template class CargoList<StationCargoList, StationCargoPacketMap>;
template uint VehicleCargoList::Reassign<VehicleCargoList::MTA_DELIVER, VehicleCargoList::MTA_KEEP>(uint, TileOrStationID);
