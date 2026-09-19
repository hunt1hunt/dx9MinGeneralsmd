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

// FILE: Money.cpp /////////////////////////////////////////////////////////
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
// File name: Money.cpp
//
// Created:   Steven Johnson, October 2001
//
// Desc:      @todo
//
//-----------------------------------------------------------------------------

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/Money.h"

//#include "Common/AudioSettings.h"
#include "Common/GameAudio.h"
#include "Common/MiscAudio.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
// ------------------------------------------------------------------------------------------------
UnsignedInt Money::withdraw(UnsignedInt amountToWithdraw, Bool playSound)
{
#if defined(RTS_DEBUG) || defined(_INTERNAL) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	// 2026-09-19: restored the ORIGINAL free-build check (the migration-era
	// rewrite "enable = !buildsForFree(); if (enable) return 0;" was exactly
	// INVERTED: everything was FREE while freebuild was OFF, and building
	// charged normally right after ALT+B turned freebuild ON - the
	// field-reported "free build never works". Correct semantics: the OWNING
	// player building for free pays nothing.)
	Player* freePlayer = ThePlayerList->getNthPlayer(m_playerIndex);
	if (freePlayer != NULL && freePlayer->buildsForFree())
		return 0;
	// 2026-09-19 DIAG (field report: still charging after the fix): log the
	// first withdraws so the flag/money-object/decision chain is observable.
	{
		static int s_wdn = 0;
		if (s_wdn < 10) {
			s_wdn++;
			FILE *df = fopen("E:\\freebuild_diag.log", "a");
			if (df) {
				fprintf(df, "WITHDRAW idx=%d amt=%u free=%d playerType=%d\n",
					m_playerIndex, amountToWithdraw,
					(freePlayer && freePlayer->buildsForFree()) ? 1 : 0,
					freePlayer ? (int)freePlayer->getPlayerType() : -1);
				fclose(df);
			}
		}
	}
#endif //defined(_DEBUG) || defined(_INTERNAL)

    if (amountToWithdraw > m_money)
		amountToWithdraw = m_money;

	

	if (amountToWithdraw == 0)
		return amountToWithdraw;

	//@todo: Do we do this frequently enough that it is a performance hit?
	AudioEventRTS event = TheAudio->getMiscAudio()->m_moneyWithdrawSound;
	event.setPlayerIndex(m_playerIndex);

	// Play a sound
	if (playSound)
		TheAudio->addAudioEvent(&event);

	m_money -= amountToWithdraw;

	return amountToWithdraw;
	
}

// ------------------------------------------------------------------------------------------------
void Money::deposit(UnsignedInt amountToDeposit, Bool playSound)
{
	if (amountToDeposit == 0)
		return;

	//@todo: Do we do this frequently enough that it is a performance hit?
	AudioEventRTS event = TheAudio->getMiscAudio()->m_moneyDepositSound;
	event.setPlayerIndex(m_playerIndex);

	// Play a sound
	if (playSound)
		TheAudio->addAudioEvent(&event);
	
	m_money += amountToDeposit;

	if( amountToDeposit > 0 )
	{
		Player *player = ThePlayerList->getNthPlayer( m_playerIndex );
		if( player )
		{
			player->getAcademyStats()->recordIncome();
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void Money::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void Money::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// money value
	xfer->xferUnsignedInt( &m_money );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void Money::loadPostProcess( void )
{

}  // end loadPostProcess


// ------------------------------------------------------------------------------------------------
/** Parse a money amount for the ini file. E.g. DefaultStartingMoney = 10000 */
// ------------------------------------------------------------------------------------------------
void Money::parseMoneyAmount( INI *ini, void *instance, void *store, const void* userData )
{
  // Someday, maybe, have mulitple fields like Gold:10000 Wood:1000 Tiberian:10
  Money * money = (Money *)store;
  INI::parseUnsignedInt( ini, instance, &money->m_money, userData );
}
