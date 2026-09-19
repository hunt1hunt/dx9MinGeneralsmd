/*
** TerrainDiag.h — exe-relative path helper for diagnostic logs
**
** Replaces the old hardcoded "E:\\terrain_diag.log" habit: the log now always
** lands next to RTS.exe (the game dir), so it works no matter which drive the
** game is installed on. The path is computed once (GetModuleFileName) and
** cached; fopen callers just use GetTerrainDiagLogPath().
*/
#ifndef __TERRAINDIAG_H
#define __TERRAINDIAG_H

#include <windows.h>
#include <string.h>

// Game directory with trailing backslash, e.g. "E:\!!!!!!!QWCSB\". Cached.
__inline const char *GetGameDir(void)
{
	static char s_dir[MAX_PATH] = "";
	if (s_dir[0] == '\0')
	{
		GetModuleFileName(NULL, s_dir, MAX_PATH);
		char *slash = strrchr(s_dir, '\\');
		if (slash)
			*(slash + 1) = '\0';
		else
			s_dir[0] = '\0';
	}
	return s_dir;
}

// "<game dir>terrain_diag.log". Cached.
__inline const char *GetTerrainDiagLogPath(void)
{
	static char s_path[MAX_PATH] = "";
	if (s_path[0] == '\0')
	{
		strcpy(s_path, GetGameDir());
		strcat(s_path, "terrain_diag.log");
	}
	return s_path;
}

// "<game dir>pbr_compile.log". Cached.
__inline const char *GetPbrCompileLogPath(void)
{
	static char s_path[MAX_PATH] = "";
	if (s_path[0] == '\0')
	{
		strcpy(s_path, GetGameDir());
		strcat(s_path, "pbr_compile.log");
	}
	return s_path;
}

// "<game dir>water_diag.log". Cached.
// 2026-09-18: the water diagnostic used to fopen the hardcoded "E:\\water_diag.log"
// on every call -- exactly the off-drive habit this header was created to replace.
__inline const char *GetWaterDiagLogPath(void)
{
	static char s_path[MAX_PATH] = "";
	if (s_path[0] == '\0')
	{
		strcpy(s_path, GetGameDir());
		strcat(s_path, "water_diag.log");
	}
	return s_path;
}

#endif // __TERRAINDIAG_H
