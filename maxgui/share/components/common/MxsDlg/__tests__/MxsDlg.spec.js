/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import MxsDlg from '@share/components/common/MxsDlg'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: false,
                component: MxsDlg,
                propsData: {
                    value: false,
                    title: 'dialog title',
                    onSave: () => null,
                },
            },
            opts
        )
    )

describe('MxsDlg.vue', () => {
    let wrapper

    describe(`Child component's data communication tests`, () => {
        it(`Should pass accurate data to v-dialog`, () => {
            wrapper = mountFactory()
            const { value, width, persistent, scrollable, eager } = wrapper.findComponent({
                name: 'v-dialog',
            }).vm.$props
            expect(value).to.equal(wrapper.vm.isDlgOpened)
            expect(width).to.equal('unset')
            expect(persistent).to.be.true
            expect(eager).to.be.true
            expect(scrollable).to.equal(wrapper.vm.$props.scrollable)
        })

        const dividerTests = [true, false]
        dividerTests.forEach(v => {
            it(`Should ${
                v ? '' : 'not'
            } render v-divider when hasFormDivider props is ${v}`, () => {
                wrapper = mountFactory({ propsData: { hasFormDivider: v } })
                expect(wrapper.findComponent({ name: 'v-divider' }).exists()).to.be[v]
            })
        })

        it(`Should pass accurate data to v-form`, () => {
            wrapper = mountFactory()
            const { value, lazyValidation } = wrapper.findComponent({
                name: 'v-form',
            }).vm.$props
            expect(value).to.equal(wrapper.vm.$data.isFormValid)
            expect(lazyValidation).to.equal(wrapper.vm.$props.lazyValidation)
        })

        const closeBtnTests = [
            { description: 'Should render close button by default', props: {}, render: true },
            {
                description: 'Should not render close button when showCloseBtn props is false',
                props: { showCloseBtn: false },
                render: false,
            },
            {
                description: 'Should not render close button when isForceAccept props is true',
                props: { isForceAccept: true, showCloseBtn: true },
                render: false,
            },
        ]
        closeBtnTests.forEach(({ description, props, render }) => {
            it(description, () => {
                wrapper = mountFactory({ propsData: props })
                expect(wrapper.find('[data-test="close-btn"]').exists()).to.be[render]
            })
        })

        const slotTests = [
            { slot: 'body', dataTest: 'body-slot-ctr' },
            { slot: 'form-body', dataTest: 'form-body-slot-ctr' },
            { slot: 'action-prepend', dataTest: 'action-ctr' },
            { slot: 'save-btn', dataTest: 'action-ctr' },
        ]
        const stubSlot = '<div>slot content</div>'
        slotTests.forEach(({ slot, dataTest }) => {
            it(`Should render slot ${slot} accurately`, () => {
                wrapper = mountFactory({ slots: { [slot]: stubSlot } })
                expect(wrapper.find(`[data-test="${dataTest}"]`).html()).to.contain(stubSlot)
            })
        })

        const cancelBtnTests = [
            { description: 'Should render cancel button by default', props: {}, render: true },
            {
                description: 'Should not render cancel button when isForceAccept props is true',
                props: { isForceAccept: true },
                render: false,
            },
        ]
        cancelBtnTests.forEach(({ description, props, render }) => {
            it(description, () => {
                wrapper = mountFactory({ propsData: props })
                expect(wrapper.find('[data-test="cancel-btn"]').exists()).to.be[render]
            })
        })
        it('save-btn should be passed with accurate disabled props', () => {
            expect(wrapper.find('[data-test="save-btn"]').vm.$props.disabled).to.equal(
                wrapper.vm.isSaveDisabled
            )
        })

        const btnTxtTests = [
            { dataTest: 'cancel-btn', txt: 'cancel' },
            { dataTest: 'save-btn', txt: 'save' },
        ]

        btnTxtTests.forEach(({ txt, dataTest }) => {
            it(`Should have default text for ${dataTest}`, () => {
                wrapper = mountFactory()
                expect(wrapper.find(`[data-test="${dataTest}"]`).html()).to.contain(txt)
            })
        })
    })
    describe(`Computed properties and method tests`, () => {
        it(`Should return accurate value for isDlgOpened`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.isDlgOpened).to.be.eql(wrapper.vm.$props.value)
        })

        it(`Should emit input event`, () => {
            wrapper = mountFactory()
            wrapper.vm.isDlgOpened = true
            expect(wrapper.emitted('input')[0][0]).to.be.eql(true)
        })

        it(`Should emit is-form-valid event`, async () => {
            wrapper = mountFactory()
            await wrapper.setData({ isFormValid: false })
            expect(wrapper.emitted('is-form-valid')[0][0]).to.be.eql(false)
        })

        it(`Should emit input event when closeDialog is called`, () => {
            wrapper = mountFactory({ propsData: { value: true } })
            wrapper.vm.closeDialog()
            expect(wrapper.emitted('input')[0][0]).to.be.eql(false)
        })

        const btnHandlerTests = [
            { dataTest: 'close-btn', handler: 'close' },
            { dataTest: 'cancel-btn', handler: 'cancel' },
            { dataTest: 'save-btn', handler: 'save' },
        ]
        btnHandlerTests.forEach(({ handler, dataTest }) => {
            it(`Should call ${handler} method when ${dataTest} is clicked`, async () => {
                const spy = sinon.spy(MxsDlg.methods, handler)
                wrapper = mountFactory({ propsData: { value: true, closeImmediate: true } })
                await wrapper.find(`[data-test="${dataTest}"]`).trigger('click')
                spy.should.have.been.calledOnce
                spy.restore()
            })
        })

        const cancelAndCloseEvtTests = [
            { dataTest: 'close-btn', handler: 'close', evt: 'after-close' },
            { dataTest: 'cancel-btn', handler: 'cancel', evt: 'after-cancel' },
        ]
        cancelAndCloseEvtTests.forEach(({ handler, evt }) => {
            it(`Should emit ${evt} event when ${handler} is called`, () => {
                wrapper = mountFactory({ propsData: { value: true } })
                wrapper.vm[handler]()
                expect(wrapper.emitted(evt))
                    .to.be.an('array')
                    .and.to.have.lengthOf(1)
            })
        })

        it(`Should call cleanUp method when cancel method is called`, () => {
            wrapper = mountFactory({ propsData: { value: true } })
            const spy = sinon.spy(wrapper.vm, 'cleanUp')
            wrapper.vm.cancel()
            spy.should.have.been.calledOnce
            spy.restore()
        })

        it('Should call validate form', async () => {
            wrapper = mountFactory({ propsData: { value: true } })
            const spy = sinon.spy(wrapper.vm.$refs.form, 'validate')
            await wrapper.vm.save()
            spy.should.have.been.calledOnce
            spy.restore()
        })

        it('Should call validateForm method when save method is called', async () => {
            const spy = sinon.spy(MxsDlg.methods, 'validateForm')
            wrapper = mountFactory({ propsData: { value: true } })
            await wrapper.vm.save()
            spy.should.have.been.calledOnce
            spy.restore()
        })

        it('If closeImmediate props is true, should close the dialog immediately', async () => {
            wrapper = mountFactory({ propsData: { value: true, closeImmediate: true } })
            await wrapper.vm.save()
            expect(wrapper.emitted('input')[0][0]).to.be.eql(false)
        })
    })
})
