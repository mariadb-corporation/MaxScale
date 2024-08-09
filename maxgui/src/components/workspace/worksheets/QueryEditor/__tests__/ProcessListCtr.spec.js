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
 *  Public License.
 */
import mount from '@/tests/mount'
import ProcessListCtr from '@wkeComps/QueryEditor/ProcessListCtr.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(
    ProcessListCtr,
    lodash.merge(
      {
        props: {
          dim: { width: 800, height: 500 },
          data: {},
          queryTabConn: {},
          dataTableProps: {},
          isLoading: false,
          resInfoBarProps: {},
        },
      },
      opts
    )
  )

describe('ProcessListCtr', () => {
  let wrapper

  const queryProcessListMock = vi.hoisted(() => vi.fn(() => ({})))
  vi.mock('@/services/workspace/queryResultService', async (importOriginal) => ({
    default: {
      ...(await importOriginal),
      queryProcessList: queryProcessListMock,
    },
  }))

  afterEach(() => vi.clearAllMocks())

  it(`Should pass expected data to DataTable `, () => {
    wrapper = mountFactory({
      shallow: false,
      props: { isLoading: false, queryTabConn: { id: '123s' } },
    })
    const {
      $attrs: { selectedItems, showSelect },
      $props: { data, height, width, toolbarProps, defHiddenHeaderIndexes },
    } = wrapper.findComponent({ name: 'DataTable' }).vm
    expect(selectedItems).toStrictEqual(wrapper.vm.selectedItems)
    expect(data).toStrictEqual(wrapper.vm.resultSet)
    expect(toolbarProps).toStrictEqual({
      deleteItemBtnTooltipTxt: 'killNProcess',
      customFilterActive: Boolean(wrapper.vm.processTypesToShow.length),
      statement: wrapper.vm.statement,
      onDelete: wrapper.vm.handleOpenExecSqlDlg,
      onReload: wrapper.vm.onReload,
    })
    expect(defHiddenHeaderIndexes).toStrictEqual(wrapper.vm.defHiddenHeaderIndexes)
    expect(showSelect).toBeDefined()
    expect(height).toBeDefined()
    expect(width).toBeDefined()
  })
})
