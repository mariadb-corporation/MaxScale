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
import FkColFieldInput from '@wsComps/DdlEditor/FkColFieldInput.vue'
import { lodash } from '@/utils/helpers'
import { FK_EDITOR_ATTRS } from '@/constants/workspace'

const referencingColOptsStub = [
  {
    id: 'col_750b1f70-3b5b-11ee-a3ad-dfd43862371d',
    text: 'id',
    type: 'int(11)',
    disabled: false,
  },
  {
    id: 'col_750b1f71-3b5b-11ee-a3ad-dfd43862371d',
    text: 'department_id',
    type: 'int(11)',
    disabled: false,
  },
]
const refColOptsStub = [
  {
    id: 'col_f2404150-3b6f-11ee-9c95-7dfe6062fdca',
    text: 'id',
    type: 'int(11)',
    disabled: false,
  },
  {
    id: 'col_f2404151-3b6f-11ee-9c95-7dfe6062fdca',
    text: 'name',
    type: 'varchar(100)',
    disabled: false,
  },
]

const mountFactory = (opts) =>
  mount(
    FkColFieldInput,
    lodash.merge(
      {
        props: {
          field: FK_EDITOR_ATTRS.COLS,
          referencingColOptions: referencingColOptsStub,
          refColOpts: refColOptsStub,
        },
        attrs: {
          modelValue: [
            'col_41edfb20-3dbe-11ee-89d0-3d05b76780b9',
            'col_41edfb21-3dbe-11ee-89d0-3d05b76780b9',
          ],
        },
      },
      opts
    )
  )

describe('FkColFieldInput', () => {
  let wrapper
  it(`Should pass expected data to LazyInput`, () => {
    wrapper = mountFactory()
    const {
      $attrs: {
        items,
        multiple,
        ['item-title']: itemTitle,
        ['item-value']: itemValue,
        rules,
        modelValue,
      },
      $props: { isSelect, selectionText, required, useCustomInput },
    } = wrapper.findComponent({
      name: 'LazyInput',
    }).vm
    expect(items).toStrictEqual(wrapper.vm.items)
    expect(itemTitle).toStrictEqual('text')
    expect(itemValue).toStrictEqual('id')
    expect(multiple).toBeDefined()
    expect(required).toBe(true)
    expect(rules).toBeInstanceOf(Array)
    expect(selectionText).toBe(wrapper.vm.selectionText)
    expect(isSelect).toBe(true)
    expect(useCustomInput).toBe(true)
    expect(modelValue).toStrictEqual(wrapper.vm.$attrs.modelValue)
  })
})
