/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@/tests/mount'
import BaseDlg from '@/components/common/BaseDlg.vue'
import { lodash } from '@/utils/helpers'
import { createStore } from 'vuex'

const store = createStore({
  commit: vi.fn(),
})

const mountFactory = (opts) =>
  mount(
    BaseDlg,
    lodash.merge(
      {
        shallow: false,
        props: {
          modelValue: false,
          title: 'dialog title',
          onSave: () => null,
          attach: true,
        },
        global: { plugins: [store] },
      },
      opts
    )
  )

describe('BaseDlg.vue', () => {
  let wrapper
  describe(`Child component's data communication tests`, () => {
    it(`Should pass accurate data to VDialog`, () => {
      wrapper = mountFactory()
      const { modelValue, width, persistent, scrollable, eager } = wrapper.findComponent({
        name: 'VDialog',
      }).vm.$props
      expect(modelValue).to.equal(wrapper.vm.isDlgOpened)
      expect(width).to.equal('unset')
      expect(persistent).to.be.true
      expect(eager).to.be.true
      expect(scrollable).to.equal(wrapper.vm.$props.scrollable)
    })

    const dividerTests = [true, false]
    dividerTests.forEach((v) => {
      it(`Should ${v ? '' : 'not'} render v-divider when hasFormDivider props is ${v}`, () => {
        wrapper = mountFactory({ props: { hasFormDivider: v } })
        expect(wrapper.findComponent({ name: 'v-divider' }).exists()).to.be[v]
      })
    })

    it(`Should pass accurate data to VForm`, () => {
      wrapper = mountFactory()
      const { modelValue, validateOn } = wrapper.findComponent({
        name: 'VForm',
      }).vm.$props
      expect(modelValue).to.equal(wrapper.vm.isFormValid)
      expect(validateOn).to.equal(wrapper.vm.$props.lazyValidation ? 'lazy' : 'input')
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
        wrapper = mountFactory({ props })
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
        wrapper = mountFactory({ props: props })
        expect(wrapper.find('[data-test="cancel-btn"]').exists()).to.be[render]
      })
    })

    it('save-btn should be passed with accurate disabled props', () => {
      expect(wrapper.findComponent('[data-test="save-btn"]').vm.$props.disabled).to.equal(
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
      expect(wrapper.vm.isDlgOpened).to.be.eql(wrapper.vm.$props.modelValue)
    })

    it(`Should emit update:modelValue event`, () => {
      wrapper = mountFactory()
      wrapper.vm.isDlgOpened = true
      expect(wrapper.emitted('update:modelValue')[0][0]).to.be.eql(true)
    })

    it(`Should emit update:modelValue event when closeDialog is called`, () => {
      wrapper = mountFactory({ props: { value: true } })
      wrapper.vm.closeDialog()
      expect(wrapper.emitted('update:modelValue')[0][0]).to.be.eql(false)
    })

    const cancelAndCloseEvtTests = [
      { dataTest: 'close-btn', handler: 'close', evt: 'after-close' },
      { dataTest: 'cancel-btn', handler: 'cancel', evt: 'after-cancel' },
    ]
    cancelAndCloseEvtTests.forEach(({ handler, evt }) => {
      it(`Should emit ${evt} event when ${handler} is called`, () => {
        wrapper = mountFactory({ props: { value: true } })
        wrapper.vm[handler]()
        expect(wrapper.emitted(evt)).to.be.an('array').and.to.have.lengthOf(1)
      })
    })

    it('If closeImmediate props is true, should close the dialog immediately', async () => {
      wrapper = mountFactory({ props: { value: true, closeImmediate: true } })
      await wrapper.vm.save()
      expect(wrapper.emitted('update:modelValue')[0][0]).to.be.eql(false)
    })
  })
})
