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
import BoolInput from '@/components/workspace/DdlEditor/BoolInput.vue'
import { COL_ATTRS } from '@/constants/workspace'
import { lodash } from '@/utils/helpers'
import { rowDataStub } from '@/components/workspace/DdlEditor/__tests__/stubData'

const mountFactory = (opts) =>
  mount(
    BoolInput,
    lodash.merge(
      {
        props: {
          modelValue: true,
          rowData: rowDataStub,
          field: COL_ATTRS.PK,
        },
      },
      opts
    )
  )

describe('BoolInput', () => {
  let wrapper
  describe(`Child component's data communication tests`, () => {
    it(`Should pass accurate data to VCheckbox`, () => {
      wrapper = mountFactory()

      const { modelValue, hideDetails, disabled } = wrapper.findComponent({
        name: 'VCheckbox',
      }).vm.$props
      expect(modelValue).to.be.eql(wrapper.vm.inputValue)
      expect(disabled).to.be.eql(wrapper.vm.isDisabled)
      expect(hideDetails).to.be.true
    })
  })

  describe(`Computed properties tests`, () => {
    it(`colData should have expected properties`, () => {
      wrapper = mountFactory()
      expect(wrapper.vm.colData).to.have.all.keys('type', 'isPK', 'isAI', 'isGenerated')
    })
    it(`Should return accurate modelValue for inputValue`, () => {
      wrapper = mountFactory()
      expect(wrapper.vm.inputValue).to.be.eql(wrapper.vm.$props.modelValue)
    })

    it(`Should emit update:modelValue event`, () => {
      wrapper = mountFactory()
      wrapper.vm.inputValue = false
      expect(wrapper.emitted('update:modelValue')[0]).to.be.eql([false])
    })
  })
})
