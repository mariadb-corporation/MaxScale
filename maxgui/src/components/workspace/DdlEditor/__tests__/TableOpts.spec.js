/*
 * Copyright (c) 2023 MariaDB plc
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
import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import TableOpts from '@wsComps/DdlEditor/TableOpts.vue'
import { editorDataStub, charsetCollationMapStub } from '@wsComps/DdlEditor/__tests__/stubData'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(
    TableOpts,
    lodash.merge(
      {
        shallow: false,
        props: {
          modelValue: editorDataStub.options,
          engines: ['InnoDB'],
          defDbCharset: 'utf8mb4',
          charsetCollationMap: charsetCollationMapStub,
          schemas: ['company'],
          isCreating: false,
        },
      },
      opts
    )
  )

let wrapper

describe('TableOpts', () => {
  beforeEach(async () => {
    wrapper = mountFactory()
    await wrapper.vm.$nextTick()
    await wrapper.vm.$nextTick()
  })

  const debouncedInputFields = ['name', 'comment']
  debouncedInputFields.forEach((field) => {
    it(`Should pass expected data to DebouncedTextField ${field}`, () => {
      const { modelValue } = find(wrapper, field).vm.$attrs
      expect(modelValue).toStrictEqual(wrapper.vm.tblOpts[field])
    })
  })

  it(`Should pass expected data to schemas dropdown`, () => {
    const { modelValue, items, disabled } = find(wrapper, 'schemas').vm.$props
    expect(modelValue).toStrictEqual(wrapper.vm.tblOpts.schema)
    expect(items).toStrictEqual(wrapper.vm.$props.schemas)
    expect(disabled).toStrictEqual(!wrapper.vm.$props.isCreating)
  })

  it(`Should pass expected data to engines dropdown`, () => {
    const { modelValue, items } = find(wrapper, 'table-engine').vm.$props
    expect(modelValue).toStrictEqual(wrapper.vm.tblOpts.engine)
    expect(items).toStrictEqual(wrapper.vm.$props.engines)
  })

  it(`Should pass expected data to charset dropdown`, () => {
    const {
      $attrs: { modelValue, items },
      $props: { defItem },
    } = find(wrapper, 'charset').vm
    expect(modelValue).toStrictEqual(wrapper.vm.tblOpts.charset)
    expect(items).toStrictEqual(Object.keys(wrapper.vm.$props.charsetCollationMap))
    expect(defItem).toStrictEqual(wrapper.vm.$props.defDbCharset)
  })

  it(`Should pass expected data to collation dropdown`, () => {
    const {
      $attrs: { modelValue, items },
      $props: { defItem },
    } = find(wrapper, 'collation').vm

    expect(modelValue).toStrictEqual(wrapper.vm.tblOpts.collation)
    expect(items).toStrictEqual(
      wrapper.vm.$props.charsetCollationMap[wrapper.vm.tblOpts.charset].collations
    )
    expect(defItem).toStrictEqual(wrapper.vm.defCollation)
  })

  it('Should return accurate value for title', async () => {
    expect(wrapper.vm.title).toStrictEqual(wrapper.vm.$t('alterTbl'))
    await wrapper.setProps({ isCreating: true })
    expect(wrapper.vm.title).toStrictEqual(wrapper.vm.$t('createTbl'))
  })

  it('Should return accurate value for tblOpts', () => {
    expect(wrapper.vm.tblOpts).toStrictEqual(wrapper.vm.$props.modelValue)
  })

  it('Should emit update:modelValue event', async () => {
    wrapper.vm.tblOpts = null
    await wrapper.vm.$nextTick()
    expect(wrapper.emitted('update:modelValue')[0][0]).toBe(null)
  })

  it('Should return accurate value for defCollation', () => {
    expect(wrapper.vm.defCollation).toStrictEqual(
      wrapper.vm.charsetCollationMap[wrapper.vm.tblOpts.charset].defCollation
    )
  })
})
