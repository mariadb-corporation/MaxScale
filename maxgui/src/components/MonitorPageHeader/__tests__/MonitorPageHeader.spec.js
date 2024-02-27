/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import MonitorPageHeader from '@src/components/MonitorPageHeader'
import { dummy_all_monitors, triggerBtnClick, assertSendingRequest } from '@tests/unit/utils'
import { MRDB_MON } from '@src/constants'

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

const ALL_OP_CLASS_NAMES = [
    'stop',
    'start',
    'destroy',
    'async-reset-replication',
    'async-release-locks',
    'async-failover',
]

describe('MonitorPageHeader', () => {
    let wrapper

    beforeEach(() => {
        wrapper = computedFactory()
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

    it(`Should pass necessary props to confirm-dlg`, () => {
        wrapper = computedFactory()
        const confirmDialog = wrapper.findComponent({
            name: 'confirm-dlg',
        })
        expect(confirmDialog.exists()).to.be.true
        const { value, title, saveText, onSave } = confirmDialog.vm.$attrs
        const { type, item, smallInfo } = confirmDialog.vm.$props
        const {
            isOpened,
            title: confDlgTitle,
            type: confDlgType,
            targetNode,
            smallInfo: confDlgSmallInfo,
        } = wrapper.vm.$data.confDlg

        expect(value).to.be.equals(isOpened)
        expect(title).to.be.equals(confDlgTitle)
        expect(type).to.be.equals(confDlgType)
        expect(item).to.be.deep.equals(targetNode)
        expect(smallInfo).to.be.equals(confDlgSmallInfo)
        expect(saveText).to.be.equals(wrapper.vm.confDlgSaveTxt)
        expect(onSave).to.be.equals(wrapper.vm.onConfirm)
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
        ALL_OP_CLASS_NAMES.forEach((prefix, i) =>
            it(`Should emit chosen-op-type event when ${prefix} option is clicked`, async () => {
                // currState stub
                wrapper = computedFactory({
                    currState: () => dummyState[i],
                    monitorModule: () => MRDB_MON,
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
                expect(wrapper.emitted()).to.have.property('chosen-op-type')
                expect(wrapper.emitted()['chosen-op-type'][0]).to.be.eql([
                    wrapper.vm.allOps[prefix].type,
                ])
            })
        )
    })

    describe('Operation test assertions', () => {
        beforeEach(() => {
            wrapper = computedFactory()
        })

        // dummy states that btn can be clicked
        const dummyState = ['Running', 'Stopped', 'Running', 'Running', 'Running', 'Running']
        ALL_OP_CLASS_NAMES.forEach(async (op, i) => {
            // currState stub
            const wrapper = computedFactory()
            const cssSelector = `.${op}-op`
            const id = dummy_all_monitors[0].id
            let axiosStub,
                httpMethod = 'POST'

            switch (op) {
                case 'destroy':
                    httpMethod = 'DELETE'
                    break
                case 'stop':
                case 'start':
                    httpMethod = 'PUT'
                    break
            }

            const des = `Should send ${httpMethod} request after confirming ${op} a monitor`
            it(des, async () => {
                // stub targetMonitor to make all mariadbmon operation clickable
                await wrapper.setProps({
                    targetMonitor: {
                        ...wrapper.vm.$props.targetMonitor,
                        attributes: {
                            ...wrapper.vm.$props.targetMonitor.attributes,
                            monitor_diagnostics: { primary: true },
                            parameters: { auto_failover: false },
                            module: MRDB_MON,
                            state: dummyState[i],
                        },
                    },
                })
                switch (op) {
                    case 'stop':
                    case 'start': {
                        axiosStub = sinon.stub(wrapper.vm.$http, 'put').returns(Promise.resolve())
                        await assertSendingRequest({
                            wrapper,
                            cssSelector,
                            axiosStub,
                            axiosStubCalledWith: `/monitors/${id}/${op}`,
                        })

                        axiosStub.restore()
                        break
                    }
                    case 'destroy': {
                        axiosStub = sinon
                            .stub(wrapper.vm.$http, 'delete')
                            .returns(Promise.resolve())
                        await assertSendingRequest({
                            wrapper,
                            cssSelector,
                            axiosStub,
                            axiosStubCalledWith: `/monitors/${id}?force=yes`,
                        })
                        axiosStub.restore()
                        break
                    }
                    case 'async-reset-replication':
                    case 'async-release-locks':
                    case 'async-failover': {
                        axiosStub = sinon.stub(wrapper.vm.$http, 'post').returns(Promise.resolve())
                        await assertSendingRequest({
                            wrapper,
                            cssSelector,
                            axiosStub,
                            axiosStubCalledWith: `/maxscale/modules/mariadbmon/${op}?${id}`,
                        })
                        break
                    }
                }
                axiosStub.restore()
            })
        })
    })
})
