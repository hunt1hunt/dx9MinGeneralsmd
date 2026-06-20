"""
Generate fallback IBL CubeMap DDS textures for Phase 5 PBR testing.
Creates:
  - env_irradiance.dds: Simple sky/ground gradient CubeMap (32x32x6)
  - env_brdf_lut.dds: Approximate BRDF integration LUT (256x256)

Place these in the game's Art/Textures/ directory or alongside the executable.
"""

import struct
import math

# DDS format constants
DDS_MAGIC = b'DDS '

DDSD_CAPS = 0x1
DDSD_HEIGHT = 0x2
DDSD_WIDTH = 0x4
DDSD_PITCH = 0x8
DDSD_PIXELFORMAT = 0x1000
DDSD_MIPMAPCOUNT = 0x20000
DDSD_LINEARSIZE = 0x80000
DDSD_DEPTH = 0x800000

DDPF_ALPHAPIXELS = 0x1
DDPF_RGB = 0x40

DDSCAPS_COMPLEX = 0x8
DDSCAPS_TEXTURE = 0x1000
DDSCAPS_MIPMAP = 0x400000

DDSCAPS2_CUBEMAP = 0x200
DDSCAPS2_CUBEMAP_POSITIVEX = 0x400
DDSCAPS2_CUBEMAP_NEGATIVEX = 0x800
DDSCAPS2_CUBEMAP_POSITIVEY = 0x1000
DDSCAPS2_CUBEMAP_NEGATIVEY = 0x2000
DDSCAPS2_CUBEMAP_POSITIVEZ = 0x4000
DDSCAPS2_CUBEMAP_NEGATIVEZ = 0x8000

ALL_CUBEMAP_FACES = (
    DDSCAPS2_CUBEMAP_POSITIVEX |
    DDSCAPS2_CUBEMAP_NEGATIVEX |
    DDSCAPS2_CUBEMAP_POSITIVEY |
    DDSCAPS2_CUBEMAP_NEGATIVEY |
    DDSCAPS2_CUBEMAP_POSITIVEZ |
    DDSCAPS2_CUBEMAP_NEGATIVEZ
)


def make_dds_header(width, height, mip_count, is_cubemap, pitch_or_size):
    """Create a DDS header (128 bytes)."""
    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT
    if mip_count > 1:
        flags |= DDSD_MIPMAPCOUNT
    if pitch_or_size:
        flags |= DDSD_LINEARSIZE

    caps = DDSCAPS_TEXTURE
    if mip_count > 1:
        caps |= DDSCAPS_MIPMAP
    caps2 = 0

    if is_cubemap:
        caps |= DDSCAPS_COMPLEX
        caps2 = ALL_CUBEMAP_FACES

    pf_size = 32
    pf_flags = DDPF_RGB | DDPF_ALPHAPIXELS
    pf_fourcc = 0
    pf_bitcount = 32
    pf_r_mask = 0x00FF0000  # D3D uses A8R8G8B8
    pf_g_mask = 0x0000FF00
    pf_b_mask = 0x000000FF
    pf_a_mask = 0xFF000000

    data = struct.pack('<4s', DDS_MAGIC)
    data += struct.pack('<I', 124)  # dwSize
    data += struct.pack('<I', flags)
    data += struct.pack('<I', height)
    data += struct.pack('<I', width)
    data += struct.pack('<I', pitch_or_size)
    data += struct.pack('<I', 0)  # dwDepth
    data += struct.pack('<I', mip_count)
    data += struct.pack('<IIIIIIIIIII', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)  # reserved1[11]
    data += struct.pack('<I', pf_size)
    data += struct.pack('<I', pf_flags)
    data += struct.pack('<I', pf_fourcc)
    data += struct.pack('<I', pf_bitcount)
    data += struct.pack('<I', pf_r_mask)
    data += struct.pack('<I', pf_g_mask)
    data += struct.pack('<I', pf_b_mask)
    data += struct.pack('<I', pf_a_mask)
    data += struct.pack('<I', caps)
    data += struct.pack('<I', caps2)
    data += struct.pack('<I', 0)  # dwCaps3
    data += struct.pack('<I', 0)  # dwCaps4
    data += struct.pack('<I', 0)  # dwReserved2

    assert len(data) == 128, f"Header size is {len(data)}, expected 128"
    return data


def pixel(r, g, b, a=255):
    """A8R8G8B8 pixel."""
    r = max(0, min(255, int(r)))
    g = max(0, min(255, int(g)))
    b = max(0, min(255, int(b)))
    a = max(0, min(255, int(a)))
    return struct.pack('<BBBB', b, g, r, a)  # D3D stores as A8R8G8B8 -> BGRA in memory


