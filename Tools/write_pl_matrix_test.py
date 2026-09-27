import os

# PS swap: four-quadrant color matrix test
OLD_PS_HEAD = '''    float3 rd = normalize(i.ViewPos) ;               // ray from camera (origin)
    float3 oc = -i.LightCenterV ;                    // ro(0) - center
    float tca = dot(oc, rd) ;
    if (tca < 0.0) return 0 ;                        // light behind the camera
    float d2 = dot(oc, oc) - tca * tca ;             // squared ray-center distance
    float R2 = Range * Range ;
    if (d2 > R2) return 0 ;                          // ray misses the light sphere
    float x2 = d2 / R2 ;
    float glow = pow(saturate(1.0 - x2), 2.0) ;      // round soft falloff
    float chord = 2.0 * sqrt(R2 - d2) / (2.0 * Range) ;
    float3 col = i.LightSourceColor * glow * (0.35 + 0.65 * chord) ;'''

NEW_PS_HEAD = '''    // QUADRANT TEST (remove): which VS-carried quantity is dead?
    // RED tint   = ViewPos sane (View matrix reached the VS, z>0 = in front)
    // GREEN tint = LightCenterV nonzero in view space
    // Both dead  = blue box. Warm glow = everything sane.
    float3 col = 0 ;
    bool viewOK = (i.ViewPos.z > 1.0) ;
    bool centerOK = (length(i.LightCenterV) > 1.0) ;
    if (!viewOK && !centerOK) return float4(0.0, 0.0, 0.15, 1) ;
    if (!viewOK) return float4(0.0, 0.12, 0.0, 1) ;
    if (!centerOK) return float4(0.12, 0.0, 0.0, 1) ;
    float3 rd = normalize(i.ViewPos) ;
    float3 oc = -i.LightCenterV ;
    float tca = dot(oc, rd) ;
    if (tca < 0.0) return float4(0.05, 0.05, 0.0, 1) ;   // yellow tinge: tca<0
    float d2 = dot(oc, oc) - tca * tca ;
    float R2 = Range * Range ;
    if (d2 > R2) return float4(0.03, 0.03, 0.05, 1) ;    // dark violet: miss sphere (Range?)
    float x2 = d2 / R2 ;
    float glow = pow(saturate(1.0 - x2), 2.0) ;
    float chord = 2.0 * sqrt(R2 - d2) / (2.0 * Range) ;
    col = i.LightSourceColor * glow * (0.35 + 0.65 * chord) ;'''

for t in [r'D:\!!!!!!!QWCSB\!!!!!!!QWCSB\Shaders\RA3\w3x_pointlight.fx',
          r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\Shaders\RA3\w3x_pointlight.fx']:
    d = open(t,'rb').read()
    assert d.count(OLD_PS_HEAD.encode()) == 1, t
    open(t,'wb').write(d.replace(OLD_PS_HEAD.encode(), NEW_PS_HEAD.encode()))
    print('quadrant test:', t)
