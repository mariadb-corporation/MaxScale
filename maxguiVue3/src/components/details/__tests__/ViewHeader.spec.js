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
import { find } from '@/tests/utils'
import ViewHeader from '@/components/details/ViewHeader.vue'
import { MXS_OBJ_TYPES } from '@/constants'
import { lodash } from '@/utils/helpers'
import { createStore } from 'vuex'

const mockRoute = { params: { id: 'row_server_1' } }
const store = createStore({ getters: { 'users/isAdmin': () => true } })
const stubMonitor = { attributes: { module: 'MariadbMon', state: 'Running' } }
const PortalStub = { name: 'PortalStub', template: `<div><slot></slot></div>` }

const mountFactory = (opts, factoryStore) =>
  mount(
    ViewHeader,
    lodash.merge(
      {
        shallow: false,
        props: { item: stubMonitor, onConfirm: vi.fn(), type: MXS_OBJ_TYPES.MONITORS },
        global: {
          mocks: { $route: mockRoute },
          stubs: { portal: PortalStub },
        },
      },
      opts
    ),
    factoryStore || store
  )

describe('ViewHeader', () => {
  let wrapper

  it(`Should render accurate page title`, () => {
    wrapper = mountFactory()
    const pageTitleEle = find(wrapper, 'page-title')
    expect(pageTitleEle.exists()).toBe(true)
    expect(pageTitleEle.text()).toBe('row_server_1')
  })

  const slotsTestCases = ['page-title', 'state-append', 'confirm-dlg-body-append']
  slotsTestCases.forEach((slotName) => {
    it(`Should render accurate content when ${slotName} slot is used`, async () => {
      wrapper = mountFactory({
        slots: {
          [slotName]: '<div data-test="test">slot content</div>',
        },
      })
      if (slotName === 'confirm-dlg-body-append') {
        wrapper.vm.opHandler({ title: 'delete', type: 'delete' }) // mock open the dialog
        await wrapper.vm.$nextTick(() => expect(find(wrapper, 'test').exists()).toBe(true))
      } else expect(find(wrapper, 'test').exists()).toBe(true)
    })
  })

  it(`Should not render menu-activator-btn when the user is not admin`, async () => {
    wrapper = mountFactory({}, createStore({ getters: { 'users/isAdmin': () => false } }))
    expect(find(wrapper, 'menu-activator-btn').exists()).toBe(false)
  })

  const horizOperationListPropsTestCases = [
    { component: 'HorizOperationsList', value: true },
    { component: 'OperationsList', value: false },
  ]
  horizOperationListPropsTestCases.forEach(({ component, value }) => {
    it(`Should render ${component} when horizOperationList is ${value}`, async () => {
      wrapper = mountFactory({ props: { horizOperationList: value } })
      await find(wrapper, 'menu-activator-btn').trigger('click')
      expect(wrapper.findComponent({ name: component }).exists()).toBe(true)
    })
  })

  const componentRenderTestCases = [
    { props: { onCountDone: undefined }, component: 'RefreshRate' },
    { props: { showGlobalSearch: false }, component: 'GlobalSearch' },
    { props: { showStateIcon: false }, component: 'StatusIcon' },
  ]
  componentRenderTestCases.forEach(({ props, component }) => {
    it(`Should not render ${component}`, () => {
      wrapper = mountFactory({ props })
      expect(wrapper.findComponent({ name: component }).exists()).toBe(false)
    })
  })

  it(`Should pass expected props to RefreshRate`, () => {
    wrapper = mountFactory({ props: { onCountDone: vi.fn() } })
    const { onCountDone } = wrapper.findComponent({ name: 'RefreshRate' }).vm.$props
    expect(onCountDone).toStrictEqual(wrapper.vm.$props.onCountDone)
  })

  it(`Should pass expected props to CreateMxsObj `, () => {
    wrapper = mountFactory()
    const { defFormType, defRelationshipObj } = wrapper.findComponent({ name: 'CreateMxsObj' }).vm
      .$props
    expect(defFormType).toBe(wrapper.vm.$props.defFormType)
    expect(defRelationshipObj).toStrictEqual({
      id: mockRoute.params.id,
      type: wrapper.vm.$props.type,
    })
  })

  it(`Should pass expected props to ConfirmDlg`, () => {
    wrapper = mountFactory()
    const confirmDialog = wrapper.findComponent({ name: 'ConfirmDlg' })
    const { modelValue, title, saveText, onSave } = confirmDialog.vm.$attrs
    const { item, type, smallInfo } = confirmDialog.vm.$props
    const {
      modelValue: isOpened,
      title: confDlgTitle,
      saveText: confDlgSaveTxt,
      type: confDlgType,
      smallInfo: confDlgSmallInfo,
      onSave: confDlgOnSave,
    } = wrapper.vm.confirmDlg
    expect(item).toStrictEqual(wrapper.vm.$props.item)
    expect(modelValue).toBe(isOpened)
    expect(title).toBe(confDlgTitle)
    expect(type).toBe(confDlgType)
    expect(smallInfo).toBe(confDlgSmallInfo)
    expect(saveText).toBe(confDlgSaveTxt)
    expect(onSave).toStrictEqual(confDlgOnSave)
  })

  it(`Should pass expected props to StatusIcon`, () => {
    wrapper = mountFactory({ props: { showStateIcon: true } })
    const { size, type, value } = wrapper.findComponent({ name: 'StatusIcon' }).vm.$props
    expect(size).toBe('16')
    expect(type).toBe(wrapper.vm.$props.type)
    expect(value).toStrictEqual(stubMonitor.attributes.state)
  })

  it(`Should render accurate state label if it is defined`, async () => {
    wrapper = mountFactory()
    expect(find(wrapper, 'state-label').exists()).toBe(false)
    const mockStateLabel = 'Start'
    await wrapper.setProps({ stateLabel: mockStateLabel })
    expect(find(wrapper, 'state-label').text()).toBe(mockStateLabel)
  })
})