def fill_cube_face(width, height, face_idx):
    """Generate pixel data for one CubeMap face.

    face_idx: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z

    Returns flat RGBA pixel data.
    """
    pixels = b''
    for y in range(height):
        for x in range(width):
            # Normalize coordinates to [-1, 1]
            u = (x + 0.5) / width * 2.0 - 1.0
            v = (y + 0.5) / height * 2.0 - 1.0

            # Direction vector for this face
            if face_idx == 0:  # +X
                dx, dy, dz = 1.0, -v, -u
            elif face_idx == 1:  # -X
                dx, dy, dz = -1.0, -v, u
            elif face_idx == 2:  # +Y
                dx, dy, dz = u, 1.0, v
            elif face_idx == 3:  # -Y
                dx, dy, dz = u, -1.0, -v
            elif face_idx == 4:  # +Z
                dx, dy, dz = u, -v, 1.0
            elif face_idx == 5:  # -Z
                dx, dy, dz = -u, -v, -1.0
            else:
                raise ValueError(f"Invalid face index: {face_idx}")

            # Normalize
            length = math.sqrt(dx * dx + dy * dy + dz * dz)
            dx /= length
            dy /= length
            dz /= length

            # Sky color: blend based on direction
            # Top (+Y) = sky blue, Bottom (-Y) = warm ground
            # Sides = horizon blend

            # Sky top color
            sky_r, sky_g, sky_b = 0.4, 0.6, 0.9  # Light blue
            # Ground color
            ground_r, ground_g, ground_b = 0.2, 0.15, 0.1  # Dark warm brown

            # Horizon color (distant haze)
            horiz_r, horiz_g, horiz_b = 0.7, 0.7, 0.75  # Pale gray-blue

            # Vertical blend factor: 0 = bottom, 1 = top
            t = dy * 0.5 + 0.5
            t = max(0.0, min(1.0, t))

            # Sun direction (slightly off-center)
            sun_dir = (0.3, 0.6, 0.4)
            sun_len = math.sqrt(sun_dir[0]**2 + sun_dir[1]**2 + sun_dir[2]**2)
            sun_dir = (sun_dir[0]/sun_len, sun_dir[1]/sun_len, sun_dir[2]/sun_len)

            # Sun contribution (diffuse scatter in sky)
            sun_dot = dx * sun_dir[0] + dy * sun_dir[1] + dz * sun_dir[2]
            sun_dot = max(0.0, sun_dot)

            # Blend between ground and sky
            if t > 0.5:
                # Sky side: blend from horizon to sky
                sky_factor = (t - 0.5) * 2.0
                r = horiz_r + (sky_r - horiz_r) * sky_factor
                g = horiz_g + (sky_g - horiz_g) * sky_factor
                b = horiz_b + (sky_b - horiz_b) * sky_factor
            else:
                # Ground side: blend from horizon to ground
                ground_factor = (0.5 - t) * 2.0
                r = horiz_r + (ground_r - horiz_r) * ground_factor
                g = horiz_g + (ground_g - horiz_g) * ground_factor
                b = horiz_b + (ground_b - horiz_b) * ground_factor

            # Add sun glow near sun direction
            sun_glow = sun_dot ** 8.0
            r += sun_glow * 0.3
            g += sun_glow * 0.2
            b += sun_glow * 0.1

            # Clamp
            r = max(0.0, min(1.0, r)) * 255
            g = max(0.0, min(1.0, g)) * 255
            b = max(0.0, min(1.0, b)) * 255

            pixels += pixel(r, g, b)

    return pixels


def create_cubemap_dds(width, height, filename):
    """Create a simple gradient CubeMap DDS file."""
    mip_count = 1
    face_size = width * height * 4
    total_data = b''

    for face_idx in range(6):
        total_data += fill_cube_face(width, height, face_idx)

    header = make_dds_header(width, height, mip_count, True, face_size)
    with open(filename, 'wb') as f:
        f.write(header)
        f.write(total_data)
    print(f"Created {filename}: {width}x{height} CubeMap ({6*face_size} bytes data)")


def create_brdf_lut_dds(width, height, filename):
    """Generate an approximate BRDF integration LUT.

    This is a pre-integrated environment map BRDF - the Split-Sum approximation.
    It encodes F0 scale (R) and F0 bias (G) as a function of:
      x = NdotV (cosine of angle between normal and view)
      y = roughness

    Uses the Schlick approximation + Smith GGX integration.
    This is an approximation - a proper LUT would use numerical integration.
    """
    mip_count = 1
    pitch = width * 4
    pixels = b''

    for y in range(height):
        roughness = (y + 0.5) / height
        a = roughness * roughness
        a2 = a * a

        for x in range(width):
            NdotV = (x + 0.5) / width
            # Guard against edge cases
            NdotV = max(0.001, min(0.999, NdotV))

            # Smith GGX visibility term for the LUT
            # V = 1 / (NdotV + sqrt(a2 + (1 - a2) * NdotV*NdotV))
            k = a * 0.5  # k = a/2 for IBL
            G_V = NdotV / (NdotV * (1.0 - k) + k)

            # Approximate integrated BRDF
            # For roughness=0, envBRDF = (1, 0) -> mirrors perfect specular
            # For roughness=1, envBRDF approximates cosine-weighted integral
            env_brdf_x = 1.0 - roughness * 0.5  # F0 scale
            env_brdf_y = roughness * 0.25  # F0 bias

            # Better approximation using Smith GGX
            # These approximate the numerical integration results
            env_brdf_x = G_V  # Scale factor
            env_brdf_y = (1.0 - G_V) * (1.0 - roughness * 0.5)  # Bias term

            # Clamp
            env_brdf_x = max(0.0, min(1.0, env_brdf_x))
            env_brdf_y = max(0.0, min(1.0, env_brdf_y))

            # Store in R (scale) and G (bias) channels, B and A unused
            r = int(env_brdf_x * 255)
            g = int(env_brdf_y * 255)
            pixels += pixel(r, g, 0, 255)

    header = make_dds_header(width, height, mip_count, False, pitch)
    with open(filename, 'wb') as f:
        f.write(header)
        f.write(pixels)
    print(f"Created {filename}: {width}x{height} 2D texture ({len(pixels)} bytes)")


if __name__ == '__main__':
    create_cubemap_dds(32, 32, 'env_irradiance.dds')
    create_cubemap_dds(32, 32, 'env_prefiltered.dds')
    create_brdf_lut_dds(256, 256, 'env_brdf_lut.dds')
    print("Done - all IBL textures generated.")
