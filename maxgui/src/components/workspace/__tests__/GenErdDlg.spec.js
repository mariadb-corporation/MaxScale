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
import GenErdDlg from '@wsComps/GenErdDlg.vue'
import { lodash } from '@/utils/helpers'

const genErdDlgDataStub = {
  is_opened: true,
  preselected_schemas: [],
  connection: null,
  gen_in_new_ws: false,
}

const mockStore = createStore({
  state: {
    workspace: { gen_erd_dlg: genErdDlgDataStub },
    ddlEditor: { charset_collation_map: {} },
  },
  getters: {
    'worksheets/activeRequestConfig': () => false,
    'worksheets/activeId': () => '123',
  },
  commit: vi.fn(),
})

const mountFactory = (opts) =>
  mount(
    GenErdDlg,
    lodash.merge(
      {
        shallow: false,
        attrs: { attach: true },
        global: { stubs: { SelectableSchemaTableTree: true } },
      },
      opts
    ),
    mockStore
  )

describe('GenErdDlg', () => {
  let wrapper
  beforeEach(() => {
    wrapper = mountFactory()
  })

  it('Should pass expected data to BaseDlg', () => {
    const { modelValue, title, saveText, allowEnterToSubmit, onSave, hasSavingErr } =
      wrapper.findComponent({
        name: 'BaseDlg',
      }).vm.$props
    expect(modelValue).toBe(wrapper.vm.isOpened)
    expect(title).toBe(wrapper.vm.$t('selectObjsToVisualize'))
    expect(saveText).toBe('visualize')
    expect(allowEnterToSubmit).toBe(false)
    expect(hasSavingErr).toBe(wrapper.vm.hasSavingErr)
    expect(onSave).toStrictEqual(wrapper.vm.visualize)
  })

  it('Should pass expected data to name input', () => {
    const {
      $attrs: { required },
      $props: { modelValue, label },
    } = wrapper.findComponent({
      name: 'LabelField',
    }).vm
    expect(modelValue).toBe(wrapper.vm.name)
    expect(label).toBe(wrapper.vm.$t('name'))
    expect(required).toBeDefined()
  })

  it('Should pass expected data to SelectableSchemaTableTree', () => {
    const { connId, preselectedSchemas, triggerDataFetch, excludeNonFkSupportedTbl } =
      wrapper.findComponent({
        name: 'SelectableSchemaTableTree',
      }).vm.$props
    expect(connId).toBe(wrapper.vm.connId)
    expect(preselectedSchemas).toStrictEqual(wrapper.vm.preselectedSchemas)
    expect(triggerDataFetch).toBe(wrapper.vm.isOpened)
    expect(excludeNonFkSupportedTbl).toBe(true)
  })

  it(`Should render info text`, () => {
    expect(find(wrapper, 'erd-support-table-info').exists()).toBe(true)
  })

  it(`Should render error text when there is an error when generate ERD`, async () => {
    expect(find(wrapper, 'err-msg').exists()).toBe(false)
    wrapper.vm.errMsg = 'An error message'
    await wrapper.vm.$nextTick()
    expect(find(wrapper, 'err-msg').exists()).toBe(true)
  })

  it('hasSavingErr should return true when there is error in visualizing ERD', async () => {
    wrapper.vm.errMsg = 'An error message'
    await wrapper.vm.$nextTick()
    expect(wrapper.vm.hasSavingErr).toBe(true)
  })

  it('hasSavingErr should return true when selectedTargets is empty', async () => {
    wrapper.vm.selectedTargets = []
    await wrapper.vm.$nextTick()
    expect(wrapper.vm.hasSavingErr).toBe(true)
  })
})
