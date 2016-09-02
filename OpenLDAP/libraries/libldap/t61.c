/* $OpenLDAP: pkg/ldap/libraries/libldap/t61.c,v 1.2.2.2 2003/03/24 03:08:22 kurt Exp $ */
/*
 * Copyright 2002-2003 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

/*
 * Basic T.61 <-> UTF-8 conversion
 *
 * These routines will perform a lossless translation from T.61 to UTF-8
 * and a lossy translation from UTF-8 to T.61.
 */

#include "portable.h"

#include <stdio.h>

#include <ac/stdlib.h>

#include <ac/socket.h>
#include <ac/string.h>
#include <ac/time.h>

#include "ldap-int.h"
#include "ldap_utf8.h"

#include "ldap_defaults.h"

/*
 * T.61 is somewhat braindead; even in the 7-bit space it is not
 * completely equivalent to 7-bit US-ASCII. Our definition of the
 * character set comes from RFC 1345 with a slightly more readable
 * rendition at http://std.dkuug.dk/i18n/charmaps/T.61-8BIT.
 *
 * Even though '#' and '$' are present in the 7-bit US-ASCII space,
 * (x23 and x24, resp.) in T.61 they are mapped to 8-bit characters
 * xA6 and xA4. 
 *
 * Also T.61 lacks
 *	backslash 	\	(x5C)
 *	caret		^	(x5E)
 *	backquote	`	(x60)
 *	left brace	{	(x7B)
 *	right brace	}	(x7D)
 *	tilde		~	(x7E)
 *
 * In T.61, the codes xC1 to xCF (excluding xC9, unused) are non-spacing
 * accents of some form or another. There are predefined combinations
 * for certain characters, but they can also be used arbitrarily. The
 * table at dkuug.dk maps these accents to the E000 "private use" range
 * of the Unicode space, but I believe they more properly belong in the
 * 0300 range (non-spacing accents). The transformation is complicated
 * slightly because Unicode wants the non-spacing character to follow
 * the base character, while T.61 has the non-spacing character leading.
 * Also, T.61 specifically recognizes certain combined pairs as "characters"
 * but doesn't specify how to treat unrecognized pairs. This code will
 * always attempt to combine pairs when a known Unicode composite exists.
 */

static const wchar_t t61_tab[] = {
	0x000, 0x001, 0x002, 0x003, 0x004, 0x005, 0x006, 0x007,
	0x008, 0x009, 0x00a, 0x00b, 0x00c, 0x00d, 0x00e, 0x00f,
	0x010, 0x011, 0x012, 0x013, 0x014, 0x015, 0x016, 0x017,
	0x018, 0x019, 0x01a, 0x01b, 0x01c, 0x01d, 0x01e, 0x01f,
	0x020, 0x021, 0x022, 0x000, 0x000, 0x025, 0x026, 0x027,
	0x028, 0x029, 0x02a, 0x02b, 0x02c, 0x02d, 0x02e, 0x02f,
	0x030, 0x031, 0x032, 0x033, 0x034, 0x035, 0x036, 0x037,
	0x038, 0x039, 0x03a, 0x03b, 0x03c, 0x03d, 0x03e, 0x03f,
	0x040, 0x041, 0x042, 0x043, 0x044, 0x045, 0x046, 0x047,
	0x048, 0x049, 0x04a, 0x04b, 0x04c, 0x04d, 0x04e, 0x04f,
	0x050, 0x051, 0x052, 0x053, 0x054, 0x055, 0x056, 0x057,
	0x058, 0x059, 0x05a, 0x05b, 0x000, 0x05d, 0x000, 0x05f,
	0x000, 0x061, 0x062, 0x063, 0x064, 0x065, 0x066, 0x067,
	0x068, 0x069, 0x06a, 0x06b, 0x06c, 0x06d, 0x06e, 0x06f,
	0x070, 0x071, 0x072, 0x073, 0x074, 0x075, 0x076, 0x077,
	0x078, 0x079, 0x07a, 0x000, 0x07c, 0x000, 0x000, 0x07f,
	0x080, 0x081, 0x082, 0x083, 0x084, 0x085, 0x086, 0x087,
	0x088, 0x089, 0x08a, 0x08b, 0x08c, 0x08d, 0x08e, 0x08f,
	0x090, 0x091, 0x092, 0x093, 0x094, 0x095, 0x096, 0x097,
	0x098, 0x099, 0x09a, 0x09b, 0x09c, 0x09d, 0x09e, 0x09f,
	0x0a0, 0x0a1, 0x0a2, 0x0a3, 0x024, 0x0a5, 0x023, 0x0a7,
	0x0a4, 0x000, 0x000, 0x0ab, 0x000, 0x000, 0x000, 0x000,
	0x0b0, 0x0b1, 0x0b2, 0x0b3, 0x0d7, 0x0b5, 0x0b6, 0x0b7,
	0x0f7, 0x000, 0x000, 0x0bb, 0x0bc, 0x0bd, 0x0be, 0x0bf,
	0x000, 0x300, 0x301, 0x302, 0x303, 0x304, 0x306, 0x307,
	0x308, 0x000, 0x30a, 0x327, 0x332, 0x30b, 0x328, 0x30c,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x2126, 0xc6, 0x0d0, 0x0aa, 0x126, 0x000, 0x132, 0x13f,
	0x141, 0x0d8, 0x152, 0x0ba, 0x0de, 0x166, 0x14a, 0x149,
	0x138, 0x0e6, 0x111, 0x0f0, 0x127, 0x131, 0x133, 0x140,
	0x142, 0x0f8, 0x153, 0x0df, 0x0fe, 0x167, 0x14b, 0x000
};

