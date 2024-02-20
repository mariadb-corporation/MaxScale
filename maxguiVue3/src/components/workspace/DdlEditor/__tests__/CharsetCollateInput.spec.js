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
import CharsetCollateInput from '@/components/workspace/DdlEditor/CharsetCollateInput.vue'
import { lodash } from '@/utils/helpers'
import { COL_ATTRS_IDX_MAP, COL_ATTRS } from '@/constants/workspace'
import {
  rowDataStub,
  charsetCollationMapStub,
} from '@/components/workspace/DdlEditor/__tests__/stubData'

const mountFactory = (opts) =>
  mount(
    CharsetCollateInput,
    lodash.merge(
      {
        props: {
          modelValue: '',
          rowData: rowDataStub,
          field: COL_ATTRS.CHARSET,
          charsetCollationMap: charsetCollationMapStub,
        },
      },
      opts
    )
  )

describe('CharsetCollateInput', () => {
  let wrapper
  describe(`Child component's data communication tests`, () => {
    it(`Should pass accurate data to CharsetCollateSelect`, () => {
      wrapper = mountFactory()
      const {
        $attrs: { modelValue, items, disabled },
        $props: { defItem },
      } = wrapper.findComponent({
        name: 'CharsetCollateSelect',
      }).vm
      expect(modelValue).to.be.eql(wrapper.vm.inputValue)
      expect(items).to.be.eql(Object.keys(charsetCollationMapStub))
      expect(disabled).to.be.eql(wrapper.vm.isDisabled)
      expect(defItem).to.be.eql(wrapper.vm.$props.defTblCharset)
    })
  })

  describe(`Computed properties and method tests`, () => {
    it(`isDisabled should return true if columnType includes 'NATIONAL'`, () => {
      let idxOfType = COL_ATTRS_IDX_MAP[COL_ATTRS.TYPE]
      wrapper = mountFactory({
        props: {
          rowData: [
            ...rowDataStub.slice(0, idxOfType),
            'NATIONAL CHAR',
            ...rowDataStub.slice(idxOfType + 1),
          ],
        },
      })
      expect(wrapper.vm.isDisabled).to.be.true
    })
    it(`Should return accurate modelValue for inputValue`, () => {
      wrapper = mountFactory()
      expect(wrapper.vm.inputValue).to.be.eql(wrapper.vm.$props.modelValue)
    })

    it(`Should emit update:modelValue event`, () => {
      wrapper = mountFactory()
      wrapper.vm.inputValue = 'dec8'
      expect(wrapper.emitted('update:modelValue')[0]).to.be.eql(['dec8'])
    })
  })
})
