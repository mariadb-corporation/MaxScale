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
import QueryTabNavToolbar from '@wkeComps/QueryEditor/QueryTabNavToolbar.vue'
import { lodash } from '@/utils/helpers'
import { QUERY_TAB_TYPE_MAP } from '@/constants/workspace'
import QueryTab from '@/store/orm/models/QueryTab'
import { expect } from 'vitest'

const activeQueryTabConnStub = { meta: { name: 'server_0' } }
const userManagementObjMock = { type: QUERY_TAB_TYPE_MAP.USER_MANAGEMENT }
const mountFactory = (opts = {}) =>
  mount(QueryTabNavToolbar, lodash.merge({ shallow: false }, opts))
const queryMockReturnEmpty = vi.hoisted(() =>
  vi.fn(() => ({
    where: () => ({ first: () => null }),
  }))
)

describe(`QueryTabNavToolbar`, () => {
  let wrapper

  vi.mock('@wsModels/QueryTab', async (importOriginal) => {
    const Original = await importOriginal()
    return {
      default: class extends Original.default {
        static query = queryMockReturnEmpty
      },
    }
  })

  afterEach(() => vi.clearAllMocks())

  it.each`
    mockData                 | when                                      | expected
    ${userManagementObjMock} | ${'when there is a User Management tab'}  | ${true}
    ${null}                  | ${'when there is no User Management tab'} | ${false}
  `('Should return $expected for hasUserManagementTab $when', ({ mockData, expected }) => {
    QueryTab.query.mockReturnValue({ where: () => ({ first: () => mockData }) })
    wrapper = mountFactory({ props: { activeQueryTabConn: activeQueryTabConnStub } })
    expect(wrapper.vm.hasUserManagementTab).toBe(expected)
  })

  it.each`
    mockData                  | expected
    ${activeQueryTabConnStub} | ${activeQueryTabConnStub.meta.name}
    ${{}}                     | ${''}
  `('Should compute connectedServerName as expected', ({ mockData, expected }) => {
    wrapper = mountFactory({ props: { activeQueryTabConn: mockData } })
    expect(wrapper.vm.connectedServerName).toBe(expected)
  })

  it.each`
    btn
    ${'add-btn'}
    ${'conn-btn'}
    ${'user-management-btn'}
  `('Should render $btn', ({ btn }) => {
    wrapper = mountFactory({ props: { activeQueryTabConn: activeQueryTabConnStub } })
    expect(find(wrapper, btn).exists()).toBe(true)
  })

  it.each`
    btn                      | event
    ${'add-btn'}             | ${'add'}
    ${'conn-btn'}            | ${'edit-conn'}
    ${'user-management-btn'} | ${'show-user-management'}
  `('Should emit $event event when $btn is clicked', async ({ btn, event }) => {
    wrapper = mountFactory({ props: { activeQueryTabConn: activeQueryTabConnStub } })
    await find(wrapper, btn).trigger('click')
    expect(wrapper.emitted(event)).toBeTruthy()
  })

  it.each`
    description                           | shouldBe                    | disabled
    ${'when activeQueryTabConn is empty'} | ${'should be disabled'}     | ${true}
    ${'when activeQueryTabConn exists'}   | ${'should not be disabled'} | ${false}
  `('$description, the add button $shouldBe', ({ disabled }) => {
    wrapper = mountFactory({
      props: { activeQueryTabConn: disabled ? {} : activeQueryTabConnStub },
    })
    expect(find(wrapper, 'add-btn').vm.$props.disabled).toBe(disabled)
  })

  it('Should compute connectedServerName correctly from activeQueryTabConn', () => {
    wrapper = mountFactory({ props: { activeQueryTabConn: activeQueryTabConnStub } })
    expect(wrapper.vm.connectedServerName).toBe(activeQueryTabConnStub.meta.name)
  })

  it('Should render content inside the query-tab-nav-toolbar-right slot', () => {
    const dataTest = 'slot-to-test'
    const wrapper = mountFactory({
      props: { activeQueryTabConn: activeQueryTabConnStub },
      slots: { 'query-tab-nav-toolbar-right': `<div data-test="${dataTest}"/>` },
    })
    expect(find(wrapper, dataTest).exists()).toBe(true)
  })

  it('Should pass expected disabled props to user-management-btn', () => {
    wrapper = mountFactory({ props: { activeQueryTabConn: activeQueryTabConnStub } })
    const { disabled } = find(wrapper, 'user-management-btn').vm.$props
    expect(disabled).toBe(!wrapper.vm.connectedServerName)
  })
})
