/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import PageHeader from '@rootSrc/pages/ServerDetail/PageHeader'

import {
    dummy_all_servers,
    triggerBtnClick,
    openConfirmDialog,
    assertSendingRequest,
} from '@tests/unit/utils'

const computedFactory = (computed = {}) =>
    mount({
        shallow: false,
        component: PageHeader,
        propsData: {
            currentServer: dummy_all_servers[0],
        },
        computed,
        stubs: {
            'refresh-rate': "<div class='refresh-rate'></div>",
        },
    })

/**
 * @param {Object} payload.wrapper - mounted component
 * @param {String} payload.status - status: Healthy, Unhealthy, Maintenance
 * @param {Number} payload.dummyIconFrameIndex - index value for stateIconFrame stub
 */
const serverStatusTestAssertions = ({ wrapper, status, dummyIconFrameIndex }) => {
    it(`Should render ${status}`, () => {
        // stateIconFrame stub
        wrapper = computedFactory({
            stateIconFrame: () => dummyIconFrameIndex,
        })
        const span = wrapper.find('.server-healthy')
        expect(span.exists()).to.be.true
        expect(span.text()).to.be.equals(status)
    })
}

/**
 * @param {Object} payload.wrapper - mounted component
 * @param {String} payload.expectStateMode - expect state mode
 * @param {String} payload.dummyServerState - dummy server state
 */
const serverStateModeAssertion = ({ wrapper, expectStateMode, dummyServerState }) => {
    it(`Should return accurate currStateMode from state: '${dummyServerState}' to
    '${expectStateMode}'`, () => {
        // serverState stub
        wrapper = computedFactory({ serverState: () => dummyServerState })
        expect(wrapper.vm.currStateMode).to.be.equals(expectStateMode)
    })
}

describe('ServerDetail - PageHeader: render assertions', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: PageHeader,
            propsData: {
                currentServer: dummy_all_servers[0],
            },
            stubs: {
                'refresh-rate': "<div class='refresh-rate'></div>",
            },
        })
    })

    it(`Should render version_string if it has value`, () => {
        const span = wrapper.find('.version-string')
        expect(span.exists()).to.be.true
        expect(span.text()).to.be.equals(
            `Version ${dummy_all_servers[0].attributes.version_string}`
        )
    })

    it(`Should not render version_string if it doesn't have value`, () => {
        // version_string mocks
        wrapper = computedFactory({
            version_string: () => '',
        })
        const span = wrapper.find('.version-string')
        expect(span.exists()).to.be.false
    })

    describe('Server healthy status tests', () => {
        const expectStatuses = ['Unhealthy', 'Healthy', 'Maintenance']
        expectStatuses.forEach((status, dummyIconFrameIndex) =>
            serverStatusTestAssertions({
                wrapper,
                status: status,
                dummyIconFrameIndex,
            })
        )
    })

    describe('Server state mode test assertions', () => {
        const expectStateModes = ['down', 'drained', 'maintenance', 'slave']
        const dummyServerStates = [
            'Down',
            'Drained, Slave, Running',
            'Maintenance, Running',
            'Slave, Running',
        ]
        dummyServerStates.forEach((dummyState, i) =>
            serverStateModeAssertion({
                wrapper,
                expectStateMode: expectStateModes[i],
                dummyServerState: dummyState,
            })
        )
    })
})

const ALL_BTN_CLASS_PREFIXES = ['maintain', 'clear', 'drain', 'delete']