typedef wchar_t wvec16[16];
typedef wchar_t wvec32[32];
typedef wchar_t wvec64[64];

/* Substitutions when 0xc1-0xcf appears by itself or with space 0x20 */
static const wvec16 accents = {
	0x000, 0x060, 0x0b4, 0x05e, 0x07e, 0x0af, 0x2d8, 0x2d9,
	0x0a8, 0x000, 0x2da, 0x0b8, 0x000, 0x2dd, 0x2db, 0x2c7};

/* In the following tables, base characters commented in (parentheses)
 * are not defined by T.61 but are mapped anyway since their Unicode
 * composite exists.
 */

/* Grave accented chars AEIOU (NWY) */
static const wvec32 c1_vec1 = {
	/* Upper case */
	0, 0xc0, 0, 0, 0, 0xc8, 0, 0, 0, 0xcc, 0, 0, 0, 0, 0x1f8, 0xd2,
	0, 0, 0, 0, 0, 0xd9, 0, 0x1e80, 0, 0x1ef2, 0, 0, 0, 0, 0, 0};
static const wvec32 c1_vec2 = {
	/* Lower case */
	0, 0xe0, 0, 0, 0, 0xe8, 0, 0, 0, 0xec, 0, 0, 0, 0, 0x1f9, 0xf2,
	0, 0, 0, 0, 0, 0xf9, 0, 0x1e81, 0, 0x1ef3, 0, 0, 0, 0, 0, 0};
	
static const wvec32 *c1_grave[] = {
	NULL, NULL, &c1_vec1, &c1_vec2, NULL, NULL, NULL, NULL
};

/* Acute accented chars AEIOUYCLNRSZ (GKMPW) */
static const wvec32 c2_vec1 = {
	/* Upper case */
	0, 0xc1, 0, 0x106, 0, 0xc9, 0, 0x1f4,
	0, 0xcd, 0, 0x1e30, 0x139, 0x1e3e, 0x143, 0xd3,
	0x1e54, 0, 0x154, 0x15a, 0, 0xda, 0, 0x1e82,
	0, 0xdd, 0x179, 0, 0, 0, 0, 0};
static const wvec32 c2_vec2 = {
	/* Lower case */
	0, 0xe1, 0, 0x107, 0, 0xe9, 0, 0x1f5,
	0, 0xed, 0, 0x1e31, 0x13a, 0x1e3f, 0x144, 0xf3,
	0x1e55, 0, 0x155, 0x15b, 0, 0xfa, 0, 0x1e83,
	0, 0xfd, 0x17a, 0, 0, 0, 0, 0};
