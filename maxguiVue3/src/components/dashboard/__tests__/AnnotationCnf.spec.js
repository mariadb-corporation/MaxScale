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
import { find } from '@/tests/utils'
import { lodash } from '@/utils/helpers'
import AnnotationCnf from '@/components/dashboard/AnnotationCnf.vue'

const mountFactory = (opts) =>
  mount(
    AnnotationCnf,
    lodash.merge(
      {
        shallow: false,
        props: {
          modelValue: {
            display: true,
            yMin: 80,
            yMax: 80,
            borderColor: '#EB5757',
            borderWidth: 1,
            label: {
              backgroundColor: '#EB5757',
              color: '#FFFFFF',
              content: 'max',
              display: true,
              padding: 2,
            },
          },
        },
        global: { stubs: { VMenu: true, TooltipBtn: true } },
      },
      opts
    )
  )

let wrapper

describe('AnnotationCnf', () => {
  it('Each field in annotationFields should have expected attributes', async () => {
    wrapper = mountFactory()
    wrapper.vm.annotationFields.forEach((field) =>
      expect(field).to.include.all.keys('id', 'dataId', 'label', 'type')
    )
  })

  it('Should emit "on-delete" event when delete button is clicked', async () => {
    wrapper = mountFactory()
    await find(wrapper, 'delete-btn').trigger('click')
    expect(wrapper.emitted('on-delete').length).to.equal(1)
  })

  const mockFields = [
    { dataId: 'content', label: 'label', type: 'string', isLabel: true },
    { dataId: 'color', label: 'color-input', type: 'color', isLabel: true },
  ]
  mockFields.forEach((field) => {
    const updateMethod = field.type === 'color' ? 'onUpdateColor' : 'setFieldValue'

    it(`Should call ${updateMethod} when value is changed`, () => {
      wrapper = mountFactory({ computed: { annotationFields: () => [field] } })
      const spyOn = vi.spyOn(wrapper.vm, updateMethod)
      find(wrapper, `cnf-field-${field.type}`).vm.$emit('update:modelValue', '123')
      expect(spyOn).toHaveBeenCalled()
    })

    if (field.type === 'color')
      it(`Should call onClickColorInput when input is clicked`, async () => {
        wrapper = mountFactory({ computed: { annotationFields: () => [field] } })
        const spyOn = vi.spyOn(wrapper.vm, 'onClickColorInput')
        await find(wrapper, `cnf-field-${field.type}`).trigger('click')
        expect(spyOn).toHaveBeenCalled(1)
      })
  })
})
