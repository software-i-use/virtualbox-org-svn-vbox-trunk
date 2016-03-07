/* $Id$ */
/** @file
 * BS3Kit - Bs3TestSub, Bs3TestSubF, Bs3TestSubV.
 */

/*
 * Copyright (C) 2007-2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "bs3kit-template-header.h"
#include "bs3-cmn-test.h"



/**
 * Equivalent to RTTestISubV.
 */
BS3_DECL(void) Bs3TestSubV(const char *pszFormat, va_list va)
{
    size_t cch;

    /*
     * Cleanup any previous sub-test.
     */
    bs3TestSubCleanup();

    /*
     * Format the sub-test name and update globals.
     */
    cch = Bs3StrPrintfV(BS3_DATA_NM(g_szBs3SubTest), sizeof(BS3_DATA_NM(g_szBs3SubTest)), pszFormat, va);
    BS3_DATA_NM(g_cusBs3SubTestAtErrors) = BS3_DATA_NM(g_cusBs3TestErrors);
    BS3_ASSERT(!BS3_DATA_NM(g_fbBs3SubTestSkipped));

    /*
     * Tell VMMDev and output to the console.
     */
    bs3TestSendCmdWithStr(VMMDEV_TESTING_CMD_SUB_NEW, BS3_DATA_NM(g_szBs3SubTest));

    Bs3PrintStr(BS3_DATA_NM(g_szBs3SubTest));
    Bs3PrintChr(':');
    do
       Bs3PrintChr(' ');
    while (cch++ < 49);
    Bs3PrintStr(" TESTING\n");
}


/**
 * Equivalent to RTTestIFailedF.
 */
BS3_DECL(void) Bs3TestSubF(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    Bs3TestSubV(pszFormat, va);
    va_end(va);
}


/**
 * Equivalent to RTTestISub.
 */
BS3_DECL(void) Bs3TestSub(const char *pszMessage)
{
    Bs3TestSubF("%s", pszMessage);
}

