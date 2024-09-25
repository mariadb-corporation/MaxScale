/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "inventory.hh"
#include <maxbase/log.hh>

namespace pinloki
{

#ifdef SS_DEBUG

/** The purpose of this class is to be able to test recovery scenarios.
 *  The actual code calls CRASH_TEST(test_case) and CrashTest does
 *  whatever the test_case needs and calls exit() when a trigger occurs.
 */
class CrashTest
{
public:
    CrashTest(InventoryWriter* m_inventory);
    bool fail_mid_trx();
    bool fail_after_commit();
    bool startup_recovery_soft();
private:
    InventoryWriter& m_inventory;
    int              m_fail_mid_trx = 0;
    int              m_fail_after_commit = 0;
    bool             m_startup_recovery_soft = false;
};

void       init_crash_test(InventoryWriter* pInv);
CrashTest& crash_test();

#define CRASH_TEST(test_case) \
    if (crash_test().test_case()) \
    {   \
        MXB_SERROR("recovery test exit on test case " << #test_case); \
        exit(101); \
    }

#else
inline void init_crash_test(InventoryWriter*)
{
}
#define CRASH_TEST(test_case)
#endif
}
