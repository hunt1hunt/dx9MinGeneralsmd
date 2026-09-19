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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: Energy.cpp /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: Energy.cpp
//
// Created:   Steven Johnson, October 2001
//
// Desc:      @todo
//
//-----------------------------------------------------------------------------

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/System/TerrainDiag.h"

#include "Common/AudioSettings.h"
#include "Common/GameAudio.h"
#include "Common/MiscAudio.h"

#include "Common/Energy.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-----------------------------------------------------------------------------
Energy::Energy()
{
	m_energyProduction = 0;
	m_energyConsumption = 0;
	m_owner = NULL;
	m_powerSabotagedTillFrame = 0;
}

//-----------------------------------------------------------------------------
Int Energy::getProduction() const
{ 
	if( TheGameLogic->getFrame() < m_powerSabotagedTillFrame )
	{
		//Power sabotaged, therefore no power.
		return 0;
	}
	return m_energyProduction; 
}

//-----------------------------------------------------------------------------
Real Energy::getEnergySupplyRatio() const 
{ 
	DEBUG_ASSERTCRASH(m_energyProduction >= 0 && m_energyConsumption >= 0, ("neg Energy numbers\n"));

	if( TheGameLogic->getFrame() < m_powerSabotagedTillFrame )
	{
		//Power sabotaged, therefore no power, no ratio.
		return 0.0f;
	}

	if (m_energyConsumption == 0)
		return (Real)m_energyProduction;

	return (Real)m_energyProduction / (Real)m_energyConsumption;
}

//-------------------------------------------------------------------------------------------------
Bool Energy::hasSufficientPower(void) const
{
	if( TheGameLogic->getFrame() < m_powerSabotagedTillFrame )
	{
		//Power sabotaged, therefore no power.
		return FALSE;
	}
	return m_energyProduction >= m_energyConsumption;
}

