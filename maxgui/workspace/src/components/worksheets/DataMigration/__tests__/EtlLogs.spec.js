/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import EtlLogs from '@wkeComps/DataMigration/EtlLogs'
import { task } from '@wkeComps/DataMigration/__tests__/stubData'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(lodash.merge({ shallow: true, component: EtlLogs, propsData: { task } }, opts))

describe('EtlLogs', () => {
    let wrapper

    afterEach(() => sinon.restore())

    const properties = [
        {
            name: 'etlLog',
            datatype: 'object',
            expectedValue: task.logs,
        },
        {
            name: 'activeStageIdx',
            datatype: 'number',
            expectedValue: task.active_stage_index,
        },
        {
            name: 'logs',
            datatype: 'array',
            expectedValue: task.logs[task.active_stage_index] || [],
        },
    ]
    properties.forEach(({ name, datatype, expectedValue }) => {
        it(`Should return accurate data for ${name} computed property`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm[name]).to.be.an(datatype)
            expect(wrapper.vm[name]).to.eql(expectedValue)
        })
    })

    it(`Should call scrollToBottom method when component is mounted`, () => {
        const stub = sinon.stub(EtlLogs.methods, 'scrollToBottom')
        wrapper = mountFactory()
        stub.should.be.calledOnce
    })

    it(`Should call scrollToBottom method when logs is changed`, async () => {
        wrapper = mountFactory({ propsData: { task: { ...task, active_stage_index: 1 } } })
        const stub = sinon.stub(wrapper.vm, 'scrollToBottom')
        // mock new lock entry
        await wrapper.setProps({
            task: {
                ...task,
                active_stage_index: 1,
                logs: {
                    '1': [{ name: 'Opening connections...', timestamp: 169443682753 }],
                    '2': [],
                    '3': [],
                },
            },
        })
        stub.should.be.calledOnce
    })
})
