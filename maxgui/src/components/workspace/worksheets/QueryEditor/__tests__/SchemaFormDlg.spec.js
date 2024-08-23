/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import SchemaFormDlg from '@wkeComps/QueryEditor/SchemaFormDlg.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(
    SchemaFormDlg,
    lodash.merge(
      {
        shallow: false,
        attrs: { modelValue: true, attach: true },
        global: { stubs: { SqlEditor: true } },
      },
      opts
    )
  )

describe(`SchemaFormDlg`, () => {
  let wrapper
  beforeEach(() => (wrapper = mountFactory()))

  afterEach(() => vi.clearAllMocks())

  it(`Should pass expected data to BaseDlg`, () => {
    const {
      title,
      onSave,
      saveText,
      saveDisabled,
      hasSavingErr,
      allowEnterToSubmit,
      lazyValidation,
    } = wrapper.findComponent({ name: 'BaseDlg' }).vm.props
    expect(title).toBe(wrapper.vm.title)
    expect(onSave).toBe(wrapper.vm.exe)
    expect(saveText).toBe(wrapper.vm.saveText)
    expect(saveDisabled).toBe(wrapper.vm.saveDisabled)
    expect(hasSavingErr).toBe(wrapper.vm.hasError)
    expect(allowEnterToSubmit).toBe(false)
    expect(lazyValidation).toBe(false)
  })

  const inputs = ['schema', 'charset', 'collation', 'comment']
  inputs.forEach((name) => {
    it(`Should render ${name} input`, () => {
      expect(find(wrapper, name).exists()).toBe(true)
    })

    if (name === 'charset' || name === 'collation') {
      it(`Should pass expected data to ${name} input`, () => {
        const {
          $props: { defItem },
          $attrs: { modelValue, items },
        } = find(wrapper, name).vm
        expect(defItem).toBe(name === 'charset' ? wrapper.vm.defDbCharset : wrapper.vm.defCollation)
        expect(modelValue).toBe(
          name === 'charset' ? wrapper.vm.form.charset : wrapper.vm.form.collation
        )
        expect(items).toStrictEqual(
          name === 'charset' ? wrapper.vm.charsets : wrapper.vm.collations
        )
      })

      it(`Should not apply required rule to ${name} input if isAltering props is false`, async () => {
        expect(find(wrapper, name).vm.$attrs.rules).toStrictEqual([])
      })
    }

    if (name === 'comment')
      it(`Should pass expected data to ${name} input`, () => {
        const { modelValue, autoGrow, rows } = find(wrapper, name).vm.$props
        expect(modelValue).toStrictEqual(wrapper.vm.form.comment)
        expect(autoGrow).toBe(true)
        expect(rows).toBe(2)
      })
  })

  it(`Should disable schema input if isAltering props is true`, async () => {
    await wrapper.setProps({ isAltering: true })
    expect(find(wrapper, 'schema').vm.$props.disabled).toBe(true)
  })

  it(`Should not show SQL Preview if form is invalid`, async () => {
    wrapper.vm.formValidity = false
    await wrapper.vm.$nextTick()
    expect(wrapper.findComponent({ name: 'SqlEditor' }).exists()).toBe(false)
  })

  it(`Should pass expected data to SqlEditor`, () => {
    const { modelValue, options, readOnly } = wrapper.findComponent({
      name: 'SqlEditor',
    }).vm.$props
    expect(modelValue).toBe(wrapper.vm.sql)
    expect(options).toStrictEqual({
      fontSize: 10,
      contextmenu: false,
      lineNumbers: 'off',
      folding: false,
      lineNumbersMinChars: 0,
      lineDecorationsWidth: 12,
    })
    expect(readOnly).toBe(true)
  })

  it(`Should render result error section`, async () => {
    expect(find(wrapper, 'result-error-tbl').exists()).toBe(false)
    wrapper.vm.resultErr = {
      errno: 1064,
      message: 'You have an error in your SQL syntax;',
      sqlstate: '42000',
    }
    await wrapper.vm.$nextTick()
    expect(find(wrapper, 'result-error-tbl').exists()).toBe(true)
  })
})
