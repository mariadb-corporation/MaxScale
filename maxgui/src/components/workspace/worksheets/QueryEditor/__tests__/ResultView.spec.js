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
import ResultView from '@wkeComps/QueryEditor/ResultView.vue'
import { lodash } from '@/utils/helpers'
import { stmtStub } from '@wkeComps/QueryEditor/__tests__/stubData'

const stubFieldName = 'name'
const dataStub = {
  data: {
    attributes: {
      sql: stmtStub.text,
      results: [{ data: [['a']], fields: [stubFieldName], statement: stmtStub }],
      execution_time: 0.000314734,
    },
  },
  end_time: 1725433688167,
  is_loading: false,
  start_time: 1725433688127,
}

const mountFactory = (opts = {}) =>
  mount(
    ResultView,
    lodash.merge(
      {
        shallow: false,
        props: { data: dataStub, dim: { width: 800, height: 600 } },
      },
      opts
    )
  )

describe(`ResultView`, () => {
  let wrapper

  it('Should pass expected data to QueryResultTabWrapper', () => {
    wrapper = mountFactory()
    const { dim, isLoading, showFooter, resInfoBarProps } = wrapper.findComponent({
      name: 'QueryResultTabWrapper',
    }).vm.$props

    expect(dim).toBe(wrapper.vm.$props.dim)
    expect(isLoading).toBe(dataStub.is_loading)
    expect(showFooter).toBe(dataStub.is_loading || wrapper.vm.hasRes)
    expect(resInfoBarProps).toStrictEqual({
      result: dataStub.data.attributes.results[0],
      startTime: wrapper.vm.startTime,
      execTime: wrapper.vm.execTime,
      endTime: wrapper.vm.endTime,
    })
  })

  it('Should pass expected data to DataTable', () => {
    const placeToEditorStub = vi.fn()
    wrapper = mountFactory({ props: { dataTableProps: { placeToEditor: placeToEditorStub } } })
    const { data, toolbarProps, placeToEditor } = wrapper.findComponent({
      name: 'DataTable',
    }).vm.$props
    expect(data).toStrictEqual(dataStub.data.attributes.results[0])
    expect(toolbarProps).toStrictEqual({ statement: stmtStub, onReload: wrapper.vm.$props.reload })
    expect(placeToEditor).toStrictEqual(placeToEditorStub)
  })

  it.each`
    slotName
    ${'toolbar-left-append'}
    ${`header-${stubFieldName}`}
  `(`Should render DataTable's slot $slotName `, ({ slotName }) => {
    wrapper = mountFactory({
      slots: {
        [slotName]: `<div data-test="${slotName}"/>`,
      },
    })
    expect(find(wrapper, slotName).exists()).toBe(true)
  })
})
