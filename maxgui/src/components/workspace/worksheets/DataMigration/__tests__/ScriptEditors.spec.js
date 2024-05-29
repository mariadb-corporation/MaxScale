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
import ScriptEditors from '@wkeComps/DataMigration/ScriptEditors.vue'

const modelValueStub = {
  select: 'select script',
  create: 'create script',
  insert: 'insert script',
}

describe('ScriptEditors', () => {
  let wrapper
  const editorComponents = [
    {
      target: 'select-script',
      dataField: 'select',
      labelField: 'retrieveDataFromSrc',
    },
    {
      target: 'create-script',
      dataField: 'create',
      labelField: 'createObjInDest',
    },
    {
      target: 'insert-script',
      dataField: 'insert',
      labelField: 'insertDataInDest',
    },
  ]
  editorComponents.forEach(({ target, dataField, labelField }) => {
    it(`Should pass expected data to ${target} ScriptEditor`, () => {
      wrapper = mount(ScriptEditors, { props: { modelValue: modelValueStub, hasChanged: false } })
      const { modelValue, label, skipRegEditorCompleters } = find(wrapper, target).vm.$props
      expect(modelValue).toBe(wrapper.vm.stagingRow[dataField])
      expect(label).toBe(wrapper.vm.$t(labelField))
      expect(skipRegEditorCompleters).toBe(dataField !== 'select')
    })
  })

  it(`Should not render an error message if isInErrState is false`, () => {
    wrapper = mount(ScriptEditors, {
      props: { modelValue: modelValueStub, hasChanged: false },
    })
    expect(find(wrapper, 'script-err-msg').exists()).toBe(false)
  })

  it(`Should render an error message if isInErrState is true`, () => {
    wrapper = mount(ScriptEditors, {
      // mock isInErrState state
      props: { modelValue: { select: '', create: '', insert: '' }, hasChanged: false },
    })
    expect(find(wrapper, 'script-err-msg').text()).toBe(wrapper.vm.$t('errors.scriptCanNotBeEmpty'))
  })

  const hasChangedTests = [false, true]
  hasChangedTests.forEach((v) => {
    it(`Should${v ? '' : ' not'} render discard-btn when hasChanged is ${v}`, () => {
      wrapper = mount(ScriptEditors, { props: { modelValue: modelValueStub, hasChanged: v } })
      expect(find(wrapper, 'discard-btn').exists()).toBe(v)
    })
  })

  const testCases = [
    {
      name: 'create is an empty string',
      data: { select: 'select script', create: '', insert: 'insert script' },
      expected: true,
    },
    {
      name: 'all properties are defined and not empty',
      data: { select: 'select script', create: 'create script', insert: 'insert script' },
      expected: false,
    },
  ]
  testCases.forEach((testCase) => {
    it(`Should return ${testCase.expected} when ${testCase.name}`, () => {
      wrapper = mount(ScriptEditors, { props: { modelValue: testCase.data, hasChanged: false } })
      expect(wrapper.vm.isInErrState).toBe(testCase.expected)
    })
  })

  it(`Should return accurate value for stagingRow`, () => {
    wrapper = mount(ScriptEditors, { props: { modelValue: modelValueStub, hasChanged: false } })
    expect(wrapper.vm.stagingRow).toStrictEqual(modelValueStub)
  })

  it(`Should emit 'update:modelValue' event when stagingRow is changed`, () => {
    wrapper = mount(ScriptEditors, { props: { modelValue: modelValueStub, hasChanged: false } })
    const newValue = { ...modelValueStub, select: 'new value' }
    wrapper.vm.stagingRow = newValue
    expect(wrapper.emitted('update:modelValue')[0][0]).toStrictEqual(newValue)
  })
})
