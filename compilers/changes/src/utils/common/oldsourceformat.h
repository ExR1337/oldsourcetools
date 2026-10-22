//========= Old Source format ================================================//
//
// Purpose: these compilers write compiled maps in the format the Old Source
//          engine reads (the format of its own compilers):
//
//   - leaf ambient lighting: one light cube per leaf in LUMP_LEAF_AMBIENT_LIGHTING
//     (the newer engine's per-leaf sample lists are turned into it here)
//   - static props: version 5
//   - no face ids, displacement collision lump or overlay fades (newer engine only)
//   - no hammerid / mapversion entity keys
//   - default cubemaps as VTF 7.2
//   - no newer flag bits in faces and overlays, no per-texel prop lighting
//
// The helpers below only use plain C, so they can be checked outside the
// engine's build.
//
//=============================================================================//

#ifndef OLDSOURCEFORMAT_H
#define OLDSOURCEFORMAT_H
#ifdef _WIN32
#pragma once
#endif

#include <string.h>
#include <math.h>
#include <stdlib.h>

// shown in the compilers' banner
#define OLDSOURCE_BUILD_DATE "Oct 22 2026"

//-----------------------------------------------------------------------------
// Light colours: ColorRGBExp32 (r, g, b, signed exponent), value = c * 2^exp / 255
//-----------------------------------------------------------------------------
inline void OldSource_DecodeRGBExp( const unsigned char *c, float out[3] )
{
	float s = (float)ldexp( 1.0, (int)(signed char)c[3] ) / 255.0f;
	out[0] = c[0] * s;
	out[1] = c[1] * s;
	out[2] = c[2] * s;
}

// the smallest exponent that keeps the brightest channel within 255
inline void OldSource_EncodeRGBExp( const float in[3], unsigned char *c )
{
	float m = in[0];
	if ( in[1] > m ) m = in[1];
	if ( in[2] > m ) m = in[2];
	if ( !( m > 0.0f ) )
	{
		c[0] = c[1] = c[2] = c[3] = 0;
		return;
	}
	int e;
	double f = frexp( (double)m, &e );	// m = f * 2^e, f in [0.5, 1)
	if ( f == 0.5 )
		e--;
	if ( e < -128 ) e = -128;
	if ( e > 127 ) e = 127;
	double s = 255.0 / ldexp( 1.0, e );
	for ( int i = 0; i < 3; i++ )
	{
		double v = floor( in[i] * s + 0.5 );
		if ( v < 0 ) v = 0;
		if ( v > 255 ) v = 255;
		c[i] = (unsigned char)v;
	}
	c[3] = (unsigned char)(signed char)e;
}

//-----------------------------------------------------------------------------
// Leaf ambient lighting
//-----------------------------------------------------------------------------
struct OldSourceLeafBox
{
	float mins[3];
	float maxs[3];
};

inline float OldSource_BoxDistance( const float p[3], const OldSourceLeafBox &b )
{
	float d2 = 0;
	for ( int a = 0; a < 3; a++ )
	{
		float d = 0;
		if ( p[a] < b.mins[a] ) d = b.mins[a] - p[a];
		else if ( p[a] > b.maxs[a] ) d = p[a] - b.maxs[a];
		d2 += d * d;
	}
	return sqrtf( d2 );
}

inline int OldSource_FloorDiv( float v, float cell )
{
	return (int)floorf( v / cell );
}

