import re

# GlobalData.h
p = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngine\Include\Common\GlobalData.h'
d = open(p, 'rb').read().decode('utf-8')
m = re.search(r'^\tReal m_hdrLimiter;.*$', p and d, re.M)
assert m
d = d.replace(m.group(0), m.group(0) + '\n\tInt m_pointLightMode;\t\t///< P6: 0=off, 1=always on, 2=auto (night maps only, default)', 1)
open(p, 'wb').write(d.encode('utf-8'))

# GlobalData.cpp
p2 = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngine\Source\Common\GlobalData.cpp'
d2 = open(p2, 'rb').read().decode('utf-8')
m2 = re.search(r'^\t\{ "HDRLimiter".*$', d2, re.M)
assert m2
d2 = d2.replace(m2.group(0), m2.group(0) + '\n\t{ "PointLightMode",\t\t\tINI::parseInt,\t\t\t\tNULL,\t\t\toffsetof( GlobalData, m_pointLightMode ) },', 1)
m3 = re.search(r'^\tm_hdrLimiter = 6\.0f;.*$', d2, re.M)
assert m3
d2 = d2.replace(m3.group(0), m3.group(0) + '\n\tm_pointLightMode = 2;\t\t// P6: auto - building lights on night maps only', 1)
open(p2, 'wb').write(d2.encode('utf-8'))

# W3XEffectManager.cpp: feed gate
p3 = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3XEffectManager.cpp'
d3 = open(p3, 'rb').read().decode('utf-8')
a = 'const W3XForwardPointLight *W3XGetForwardPointLights(void)'
assert a in d3
gate = '''// P6: effective light count after the GameData.ini PointLightMode gate
// (0=off, 1=always, 2=auto: night maps only - matches the authored intent of
// lamp-lit buildings reading best after dark).
static int PLightFeedCount(void)
{
	if (TheGlobalData) {
		Int mode = TheGlobalData->m_pointLightMode;
		if (mode == 0) return 0;
		if (mode == 2 && TheGlobalData->m_timeOfDay != TIME_OF_DAY_NIGHT) return 0;
	}
	return s_plightCount;
}

'''
d3 = d3.replace(a, gate + a, 1)
# W3XSelectPointLights: gate the count
b = 'int W3XSelectPointLights(const float camPos[3], W3XForwardPointLight out[8])\n{\n\t// Selection sort by squared distance (registry is tiny - 32 max).\n\tint n = s_plightCount;'
assert b in d3, 'select head'
d3 = d3.replace(b, 'int W3XSelectPointLights(const float camPos[3], W3XForwardPointLight out[8])\n{\n\t// Selection sort by squared distance (registry is tiny - 32 max).\n\tint n = PLightFeedCount();', 1)
# case 19: gate too
c = 'effect->SetInt(param, s_plightCount < 8 ? s_plightCount : 8);'
assert c in d3, 'case19'
d3 = d3.replace(c, '{ int pn = PLightFeedCount(); effect->SetInt(param, pn < 8 ? pn : 8); }', 1)
open(p3, 'wb').write(d3.encode('utf-8'))
print('point light mode gate done')
