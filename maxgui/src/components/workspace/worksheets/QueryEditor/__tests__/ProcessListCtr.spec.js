/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
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
          resultDataTableProps: {},
          isLoading: false,
        },
      },
      opts
    )
  )

describe('ProcessListCtr', () => {
  let wrapper

  const isLoadingTestCases = [true, false]

  isLoadingTestCases.forEach((v) => {
    describe(`When isLoading is ${v}`, () => {
      beforeEach(() => (wrapper = mountFactory({ props: { isLoading: v } })))
      it(`Should ${v ? '' : 'not '}render loading indicator`, () => {
        expect(wrapper.findComponent({ name: 'VProgressLinear' }).exists()).toBe(v)
      })

      it(`Should ${v ? 'not ' : ''}render ResultSetTable`, () => {
        expect(wrapper.findComponent({ name: 'ResultSetTable' }).exists()).toBe(!v)
      })
    })
  })

  const queryProcessListMock = vi.hoisted(() => vi.fn(() => ({})))
  vi.mock('@/services/workspace/queryResultService', async (importOriginal) => ({
    default: {
      ...(await importOriginal),
      queryProcessList: queryProcessListMock,
    },
  }))

  afterEach(() => vi.clearAllMocks())

  it('Should immediately fetch data if queryTabConn.id is defined', async () => {
    wrapper = mountFactory({ props: { queryTabConn: { id: 456 } } })
    expect(queryProcessListMock).toHaveBeenCalledTimes(1)
  })

  it('Should fetch data if queryTabConn.id is changed', async () => {
    wrapper = mountFactory()
    await wrapper.setProps({ queryTabConn: { id: 456 } })
    expect(queryProcessListMock).toHaveBeenCalledTimes(1)
  })
})
