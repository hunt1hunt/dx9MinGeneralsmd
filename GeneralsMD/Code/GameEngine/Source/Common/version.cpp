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

// FILE: version.cpp //////////////////////////////////////////////////////
// Generals version number class
// Author: Matthew D. Campbell, November 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/GameText.h"
#include "Common/Version.h"

Version *TheVersion = NULL;	///< The Version singleton

Version::Version()
{
	m_major = 1;
	m_minor = 0;
	m_buildNum = 0;
	m_localBuildNum = 0;
	m_buildUser = AsciiString("青蛙大大");
	m_buildLocation = AsciiString("somewhere");
#if defined _DEBUG || defined _INTERNAL || defined _release
	m_showFullVersion = TRUE;
#else
	//m_showFullVersion = FALSE;
	m_showFullVersion = TRUE;
#endif
}

void Version::setVersion(Int major, Int minor, Int buildNum,
												 Int localBuildNum, AsciiString user, AsciiString location,
												 AsciiString buildTime, AsciiString buildDate)
{
	m_major = major;
	m_minor = minor;
	m_buildNum = buildNum;
	m_localBuildNum = localBuildNum;
	m_buildUser = user;
	m_buildLocation = location;
	m_buildTime = buildTime;
	m_buildDate = buildDate;
}

UnsignedInt Version::getVersionNumber( void )
{
	return m_major << 16 | m_minor;
}

AsciiString Version::getAsciiVersion( void )
{
	AsciiString version;
#if defined _DEBUG || defined _INTERNAL || defined _release
	if (m_localBuildNum)
		version.format("BY青蛙大大%d.%d.%d.%d%c%c", m_major, m_minor, m_buildNum, m_localBuildNum,
			m_buildUser.getCharAt(0), m_buildUser.getCharAt(1), m_buildUser.getCharAt(2), m_buildUser.getCharAt(3), m_buildUser.getCharAt(4));
	else
		//version.format("%d.%d.%d", m_major, m_minor, m_buildNum);
		version.format("BY青蛙大大%d.%d.%d.%d%c%c", m_major, m_minor, m_buildNum, m_localBuildNum,
			m_buildUser.getCharAt(0), m_buildUser.getCharAt(1), m_buildUser.getCharAt(2), m_buildUser.getCharAt(3), m_buildUser.getCharAt(4));
#else // defined _DEBUG || defined _INTERNAL
	//version.format("BY青蛙大大", "%d.%d", m_major, m_minor);
	version.format("BY青蛙大大%d.%d.%d.%d%c%c", m_major, m_minor, m_buildNum, m_localBuildNum,
		m_buildUser.getCharAt(0), m_buildUser.getCharAt(1), m_buildUser.getCharAt(2), m_buildUser.getCharAt(3), m_buildUser.getCharAt(4));
	
#endif // defined _DEBUG || defined _INTERNAL

	return version;
}

UnicodeString Version::getUnicodeVersion( void )
{
	UnicodeString version;

#if defined _DEBUG || defined _INTERNAL || defined _release
	if (!m_localBuildNum)
		//version.format(TheGameText->fetch("Version:Format3").str(), m_major, m_minor, m_buildNum);
		version.format(TheGameText->fetch("Version:Format4").str(), m_major, m_minor, m_buildNum, m_localBuildNum,
			m_buildUser.getCharAt(0), m_buildUser.getCharAt(1), m_buildUser.getCharAt(2), m_buildUser.getCharAt(3), m_buildUser.getCharAt(4));
	else
		version.format(TheGameText->fetch("Version:Format4").str(), m_major, m_minor, m_buildNum, m_localBuildNum,
			m_buildUser.getCharAt(0), m_buildUser.getCharAt(1), m_buildUser.getCharAt(2), m_buildUser.getCharAt(3), m_buildUser.getCharAt(4));
#else // defined _DEBUG || defined _INTERNAL
	//version.format(TheGameText->fetch("BY青蛙大大Version:Format2").str(), m_major, m_minor);
	//version.format("BY青蛙大大", "%d.%d", m_major, m_minor);
	version.format(TheGameText->fetch("Version:Format4").str(), m_major, m_minor, m_buildNum, m_localBuildNum,
		m_buildUser.getCharAt(0), m_buildUser.getCharAt(1), m_buildUser.getCharAt(2), m_buildUser.getCharAt(3), m_buildUser.getCharAt(4));
#endif // defined _DEBUG || defined _INTERNAL

#ifdef _DEBUG
	version.concat(UnicodeString(L"BY青蛙大大 Debug"));
#endif

#ifdef _INTERNAL
	version.concat(UnicodeString(L"BY青蛙大大 Internal"));
#endif
	//version.concat(UnicodeString(L" BY青蛙大大"));
	//return version;
#ifdef _release
	version.concat(UnicodeString(L"BY青蛙大大 release"));
#endif
	return version;
}

