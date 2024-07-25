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
import CharsetCollateInput from '@wsComps/TableStructureEditor/CharsetCollateInput.vue'
import { lodash } from '@/utils/helpers'
import { COL_ATTRS_IDX_MAP, COL_ATTRS } from '@/constants/workspace'
import { charsetCollationMapStub } from '@wsComps/TableStructureEditor/__tests__/stubData'

const rowDataStub = [
  'col_6c423730-3d9e-11ee-ae7d-f7b5c34f152c',
  'name',
  'VARCHAR(100)',
  false,
  false,
  false,
  false,
  false,
  false,
  '(none)',
  null,
  'utf8mb4',
  'utf8mb4_general_ci',
  '',
]

const mountFactory = (opts) =>
  mount(
    CharsetCollateInput,
    lodash.merge(
      {
        props: {
          rowData: rowDataStub,
          field: COL_ATTRS.CHARSET,
          charsetCollationMap: charsetCollationMapStub,
        },
        attrs: { modelValue: 'utf8mb4' },
      },
      opts
    )
  )

describe('CharsetCollateInput', () => {
  let wrapper
  describe(`Child component's data communication tests`, () => {
    it(`Should pass accurate data to LazyInput`, () => {
      wrapper = mountFactory()
      const {
        $attrs: {
          modelValue,
          items,
          disabled,
          'persistent-placeholder': persistentPlaceholder,
          placeholder,
        },
        $props: { isSelect, useCustomInput },
      } = wrapper.findComponent({ name: 'LazyInput' }).vm
      expect(modelValue).toBe(wrapper.vm.modelValue)
      expect(items).toStrictEqual(Object.keys(charsetCollationMapStub))
      expect(disabled).toBe(wrapper.vm.isDisabled)
      expect(persistentPlaceholder).toBeDefined()
      expect(placeholder).toBe(wrapper.vm.placeholder)
      expect(isSelect).toBe(true)
      expect(useCustomInput).toBe(true)
    })
  })
  describe(`Computed properties and method tests`, () => {
    it(`isDisabled should return true if columnType includes 'NATIONAL'`, () => {
      const idxOfType = COL_ATTRS_IDX_MAP[COL_ATTRS.TYPE]
      wrapper = mountFactory({
        props: {
          rowData: [
            ...rowDataStub.slice(0, idxOfType),
            'NATIONAL CHAR',
            ...rowDataStub.slice(idxOfType + 1),
          ],
        },
      })
      expect(wrapper.vm.isDisabled).toBe(true)
    })
    it(`modelValue should be null when attrs.modelValue is an empty string`, () => {
      wrapper = mountFactory({ attrs: { modelValue: '' } })
      expect(wrapper.vm.modelValue).toBe(null)
    })
  })
})
