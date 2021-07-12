/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_properties.h Properties of NewGRF Action 0. */

#ifndef NEWGRF_PROPERTIES_H
#define NEWGRF_PROPERTIES_H

/**
 * List of NewGRF properties used in Action 0 or Callback 0x36 (CBID_VEHICLE_MODIFY_PROPERTY).
 * Names are formatted as PROP_<CLASS>_<NAME>
 * @todo Currently the list only contains properties which are used more than once in the code. I.e. they are available for callback 0x36.
 */
enum PropertyID {
	PROP_VEHICLE_LOAD_AMOUNT                    = 0x07, ///< Loading speed

	PROP_TRAIN_SPEED                            = 0x09, ///< Max. speed: 1 unit = 1/1.6 mph = 1 km-ish/h
	PROP_TRAIN_POWER                            = 0x0B, ///< Power in hp          (if dualheaded: sum of both vehicles)
	PROP_TRAIN_RUNNING_COST_FACTOR              = 0x0D, ///< Yearly runningcost   (if dualheaded: sum of both vehicles)
	PROP_TRAIN_CARGO_CAPACITY                   = 0x14, ///< Capacity             (if dualheaded: for each single vehicle)
	PROP_TRAIN_WEIGHT                           = 0x16, ///< Weight in t          (if dualheaded: for each single vehicle)
	PROP_TRAIN_COST_FACTOR                      = 0x17, ///< Purchase cost        (if dualheaded: sum of both vehicles)
	PROP_TRAIN_TRACTIVE_EFFORT                  = 0x1F, ///< Tractive effort coefficient in 1/256
	PROP_TRAIN_SHORTEN_FACTOR                   = 0x21, ///< Shorter vehicles
	PROP_TRAIN_USER_DATA                        = 0x25, ///< User defined data for vehicle variable 0x42
	PROP_TRAIN_CARGO_AGE_PERIOD                 = 0x2B, ///< Number of ticks before carried cargo is aged
	PROP_TRAIN_CURVE_SPEED_MOD                  = 0x2E, ///< Modifier to maximum speed in curves

	PROP_ROADVEH_RUNNING_COST_FACTOR            = 0x09, ///< Yearly runningcost
	PROP_ROADVEH_CARGO_CAPACITY                 = 0x0F, ///< Capacity
	PROP_ROADVEH_COST_FACTOR                    = 0x11, ///< Purchase cost
	PROP_ROADVEH_POWER                          = 0x13, ///< Power in 10 HP
	PROP_ROADVEH_WEIGHT                         = 0x14, ///< Weight in 1/4 t
	PROP_ROADVEH_SPEED                          = 0x15, ///< Max. speed: 1 unit = 1/0.8 mph = 2 km-ish/h
	PROP_ROADVEH_TRACTIVE_EFFORT                = 0x18, ///< Tractive effort coefficient in 1/256
	PROP_ROADVEH_CARGO_AGE_PERIOD               = 0x22, ///< Number of ticks before carried cargo is aged
	PROP_ROADVEH_SHORTEN_FACTOR                 = 0x23, ///< Shorter vehicles

	PROP_SHIP_COST_FACTOR                       = 0x0A, ///< Purchase cost
	PROP_SHIP_SPEED                             = 0x0B, ///< Max. speed: 1 unit = 1/3.2 mph = 0.5 km-ish/h
	PROP_SHIP_CARGO_CAPACITY                    = 0x0D, ///< Capacity
	PROP_SHIP_RUNNING_COST_FACTOR               = 0x0F, ///< Yearly runningcost
	PROP_SHIP_CARGO_AGE_PERIOD                  = 0x1D, ///< Number of ticks before carried cargo is aged

	PROP_AIRCRAFT_COST_FACTOR                   = 0x0B, ///< Purchase cost
	PROP_AIRCRAFT_SPEED                         = 0x0C, ///< Max. speed: 1 unit = 8 mph = 12.8 km-ish/h
	PROP_AIRCRAFT_RUNNING_COST_FACTOR           = 0x0E, ///< Yearly runningcost
	PROP_AIRCRAFT_PASSENGER_CAPACITY            = 0x0F, ///< Passenger Capacity
	PROP_AIRCRAFT_MAIL_CAPACITY                 = 0x11, ///< Mail Capacity
	PROP_AIRCRAFT_CARGO_AGE_PERIOD              = 0x1C, ///< Number of ticks before carried cargo is aged
	PROP_AIRCRAFT_RANGE                         = 0x1F, ///< Aircraft range
};

#endif /* NEWGRF_PROPERTIES_H */
