/* $Id$ */
/** @file
 * BS3Kit - Bs3TestInit
 */

/*
 * Copyright (C) 2007-2015 Oracle Corporation
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
 * Equivalent to RTTestCreate + RTTestBanner.
 *
 * @param   pszTest         The test name.
 */
BS3_DECL(void) Bs3TestInit(const char BS3_FAR *pszTest)
{
    /*
     * Initialize the globals.
     */
    BS3_CMN_NM(g_pszBs3Test)                 = pszTest;
    BS3_DATA_NM(g_szBs3SubTest)[0]           = '\0';
    BS3_DATA_NM(g_cusBs3TestErrors)          = 0;
    BS3_DATA_NM(g_cusBs3SubTestAtErrors)     = 0;
    BS3_DATA_NM(g_fbBs3SubTestReported)      = true;
    BS3_DATA_NM(g_fbBs3SubTestSkipped)       = false;
    BS3_DATA_NM(g_cusBs3SubTests)            = 0;
    BS3_DATA_NM(g_cusBs3SubTestsFailed)      = 0;
    BS3_DATA_NM(g_fbBs3VMMDevTesting)        = bs3TestIsVmmDevTestingPresent();

    /*
     * Print the name - RTTestBanner.
     */
    Bs3PrintStr(pszTest);
    Bs3PrintStr(": TESTING...\r\n");

    /*
     * Report it to the VMMDev.
     */
    bs3TestSendCmdWithStr(VMMDEV_TESTING_CMD_INIT, pszTest);
}

