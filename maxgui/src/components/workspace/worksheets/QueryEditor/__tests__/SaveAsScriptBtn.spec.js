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
import SaveAsScriptBtn from '@wkeComps/QueryEditor/SaveAsScriptBtn.vue'
import { QUERY_TAB_TYPE_MAP } from '@/constants/workspace'
import { useSaveFile } from '@/composables/fileSysAccess'
import { lodash } from '@/utils/helpers'

const queryTabStub = {
  id: 'c3b9a5e0-645b-11ef-aa5d-c974b8dc56b6',
  name: 'Query Tab 1',
  type: QUERY_TAB_TYPE_MAP.SQL_EDITOR,
}

const mountFactory = (opts = {}) =>
  mount(SaveAsScriptBtn, lodash.merge({ shallow: false, props: { queryTab: queryTabStub } }, opts))

vi.mock('@/composables/fileSysAccess', () => ({ useSaveFile: vi.fn() }))

describe(`SaveAsScriptBtn`, () => {
  let wrapper

  let getEditorMock, handleSaveFileAsMock

  beforeEach(() => {
    getEditorMock = vi.fn()
    handleSaveFileAsMock = vi.fn()

    vi.mocked(useSaveFile).mockReturnValue({
      getEditor: getEditorMock,
      handleSaveFileAs: handleSaveFileAsMock,
    })
  })

  afterEach(() => vi.clearAllMocks())

  it('Should not disable the save button when the editor has some text', () => {
    getEditorMock.mockReturnValue({ sql: 'SELECT 1' })
    wrapper = mountFactory()
    expect(wrapper.vm.disabled).toBe(false)
  })
  it('Should disable the save button when the editor has no text', () => {
    getEditorMock.mockReturnValue({ sql: '' })
    wrapper = mountFactory()
    expect(wrapper.vm.disabled).toBe(true)
  })

  it('Should call save when the button is clicked', async () => {
    getEditorMock.mockReturnValue({ sql: 'SELECT 1' })
    wrapper = mountFactory({ props: { hasUnsavedChanges: true } })
    const spy = vi.spyOn(wrapper.vm, 'save')
    await wrapper.find('button').trigger('click')
    expect(spy).toHaveBeenCalled()
  })
})