describe(`ServerDetail - PageHeader: child component's data communication tests `, () => {
    let wrapper

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: PageHeader,
            propsData: {
                currentServer: dummy_all_servers[0],
            },
            stubs: {
                'refresh-rate': "<div class='refresh-rate'></div>",
            },
        })
    })

    it(`Should pass necessary props to confirm-dlg`, () => {
        const confirmDialog = wrapper.findComponent({
            name: 'confirm-dlg',
        })
        expect(confirmDialog.exists()).to.be.true
        const { value, title, onSave } = confirmDialog.vm.$attrs
        const { type, smallInfo, item } = confirmDialog.vm.$props
        const {
            dialogTitle,
            dialogType,
            smallInfo: dialogSmallInfo,
            isConfDlgOpened,
        } = wrapper.vm.$data

        expect(value).to.be.equals(isConfDlgOpened)
        expect(title).to.be.equals(dialogTitle)
        expect(type).to.be.equals(dialogType)
        expect(smallInfo).to.be.equals(dialogSmallInfo)
        expect(item).to.be.deep.equals(wrapper.vm.$props.currentServer)
        expect(onSave).to.be.equals(wrapper.vm.confirmSave)
    })

    describe('confirm-dlg opening test assertions', () => {
        const dummyStateModes = ['drain', 'maintenance', 'slave', 'maintenance']
        ALL_BTN_CLASS_PREFIXES.forEach((prefix, i) =>
            it(`Should open confirm-dlg when ${prefix} button is clicked`, async () => {
                // currStateMode stub
                wrapper = computedFactory({
                    currStateMode: () => dummyStateModes[i],
                })
                await openConfirmDialog({
                    wrapper,
                    cssSelector: `.${prefix}-btn`,
                })
                const confirmDialog = wrapper.findComponent({ name: 'confirm-dlg' })
                expect(confirmDialog.vm.$attrs.value).to.be.true
            })
        )
    })
})
describe('ServerDetail - PageHeader: Action tests', () => {
    let wrapper, axiosDeleteStub, axiosPutStub

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: PageHeader,
            propsData: {
                currentServer: dummy_all_servers[0],
            },
            stubs: {
                'refresh-rate': "<div class='refresh-rate'></div>",
            },
        })

        axiosDeleteStub = sinon.stub(wrapper.vm.$http, 'delete').returns(Promise.resolve())
        axiosPutStub = sinon.stub(wrapper.vm.$http, 'put').returns(Promise.resolve())
    })

    afterEach(async () => {
        await axiosDeleteStub.restore()
        await axiosPutStub.restore()
    })

    describe('button disable test assertions', () => {
        const btnClassPrefixes = ['maintain', 'clear', 'drain', 'drain']
        const dummyStateModes = ['maintenance', 'slave', 'drained', 'maintenance']
        btnClassPrefixes.forEach((prefix, i) => {
            let des = `Should disable ${prefix} btn when currStateMode is: ${dummyStateModes[i]}`
            if (prefix === 'clear')
                des = `Should disable clear btn when currStateMode !== maintenance or drained`
            it(des, async () => {
                // currStateMode stub
                wrapper = computedFactory({
                    currStateMode: () => dummyStateModes[i],
                })
                await triggerBtnClick(wrapper, '.gear-btn')
                const btn = wrapper.find(`.${prefix}-btn`)
                expect(btn.attributes().disabled).to.be.equals('disabled')
            })
        })
    })

    describe('Server state update and server deletion test assertions', () => {
        // dummy states that btn can be clicked
        const dummyStateModes = ['slave', 'drained', 'slave', 'maintenance']
        ALL_BTN_CLASS_PREFIXES.forEach((prefix, i) => {
            const wrapper = computedFactory({
                currStateMode: () => dummyStateModes[i],
            })
            const cssSelector = `.${prefix}-btn`
            const id = dummy_all_servers[0].id
            let httpMethod = 'PUT'

            if (prefix === 'delete') httpMethod = 'DELETE'

            it(`Should send ${httpMethod} request after confirming ${prefix}`, async () => {
                switch (prefix) {
                    case 'maintain':
                        await assertSendingRequest({
                            wrapper,
                            cssSelector,
                            axiosStub: axiosPutStub,
                            axiosStubCalledWith: `/servers/${id}/set?state=maintenance`,
                        })
                        break
                    case 'clear':
                        await assertSendingRequest({
                            wrapper,
                            cssSelector,
                            axiosStub: axiosPutStub,
                            axiosStubCalledWith: `/servers/${id}/clear?state=drain`,
                        })
                        break
                    case 'drain':
                        await assertSendingRequest({
                            wrapper,
                            cssSelector,
                            axiosStub: axiosPutStub,
                            axiosStubCalledWith: `/servers/${id}/set?state=drain`,
                        })
                        break
                    case 'delete':
                        await assertSendingRequest({
                            wrapper,
                            cssSelector,
                            axiosStub: axiosDeleteStub,
                            axiosStubCalledWith: `/servers/${id}?force=yes`,
                        })
                        break
                }
            })
        })
    })

    it(`Should add force=yes params when 'Force closing' checkbox is ticked `, async () => {
        const callWith = `/servers/${dummy_all_servers[0].id}/set?state=maintenance&force=yes`

        wrapper = computedFactory({
            currStateMode: () => 'slave',
        })
        await wrapper.setData({
            forceClosing: true,
        })
        await assertSendingRequest({
            wrapper,
            cssSelector: `.maintain-btn`,
            axiosStub: axiosPutStub,
            axiosStubCalledWith: callWith,
        })
    })
})
