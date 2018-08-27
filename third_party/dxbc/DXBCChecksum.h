// Xenia: removed Windows.h dependency.

//=====================================================================
// Copyright 2008-2016 (c), Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools Team
/// \file DXBCChecksum.h
///
//=====================================================================

//=====================================================================
// $Id: //devtools/main/Common/Src/ShaderUtils/DX10/DXBCChecksum.h#4 $
//
// Last checkin:  $DateTime: 2016/04/18 06:01:26 $
// Last edited by: $Author:  AMD Developer Tools Team
//=====================================================================

#pragma once
#ifndef DXBCCHECKSUM_H
#define DXBCCHECKSUM_H

/*
 **********************************************************************
 ** MD5.h                                                            **
 **                                                                  **
 ** - Style modified by Tony Ray, January 2001                       **
 **   Added support for randomizing initialization constants         **
 ** - Style modified by Dominik Reichl, September 2002               **
 **   Optimized code                                                 **
 **                                                                  **
 **********************************************************************
 */

/*
 **********************************************************************
 ** MD5.h -- Header file for implementation of MD5                   **
 ** RSA Data Security, Inc. MD5 Message Digest Algorithm             **
 ** Created: 2/17/90 RLR                                             **
 ** Revised: 12/27/90 SRD,AJ,BSK,JT Reference C version              **
 ** Revised (for MD5): RLR 4/27/91                                   **
 **   -- G modified to have y&~z instead of y&z                      **
 **   -- FF, GG, HH modified to add in last register done            **
 **   -- Access pattern: round 2 works mod 5, round 3 works mod 3    **
 **   -- distinct additive constant for each step                    **
 **   -- round 4 added, working mod 7                                **
 **********************************************************************
 */

/*
 **********************************************************************
 ** Copyright (C) 1990, RSA Data Security, Inc. All rights reserved. **
 **                                                                  **
 ** License to copy and use this software is granted provided that   **
 ** it is identified as the "RSA Data Security, Inc. MD5 Message     **
 ** Digest Algorithm" in all material mentioning or referencing this **
 ** software or this function.                                       **
 **                                                                  **
 ** License is also granted to make and use derivative works         **
 ** provided that such works are identified as "derived from the RSA **
 ** Data Security, Inc. MD5 Message Digest Algorithm" in all         **
 ** material mentioning or referencing the derived work.             **
 **                                                                  **
 ** RSA Data Security, Inc. makes no representations concerning      **
 ** either the merchantability of this software or the suitability   **
 ** of this software for any particular purpose.  It is provided "as **
 ** is" without express or implied warranty of any kind.             **
 **                                                                  **
 ** These notices must be retained in any copies of any part of this **
 ** documentation and/or software.                                   **
 **********************************************************************
 */

/// Calculate the DXBC checksum for the provided chunk of memory.
/// \param[in] pData    A pointer to the memory to checksum.
/// \param[in] dwSize   The size of the memory to checksum.
/// \param[out] dwHash  A DWORD array to copy the calculated check sum into.
void CalculateDXBCChecksum(unsigned char* pData, unsigned int dwSize, unsigned int dwHash[4]);

#endif // DXBCCHECKSUM_H
