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
import { find } from '@/tests/utils'
import ResultExport from '@wkeComps/QueryEditor/ResultExport.vue'
import { lodash } from '@/utils/helpers'

const propsStub = {
  fields: ['id', 'name', 'description'],
  rows: [
    [1, 'maxscale', null],
    [2, 'mariadb', 'string with quote "'],
  ],
  defExportFileName: 'TestExport',
  exportAsSQL: true,
  metadata: [],
}

const mountFactory = (opts = {}) =>
  mount(ResultExport, lodash.merge({ shallow: false, props: propsStub }, opts))

describe(`ResultExport`, () => {
  let wrapper

  it.each`
    case             | exportAsSQL | expectedFormats
    ${'exclude'}     | ${false}    | ${['csv', 'json']}
    ${'not exclude'} | ${true}     | ${['csv', 'json', 'sql']}
  `(`Should $case sql option`, ({ exportAsSQL, expectedFormats }) => {
    wrapper = mountFactory({ props: { exportAsSQL } })
    const fileFormats = wrapper.vm.fileFormats
    expect(fileFormats.length).toBe(exportAsSQL ? 3 : 2)
    expect(fileFormats.map((f) => f.extension)).toStrictEqual(expectedFormats)
  })

  it.each`
    case                               | excludedFieldIndexes | expectedFields
    ${'when all fields are selected'}  | ${[]}                | ${propsStub.fields.map((field, index) => ({ value: field, index }))}
    ${'when some fields are selected'} | ${[1, 2]}            | ${[{ value: 'id', index: 0 }]}
  `(
    'Should compute selectedFields correctly $case',
    async ({ excludedFieldIndexes, expectedFields }) => {
      wrapper = mountFactory()
      wrapper.vm.excludedFieldIndexes = excludedFieldIndexes
      await wrapper.vm.$nextTick()
      expect(wrapper.vm.selectedFields).toEqual(expectedFields)
    }
  )

  it('Should compute selectedFieldsLabel correctly', () => {
    wrapper = mountFactory()
    const firstSelectedField = wrapper.vm.selectedFields[0].value
    expect(wrapper.vm.selectedFieldsLabel).toBe(
      `${firstSelectedField} (+${wrapper.vm.totalSelectedFields - 1} others)`
    )
  })

  it('Should open the dialog when the download icon button is clicked', async () => {
    wrapper = mountFactory()
    await find(wrapper, 'download-btn').trigger('click')
    expect(wrapper.vm.isConfigDialogOpened).toBe(true)
  })

  it('Should pass expected data to BaseDlg', () => {
    wrapper = mountFactory()
    const { modelValue, onSave, lazyValidation } = wrapper.findComponent({ name: 'BaseDlg' }).vm
      .$props
    expect(modelValue).toBe(wrapper.vm.isConfigDialogOpened)
    expect(onSave).toStrictEqual(wrapper.vm.onExport)
    expect(lazyValidation).toBe(false)
  })
})
