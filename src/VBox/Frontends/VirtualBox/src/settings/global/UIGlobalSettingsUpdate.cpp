/* $Id$ */
/** @file
 * VBox Qt GUI - UIGlobalSettingsUpdate class implementation.
 */

/*
 * Copyright (C) 2006-2017 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifdef VBOX_WITH_PRECOMPILED_HEADERS
# include <precomp.h>
#else  /* !VBOX_WITH_PRECOMPILED_HEADERS */

/* GUI includes: */
# include "UIGlobalSettingsUpdate.h"
# include "UIExtraDataManager.h"
# include "VBoxGlobal.h"

#endif /* !VBOX_WITH_PRECOMPILED_HEADERS */


UIGlobalSettingsUpdate::UIGlobalSettingsUpdate()
    : m_pLastChosenRadio(0)
{
    /* Apply UI decorations: */
    Ui::UIGlobalSettingsUpdate::setupUi(this);

    /* Setup connections: */
    connect(m_pCheckBoxUpdate, SIGNAL(toggled(bool)), this, SLOT(sltUpdaterToggled(bool)));
    connect(m_pComboBoxUpdatePeriod, SIGNAL(activated(int)), this, SLOT(sltPeriodActivated()));

    /* Apply language settings: */
    retranslateUi();
}

void UIGlobalSettingsUpdate::loadToCacheFrom(QVariant &data)
{
    /* Fetch data to properties & settings: */
    UISettingsPageGlobal::fetchData(data);

    /* Clear cache initially: */
    m_cache.clear();

    /* Prepare old data: */
    UIDataSettingsGlobalUpdate oldData;

    /* Gather old data: */
    VBoxUpdateData updateData(gEDataManager->applicationUpdateData());
    oldData.m_fCheckEnabled = !updateData.isNoNeedToCheck();
    oldData.m_periodIndex = updateData.periodIndex();
    oldData.m_branchIndex = updateData.branchIndex();
    oldData.m_strDate = updateData.date();

    /* Cache old data: */
    m_cache.cacheInitialData(oldData);

    /* Upload properties & settings to data: */
    UISettingsPageGlobal::uploadData(data);
}

void UIGlobalSettingsUpdate::getFromCache()
{
    /* Get old data from cache: */
    const UIDataSettingsGlobalUpdate &oldData = m_cache.base();

    /* Load old data from cache: */
    m_pCheckBoxUpdate->setChecked(oldData.m_fCheckEnabled);
    if (m_pCheckBoxUpdate->isChecked())
    {
        m_pComboBoxUpdatePeriod->setCurrentIndex(oldData.m_periodIndex);
        if (oldData.m_branchIndex == VBoxUpdateData::BranchWithBetas)
            m_pRadioUpdateFilterBetas->setChecked(true);
        else if (oldData.m_branchIndex == VBoxUpdateData::BranchAllRelease)
            m_pRadioUpdateFilterEvery->setChecked(true);
        else
            m_pRadioUpdateFilterStable->setChecked(true);
    }
    m_pUpdateDateText->setText(oldData.m_strDate);
    sltUpdaterToggled(oldData.m_fCheckEnabled);
}

void UIGlobalSettingsUpdate::putToCache()
{
    /* Prepare new data: */
    UIDataSettingsGlobalUpdate newData = m_cache.base();

    /* Gather new data: */
    newData.m_periodIndex = periodType();
    newData.m_branchIndex = branchType();

    /* Cache new data: */
    m_cache.cacheCurrentData(newData);
}

void UIGlobalSettingsUpdate::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to properties & settings: */
    UISettingsPageGlobal::fetchData(data);

    /* Save new data from cache: */
    if (m_cache.wasChanged())
    {
        /* Gather corresponding values from internal variables: */
        VBoxUpdateData newData(m_cache.data().m_periodIndex, m_cache.data().m_branchIndex);
        gEDataManager->setApplicationUpdateData(newData.data());
    }

    /* Upload properties & settings to data: */
    UISettingsPageGlobal::uploadData(data);
}

void UIGlobalSettingsUpdate::setOrderAfter(QWidget *pWidget)
{
    /* Configure navigation: */
    setTabOrder(pWidget, m_pCheckBoxUpdate);
    setTabOrder(m_pCheckBoxUpdate, m_pComboBoxUpdatePeriod);
    setTabOrder(m_pComboBoxUpdatePeriod, m_pRadioUpdateFilterStable);
    setTabOrder(m_pRadioUpdateFilterStable, m_pRadioUpdateFilterEvery);
    setTabOrder(m_pRadioUpdateFilterEvery, m_pRadioUpdateFilterBetas);
}

void UIGlobalSettingsUpdate::retranslateUi()
{
    /* Translate uic generated strings: */
    Ui::UIGlobalSettingsUpdate::retranslateUi(this);

    /* Retranslate m_pComboBoxUpdatePeriod combobox: */
    int iCurrenIndex = m_pComboBoxUpdatePeriod->currentIndex();
    m_pComboBoxUpdatePeriod->clear();
    VBoxUpdateData::populate();
    m_pComboBoxUpdatePeriod->insertItems(0, VBoxUpdateData::list());
    m_pComboBoxUpdatePeriod->setCurrentIndex(iCurrenIndex == -1 ? 0 : iCurrenIndex);
}

void UIGlobalSettingsUpdate::sltUpdaterToggled(bool fEnabled)
{
    /* Update activity status: */
    m_pContainerUpdate->setEnabled(fEnabled);

    /* Update time of next check: */
    sltPeriodActivated();

    /* Temporary remember branch type if was switched off: */
    if (!fEnabled)
    {
        m_pLastChosenRadio = m_pRadioUpdateFilterBetas->isChecked() ? m_pRadioUpdateFilterBetas :
                             m_pRadioUpdateFilterEvery->isChecked() ? m_pRadioUpdateFilterEvery : m_pRadioUpdateFilterStable;
    }

    /* Check/uncheck last selected radio depending on activity status: */
    if (m_pLastChosenRadio)
        m_pLastChosenRadio->setChecked(fEnabled);
}

void UIGlobalSettingsUpdate::sltPeriodActivated()
{
    VBoxUpdateData data(periodType(), branchType());
    m_pUpdateDateText->setText(data.date());
}

VBoxUpdateData::PeriodType UIGlobalSettingsUpdate::periodType() const
{
    VBoxUpdateData::PeriodType result = m_pCheckBoxUpdate->isChecked() ?
        (VBoxUpdateData::PeriodType)m_pComboBoxUpdatePeriod->currentIndex() : VBoxUpdateData::PeriodNever;
    return result == VBoxUpdateData::PeriodUndefined ? VBoxUpdateData::Period1Day : result;
}

VBoxUpdateData::BranchType UIGlobalSettingsUpdate::branchType() const
{
    if (m_pRadioUpdateFilterBetas->isChecked())
        return VBoxUpdateData::BranchWithBetas;
    else if (m_pRadioUpdateFilterEvery->isChecked())
        return VBoxUpdateData::BranchAllRelease;
    else
        return VBoxUpdateData::BranchStable;
}

