/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-07-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import PageHeader from '@/pages/MonitorDetail/PageHeader'
import { dummy_all_monitors, assertSendingRequest } from '@tests/unit/utils'

const computedFactory = (computed = {}) =>
    mount({
        shallow: false,
        component: PageHeader,
        propsData: {
            currentMonitor: dummy_all_monitors[0],
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
let wrapper
describe('MonitorDetail - PageHeader', () => {
    it(`Should pass necessary props to confirm-dialog`, () => {
        wrapper = computedFactory()
        const confirmDialog = wrapper.findComponent({
            name: 'confirm-dialog',
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
    describe('Monitor state update and monitor deletion test assertions', () => {
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
                // stub currentMonitor to make all mariadbmon operation clickable
                await wrapper.setProps({
                    currentMonitor: {
                        ...wrapper.vm.$props.currentMonitor,
                        attributes: {
                            ...wrapper.vm.$props.currentMonitor.attributes,
                            monitor_diagnostics: { primary: true },
                            parameters: { auto_failover: false },
                            module: 'mariadbmon',
                            state: dummyState[i],
                        },
                    },
                })
                switch (op) {
                    case 'stop':
                    case 'start': {
                        axiosStub = sinon
                            .stub(wrapper.vm.$store.$http, 'put')
                            .returns(Promise.resolve())
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
                            .stub(wrapper.vm.$store.$http, 'delete')
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
                        axiosStub = sinon
                            .stub(wrapper.vm.$store.$http, 'post')
                            .returns(Promise.resolve())
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
