/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import ScriptEditor from '@wkeComps/DataMigration/ScriptEditor.vue'

describe('ScriptEditor', () => {
  let wrapper
  beforeEach(() => {
    wrapper = mount(ScriptEditor, {
      props: {
        modelValue: 'SELECT `id` FROM `Workspace`.`AnalyzeEditor`',
        label: 'Retrieve data from source',
        skipRegEditorCompleters: false,
      },
    })
  })

  it('Should add script-editor--error class if there is no value', async () => {
    await wrapper.setProps({ modelValue: '' })
    expect(wrapper.classes('script-editor--error')).toBe(true)
  })

  it('Should render min/max button', () => {
    expect(find(wrapper, 'min-max-btn').exists()).toBe(true)
  })

  it('Should pass expected data to SqlEditor', () => {
    const { modelValue, options, skipRegCompleters } = wrapper.findComponent({
      name: 'SqlEditor',
    }).vm.$props
    expect(modelValue).toBe(wrapper.vm.sql)
    expect(options).toStrictEqual({ contextmenu: false, wordWrap: 'on' })
    expect(skipRegCompleters).toBe(wrapper.vm.$props.skipRegEditorCompleters)
  })

  it('Should emit update:modelValue event when sql is changed', () => {
    const newValue = 'SELECT 1'
    wrapper.vm.sql = newValue
    expect(wrapper.emitted('update:modelValue')[0][0]).toBe(newValue)
  })
})
