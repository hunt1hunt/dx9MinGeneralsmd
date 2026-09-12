/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once
#ifndef __WEAPON_FIRE_LASER_UPDATE_H
#define __WEAPON_FIRE_LASER_UPDATE_H

#include "Common/ClientUpdateModule.h"

class WeaponFireLaserUpdateModuleData : public ClientUpdateModuleData
{
public:
	WeaponFireLaserUpdateModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);

	UnsignedInt	m_weaponSlotLookup;			///< weapon slot whose fire state drives the beam
	AsciiString	m_muzzleBoneName;				///< parent bone the beam start follows (e.g. "fx01")
	UnsignedInt	m_holdFrames;						///< keep the beam up this long after the last shot
	UnsignedInt	m_decayFrames;					///< fade-out length once firing stops
	Real				m_muzzleHeight;					///< fallback muzzle lift above the shooter origin

private:
};

/** Drives the on-object LaserUpdate/W3DLaserDraw pair from weapon fire: while
* the configured weapon slot fired recently and a victim exists, the beam is
* re-targeted (muzzle bone -> victim, auto-following both objects); once
* firing stops, the beam decays away. */
class WeaponFireLaserUpdate : public ClientUpdateModule
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( WeaponFireLaserUpdate, "WeaponFireLaserUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( WeaponFireLaserUpdate, WeaponFireLaserUpdateModuleData );

public:
	WeaponFireLaserUpdate( Thing *thing, const ModuleData* moduleData );

	virtual void clientUpdate( void );

private:
	Bool m_wasFiring;
};

#endif /* __WEAPON_FIRE_LASER_UPDATE_H */