// pIndex: 2 unsigned shorts per leaf (sample count, first sample).
// pSamples: 28 bytes per sample (24-byte light cube, x y z as fractions 0..255 of the
// leaf's bounds, 1 pad byte). pOut: 24 bytes per leaf.
// Each leaf gets the average of its samples. Leaves without samples (solid ones) get the
// nearest sample of another leaf: the Old Source compilers give every leaf a cube.
// Returns how many leaves were filled from a neighbour.
inline int OldSource_LeafAmbientCubes( int numLeafs, const OldSourceLeafBox *pBoxes, const unsigned short *pIndex,
	const unsigned char *pSamples, int numSamples, unsigned char *pOut )
{
	memset( pOut, 0, numLeafs * 24 );
	if ( numLeafs <= 0 )
		return 0;

	float *pPos = (float *)malloc( sizeof( float ) * 3 * ( numSamples > 0 ? numSamples : 1 ) );
	unsigned char *pOwned = (unsigned char *)calloc( numSamples > 0 ? numSamples : 1, 1 );
	int *pEmpty = (int *)malloc( sizeof( int ) * numLeafs );
	int nEmpty = 0;

	for ( int i = 0; i < numLeafs; i++ )
	{
		int count = pIndex[i * 2];
		int first = pIndex[i * 2 + 1];
		if ( count == 0 || first + count > numSamples )
		{
			pEmpty[nEmpty++] = i;
			continue;
		}
		const OldSourceLeafBox &box = pBoxes[i];
		if ( count == 1 )
		{
			// one sample (a map from the Old Source compilers read back): keep it exactly
			memcpy( pOut + i * 24, pSamples + first * 28, 24 );
		}
		else
		{
			float avg[6][3];
			memset( avg, 0, sizeof( avg ) );
			for ( int s = first; s < first + count; s++ )
			{
				for ( int f = 0; f < 6; f++ )
				{
					float c[3];
					OldSource_DecodeRGBExp( pSamples + s * 28 + f * 4, c );
					for ( int k = 0; k < 3; k++ )
						avg[f][k] += c[k] / count;
				}
			}
			for ( int f = 0; f < 6; f++ )
				OldSource_EncodeRGBExp( avg[f], pOut + i * 24 + f * 4 );
		}
		for ( int s = first; s < first + count; s++ )
		{
			const unsigned char *pRec = pSamples + s * 28;
			for ( int a = 0; a < 3; a++ )
				pPos[s * 3 + a] = box.mins[a] + ( box.maxs[a] - box.mins[a] ) * pRec[24 + a] / 255.0f;
			pOwned[s] = 1;
		}
	}

	// leaves without samples: the nearest sample, found through a grid of the samples
	int nFilled = 0;
	int nPts = 0;
	for ( int s = 0; s < numSamples; s++ )
		nPts += pOwned[s];
	if ( nEmpty > 0 && nPts > 0 )
	{
		const float cell = 256.0f;
		int lo[3] = { 0x7fffffff, 0x7fffffff, 0x7fffffff }, hi[3] = { -0x7fffffff, -0x7fffffff, -0x7fffffff };
		for ( int s = 0; s < numSamples; s++ )
		{
			if ( !pOwned[s] )
				continue;
			for ( int a = 0; a < 3; a++ )
			{
				int k = OldSource_FloorDiv( pPos[s * 3 + a], cell );
				if ( k < lo[a] ) lo[a] = k;
				if ( k > hi[a] ) hi[a] = k;
			}
		}
		int dim[3] = { hi[0] - lo[0] + 1, hi[1] - lo[1] + 1, hi[2] - lo[2] + 1 };
		int nCells = dim[0] * dim[1] * dim[2];
		int *pStart = (int *)calloc( nCells + 1, sizeof( int ) );
		int *pFill = (int *)malloc( sizeof( int ) * nCells );
		int *pSorted = (int *)malloc( sizeof( int ) * nPts );
#define OLDSOURCE_CELL( x, y, z ) ( ( ( (x) - lo[0] ) * dim[1] + ( (y) - lo[1] ) ) * dim[2] + ( (z) - lo[2] ) )
		for ( int s = 0; s < numSamples; s++ )
		{
			if ( !pOwned[s] )
				continue;
			int c = OLDSOURCE_CELL( OldSource_FloorDiv( pPos[s * 3], cell ), OldSource_FloorDiv( pPos[s * 3 + 1], cell ), OldSource_FloorDiv( pPos[s * 3 + 2], cell ) );
			pStart[c + 1]++;
		}
		for ( int c = 1; c <= nCells; c++ )
			pStart[c] += pStart[c - 1];
		memcpy( pFill, pStart, sizeof( int ) * nCells );
		for ( int s = 0; s < numSamples; s++ )
		{
			if ( !pOwned[s] )
				continue;
			int c = OLDSOURCE_CELL( OldSource_FloorDiv( pPos[s * 3], cell ), OldSource_FloorDiv( pPos[s * 3 + 1], cell ), OldSource_FloorDiv( pPos[s * 3 + 2], cell ) );
			pSorted[pFill[c]++] = s;
		}

		for ( int e = 0; e < nEmpty; e++ )
		{
			int i = pEmpty[e];
			const OldSourceLeafBox &box = pBoxes[i];
			float ctr[3];
			int a0[3], a1[3];
			for ( int a = 0; a < 3; a++ )
			{
				ctr[a] = ( box.mins[a] + box.maxs[a] ) * 0.5f;
				a0[a] = OldSource_FloorDiv( box.mins[a], cell );
				a1[a] = OldSource_FloorDiv( box.maxs[a], cell );
			}
			int best = -1;
			float bestD = 1e30f, bestC = 1e30f;
			for ( int r = 0; ; r++ )
			{
				bool covered = true;
				int b0[3], b1[3];
				for ( int a = 0; a < 3; a++ )
				{
					if ( a0[a] - r > lo[a] || a1[a] + r < hi[a] )
						covered = false;
					b0[a] = a0[a] - r < lo[a] ? lo[a] : ( a0[a] - r > hi[a] ? hi[a] : a0[a] - r );
					b1[a] = a1[a] + r > hi[a] ? hi[a] : ( a1[a] + r < lo[a] ? lo[a] : a1[a] + r );
				}
				for ( int x = b0[0]; x <= b1[0]; x++ )
				for ( int y = b0[1]; y <= b1[1]; y++ )
				for ( int z = b0[2]; z <= b1[2]; z++ )
				{
					// cells inside the previous round's box were looked at already
					if ( r > 0 && x > a0[0] - r && x < a1[0] + r && y > a0[1] - r && y < a1[1] + r && z > a0[2] - r && z < a1[2] + r )
						continue;
					int c = OLDSOURCE_CELL( x, y, z );
					for ( int k = pStart[c]; k < pStart[c + 1]; k++ )
					{
						int s = pSorted[k];
						const float *p = pPos + s * 3;
						float d = OldSource_BoxDistance( p, box );
						float dc = sqrtf( ( p[0] - ctr[0] ) * ( p[0] - ctr[0] ) + ( p[1] - ctr[1] ) * ( p[1] - ctr[1] ) + ( p[2] - ctr[2] ) * ( p[2] - ctr[2] ) );
						if ( d < bestD || ( d == bestD && dc < bestC ) )
						{
							best = s;
							bestD = d;
							bestC = dc;
						}
					}
				}
				if ( ( best >= 0 && bestD <= r * cell ) || covered )
					break;
			}
			if ( best >= 0 )
			{
				// the nearest sample's own cube (not the average of its leaf)
				memcpy( pOut + i * 24, pSamples + best * 28, 24 );
				nFilled++;
			}
		}
#undef OLDSOURCE_CELL
		free( pStart );
		free( pFill );
		free( pSorted );
	}
	free( pPos );
	free( pOwned );
	free( pEmpty );
	return nFilled;
}

