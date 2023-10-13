/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import EtlOverviewStage from '@wkeComps/DataMigration/EtlOverviewStage'
import { lodash } from '@share/utils/helpers'
import { task } from '@wkeComps/DataMigration/__tests__/stubData'
import { ETL_STATUS } from '@wsSrc/store/config'

const mountFactory = opts =>
    mount(lodash.merge({ shallow: true, component: EtlOverviewStage, propsData: { task } }, opts))

describe('EtlOverviewStage', () => {
    let wrapper

    it(`Should render etl-stage-ctr`, () => {
        wrapper = mountFactory()
        expect(wrapper.findComponent({ name: 'etl-stage-ctr' }).exists()).to.be.true
    })

    const areConnsAliveTestCases = [true, false]
    areConnsAliveTestCases.forEach(value => {
        it(`Should disable the Set Up Connections button when
        areConnsAlive is ${value}`, () => {
            wrapper = mountFactory({
                computed: { areConnsAlive: () => value },
            })
            expect(wrapper.vm.disabled).to.be[value]
        })
    })

    const taskStatusTestCases = [ETL_STATUS.COMPLETE, ETL_STATUS.RUNNING]
    taskStatusTestCases.forEach(status => {
        it(`Should disable the Set Up Connections button when task status is ${status}`, () => {
            wrapper = mountFactory({
                computed: { areConnsAlive: () => false, task: () => ({ ...task, status }) },
            })
            expect(wrapper.vm.disabled).to.be.true
        })
    })
})
