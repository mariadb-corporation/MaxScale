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
 *  Public License.
 */
import mount from '@/tests/mount'
import SaveScriptBtn from '@wkeComps/QueryEditor/SaveScriptBtn.vue'
import { lodash } from '@/utils/helpers'
import { QUERY_TAB_TYPE_MAP } from '@/constants/workspace'
import { useSaveFile } from '@/composables/fileSysAccess'

const queryTabStub = {
  id: 'c3b9a5e0-645b-11ef-aa5d-c974b8dc56b6',
  name: 'Query Tab 1',
  type: QUERY_TAB_TYPE_MAP.SQL_EDITOR,
}

const mountFactory = (opts = {}) =>
  mount(
    SaveScriptBtn,
    lodash.merge(
      { shallow: false, props: { queryTab: queryTabStub, hasUnsavedChanges: false } },
      opts
    )
  )

vi.mock('@/composables/fileSysAccess', () => ({ useSaveFile: vi.fn() }))

describe(`SaveScriptBtn`, () => {
  let wrapper

  let isFileHandleValidMock, saveFileToDiskMock

  beforeEach(() => {
    isFileHandleValidMock = vi.fn().mockReturnValue(true)
    saveFileToDiskMock = vi.fn()

    vi.mocked(useSaveFile).mockReturnValue({
      isFileHandleValid: isFileHandleValidMock,
      saveFileToDisk: saveFileToDiskMock,
    })
  })

  afterEach(() => vi.clearAllMocks())

  it('Should disable the save button when there are no unsaved changes or file handle is invalid', () => {
    // hasUnsavedChanges is false, button should be disabled
    isFileHandleValidMock.mockReturnValue(true)
    wrapper = mountFactory({ props: { hasUnsavedChanges: false } })
    expect(wrapper.vm.disabled).toBe(true)
    // file handle is invalid, button should be disabled
    isFileHandleValidMock.mockReturnValue(false)
    wrapper.setProps({ hasUnsavedChanges: true })
    expect(wrapper.vm.disabled).toBe(true)
  })

  it('should not disable the save button when there are unsaved changes and file handle is valid', () => {
    isFileHandleValidMock.mockReturnValue(true)
    const wrapper = mountFactory({ props: { hasUnsavedChanges: true } })
    expect(wrapper.vm.disabled).toBe(false)
  })

  it('Should call save when the button is clicked', async () => {
    wrapper = mountFactory({ props: { hasUnsavedChanges: true } })
    const spy = vi.spyOn(wrapper.vm, 'save')
    await wrapper.find('button').trigger('click')
    expect(spy).toHaveBeenCalled()
  })
})