//-----------------------------------------------------------------------------
// VTF 7.3 and newer (with a resource list) -> VTF 7.2 (80-byte header, low-res image,
// high-res image). Cubemaps keep 7 faces (the old engine expects the sphere map face).
//-----------------------------------------------------------------------------
inline int OldSource_VTFImageSize( int fmt, int w, int h )
{
	if ( w < 1 ) w = 1;
	if ( h < 1 ) h = 1;
	switch ( fmt )
	{
	case 13: case 20:	// DXT1, DXT1 one bit alpha
		return ( ( w + 3 ) / 4 ) * ( ( h + 3 ) / 4 ) * 8;
	case 14: case 15:	// DXT3, DXT5
		return ( ( w + 3 ) / 4 ) * ( ( h + 3 ) / 4 ) * 16;
	case 0: case 1: case 11: case 12: case 16: case 23: case 26:
		return w * h * 4;
	case 2: case 3: case 9: case 10:
		return w * h * 3;
	case 4: case 6: case 17: case 18: case 19: case 21: case 22:
		return w * h * 2;
	case 5: case 7: case 8:
		return w * h;
	case 24: case 25:	// RGBA16161616F, RGBA16161616
		return w * h * 8;
	}
	return -1;
}

inline unsigned int OldSource_U32( const unsigned char *p ) { unsigned int v; memcpy( &v, p, 4 ); return v; }
inline unsigned short OldSource_U16( const unsigned char *p ) { unsigned short v; memcpy( &v, p, 2 ); return v; }

inline int OldSource_VTFMipDim( int v, int m )
{
	v >>= m;
	return v < 1 ? 1 : v;
}

// Bytes of the high-res image block for a face count; -1 for an unknown format.
inline int OldSource_VTFHighResSize( int fmt, int w, int h, int depth, int mips, int frames, int faces )
{
	int total = 0;
	for ( int m = 0; m < mips; m++ )
	{
		int s = OldSource_VTFImageSize( fmt, OldSource_VTFMipDim( w, m ), OldSource_VTFMipDim( h, m ) );
		if ( s < 0 )
			return -1;
		total += s * OldSource_VTFMipDim( depth, m ) * frames * faces;
	}
	return total;
}

