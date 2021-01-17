/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file base_consist.cpp Properties for front vehicles/consists. */

#include "stdafx.h"
#include "base_consist.h"
#include "vehicle_base.h"
#include "string_func.h"

#include "safeguards.h"


/**
 * Copy properties of other BaseConsist.
 * @param src Source for copying
 */
void BaseConsist::CopyConsistPropertiesFrom(const BaseConsist *src)
{
	if (this == src) return;

	this->name = src->name;

	this->current_order_time = src->current_order_time;
	this->lateness_counter = src->lateness_counter;
	this->timetable_start = src->timetable_start;

	this->service_interval = src->service_interval;

	this->cur_real_order_index = src->cur_real_order_index;
	this->cur_implicit_order_index = src->cur_implicit_order_index;

	if (HasBit(src->vehicle_flags, VF_TIMETABLE_STARTED)) SetBit(this->vehicle_flags, VF_TIMETABLE_STARTED);
	if (HasBit(src->vehicle_flags, VF_AUTOFILL_TIMETABLE)) SetBit(this->vehicle_flags, VF_AUTOFILL_TIMETABLE);
	if (HasBit(src->vehicle_flags, VF_AUTOFILL_PRES_WAIT_TIME)) SetBit(this->vehicle_flags, VF_AUTOFILL_PRES_WAIT_TIME);
	if (HasBit(src->vehicle_flags, VF_SERVINT_IS_PERCENT) != HasBit(this->vehicle_flags, VF_SERVINT_IS_PERCENT)) {
		ToggleBit(this->vehicle_flags, VF_SERVINT_IS_PERCENT);
	}
	if (HasBit(src->vehicle_flags, VF_SERVINT_IS_CUSTOM)) SetBit(this->vehicle_flags, VF_SERVINT_IS_CUSTOM);
}

/**
 * Resets all the data used for automatic separation
 */
void BaseConsist::ResetAutomaticSeparation()
{
	this->first_order_last_departure = 0;
	this->first_order_round_trip_time = 0;
}
