/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@/tests/mount'
import BaseDlg from '@/components/common/BaseDlg.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(
    BaseDlg,
    lodash.merge(
      {
        props: {
          modelValue: true,
          title: 'dialog title',
          onSave: () => null,
        },
        attrs: { attach: true },
      },
      opts
    )
  )

describe('BaseDlg.vue', () => {
  let wrapper
  describe(`Child component's data communication tests`, () => {
    it(`Should pass accurate data to VDialog`, () => {
      wrapper = mountFactory()
      const { modelValue, width, persistent, scrollable } = wrapper.findComponent({
        name: 'VDialog',
      }).vm.$props
      expect(modelValue).toBe(wrapper.vm.isDlgOpened)
      expect(width).toBe('unset')
      expect(persistent).toBe(true)
      expect(scrollable).toBe(wrapper.vm.$props.scrollable)
    })

    const dividerTests = [true, false]
    dividerTests.forEach((v) => {
      it(`Should${v ? '' : ' not'} render VDivider when hasFormDivider props is ${v}`, () => {
        wrapper = mountFactory({ shallow: false, props: { hasFormDivider: v } })
        expect(wrapper.findComponent({ name: 'VDivider' }).exists()).toBe(v)
      })
    })

    it(`Should pass accurate data to VForm`, () => {
      wrapper = mountFactory({ shallow: false })
      const { modelValue, validateOn } = wrapper.findComponent({
        name: 'VForm',
      }).vm.$props
      expect(modelValue).toBe(wrapper.vm.formValidity)
      expect(validateOn).toBe(wrapper.vm.$props.lazyValidation ? 'lazy input' : 'input')
    })

    const closeBtnTests = [
      { description: 'Should render close button by default', props: {}, render: true },
      {
        description: 'Should not render close button when showCloseBtn props is false',
        props: { showCloseBtn: false },
        render: false,
      },
    ]
    closeBtnTests.forEach(({ description, props, render }) => {
      it(description, () => {
        wrapper = mountFactory({ shallow: false, props })
        expect(wrapper.find('[data-test="close-btn"]').exists()).toBe(render)
      })
    })

    const slotTests = [
      { slot: 'body', dataTest: 'body-slot-ctr' },
      { slot: 'form-body', dataTest: 'form-body-slot-ctr' },
      { slot: 'action-prepend', dataTest: 'action-ctr' },
    ]
    const stubSlot = '<div>slot content</div>'
    slotTests.forEach(({ slot, dataTest }) => {
      it(`Should render slot ${slot} accurately`, () => {
        wrapper = mountFactory({ shallow: false, slots: { [slot]: stubSlot } })
        expect(wrapper.find(`[data-test="${dataTest}"]`).html()).toContain(stubSlot)
      })
    })

    const cancelBtnTests = [
      { description: 'Should render cancel button by default', props: {}, render: true },
    ]
    cancelBtnTests.forEach(({ description, props, render }) => {
      it(description, () => {
        wrapper = mountFactory({ shallow: false, props })
        expect(wrapper.find('[data-test="cancel-btn"]').exists()).toBe(render)
      })
    })

    it('save-btn should be passed with accurate disabled props', () => {
      expect(wrapper.findComponent('[data-test="save-btn"]').vm.$props.disabled).toBe(
        wrapper.vm.isSaveDisabled
      )
    })

    const btnTxtTests = [
      { dataTest: 'cancel-btn', txt: 'cancel' },
      { dataTest: 'save-btn', txt: 'save' },
    ]

    btnTxtTests.forEach(({ txt, dataTest }) => {
      it(`Should have default text for ${dataTest}`, () => {
        wrapper = mountFactory({ shallow: false })
        expect(wrapper.find(`[data-test="${dataTest}"]`).html()).toContain(txt)
      })
    })
  })

  describe(`Computed properties and method tests`, () => {
    it(`Should return accurate value for isDlgOpened`, () => {
      wrapper = mountFactory()
      expect(wrapper.vm.isDlgOpened).toBe(wrapper.vm.$props.modelValue)
    })

    it(`Should emit update:modelValue event`, () => {
      wrapper = mountFactory()
      wrapper.vm.isDlgOpened = true
      expect(wrapper.emitted('update:modelValue')[0][0]).toBe(true)
    })

    it(`Should emit update:modelValue event when closeDialog is called`, () => {
      wrapper = mountFactory({ props: { value: true } })
      wrapper.vm.closeDialog()
      expect(wrapper.emitted('update:modelValue')[0][0]).toBe(false)
    })

    const cancelAndCloseEvtTests = [
      { dataTest: 'close-btn', handler: 'close', evt: 'after-close' },
      { dataTest: 'cancel-btn', handler: 'cancel', evt: 'after-cancel' },
    ]
    cancelAndCloseEvtTests.forEach(({ handler, evt }) => {
      it(`Should emit ${evt} event when ${handler} is called`, () => {
        wrapper = mountFactory({ props: { value: true } })
        wrapper.vm[handler]()
        expect(wrapper.emitted(evt)).toBeInstanceOf(Array)
        expect(wrapper.emitted(evt).length).toBe(1)
      })
    })
  })
})