// Converts pIn into pOut (pOut may be NULL to ask for the size). Returns the size of the
// VTF 7.2 file, or 0 when the file isn't a VTF 7.3+ that can be converted (it is then
// kept as it is).
inline int OldSource_VTFTo72( const unsigned char *pIn, int nInLen, unsigned char *pOut )
{
	if ( nInLen < 80 || memcmp( pIn, "VTF\0", 4 ) != 0 )
		return 0;
	unsigned int major = OldSource_U32( pIn + 4 ), minor = OldSource_U32( pIn + 8 );
	if ( major != 7 || minor < 3 )
		return 0;
	int w = OldSource_U16( pIn + 16 ), h = OldSource_U16( pIn + 18 );
	unsigned int flags = OldSource_U32( pIn + 20 );
	int frames = OldSource_U16( pIn + 24 );
	int firstFrame = OldSource_U16( pIn + 26 );
	int hiFmt = (int)OldSource_U32( pIn + 52 );
	int mips = pIn[56];
	int lowFmt = (int)OldSource_U32( pIn + 57 );
	int lowW = pIn[61], lowH = pIn[62];
	int depth = OldSource_U16( pIn + 63 );
	if ( frames < 1 ) frames = 1;
	if ( mips < 1 ) mips = 1;
	if ( depth < 1 ) depth = 1;

	int numRes = (int)OldSource_U32( pIn + 68 );
	if ( numRes < 0 || numRes > 32 || 80 + numRes * 8 > nInLen )
		return 0;
	int hiOfs = -1, loOfs = -1;
	for ( int r = 0; r < numRes; r++ )
	{
		const unsigned char *e = pIn + 80 + r * 8;
		unsigned int tag = e[0] | ( e[1] << 8 ) | ( e[2] << 16 );
		if ( e[3] & 2 )
			continue;	// value stored in the entry itself, no data
		int ofs = (int)OldSource_U32( e + 4 );
		if ( tag == 0x30 ) hiOfs = ofs;
		if ( tag == 0x01 ) loOfs = ofs;
	}
	if ( hiOfs < 0 || hiOfs > nInLen )
		return 0;

	// the low-res image (thumbnail), if there is a usable one
	int loSize = 0;
	if ( loOfs >= 0 && lowFmt != -1 && lowW > 0 && lowH > 0 )
	{
		loSize = OldSource_VTFImageSize( lowFmt, lowW, lowH );
		if ( loSize < 0 || loOfs + loSize > nInLen )
			loSize = 0;
	}
	if ( loSize == 0 )
	{
		lowFmt = -1;
		lowW = lowH = 0;
	}

	// faces: cubemaps of 7.5 and newer have no sphere map; the old engine wants 7 faces
	bool envmap = ( flags & 0x4000 ) != 0;
	int faces = 1, outFaces = 1;
	if ( envmap )
	{
		int six = OldSource_VTFHighResSize( hiFmt, w, h, depth, mips, frames, 6 );
		int seven = OldSource_VTFHighResSize( hiFmt, w, h, depth, mips, frames, 7 );
		if ( six < 0 )
			return 0;
		int avail = nInLen - hiOfs;
		if ( minor < 5 && firstFrame != 0xFFFF && seven <= avail )
			faces = 7;
		else if ( six <= avail )
			faces = 6;
		else if ( seven <= avail )
			faces = 7;
		else
			return 0;
		outFaces = 7;
	}
	int hiSize = OldSource_VTFHighResSize( hiFmt, w, h, depth, mips, frames, faces );
	int outHiSize = OldSource_VTFHighResSize( hiFmt, w, h, depth, mips, frames, outFaces );
	if ( hiSize < 0 || hiOfs + hiSize > nInLen )
		return 0;

	int total = 80 + loSize + outHiSize;
	if ( !pOut )
		return total;

	// header: the 7.2 fields are the start of the newer header
	memset( pOut, 0, 80 );
	memcpy( pOut, pIn, 65 );
	unsigned int v2 = 2, hs = 80;
	memcpy( pOut + 8, &v2, 4 );
	memcpy( pOut + 12, &hs, 4 );
	if ( envmap && ( firstFrame == 0xFFFF || firstFrame >= frames ) )
	{
		unsigned short ff = 0;
		memcpy( pOut + 26, &ff, 2 );
	}
	memcpy( pOut + 57, &lowFmt, 4 );
	pOut[61] = (unsigned char)lowW;
	pOut[62] = (unsigned char)lowH;
	unsigned char *o = pOut + 80;
	if ( loSize )
	{
		memcpy( o, pIn + loOfs, loSize );
		o += loSize;
	}
	if ( faces == outFaces )
	{
		memcpy( o, pIn + hiOfs, hiSize );
		return total;
	}
	// 6 faces -> 7: mips are stored smallest first, then frames, faces, depth slices; the
	// sphere map face (only used by the oldest graphics cards) is a copy of the first face
	const unsigned char *src = pIn + hiOfs;
	for ( int m = mips - 1; m >= 0; m-- )
	{
		int slice = OldSource_VTFImageSize( hiFmt, OldSource_VTFMipDim( w, m ), OldSource_VTFMipDim( h, m ) );
		int faceBytes = slice * OldSource_VTFMipDim( depth, m );
		for ( int fr = 0; fr < frames; fr++ )
		{
			memcpy( o, src, faceBytes * 6 );
			memcpy( o + faceBytes * 6, src, faceBytes );
			o += faceBytes * 7;
			src += faceBytes * 6;
		}
	}
	return total;
}

#endif // OLDSOURCEFORMAT_H
