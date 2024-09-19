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
import QueryTabNavItem from '@wkeComps/QueryEditor/QueryTabNavItem.vue'
import { queryTabStub } from '@wkeComps/QueryEditor/__tests__/stubData'
import { QUERY_TAB_TYPE_MAP } from '@/constants/workspace'
import { lodash } from '@/utils/helpers'
import { useSaveFile } from '@/composables/fileSysAccess'
import queryConnService from '@wsServices/queryConnService'
import workspaceService from '@wsServices/workspaceService'

const mountFactory = (opts = {}) =>
  mount(QueryTabNavItem, lodash.merge({ shallow: false, props: { queryTab: queryTabStub } }, opts))

const { DDL_EDITOR, TBL_EDITOR } = QUERY_TAB_TYPE_MAP
const tabTypes = Object.values(QUERY_TAB_TYPE_MAP)

vi.mock('@/composables/fileSysAccess', () => ({ useSaveFile: vi.fn() }))
vi.mock('@wsServices/queryConnService', async (importOriginal) => ({
  default: {
    ...(await importOriginal),
    findQueryTabConn: vi.fn(() => ({ is_busy: false })),
  },
}))
vi.mock('@wsServices/workspaceService', async (importOriginal) => ({
  default: {
    ...(await importOriginal),
    getIsLoading: vi.fn(() => false),
  },
}))

describe(`QueryTabNavItem`, () => {
  let wrapper, checkUnsavedChangesMock

  beforeEach(() => {
    checkUnsavedChangesMock = vi.fn().mockReturnValue(false)

    vi.mocked(useSaveFile).mockReturnValue({
      handleSaveFile: vi.fn(),
      checkUnsavedChanges: checkUnsavedChangesMock,
    })
  })

  afterEach(() => vi.clearAllMocks())

  tabTypes.forEach((type) => {
    const shouldRender = type === DDL_EDITOR || type === TBL_EDITOR

    it(`Should ${shouldRender ? '' : 'not '}render SchemaNodeIcon when tab type is ${type}`, () => {
      wrapper = mountFactory({ props: { queryTab: { ...queryTabStub, type } } })
      expect(wrapper.findComponent({ name: 'SchemaNodeIcon' }).exists()).toBe(shouldRender)
    })
  })

  it('Should emit delete event when delete button is clicked if there is no unsaved changes', async () => {
    wrapper = mountFactory()
    await find(wrapper, 'delete-btn').trigger('click')
    expect(wrapper.emitted('delete')).toHaveLength(1)
    expect(wrapper.emitted('delete')[0]).toEqual([queryTabStub.id])
  })

  it('Should show confirm dialog when delete button is clicked if there is unsaved changes', async () => {
    checkUnsavedChangesMock.mockReturnValue(true)
    wrapper = mountFactory()
    await find(wrapper, 'delete-btn').trigger('click')
    expect(wrapper.vm.confirm_dlg.is_opened).toBe(true)
  })

  it.each`
    description      | is_busy  | case          | expected
    ${'not disable'} | ${false} | ${'not busy'} | ${false}
    ${'disable'}     | ${true}  | ${'busy'}     | ${true}
  `(
    'Should $description delete button when query tab connection is $case',
    ({ is_busy, expected }) => {
      queryConnService.findQueryTabConn.mockReturnValue({ is_busy })
      const wrapper = mountFactory()
      expect(find(wrapper, 'delete-btn').props('disabled')).toBe(expected)
    }
  )

  it.each`
    description     | isLoading | expected
    ${'not render'} | ${false}  | ${false}
    ${'render'}     | ${true}   | ${true}
  `(
    'Should $description loading indicator when isLoading is $isLoading',
    ({ isLoading, expected }) => {
      workspaceService.getIsLoading.mockReturnValue(isLoading)
      wrapper = mountFactory()
      expect(wrapper.findComponent({ name: 'VProgressCircular' }).exists()).toBe(expected)
    }
  )
})
