/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import MonitorPageHeader from '@/components/common/MonitorPageHeader'
import { dummy_all_monitors, triggerBtnClick } from '@tests/unit/utils'

const computedFactory = (computed = {}) =>
    mount({
        shallow: false,
        component: MonitorPageHeader,
        propsData: {
            targetMonitor: dummy_all_monitors[0],
        },
        stubs: {
            'refresh-rate': "<div class='refresh-rate'></div>",
        },
        computed,
    })

const ALL_BTN_CLASS_PREFIXES = [
    'stop',
    'start',
    'destroy',
    'async-reset-replication',
    'async-release-locks',
    'async-failover',
]

describe('MonitorPageHeader', () => {
    let wrapper, axiosDeleteStub, axiosPutStub

    beforeEach(() => {
        wrapper = computedFactory()
        axiosDeleteStub = sinon.stub(wrapper.vm.$store.$http, 'delete').returns(Promise.resolve())
        axiosPutStub = sinon.stub(wrapper.vm.$store.$http, 'put').returns(Promise.resolve())
    })

    afterEach(() => {
        axiosDeleteStub.restore()
        axiosPutStub.restore()
    })

    it(`Should render monitor state accurately`, () => {
        const span = wrapper.find('.resource-state')
        expect(span.exists()).to.be.true
        expect(span.text()).to.be.equals(dummy_all_monitors[0].attributes.state)
    })

    it(`Should render monitor module accurately`, () => {
        const span = wrapper.find('.resource-module')
        expect(span.exists()).to.be.true
        expect(span.text()).to.be.equals(dummy_all_monitors[0].attributes.module)
    })

    describe('Operation option disable test assertions', () => {
        const dummyState = ['Stopped', 'Running']
        const btnClassPrefixes = ['stop', 'start']
        btnClassPrefixes.forEach((prefix, i) => {
            let des = `Should disable ${prefix} operation when state is: ${dummyState[i]}`
            it(des, async () => {
                // state stub
                wrapper = computedFactory({ state: () => dummyState[i] })
                await triggerBtnClick(wrapper, '.gear-btn')
                const opItem = wrapper.find(`.${prefix}-op`)
                expect(opItem.classes().includes('v-list-item--disabled')).to.be.true
            })
        })
    })

    describe('Event emit test assertions', () => {
        // dummy state to make operation clickable
        const dummyState = ['Running', 'Stopped', 'Running', 'Running', 'Running', 'Running']
        ALL_BTN_CLASS_PREFIXES.forEach((prefix, i) =>
            it(`Should emit on-choose-op event when ${prefix} option is clicked`, async () => {
                // currState stub
                wrapper = computedFactory({
                    currState: () => dummyState[i],
                    monitorModule: () => 'mariadbmon',
                })
                // stub targetMonitor to make all mariadbmon operation clickable
                await wrapper.setData({
                    targetMonitor: {
                        ...wrapper.vm.$data.targetMonitor,
                        attributes: {
                            monitor_diagnostics: {
                                primary: true,
                            },
                            parameters: {
                                auto_failover: false,
                            },
                        },
                    },
                })
                await triggerBtnClick(wrapper, '.gear-btn')
                await wrapper.vm.$nextTick()
                await triggerBtnClick(wrapper, `.${prefix}-op`)
                expect(wrapper.emitted()).to.have.property('on-choose-op')
                expect(wrapper.emitted()['on-choose-op'][0]).to.be.eql([wrapper.vm.allOps[prefix]])
            })
        )
    })
})
