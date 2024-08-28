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
import OpenScriptBtn from '@wkeComps/QueryEditor/OpenScriptBtn.vue'
import { lodash } from '@/utils/helpers'
import { QUERY_TAB_TYPE_MAP } from '@/constants/workspace'

const queryTabStub = {
  id: 'c3b9a5e0-645b-11ef-aa5d-c974b8dc56b6',
  name: 'Query Tab 1',
  type: QUERY_TAB_TYPE_MAP.SQL_EDITOR,
}

const fileHandleMock = {
  name: 'test.sql',
  getFile: vi.fn().mockResolvedValue(new Blob(['file content'])),
}

const mountFactory = (opts = {}) =>
  mount(
    OpenScriptBtn,
    lodash.merge(
      {
        shallow: false,
        props: {
          queryTab: queryTabStub,
          hasUnsavedChanges: false,
          hasFileSystemReadOnlyAccess: false,
        },
      },
      opts
    )
  )

describe(`OpenScriptBtn`, () => {
  let wrapper

  it('Should call handleOpenFile when the button is clicked', async () => {
    wrapper = mountFactory()
    const spy = vi.spyOn(wrapper.vm, 'handleOpenFile')
    await wrapper.find('button').trigger('click')
    expect(spy).toHaveBeenCalled()
  })

  it('Should open confirm dialog before loading new file if there is unsaved changes', async () => {
    wrapper = mountFactory({ props: { hasUnsavedChanges: true } })
    const fileLoadChangedEvent = { target: { files: [fileHandleMock] } }
    await wrapper.vm.onFileLoadChanged(fileLoadChangedEvent)

    const { is_opened, title, i18n_interpolation } = wrapper.vm.confirm_dlg

    expect(is_opened).toBe(true)
    expect(title).toBe(wrapper.vm.$t('openScript'))
    expect(i18n_interpolation).toStrictEqual({
      keypath: 'confirmations.openScript',
      values: [queryTabStub.name, fileHandleMock.name],
    })
  })
})