static const wvec32 c2_vec3 = {
	/* (AE and ae) */
	0, 0x1fc, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0x1fd, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static const wvec32 *c2_acute[] = {
	NULL, NULL, &c2_vec1, &c2_vec2, NULL, NULL, NULL, &c2_vec3
};

/* Circumflex AEIOUYCGHJSW (Z) */
static const wvec32 c3_vec1 = {
	/* Upper case */
	0, 0xc2, 0, 0x108, 0, 0xca, 0, 0x11c,
	0x124, 0xce, 0x134, 0, 0, 0, 0, 0xd4,
	0, 0, 0, 0x15c, 0, 0xdb, 0, 0x174,
	0, 0x176, 0x1e90, 0, 0, 0, 0, 0};
static const wvec32 c3_vec2 = {
	/* Lower case */
	0, 0xe2, 0, 0x109, 0, 0xea, 0, 0x11d,
	0x125, 0xee, 0x135, 0, 0, 0, 0, 0xf4,
	0, 0, 0, 0x15d, 0, 0xfb, 0, 0x175,
	0, 0x177, 0x1e91, 0, 0, 0, 0, 0};
static const wvec32 *c3_circumflex[] = {
	NULL, NULL, &c3_vec1, &c3_vec2, NULL, NULL, NULL, NULL
};

/* Tilde AIOUN (EVY) */
static const wvec32 c4_vec1 = {
	/* Upper case */
	0, 0xc3, 0, 0, 0, 0x1ebc, 0, 0, 0, 0x128, 0, 0, 0, 0, 0xd1, 0xd5,
	0, 0, 0, 0, 0, 0x168, 0x1e7c, 0, 0, 0x1ef8, 0, 0, 0, 0, 0, 0};
static const wvec32 c4_vec2 = {
	/* Lower case */
	0, 0xe3, 0, 0, 0, 0x1ebd, 0, 0, 0, 0x129, 0, 0, 0, 0, 0xf1, 0xf5,
	0, 0, 0, 0, 0, 0x169, 0x1e7d, 0, 0, 0x1ef9, 0, 0, 0, 0, 0, 0};
static const wvec32 *c4_tilde[] = {
	NULL, NULL, &c4_vec1, &c4_vec2, NULL, NULL, NULL, NULL
};

/* Macron AEIOU (YG) */
static const wvec32 c5_vec1 = {
	/* Upper case */
	0, 0x100, 0, 0, 0, 0x112, 0, 0x1e20, 0, 0x12a, 0, 0, 0, 0, 0, 0x14c,
	0, 0, 0, 0, 0, 0x16a, 0, 0, 0, 0x232, 0, 0, 0, 0, 0, 0};
static const wvec32 c5_vec2 = {
	/* Lower case */
	0, 0x101, 0, 0, 0, 0x113, 0, 0x1e21, 0, 0x12b, 0, 0, 0, 0, 0, 0x14d,
	0, 0, 0, 0, 0, 0x16b, 0, 0, 0, 0x233, 0, 0, 0, 0, 0, 0};
static const wvec32 c5_vec3 = {
	/* (AE and ae) */
	0, 0x1e2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0x1e3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const wvec32 *c5_macron[] = {
	NULL, NULL, &c5_vec1, &c5_vec2, NULL, NULL, NULL, &c5_vec3
};

/* Breve AUG (EIO) */
static const wvec32 c6_vec1 = {
	/* Upper case */
	0, 0x102, 0, 0, 0, 0x114, 0, 0x11e, 0, 0x12c, 0, 0, 0, 0, 0, 0x14e,
	0, 0, 0, 0, 0, 0x16c, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const wvec32 c6_vec2 = {
	/* Lower case */
	0, 0x103, 0, 0, 0, 0x115, 0, 0x11f, 0, 0x12d, 0, 0, 0, 0, 0, 0x14f,
	0, 0, 0, 0, 0, 0x16d, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const wvec32 *c6_breve[] = {
	NULL, NULL, &c6_vec1, &c6_vec2, NULL, NULL, NULL, NULL
};

/* Dot Above CEGIZ (AOBDFHMNPRSTWXY) */
static const wvec32 c7_vec1 = {
	/* Upper case */
	0, 0x226, 0x1e02, 0x10a, 0x1e0a, 0x116, 0x1e1e, 0x120,
	0x1e22, 0x130, 0, 0, 0, 0x1e40, 0x1e44, 0x22e,
	0x1e56, 0, 0x1e58, 0x1e60, 0x1e6a, 0, 0, 0x1e86,
	0x1e8a, 0x1e8e, 0x17b, 0, 0, 0, 0, 0};
static const wvec32 c7_vec2 = {
	/* Lower case */
	0, 0x227, 0x1e03, 0x10b, 0x1e0b, 0x117, 0x1e1f, 0x121,
	0x1e23, 0, 0, 0, 0, 0x1e41, 0x1e45, 0x22f,
	0x1e57, 0, 0x1e59, 0x1e61, 0x1e6b, 0, 0, 0x1e87,
	0x1e8b, 0x1e8f, 0x17c, 0, 0, 0, 0, 0};
static const wvec32 *c7_dotabove[] = {
	NULL, NULL, &c7_vec1, &c7_vec2, NULL, NULL, NULL, NULL
};

/* Diaeresis AEIOUY (HWXt) */
static const wvec32 c8_vec1 = {
	/* Upper case */
	0, 0xc4, 0, 0, 0, 0xcb, 0, 0, 0x1e26, 0xcf, 0, 0, 0, 0, 0, 0xd6,
	0, 0, 0, 0, 0, 0xdc, 0, 0x1e84, 0x1e8c, 0x178, 0, 0, 0, 0, 0, 0};
static const wvec32 c8_vec2 = {
	/* Lower case */
	0, 0xe4, 0, 0, 0, 0xeb, 0, 0, 0x1e27, 0xef, 0, 0, 0, 0, 0, 0xf6,
	0, 0, 0, 0, 0x1e97, 0xfc, 0, 0x1e85, 0x1e8d, 0xff, 0, 0, 0, 0, 0, 0};
static const wvec32 *c8_diaeresis[] = {
	NULL, NULL, &c8_vec1, &c8_vec2, NULL, NULL, NULL, NULL
};

/* Ring Above AU (wy) */
static const wvec32 ca_vec1 = {
	/* Upper case */
	0, 0xc5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0x16e, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const wvec32 ca_vec2 = {
	/* Lower case */
	0, 0xe5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0x16f, 0, 0x1e98, 0, 0x1e99, 0, 0, 0, 0, 0, 0};
static const wvec32 *ca_ringabove[] = {
	NULL, NULL, &ca_vec1, &ca_vec2, NULL, NULL, NULL, NULL
};

/* Cedilla CGKLNRST (EDH) */
static const wvec32 cb_vec1 = {
	/* Upper case */
	0, 0, 0, 0xc7, 0x1e10, 0x228, 0, 0x122,
	0x1e28, 0, 0, 0x136, 0x13b, 0, 0x145, 0,
	0, 0, 0x156, 0x15e, 0x162, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const wvec32 cb_vec2 = {
	/* Lower case */
	0, 0, 0, 0xe7, 0x1e11, 0x229, 0, 0x123,
	0x1e29, 0, 0, 0x137, 0x13c, 0, 0x146, 0,
	0, 0, 0x157, 0x15f, 0x163, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const wvec32 *cb_cedilla[] = {
	NULL, NULL, &cb_vec1, &cb_vec2, NULL, NULL, NULL, NULL
};

/* Double Acute Accent OU */
static const wvec32 cd_vec1 = {
	/* Upper case */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x150,
	0, 0, 0, 0, 0, 0x170, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const wvec32 cd_vec2 = {
	/* Lower case */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x151,
	0, 0, 0, 0, 0, 0x171, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const wvec32 *cd_doubleacute[] = {
	NULL, NULL, &cd_vec1, &cd_vec2, NULL, NULL, NULL, NULL
};

/* Ogonek AEIU (O) */
static const wvec32 ce_vec1 = {
	/* Upper case */
	0, 0x104, 0, 0, 0, 0x118, 0, 0, 0, 0x12e, 0, 0, 0, 0, 0, 0x1ea,
	0, 0, 0, 0, 0, 0x172, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const wvec32 ce_vec2 = {
	/* Lower case */
	0, 0x105, 0, 0, 0, 0x119, 0, 0, 0, 0x12f, 0, 0, 0, 0, 0, 0x1eb,
	0, 0, 0, 0, 0, 0x173, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const wvec32 *ce_ogonek[] = {
	NULL, NULL, &ce_vec1, &ce_vec2, NULL, NULL, NULL, NULL
};

/* Caron CDELNRSTZ (AIOUGKjH) */
static const wvec32 cf_vec1 = {
	/* Upper case */
	0, 0x1cd, 0, 0x10c, 0x10e, 0x11a, 0, 0x1e6,
	0x21e, 0x1cf, 0, 0x1e8, 0x13d, 0, 0x147, 0x1d1,
	0, 0, 0x158, 0x160, 0x164, 0x1d3, 0, 0,
	0, 0, 0x17d, 0, 0, 0, 0, 0};
static const wvec32 cf_vec2 = {
	/* Lower case */
	0, 0x1ce, 0, 0x10d, 0x10f, 0x11b, 0, 0x1e7,
	0x21f, 0x1d0, 0x1f0, 0x1e9, 0x13e, 0, 0x148, 0x1d2,
	0, 0, 0x159, 0x161, 0x165, 0x1d4, 0, 0,
	0, 0, 0x17e, 0, 0, 0, 0, 0};
static const wvec32 *cf_caron[] = {
	NULL, NULL, &cf_vec1, &cf_vec2, NULL, NULL, NULL, NULL
};

static const wvec32 **cx_tab[] = {
	NULL, c1_grave, c2_acute, c3_circumflex, c4_tilde, c5_macron,
	c6_breve, c7_dotabove, c8_diaeresis, NULL, ca_ringabove,
	cb_cedilla, NULL, cd_doubleacute, ce_ogonek, cf_caron };

int ldap_t61s_valid( struct berval *str )
{
	unsigned char *c = (unsigned char *)str->bv_val;
	int i;

	for (i=0; i < str->bv_len; c++,i++)
		if (!t61_tab[*c])
			return 0;
	return 1;
}

/* Transform a T.61 string to UTF-8.
 */
int ldap_t61s_to_utf8s( struct berval *src, struct berval *dst )
{
	unsigned char *c;
	char *d;
	int i, wlen = 0;

	/* Just count the length of the UTF-8 result first */
	for (i=0,c=(unsigned char *)src->bv_val; i < src->bv_len; c++,i++) {
		/* Invalid T.61 characters? */
		if (!t61_tab[*c]) 
			return LDAP_INVALID_SYNTAX;
		if ((*c & 0xf0) == 0xc0) {
			int j = *c & 0x0f;
			/* If this is the end of the string, or if the base
			 * character is just a space, treat this as a regular
			 * spacing character.
			 */
			if ((!c[1] || c[1] == 0x20) && accents[j]) {
				wlen += ldap_x_wc_to_utf8(NULL, accents[j], 0);
			} else if (cx_tab[j] && cx_tab[j][c[1]>>5] &&
			/* We have a composite mapping for this pair */
				(*cx_tab[j][c[1]>>5])[c[1]&0x1f]) {
				wlen += ldap_x_wc_to_utf8( NULL,
					(*cx_tab[j][c[1]>>5])[c[1]&0x1f], 0);
			} else {
			/* No mapping, just swap it around so the base
			 * character comes first.
			 */
			 	wlen += ldap_x_wc_to_utf8(NULL, c[1], 0);
				wlen += ldap_x_wc_to_utf8(NULL,
					t61_tab[*c], 0);
			}
			c++; i++;
			continue;
		} else {
			wlen += ldap_x_wc_to_utf8(NULL, t61_tab[*c], 0);
		}
	}

	/* Now transform the string */
	dst->bv_len = wlen;
	dst->bv_val = LDAP_MALLOC( wlen+1 );
	d = dst->bv_val;
	if (!d)
		return LDAP_NO_MEMORY;

	for (i=0,c=(unsigned char *)src->bv_val; i < src->bv_len; c++,i++) {
		if ((*c & 0xf0) == 0xc0) {
			int j = *c & 0x0f;
			/* If this is the end of the string, or if the base
			 * character is just a space, treat this as a regular
			 * spacing character.
			 */
			if ((!c[1] || c[1] == 0x20) && accents[j]) {
				d += ldap_x_wc_to_utf8(d, accents[j], 6);
			} else if (cx_tab[j] && cx_tab[j][c[1]>>5] &&
			/* We have a composite mapping for this pair */
				(*cx_tab[j][c[1]>>5])[c[1]&0x1f]) {
				d += ldap_x_wc_to_utf8(d, 
				(*cx_tab[j][c[1]>>5])[c[1]&0x1f], 6);
			} else {
			/* No mapping, just swap it around so the base
			 * character comes first.
			 */
				d += ldap_x_wc_to_utf8(d, c[1], 6);
				d += ldap_x_wc_to_utf8(d, t61_tab[*c], 6);
			}
			c++; i++;
			continue;
		} else {
			d += ldap_x_wc_to_utf8(d, t61_tab[*c], 6);
		}
	}
	*d = '\0';
	return LDAP_SUCCESS;
}

/* For the reverse mapping, we just pay attention to the Latin-oriented
 * code blocks. These are
 *	0000 - 007f Basic Latin
 *	0080 - 00ff Latin-1 Supplement
 *	0100 - 017f Latin Extended-A
 *	0180 - 024f Latin Extended-B
 *	1e00 - 1eff Latin Extended Additional
 *
 * We have a special case to map Ohm U2126 back to T.61 0xe0. All other
 * unrecognized characters are replaced with '?' 0x3f.
 */

static const wvec64 u000 = {
	0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
	0x0008, 0x0009, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x000f,
	0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
	0x0018, 0x0019, 0x001a, 0x001b, 0x001c, 0x001d, 0x001e, 0x001f,
	0x0020, 0x0021, 0x0022, 0x00a6, 0x00a4, 0x0025, 0x0026, 0x0027,
	0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
	0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
	0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f};

/* In this range, we've mapped caret to xc3/x20, backquote to xc1/x20,
 * and tilde to xc4/x20. T.61 (stupidly!) doesn't define these characters
 * on their own, even though it provides them as combiners for other
 * letters. T.61 doesn't define these pairings either, so this may just
 * have to be replaced with '?' 0x3f if other software can't cope with it.
 */
static const wvec64 u001 = {
	0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
	0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
	0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
	0x0058, 0x0059, 0x005a, 0x005b, 0x003f, 0x005d, 0xc320, 0x005f,
	0xc120, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
	0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
	0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
	0x0078, 0x0079, 0x007a, 0x003f, 0x007c, 0x003f, 0xc420, 0x007f};

static const wvec64 u002 = {
	0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
	0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
	0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
	0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
	0x00a0, 0x00a1, 0x00a2, 0x00a3, 0x00a8, 0x00a5, 0x003f, 0x00a7,
	0xc820, 0x003f, 0x00e3, 0x00ab, 0x003f, 0x003f, 0x003f, 0xc520,
	0x00b0, 0x00b1, 0x00b2, 0x00b3, 0xc220, 0x00b5, 0x00b6, 0x00b7,
	0xcb20, 0x003f, 0x00eb, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x00bf};

static const wvec64 u003 = {
	0xc141, 0xc241, 0xc341, 0xc441, 0xc841, 0xca41, 0x00e1, 0xcb43,
	0xc145, 0xc245, 0xc345, 0xc845, 0xc149, 0xc249, 0xc349, 0xc849,
	0x00e2, 0xc44e, 0xc14f, 0xc24f, 0xc34f, 0xc44f, 0xc84f, 0x00b4,
	0x00e9, 0xc155, 0xc255, 0xc355, 0xc855, 0xc259, 0x00ec, 0x00fb,
	0xc161, 0xc261, 0xc361, 0xc461, 0xc861, 0xca61, 0x00f1, 0xcb63,
	0xc165, 0xc265, 0xc365, 0xc865, 0xc169, 0xc269, 0xc369, 0xc869,
	0x00f3, 0xc46e, 0xc16f, 0xc26f, 0xc36f, 0xc46f, 0xc86f, 0x00b8,
	0x00f9, 0xc175, 0xc275, 0xc375, 0xc875, 0xc279, 0x00fc, 0xc879};

/* These codes are used here but not defined by T.61:
 * x114 = xc6/x45, x115 = xc6/x65, x12c = xc6/x49, x12d = xc6/x69
 */
static const wvec64 u010 = {
	0xc541, 0xc561, 0xc641, 0xc661, 0xce41, 0xce61, 0xc243, 0xc263,
	0xc343, 0xc363, 0xc743, 0xc763, 0xcf43, 0xcf63, 0xcf44, 0xcf64,
	0x003f, 0x00f2, 0xc545, 0xc565, 0xc645, 0xc665, 0xc745, 0xc765,
	0xce45, 0xce65, 0xcf45, 0xcf65, 0xc347, 0xc367, 0xc647, 0xc667,
	0xc747, 0xc767, 0xcb47, 0xcb67, 0xc348, 0xc368, 0x00e4, 0x00f4,
	0xc449, 0xc469, 0xc549, 0xc569, 0xc649, 0xc669, 0xce49, 0xce69,
	0xc749, 0x00f5, 0x00e6, 0x00f6, 0xc34a, 0xc36a, 0xcb4b, 0xcb6b,
	0x00f0, 0xc24c, 0xc26c, 0xcb4c, 0xcb6c, 0xcf4c, 0xcf6c, 0x00e7};

/* These codes are used here but not defined by T.61:
 * x14e = xc6/x4f, x14f = xc6/x6f
 */
static const wvec64 u011 = {
	0x00f7, 0x00e8, 0x00f8, 0xc24e, 0xc26e, 0xcb4e, 0xcb6e, 0xcf4e,
	0xcf6e, 0x00ef, 0x00ee, 0x00fe, 0xc54f, 0xc56f, 0xc64f, 0xc66f,
	0xcd4f, 0xcd6f, 0x00ea, 0x00fa, 0xc252, 0xc272, 0xcb52, 0xcb72,
	0xcf52, 0xcf72, 0xc253, 0xc273, 0xc353, 0xc373, 0xcb53, 0xcb73,
	0xcf53, 0xcf73, 0xcb54, 0xcb74, 0xcf54, 0xcf74, 0x00ed, 0x00fd,
	0xc455, 0xc475, 0xc555, 0xc575, 0xc655, 0xc675, 0xca55, 0xca75,
	0xcd55, 0xcd75, 0xce55, 0xce75, 0xc357, 0xc377, 0xc359, 0xc379,
	0xc859, 0xc25a, 0xc27a, 0xc75a, 0xc77a, 0xcf5a, 0xcf7a, 0x003f};

/* All of the codes in this block are undefined in T.61.
 */
static const wvec64 u013 = {
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0xcf41, 0xcf61, 0xcf49,
	0xcf69, 0xcf4f, 0xcf6f, 0xcf55, 0xcf75, 0x003f, 0x003f, 0x003f, 
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0xc5e1, 0xc5f1, 0x003f, 0x003f, 0xcf47, 0xcf67,
	0xcf4b, 0xcf6b, 0xce4f, 0xce6f, 0x003f, 0x003f, 0x003f, 0x003f,
	0xcf6a, 0x003f, 0x003f, 0x003f, 0xc247, 0xc267, 0x003f, 0x003f,
	0xc14e, 0xc16e, 0x003f, 0x003f, 0xc2e1, 0xc2f1, 0x003f, 0x003f};

/* All of the codes in this block are undefined in T.61.
 */
static const wvec64 u020 = {
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0xcf48, 0xcf68,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0xc741, 0xc761,
	0xcb45, 0xcb65, 0x003f, 0x003f, 0x003f, 0x003f, 0xc74f, 0xc76f,
	0x003f, 0x003f, 0xc559, 0xc579, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f};

static const wvec64 u023 = {
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0xcf20,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0xc620, 0xc720, 0xca20, 0xce20, 0x003f, 0xcd20, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f};

/* These are the non-spacing characters by themselves. They should
 * never appear by themselves in actual text.
 */
static const wvec64 u030 = {
	0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x003f, 0x00c6, 0x00c7,
	0x00c8, 0x003f, 0x00ca, 0x00cd, 0x00cf, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x00cb,
	0x00ce, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x00cc, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f};

/* None of the following blocks are defined in T.61.
 */
static const wvec64 u1e0 = {
	0x003f, 0x003f, 0xc742, 0xc762, 0x003f, 0x003f, 0x003f, 0x003f, 
	0x003f, 0x003f, 0xc744, 0xc764, 0x003f, 0x003f, 0x003f, 0x003f,
	0xcb44, 0xcb64, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0xc746, 0xc766,
	0xc547, 0xc567, 0xc748, 0xc768, 0x003f, 0x003f, 0xc848, 0xc868,
	0xcb48, 0xcb68, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0xc24b, 0xc26b, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0xc24d, 0xc26d,
};

static const wvec64 u1e1 = {
	0xc74d, 0xc76d, 0x003f, 0x003f, 0xc74e, 0xc76e, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0xc250, 0xc270, 0xc750, 0xc770,
	0xc752, 0xc772, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0xc753, 0xc773, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0xc754, 0xc774, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0xc456, 0xc476, 0x003f, 0x003f,
};

static const wvec64 u1e2 = {
	0xc157, 0xc177, 0xc257, 0xc277, 0xc857, 0xc877, 0xc757, 0xc777,
	0x003f, 0x003f, 0xc758, 0xc778, 0xc858, 0xc878, 0xc759, 0xc779,
	0xc35a, 0xc37a, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0xc874,
	0xca77, 0xca79, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0xc445, 0xc465, 0x003f, 0x003f,
};

static const wvec64 u1e3 = {
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
	0x003f, 0x003f, 0xc159, 0xc179, 0x003f, 0x003f, 0x003f, 0x003f,
	0xc459, 0xc479, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f, 0x003f,
};

static const wvec64 *wc00[] = {
	&u000, &u001, &u002, &u003,
	&u010, &u011, NULL, &u013,
	&u020, NULL, NULL, &u023,
	&u030, NULL, NULL, NULL};

static const wvec64 *wc1e[] = {
	&u1e0, &u1e1, &u1e2, &u1e3};


int ldap_utf8s_to_t61s( struct berval *src, struct berval *dst )
{
	char *c, *d;
	wchar_t tmp;
	int i, j, tlen = 0;

	/* Just count the length of the T.61 result first */
	for (i=0,c=src->bv_val; i < src->bv_len;) {
		j = ldap_x_utf8_to_wc( &tmp, c );
		if (j == -1)
			return LDAP_INVALID_SYNTAX;
		switch (tmp >> 8) {
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
			if (wc00[tmp >> 6] &&
				((*wc00[tmp >> 6])[tmp & 0x3f] & 0xff00)) {
				tlen++;
			}
			tlen++;
			break;
		case 0x1e:
			if ((*wc1e[(tmp >> 6) & 3])[tmp & 0x3f] & 0xff00) {
				tlen++;
			}
		case 0x21:
		default:
			tlen ++;
			break;
		}
		i += j;
		c += j;
	}
	dst->bv_len = tlen;
	dst->bv_val = LDAP_MALLOC( tlen+1 );
	if (!dst->bv_val)
		return LDAP_NO_MEMORY;
	
	d = dst->bv_val;
	for (i=0,c=src->bv_val; i < src->bv_len;) {
		j = ldap_x_utf8_to_wc( &tmp, c );
		switch (tmp >> 8) {
		case 0x00:
		case 0x01:
		case 0x02:
			if (wc00[tmp >> 6]) {
				tmp = (*wc00[tmp >> 6])[tmp & 0x3f];
				if (tmp & 0xff00)
					*d++ = (tmp >> 8);
				*d++ = tmp & 0xff;
			} else {
				*d++ = 0x3f;
			}
			break;
		case 0x03:
			/* swap order of non-spacing characters */
			if (wc00[tmp >> 6]) {
				wchar_t t2 = (*wc00[tmp >> 6])[tmp & 0x3f];
				if (t2 != 0x3f) {
					d[0] = d[-1];
					d[-1] = t2;
					d++;
				} else {
					*d++ = 0x3f;
				}
			} else {
				*d++ = 0x3f;
			}
			break;
		case 0x1e:
			tmp = (*wc1e[(tmp >> 6) & 3])[tmp & 0x3f];
			if (tmp & 0xff00)
				*d++ = (tmp >> 8);
			*d++ = tmp & 0xff;
			break;
		case 0x21:
			if (tmp == 0x2126) {
				*d++ = 0xe0;
				break;
			}
			/* FALLTHRU */
		default:
			*d++ = 0x3f;
			break;
		}
	}
	*d = '\0';
	return LDAP_SUCCESS;
}