UnicodeString Version::getFullUnicodeVersion( void )
{
	UnicodeString version;

	if (!m_localBuildNum)
		//version.format(TheGameText->fetch("Version:Format3").str(), m_major, m_minor, m_buildNum);
		version.format(TheGameText->fetch("Version:Format4").str(), m_major, m_minor, m_buildNum, m_localBuildNum,
			m_buildUser.getCharAt(0), m_buildUser.getCharAt(1), m_buildUser.getCharAt(2), m_buildUser.getCharAt(3), m_buildUser.getCharAt(4));
	//
	else
		version.format(TheGameText->fetch("Version:Format4").str(), m_major, m_minor, m_buildNum, m_localBuildNum,
			m_buildUser.getCharAt(0), m_buildUser.getCharAt(1), m_buildUser.getCharAt(2), m_buildUser.getCharAt(3), m_buildUser.getCharAt(4));

#ifdef _DEBUG
	version.concat(UnicodeString(L"BY青蛙大大 Debug"));
#endif

#ifdef _INTERNAL
	version.concat(UnicodeString(L"BY青蛙大大 Internal"));
#endif
	//version.concat(UnicodeString(L"BY 青蛙大大"));
	//version.format(TheGameText->fetch("Version:Format4").str(), m_major, m_minor, m_buildNum, m_localBuildNum,
	//	m_buildUser.getCharAt(0), m_buildUser.getCharAt(1), m_buildUser.getCharAt(2), m_buildUser.getCharAt(3), m_buildUser.getCharAt(4));
	//return version;
#ifdef _release
	version.concat(UnicodeString(L"BY青蛙大大 release"));
#endif
	return version;
}

AsciiString Version::getAsciiBuildTime( void )
{
	AsciiString timeStr;
	timeStr.format("BY青蛙大大%s %s", m_buildDate.str(), m_buildTime.str());

	return timeStr;
}

UnicodeString Version::getUnicodeBuildTime( void )
{
	UnicodeString build;
	UnicodeString dateStr;
	UnicodeString timeStr;

	dateStr.translate(m_buildDate);
	timeStr.translate(m_buildTime);
	//build.format(TheGameText->fetch("Version:BuildTime").str(), dateStr.str(), timeStr.str());
	//RELEASE版本起作用的这句！！！使用16进制转义序列来代替，例如： L"BY\x9752\x86D9\x5927\x5927 %s %s"
	build.format(L"BY\x9752\x86D9\x5927\x5927 %s %s", dateStr.str(), timeStr.str());
	return build;
}

AsciiString Version::getAsciiBuildLocation( void )
{
	return AsciiString(m_buildLocation);
}

UnicodeString Version::getUnicodeBuildLocation( void )
{
	UnicodeString build;
	UnicodeString machine;

	machine.translate(AsciiString(m_buildLocation));
	build.format(TheGameText->fetch("Version:BuildMachine").str(), machine.str());

	return build;
}

AsciiString Version::getAsciiBuildUser( void )
{
	//return AsciiString(m_buildUser);
	return AsciiString("BY青蛙大大");
}

UnicodeString Version::getUnicodeBuildUser( void )
{
	UnicodeString build;
	UnicodeString user;

	user.translate(AsciiString(m_buildUser));
	build.format(TheGameText->fetch("BY青蛙大大Version:BuildUser").str(), user.str());

	return build;
}
