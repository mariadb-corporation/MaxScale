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
import InsightViewerTabItem from '@wkeComps/QueryEditor/InsightViewerTabItem.vue'
import { lodash } from '@/utils/helpers'
import { NODE_TYPE_MAP, INSIGHT_SPEC_MAP } from '@/constants/workspace'
import { stmtStub } from '@wkeComps/QueryEditor/__tests__/stubData'

const { COLUMNS, INDEXES, TRIGGERS, SP, FN } = INSIGHT_SPEC_MAP

const mockProps = {
  data: {
    data: {
      attributes: {
        results: [
          {
            statement: stmtStub,
            fields: ['id', 'name', 'value'],
          },
        ],
      },
    },
  },
  dim: { width: 800, height: 600 },
  spec: COLUMNS,
  nodeType: NODE_TYPE_MAP.TBL,
  isSchemaNode: false,
  onReload: vi.fn(),
}

const expectedResultSet = mockProps.data.data.attributes.results[0]

const mountFactory = (opts = {}) =>
  mount(
    InsightViewerTabItem,
    lodash.merge({ shallow: false, props: mockProps, global: { stubs: { SqlEditor: true } } }, opts)
  )

describe(`InsightViewerTabItem`, () => {
  let wrapper

  it('Should pass expected data to QueryResultTabWrapper', () => {
    wrapper = mountFactory()
    const { dim, isLoading, showFooter, resInfoBarProps } = wrapper.findComponent({
      name: 'QueryResultTabWrapper',
    }).vm.$props
    expect(dim).toEqual(mockProps.dim)
    expect(isLoading).toBe(wrapper.vm.isLoading)
    expect(showFooter).toBe(true)
    expect(resInfoBarProps).toStrictEqual({
      result: expectedResultSet,
      startTime: wrapper.vm.startTime,
      execTime: wrapper.vm.execTime,
      endTime: wrapper.vm.endTime,
    })
  })

  it('Should render DataTable when spec is not DDL', () => {
    wrapper = mountFactory()
    const dataTable = wrapper.findComponent({ name: 'DataTable' })

    expect(dataTable.exists()).toBe(true)

    const { data, defHiddenHeaderIndexes, hasInsertOpt, toolbarProps } = dataTable.vm.$props

    expect(data).toStrictEqual(expectedResultSet)
    expect(defHiddenHeaderIndexes).toStrictEqual(wrapper.vm.defHiddenHeaderIndexes)
    expect(hasInsertOpt).toBe(false)
    expect(toolbarProps).toStrictEqual({
      onReload: mockProps.onReload,
      statement: expectedResultSet.statement,
    })
  })

  it('Should render SqlEditor when spec is DDL', () => {
    wrapper = mountFactory({ props: { spec: INSIGHT_SPEC_MAP.DDL } })
    const sqlEditor = wrapper.findComponent({ name: 'SqlEditor' })
    expect(sqlEditor.exists()).toBe(true)
    const { modelValue, readOnly, options, skipRegCompleters, whiteBg } = sqlEditor.vm.$props
    expect(modelValue).toBe(wrapper.vm.ddl)
    expect(readOnly).toBe(true)
    expect(options).toStrictEqual({ contextmenu: false, fontSize: 14 })
    expect(skipRegCompleters).toBe(true)
    expect(whiteBg).toBe(true)
  })

  it.each`
    case                      | isSchemaNode | expectedExcludedFields
    ${'is a schema node'}     | ${true}      | ${['TABLE_CATALOG', 'TABLE_SCHEMA']}
    ${'is not a schema node'} | ${false}     | ${['TABLE_CATALOG', 'TABLE_SCHEMA', 'TABLE_NAME']}
  `(
    'When the active node $case, it should return expected excluded field',
    async ({ isSchemaNode, expectedExcludedFields }) => {
      wrapper = mountFactory({
        props: { isSchemaNode, nodeType: isSchemaNode ? NODE_TYPE_MAP.SCHEMA : NODE_TYPE_MAP.TBL },
      })
      expect(wrapper.vm.baseExcludedFields).toStrictEqual(expectedExcludedFields)
      expect(wrapper.vm.fieldExclusionsMap[TRIGGERS]).toStrictEqual(isSchemaNode ? [] : ['Table'])
    }
  )

  it.each`
    spec       | expectedExcludedFields
    ${INDEXES} | ${['TABLE_CATALOG', 'TABLE_SCHEMA', 'INDEX_SCHEMA']}
    ${SP}      | ${['Db', 'Type']}
    ${FN}      | ${['Db', 'Type']}
  `('When spec is $spec it should exclude expected fields', ({ spec, expectedExcludedFields }) => {
    wrapper = mountFactory({
      props: { spec, isSchemaNode: true, nodeType: NODE_TYPE_MAP.SCHEMA },
    })
    expect(wrapper.vm.fieldExclusionsMap[spec]).toStrictEqual(expectedExcludedFields)
  })
})
