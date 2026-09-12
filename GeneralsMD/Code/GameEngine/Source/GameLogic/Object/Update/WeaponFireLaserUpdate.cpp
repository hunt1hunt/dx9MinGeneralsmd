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

/*****************************************************************************
 *             C O N F I D E N T I A L --- W E S T W O O D  S T U D I O S   *
 *****************************************************************************
 *                                                                           *
 *                 Project Name : WeaponFireLaserUpdate                      *
 *                                                                           *
 *                     $Archive::                                          $*
 *                                                                           *
 *                      Author:: ZCode (2026-09-12)                          *
 *                                                                           *
 *---------------------------------------------------------------------------------
 * ④ STARRY LASER driver: while the configured weapon slot fired recently and
 * a victim exists, the object's own LaserUpdate is re-targeted every frame
 * (muzzle bone -> victim, auto-following both - the LaserUpdate records the
 * parent/target IDs). When firing stops the beam decays via setDecayFrames.
 * The visual is drawn by the companion W3DLaserDraw module; a Texture name
 * containing "starry" additionally engages the SegLineRenderer starry PS.
 ******************************************************************************/

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_WEAPONSLOTTYPE_NAMES	// TheWeaponSlotTypeNamesLookupList (WeaponSet.h)

#include "Common/Thing.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/LaserUpdate.h"
#include "GameLogic/Module/WeaponFireLaserUpdate.h"
#include "Common/GlobalData.h"

//-------------------------------------------------------------------------------------------------
WeaponFireLaserUpdateModuleData::WeaponFireLaserUpdateModuleData()
{
	m_weaponSlotLookup = PRIMARY_WEAPON;
	m_muzzleBoneName = "fx01";
	m_holdFrames = 5;
	m_decayFrames = 15;
	m_muzzleHeight = 12.0f;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void WeaponFireLaserUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	ClientUpdateModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "TriggerWeaponSlot",	INI::parseLookupList,	TheWeaponSlotTypeNamesLookupList,	offsetof( WeaponFireLaserUpdateModuleData, m_weaponSlotLookup ) },
		{ "MuzzleBoneName",			INI::parseAsciiString,	NULL,								offsetof( WeaponFireLaserUpdateModuleData, m_muzzleBoneName ) },
		{ "HoldFrames",					INI::parseDurationUnsignedInt,	NULL,				offsetof( WeaponFireLaserUpdateModuleData, m_holdFrames ) },
		{ "DecayFrames",				INI::parseDurationUnsignedInt,	NULL,				offsetof( WeaponFireLaserUpdateModuleData, m_decayFrames ) },
		{ "MuzzleHeight",				INI::parseReal,					NULL,								offsetof( WeaponFireLaserUpdateModuleData, m_muzzleHeight ) },
		{ 0, 0, 0, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
WeaponFireLaserUpdate::WeaponFireLaserUpdate( Thing *thing, const ModuleData* moduleData )
	: ClientUpdateModule( thing, moduleData )
{
	m_wasFiring = FALSE;
}

//-------------------------------------------------------------------------------------------------
WeaponFireLaserUpdate::~WeaponFireLaserUpdate( void )
{
}

//-------------------------------------------------------------------------------------------------
/** CRC - nothing beyond the base */
void WeaponFireLaserUpdate::crc( Xfer *xfer )
{
	ClientUpdateModule::crc( xfer );
}

//-------------------------------------------------------------------------------------------------
/** Xfer - the transient firing flag needs no persistence */
void WeaponFireLaserUpdate::xfer( Xfer *xfer )
{
	ClientUpdateModule::xfer( xfer );
}

//-------------------------------------------------------------------------------------------------
/** Load post process */
void WeaponFireLaserUpdate::loadPostProcess( void )
{
	ClientUpdateModule::loadPostProcess();
}

//-------------------------------------------------------------------------------------------------
/** Per-client-frame: re-target the beam while firing, decay it when not. */
void WeaponFireLaserUpdate::clientUpdate( void )
{
	const WeaponFireLaserUpdateModuleData *data = (const WeaponFireLaserUpdateModuleData*)getModuleData();

	Drawable *draw = getDrawable();
	if (!draw) return;
	Object *me = draw->getObject();
	if (!me) return;

	// The on-object LaserUpdate (paired with the W3DLaserDraw module).
	static NameKeyType key_LaserUpdate = NAMEKEY( "LaserUpdate" );
	LaserUpdate *update = (LaserUpdate*)draw->findClientUpdateModule( key_LaserUpdate );
	if (!update) return;

	// Firing = the slot's weapon shot recently AND a live victim is engaged.
	Bool firing = FALSE;
	const Coord3D *victimPos = NULL;
	Object *victim = NULL;
	{
		AIUpdateInterface *ai = me->getAI();
		Weapon *w = me->getWeaponInWeaponSlot((WeaponSlotType)data->m_weaponSlotLookup);
		victim = ai ? ai->getCurrentVictim() : NULL;
		if (w && victim) {
			victimPos = ai->getCurrentVictimPos();
			UnsignedInt now = TheGameLogic->getFrame();
			UnsignedInt lastShot = w->getLastShotFrame();
			// frame counters wrap; a "recent" shot is within holdFrames either way
			UnsignedInt age = (now >= lastShot) ? (now - lastShot) : (0xFFFFFFFF - lastShot + now);
			if (age <= data->m_holdFrames && victimPos != NULL) {
				firing = TRUE;
			}
		}
	}

	if (firing) {
		// Fresh target every frame: initLaser(sizeDelta=0) only re-records
		// parent/target IDs and endpoints - width/decay state untouched.
		// Passing the victim OBJECT makes the beam auto-follow its movement.
		Coord3D start = *me->getPosition();
		start.z += data->m_muzzleHeight;
		update->initLaser( me, victim, &start, victimPos, data->m_muzzleBoneName, 0 );
	} else if (m_wasFiring) {
		update->setDecayFrames( data->m_decayFrames );
	}
	m_wasFiring = firing;
}
