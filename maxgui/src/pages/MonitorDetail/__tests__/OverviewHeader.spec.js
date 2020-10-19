/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { expect } from 'chai'
import mount from '@tests/unit/setup'
import OverviewHeader from '@/pages/MonitorDetail/OverviewHeader'
import { dummy_all_monitors } from '@tests/unit/utils'

describe('OverviewHeader index', () => {
    let wrapper
    beforeEach(async () => {
        wrapper = mount({
            shallow: false,
            component: OverviewHeader,
            props: {
                currentMonitor: dummy_all_monitors[0],
            },
        })
    })

    it(`Should render four outlined-overview-card components`, async () => {
        const outlineOverviewCards = wrapper.findAllComponents({ name: 'outlined-overview-card' })
        expect(outlineOverviewCards.length).to.be.equals(4)
    })

    it(`Should automatically assign 'undefined' string if attribute is not defined`, async () => {
        let currentMonitor = wrapper.vm.$help.lodash.cloneDeep(dummy_all_monitors[0])
        currentMonitor.attributes.monitor_diagnostics = {}
        await wrapper.setProps({
            currentMonitor: currentMonitor,
        })
        const getTopOverviewInfo = wrapper.vm.getTopOverviewInfo
        Object.values(getTopOverviewInfo).forEach(value => expect(value).to.be.equals('undefined'))
    })

    it(`Should shows master, master_gtid_domain_id, state, primary value`, async () => {
        const expectKeys = ['master', 'master_gtid_domain_id', 'state', 'primary']
        const getTopOverviewInfo = wrapper.vm.getTopOverviewInfo
        expect(Object.keys(getTopOverviewInfo)).to.be.deep.equals(expectKeys)
    })
})