//-------------------------------------------------------------------------------------------------
void Energy::adjustPower(Int powerDelta, Bool adding)
{
	if (powerDelta == 0) {
		return;
	}

	if (powerDelta > 0) {
		if (adding) { 
			addProduction(powerDelta);
		} else {
			addProduction(-powerDelta);
		}
	} else {
		// Seems a little odd, however, consumption is reversed. Negative power is positive consumption.
		if (adding) {
			addConsumption(-powerDelta);
		} else {
			addConsumption(powerDelta);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** new 'obj' will now add/subtract from this energy construct */
//-------------------------------------------------------------------------------------------------
void Energy::objectEnteringInfluence( Object *obj )
{

	// sanity
	if( obj == NULL )
		return;

	// get the amount of energy this object produces or consumes
	Int energy = obj->getTemplate()->getEnergyProduction();

	// adjust energy
	if( energy < 0 )
		addConsumption( -energy );
	else if( energy > 0 )
		addProduction( energy );

	// sanity
	DEBUG_ASSERTCRASH( m_energyProduction >= 0 && m_energyConsumption >= 0, 
										 ("Energy - Negative Energy numbers, Produce=%d Consume=%d\n",
										 m_energyProduction, m_energyConsumption) );

}  // end objectEnteringInfluence

//-------------------------------------------------------------------------------------------------
/** 'obj' will now no longer add/subtrack from this energy construct */
//-------------------------------------------------------------------------------------------------
void Energy::objectLeavingInfluence( Object *obj )
{

	// sanity
	if( obj == NULL )
		return;

	// get the amount of energy this object produces or consumes
	Int energy = obj->getTemplate()->getEnergyProduction();

	// adjust energy
	if( energy < 0 )
	{
		// 2026-09-16 breadcrumb (terrain_diag.log, all builds): a negative
		// predicted balance here means the same object is leaving influence
		// twice (sold AND destroyed in the same tick).
		if (m_energyConsumption + energy < 0)
		{
			FILE *f = fopen(GetTerrainDiagLogPath(), "a");
			if (f) { fprintf(f, "[%u] ENERGY_DOUBLE_LEAVE obj=%s consume %d + (%d) < 0\n", (unsigned)GetTickCount(), obj->getTemplate()->getName().str(), m_energyConsumption, energy); fclose(f); }
		}
		addConsumption( energy );
	}
	else if( energy > 0 )
	{
		if (m_energyProduction - energy < 0)
		{
			FILE *f = fopen(GetTerrainDiagLogPath(), "a");
			if (f) { fprintf(f, "[%u] ENERGY_DOUBLE_LEAVE obj=%s produce %d - %d < 0\n", (unsigned)GetTickCount(), obj->getTemplate()->getName().str(), m_energyProduction, energy); fclose(f); }
		}
		addProduction( -energy );
	}

	// sanity
	DEBUG_ASSERTCRASH( m_energyProduction >= 0 && m_energyConsumption >= 0, 
										 ("Energy - Negative Energy numbers, Produce=%d Consume=%d\n",
										 m_energyProduction, m_energyConsumption) );

}

//-------------------------------------------------------------------------------------------------
/** Adds an energy bonus to the player's pool of energy when the "Control Rods" upgrade
		is made to the American Cold Fusion Plant */
//-------------------------------------------------------------------------------------------------
void Energy::addPowerBonus( Object *obj )
{

	// sanity
	if( obj == NULL )
		return;

	// 2026-09-16 breadcrumb (terrain_diag.log, all builds): tracks the Control
	// Rods bonus so a REMOVE without a matching ADD can be spotted.
	Int bonus = obj->getTemplate()->getEnergyBonus();
	{
		FILE *f = fopen(GetTerrainDiagLogPath(), "a");
		if (f) { fprintf(f, "[%u] ENERGY_BONUS_ADD obj=%s bonus=%d prod_before=%d\n", (unsigned)GetTickCount(), obj->getTemplate()->getName().str(), bonus, m_energyProduction); fclose(f); }
	}
	addProduction(bonus);

	// sanity
	DEBUG_ASSERTCRASH( m_energyProduction >= 0 && m_energyConsumption >= 0, 
										 ("Energy - Negative Energy numbers, Produce=%d Consume=%d\n",
										 m_energyProduction, m_energyConsumption) );

}

// ------------------------------------------------------------------------------------------------
/** Removed an energy bonus */
// ------------------------------------------------------------------------------------------------
void Energy::removePowerBonus( Object *obj )
{

	// sanity
	if( obj == NULL )
		return;

	// 2026-09-16 breadcrumb: pairs with ENERGY_BONUS_ADD above.
	Int bonus = obj->getTemplate()->getEnergyBonus();
	{
		FILE *f = fopen(GetTerrainDiagLogPath(), "a");
		if (f) { fprintf(f, "[%u] ENERGY_BONUS_REMOVE obj=%s bonus=%d prod_before=%d\n", (unsigned)GetTickCount(), obj->getTemplate()->getName().str(), bonus, m_energyProduction); fclose(f); }
	}
	addProduction( -bonus );

	// sanity
	DEBUG_ASSERTCRASH( m_energyProduction >= 0 && m_energyConsumption >= 0, 
										 ("Energy - Negative Energy numbers, Produce=%d Consume=%d\n",
										 m_energyProduction, m_energyConsumption) );

}  // end removePowerBonus

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Private functions
// ------------------------------------------------------------------------------------------------
void Energy::addProduction(Int amt)
{
	m_energyProduction += amt;

	// 2026-09-14 defensive clamp: a double objectLeavingInfluence (object
	// sold AND destroyed in the same tick) drives production negative, which
	// trips the "Negative Energy numbers" DEBUG_ASSERTCRASH and kills the
	// game mid-skirmish. Clamp at zero and log the delta so the offending
	// bookkeeping path can still be identified from DebugLogFileI.txt.
	if (m_energyProduction < 0)
	{
		// 2026-09-16: sample the diagnostic (first + every 100th) and write
		// it to terrain_diag.log so Release builds see it too.
		static Int s_clampProdCount = 0;
		s_clampProdCount++;
		if (s_clampProdCount == 1 || (s_clampProdCount % 100) == 0)
		{
			FILE *f = fopen(GetTerrainDiagLogPath(), "a");
			if (f) { fprintf(f, "[%u] ENERGY_CLAMP_PROD %d -> 0 (delta=%d, count=%d)\n", (unsigned)GetTickCount(), m_energyProduction, amt, s_clampProdCount); fclose(f); }
		}
		m_energyProduction = 0;
	}

	if( m_owner == NULL )
		return;

	// A repeated Brownout signal does nothing bad, and we need to handle more than just edge cases.
	// Like low power, now even more low power, refresh disable.
	m_owner->onPowerBrownOutChange( !hasSufficientPower() );
}

// ------------------------------------------------------------------------------------------------
void Energy::addConsumption(Int amt)
{
	m_energyConsumption += amt;

	// 2026-09-14 defensive clamp: see addProduction - same double-leave
	// protection for the consumption side.
	if (m_energyConsumption < 0)
	{
		// 2026-09-16: sample like addProduction; terrain_diag.log for all builds.
		static Int s_clampConsCount = 0;
		s_clampConsCount++;
		if (s_clampConsCount == 1 || (s_clampConsCount % 100) == 0)
		{
			FILE *f = fopen(GetTerrainDiagLogPath(), "a");
			if (f) { fprintf(f, "[%u] ENERGY_CLAMP_CONS %d -> 0 (delta=%d, count=%d)\n", (unsigned)GetTickCount(), m_energyConsumption, amt, s_clampConsCount); fclose(f); }
		}
		m_energyConsumption = 0;
	}

	if( m_owner == NULL )
		return;

	m_owner->onPowerBrownOutChange( !hasSufficientPower() );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void Energy::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void Energy::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// It is actually incorrect to save these, as they are reconstructed when the buildings are loaded
	// I need to version though so old games will load wrong rather than crashing

	// production
	if( version < 2 )
		xfer->xferInt( &m_energyProduction );
	
	// consumption
	if( version < 2 )
		xfer->xferInt( &m_energyConsumption );

	// owning player index
	Int owningPlayerIndex;
	if( xfer->getXferMode() == XFER_SAVE )
		owningPlayerIndex = m_owner->getPlayerIndex();
	xfer->xferInt( &owningPlayerIndex );
	m_owner = ThePlayerList->getNthPlayer( owningPlayerIndex );

	//Sabotage
	if( version >= 3 )
	{
		xfer->xferUnsignedInt( &m_powerSabotagedTillFrame );
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void Energy::loadPostProcess( void )
{

}  // end loadPostProcess
// ------------------------------------------------------------------------------------------------  
/** 给玩家增加电量，类似 Money::deposit */
// ------------------------------------------------------------------------------------------------  
void Energy::depositEnergy(Int amountToDeposit, Bool playSound)
{
	// 2026-09-16: removed the old freebuild +/-1200 hack. That hack was
	// inverted (ON subtracted 1200, OFF added 1200) and drove production
	// negative -> ENERGY_CLAMP_PROD. Cheat callers now pass the amount
	// explicitly: depositEnergy(1200) to top up, withdrawEnergy(1200) to
	// take it back (withdraw clamps to non-negative).
	if (amountToDeposit == 0)
		return;

	// 增加能量产出  
	addProduction(amountToDeposit);

	// 如果需要播放音效（可选功能）  
	if (playSound)
	{
		// 这里可以添加音效触发代码  
		// triggerAudioEvent(TheAudio->getMiscAudio()->m_energyDepositSound);  
	}
}

// ------------------------------------------------------------------------------------------------  
/** 从玩家扣除电量，类似 Money::withdraw */
// ------------------------------------------------------------------------------------------------  
Int Energy::withdrawEnergy(Int amountToWithdraw, Bool playSound)
{
#if defined(RTS_DEBUG) || defined(_INTERNAL) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	// 检查是否启用了 freebuild 作弊
	// (2026-09-19 note: in the COUPLED ALT+B flow this guard never blocks -
	// enableFreeBuild(false) runs BEFORE the withdraw, so buildsForFree() is
	// already FALSE here.)
	if (m_owner != NULL && m_owner->buildsForFree())
		return 0; // freebuild 启用时不扣除电量
#endif

	if (amountToWithdraw > m_energyProduction)
		amountToWithdraw = m_energyProduction;

	if (amountToWithdraw == 0)
		return amountToWithdraw;

	// 减少能量产出  
	addProduction(-amountToWithdraw);

	// 如果需要播放音效（可选功能）  
	if (playSound)
	{
		// 这里可以添加音效触发代码  
		// triggerAudioEvent(TheAudio->getMiscAudio()->m_energyWithdrawSound);  
	}

	return amountToWithdraw;
}

