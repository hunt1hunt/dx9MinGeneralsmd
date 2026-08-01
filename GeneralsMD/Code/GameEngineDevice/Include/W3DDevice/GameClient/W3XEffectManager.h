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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : W3XEffectManager                                             *
 *                                                                                             *
 *                     $Archive::                                                             $*
 *                                                                                             *
 *                       Author:: hzc                                                          *
 *                                                                                             *
 *                    $Modtime::                                                             $*
 *                                                                                             *
 *                   $Revision::                                                             $*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   W3XEffectManager -- singleton D3DXEffect cache + auto uniform binding                    *
 *                                                                                             *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef W3X_EFFECT_MANAGER_H
#define W3X_EFFECT_MANAGER_H

#include "always.h"
#include "Common/AsciiString.h"

struct ID3DXEffect;
class RenderInfoClass;

// ----------------------------------------------------------------------------
// W3XEffectManager: singleton that caches D3DXEffect objects and auto-binds
// engine global uniforms by parameter name.
//
// Maximum number of cached effects (practical limit, far more than needed)
// ----------------------------------------------------------------------------
#define W3X_EFFECT_CACHE_MAX	64

class W3XEffectManager
{
public:
	// Singleton access
	static W3XEffectManager *Instance(void);

	// Get or create an effect (cached by file path, ref-counted)
	// Returns NULL on failure (missing file, compile error, etc.)
	ID3DXEffect *GetEffect(const char *fxPath);

	// Release a reference to a cached effect
	void ReleaseEffect(const char *fxPath);

	// Device lost/reset — must be called from the engine's device handlers
	void OnLostDevice(void);
	void OnResetDevice(void);

	// Bind all known engine globals to effect parameters by name matching.
	// Called per-frame/per-draw before effect->Begin().
	void BindEngineConstants(ID3DXEffect *effect, const RenderInfoClass &rinfo);

	// Log effect info (diagnostics)
	void LogEffectInfo(const char *tag, ID3DXEffect *effect);

	// Get number of cached effect entries
	int GetCacheSize(void) const { return m_cacheSize; }

private:
	// Effect cache entry
	struct EffectEntry
	{
		AsciiString filePath;
		ID3DXEffect *effect;
		int refCount;
	};

	W3XEffectManager();
	~W3XEffectManager();
	W3XEffectManager(const W3XEffectManager &);
	W3XEffectManager &operator=(const W3XEffectManager &);

	// Find cache index by file path, returns -1 if not found
	int FindCacheEntry(const char *fxPath) const;

	// Internal: try to bind a single parameter by name matching
	bool BindParameter(ID3DXEffect *effect, void *voidParam,
		const char *paramName, const RenderInfoClass &rinfo);

	// The singleton instance
	static W3XEffectManager *m_instance;

	// Fixed-size cache (simple array avoids STL hash_map issues with VC6)
	EffectEntry m_cache[W3X_EFFECT_CACHE_MAX];
	int m_cacheSize;
};


#endif /* W3X_EFFECT_MANAGER_H */
