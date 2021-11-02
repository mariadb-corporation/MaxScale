/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import PageHeader from '@/pages/ServiceDetail/PageHeader'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import {
    dummy_all_services,
    triggerBtnClick,
    openConfirmDialog,
    assertSendingRequest,
} from '@tests/unit/utils'

chai.should()
chai.use(sinonChai)

const computedFactory = (computed = {}) =>
    mount({
        shallow: false,
        component: PageHeader,
        props: {
            currentService: dummy_all_services[0],
        },
        computed,
    })

/**
 * @param {Object} payload.wrapper - mounted component
 * @param {Number} payload.dummyServiceState - dummyServiceState: Started, Stopped
 */
const serviceStateTestAssertions = ({ wrapper, dummyServiceState }) => {
    it(`Should render ${dummyServiceState}`, async () => {
        // stateIconFrame stub
        wrapper = computedFactory({
            serviceState: () => dummyServiceState,
        })
        const span = wrapper.find('.service-state')
        expect(span.exists()).to.be.true
        expect(span.text()).to.be.equals(dummyServiceState)
    })
}

const ALL_BTN_CLASS_PREFIXES = ['stop', 'start', 'delete']
// dummy states that the above btn prefixes can be clicked
const DUMMY_CLICKABLE_STATES = ['Started', 'Stopped', 'Started']

describe('ServiceDetail - PageHeader', () => {
    let wrapper, axiosDeleteStub, axiosPutStub

    beforeEach(async () => {
        wrapper = mount({
            shallow: false,
            component: PageHeader,
            props: {
                currentService: dummy_all_services[0],
            },
        })

        axiosDeleteStub = sinon.stub(wrapper.vm.$axios, 'delete').returns(Promise.resolve())
        axiosPutStub = sinon.stub(wrapper.vm.$axios, 'put').returns(Promise.resolve())
    })

    afterEach(async () => {
        await axiosDeleteStub.restore()
        await axiosPutStub.restore()
    })

    it(`Should pass necessary props to confirm-dialog`, async () => {
        const confirmDialog = wrapper.findComponent({
            name: 'confirm-dialog',
        })
        expect(confirmDialog.exists()).to.be.true

        const { title, type, item, onSave, onClose, onCancel } = confirmDialog.vm.$props
        const { dialogTitle, dialogType } = wrapper.vm.$data

        expect(title).to.be.equals(dialogTitle)
        expect(type).to.be.equals(dialogType)
        expect(item).to.be.deep.equals(wrapper.vm.$props.currentService)
        expect(onSave).to.be.equals(wrapper.vm.confirmSave)
        expect(onClose).to.be.equals(wrapper.vm.handleClose)
        expect(onCancel).to.be.equals(wrapper.vm.handleClose)
    })

    describe('Service state tests', () => {
        const expectStates = ['Started', 'Stopped']
        expectStates.forEach(dummyServiceState =>
            serviceStateTestAssertions({
                wrapper,
                dummyServiceState,
            })
        )
    })

    describe('confirm-dialog opening test assertions', () => {
        ALL_BTN_CLASS_PREFIXES.forEach((prefix, i) =>
            it(`Should open confirm-dialog when ${prefix} button is clicked`, async () => {
                // stateMode stub
                wrapper = computedFactory({
                    serviceState: () => DUMMY_CLICKABLE_STATES[i],
                })
                await openConfirmDialog({
                    wrapper,
                    cssSelector: `.${prefix}-btn`,
                })
                const confirmDialog = wrapper.findComponent({
                    name: 'confirm-dialog',
                })
                expect(confirmDialog.vm.$data.isDialogOpen).to.be.true
            })
        )
    })

    describe('button disable test assertions', async () => {
        // states that disables btn with prefix in ALL_BTN_CLASS_PREFIXES
        const dummyStates = ['Stopped', 'Started']
        dummyStates.forEach((state, i) => {
            const prefix = ALL_BTN_CLASS_PREFIXES[i]
            let des = `Should disable ${prefix} btn when service state is: ${state}`
            it(des, async () => {
                // stateMode stub
                wrapper = computedFactory({
                    serviceState: () => state,
                })
                await triggerBtnClick(wrapper, '.gear-btn')
                const btn = wrapper.find(`.${prefix}-btn`)
                expect(btn.attributes().disabled).to.be.equals('disabled')
            })
        })
    })

    describe('Service state update and service deletion test assertions', async () => {
        ALL_BTN_CLASS_PREFIXES.forEach((prefix, i) => {
            const wrapper = computedFactory({
                serviceState: () => DUMMY_CLICKABLE_STATES[i],
            })
            const cssSelector = `.${prefix}-btn`
            const id = dummy_all_services[0].id
            let httpMethod = 'PUT'

            if (prefix === 'delete') httpMethod = 'DELETE'

            it(`Should send ${httpMethod} request after confirming ${prefix} service`, async () => {
                await assertSendingRequest({
                    wrapper,
                    cssSelector,
                    axiosStub: prefix === 'delete' ? axiosDeleteStub : axiosPutStub,
                    axiosStubCalledWith:
                        prefix === 'delete'
                            ? `/services/${id}?force=yes`
                            : `/services/${id}/${prefix}`,
                })
            })
        })
    })
})
