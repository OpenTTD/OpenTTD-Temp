/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_clientlist.cpp Implementation of ScriptClientList and friends. */

#include "../../stdafx.h"
#include "script_clientlist.hpp"
#include "../../network/network.h"
#include "../../network/network_base.h"

#include "../../safeguards.h"

ScriptClientList::ScriptClientList()
{
#ifdef ENABLE_NETWORK
	if (!_networking) return;
	NetworkClientInfo *ci;
	FOR_ALL_CLIENT_INFOS(ci) {
		this->AddItem(ci->client_id);
	}
#endif
}

ScriptClientList_Company::ScriptClientList_Company(ScriptCompany::CompanyID company)
{
#ifdef ENABLE_NETWORK
	if (!_networking) return;
	CompanyID c = (CompanyID)company;
	if (company == ScriptCompany::COMPANY_INVALID) c = INVALID_COMPANY;

	NetworkClientInfo *ci;
	FOR_ALL_CLIENT_INFOS(ci) {
		if (ci->client_playas == c) this->AddItem(ci->client_id);
	}
#endif
}
