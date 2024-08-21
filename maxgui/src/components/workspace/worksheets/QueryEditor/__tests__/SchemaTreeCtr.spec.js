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
import SchemaTreeCtr from '@wkeComps/QueryEditor/SchemaTreeCtr.vue'
import { lodash } from '@/utils/helpers'
import { NODE_CTX_TYPE_MAP, TABLE_STRUCTURE_SPEC_MAP, NODE_TYPE_MAP } from '@/constants/workspace'
import schemaNodeHelper from '@/utils/schemaNodeHelper'

const dbTreeDataMock = schemaNodeHelper.genNodes({
  queryResult: {
    fields: [
      'CATALOG_NAME',
      'SCHEMA_NAME',
      'DEFAULT_CHARACTER_SET_NAME',
      'DEFAULT_COLLATION_NAME',
      'SQL_PATH',
      'SCHEMA_COMMENT',
    ],
    data: [
      ['def', 'company', 'utf8mb4', 'utf8mb4_general_ci', null, ''],
      ['def', 'mysql', 'utf8mb4', 'utf8mb4_general_ci', null, ''],
    ],
  },
})
const schemaNodeMock = dbTreeDataMock[0]
const tblNodeGroupMock = schemaNodeMock.children[0]
tblNodeGroupMock.children = schemaNodeHelper.genNodes({
  queryResult: {
    fields: ['TABLE_NAME', 'CREATE_TIME', 'TABLE_TYPE', 'TABLE_ROWS', 'ENGINE'],
    data: [
      ['department', '2024-08-08 06:26:39', 'BASE TABLE', 0, 'InnoDB'],
      ['employees', '2024-01-05 12:12:04', 'BASE TABLE', 0, 'InnoDB'],
    ],
  },
  nodeGroup: tblNodeGroupMock,
})
const tblNodeMock = tblNodeGroupMock.children[0]
const colNodeGroupMock = tblNodeMock.children[0]
const idxNodeGroupMock = tblNodeMock.children[1]
const triggerNodeGroupMock = tblNodeMock.children[2]
colNodeGroupMock.children = schemaNodeHelper.genNodes({
  queryResult: {
    fields: ['COLUMN_NAME', 'COLUMN_TYPE', 'COLUMN_KEY', 'IS_NULLABLE', 'PRIVILEGES'],
    data: [
      ['id', 'int(11)', 'PRI', 'NO', 'select,insert,update,references'],
      ['name', 'varchar(100)', 'MUL', 'YES', 'select,insert,update,references'],
    ],
  },
  nodeGroup: colNodeGroupMock,
})
idxNodeGroupMock.children = schemaNodeHelper.genNodes({
  queryResult: {
    data: [
      ['departments_ibfk_0', 'name', 1, 1, 0, 'YES', 'BTREE'],
      ['PRIMARY', 'id', 0, 1, 0, '', 'BTREE'],
    ],
    fields: [
      'INDEX_NAME',
      'COLUMN_NAME',
      'NON_UNIQUE',
      'SEQ_IN_INDEX',
      'CARDINALITY',
      'NULLABLE',
      'INDEX_TYPE',
    ],
  },
  nodeGroup: idxNodeGroupMock,
})
triggerNodeGroupMock.children = schemaNodeHelper.genNodes({
  queryResult: {
    complete: true,
    data: [
      [
        'prevent_department_deletion',
        '2024-08-09 02:13:34.94',
        'DELETE',
        "BEGIN IF EXISTS (\n  SELECT\n    1\n  FROM\n    company.employees\n  WHERE\n    department_id = OLD.id\n) THEN\nSIGNAL SQLSTATE '45000'\nSET\n  MESSAGE_TEXT = 'Department cannot be deleted because it has associated employees.';\nEND IF;\nEND",
        'BEFORE',
      ],
    ],
    fields: ['TRIGGER_NAME', 'CREATED', 'EVENT_MANIPULATION', 'ACTION_STATEMENT', 'ACTION_TIMING'],
  },
  nodeGroup: triggerNodeGroupMock,
})
const triggerNodeMock = triggerNodeGroupMock.children[0]

const mountFactory = (opts) =>
  mount(
    SchemaTreeCtr,
    lodash.merge(
      {
        props: {
          queryEditorId: 'query-editor-id',
          activeQueryTabId: 'query-tab-id',
          queryEditorTmp: { db_tree: dbTreeDataMock },
          activeDb: '',
          schemaSidebar: {},
        },
      },
      opts
    )
  )

describe(`SchemaTreeCtr`, () => {
  let wrapper

  it(`Should pass expected data to VirSchemaTree`, () => {
    wrapper = mountFactory()
    const { data, expandedNodes, hasNodeCtxEvt, hasDbClickEvt, activeNode } = wrapper.findComponent(
      { name: 'VirSchemaTree' }
    ).vm.$props
    expect(data).toStrictEqual(dbTreeDataMock)
    expect(expandedNodes).toStrictEqual(wrapper.vm.expandedNodes)
    expect(hasNodeCtxEvt).toBe(true)
    expect(hasDbClickEvt).toBe(true)
    expect(activeNode).toStrictEqual(wrapper.vm.activeNode)
  })

  it(`Should pass expected data to CtxMenu`, async () => {
    wrapper = mountFactory()
    wrapper.vm.activeCtxNode = schemaNodeMock
    wrapper.vm.activeCtxItemOpts = wrapper.vm.NON_SYS_NODE_OPT_MAP[schemaNodeMock.type]
    await wrapper.vm.$nextTick()
    const {
      $attrs: { modelValue, activator, location, offset },
      $props: { items },
    } = wrapper.findComponent({ name: 'CtxMenu' }).vm
    expect(modelValue).toBe(wrapper.vm.showCtxMenu)
    expect(location).toBe('bottom end')
    expect(offset).toBe('4 8')
    expect(activator).toBe(`#ctx-menu-activator-${schemaNodeMock.key}`)
    expect(items).toStrictEqual(wrapper.vm.activeCtxItemOpts)
  })

  it(`Should bold SCHEMA node if active_db === node.qualified_name`, () => {
    wrapper = mountFactory({ shallow: false, props: { activeDb: schemaNodeMock.qualified_name } })
    const schemaNodeNameEle = wrapper.find(`#node-${schemaNodeMock.key}`).find('.node-name')
    expect(schemaNodeNameEle.classes()).toEqual(expect.arrayContaining(['font-weight-bold']))
  })

  const hoveredNodeStub = { id: 'id', key: 'key', data: {} }
  it(`Should pass expected data to preview-data-tooltip`, async () => {
    wrapper = mountFactory()
    wrapper.vm.hoveredNode = hoveredNodeStub
    await wrapper.vm.$nextTick()
    const { location, offset, activator } = find(wrapper, 'preview-data-tooltip').vm.$props
    expect(location).toBe('top')
    expect(offset).toBe(0)
    expect(activator).toBe(`#prvw-btn-tooltip-activator-${hoveredNodeStub.key}`)
  })

  it(`Should pass accurate data to node-tooltip via props`, async () => {
    wrapper = mountFactory()
    wrapper.vm.hoveredNode = hoveredNodeStub
    await wrapper.vm.$nextTick()
    const { disabled, location, offset, activator } = find(wrapper, 'node-tooltip').vm.$props
    expect(disabled).toBe(wrapper.vm.isDragging)
    expect(location).toBe('right')
    expect(offset).toBe(0)
    expect(activator).toBe(`#node-${hoveredNodeStub.key}`)
  })

  it(`Should assign hovered item to hoveredNode when item:hovered event is emitted
        from VirSchemaTree component`, () => {
    wrapper = mountFactory()
    wrapper.findComponent({ name: 'VirSchemaTree' }).vm.$emit('node-hovered', schemaNodeMock)
    expect(wrapper.vm.hoveredNode).toStrictEqual(schemaNodeMock)
  })

  it(`Should return base opts for system node when calling genNodeOpts method`, () => {
    wrapper = mountFactory()
    const sysNode = dbTreeDataMock.find((node) => node.isSys)
    expect(wrapper.vm.genNodeOpts(sysNode)).toStrictEqual([
      ...wrapper.vm.BASE_OPT_MAP[sysNode.type],
      wrapper.vm.CREATE_SCHEMA_OPT,
    ])
  })

  it(`Should return accurate opts for user node when calling genNodeOpts method`, () => {
    wrapper = mountFactory()
    const userNode = dbTreeDataMock.find((node) => !node.isSys)
    const expectOpts = [
      ...wrapper.vm.BASE_OPT_MAP[userNode.type],
      { divider: true },
      ...wrapper.vm.NON_SYS_NODE_OPT_MAP[userNode.type],
    ]
    expect(wrapper.vm.genNodeOpts(userNode)).toStrictEqual(expectOpts)
  })

  const mockOpts = Object.values(NODE_CTX_TYPE_MAP).map((type) => ({ type }))
  mockOpts.forEach((opt) => {
    it(`optionHandler should emit event as expected if context type is ${opt.type}`, () => {
      wrapper = mountFactory()
      switch (opt.type) {
        case NODE_CTX_TYPE_MAP.USE:
          wrapper.vm.optionHandler({ node: schemaNodeMock, opt })
          expect(wrapper.emitted()['use-db'][0][0]).toStrictEqual(schemaNodeMock.qualified_name)
          break
        case NODE_CTX_TYPE_MAP.PRVW_DATA:
        case NODE_CTX_TYPE_MAP.PRVW_DATA_DETAILS:
          wrapper.vm.optionHandler({ node: tblNodeMock, opt })
          expect(wrapper.emitted()['get-node-data'][0][0]).toStrictEqual({
            mode: opt.type,
            node: schemaNodeHelper.minimizeNode(tblNodeMock),
          })
          break
        case NODE_CTX_TYPE_MAP.DROP:
          wrapper.vm.optionHandler({ node: tblNodeMock, opt })
          expect(wrapper.emitted()['drop-action'][0][0]).toStrictEqual(
            `DROP TABLE ${tblNodeMock.qualified_name};`
          )
          break
        case NODE_CTX_TYPE_MAP.TRUNCATE:
          wrapper.vm.optionHandler({ node: tblNodeMock, opt })
          expect(wrapper.emitted()['truncate-tbl'][0][0]).toStrictEqual(
            `TRUNCATE TABLE ${tblNodeMock.qualified_name};`
          )
          break
        case NODE_CTX_TYPE_MAP.CREATE:
          wrapper.vm.optionHandler({ node: tblNodeMock, opt })
          expect(wrapper.emitted()['create-node'][0][0]).toStrictEqual({
            type: opt.targetNodeType,
            parentNameData: tblNodeMock.parentNameData,
          })
          break
      }
    })
  })

  describe('Should emit alter-node with expected args', () => {
    const testCases = [
      {
        operation: 'Add Column',
        node: tblNodeMock.children[0], // COL_G node
        targetNodeType: NODE_TYPE_MAP.COL,
        optType: NODE_CTX_TYPE_MAP.ADD,
        targetAlterNode: tblNodeMock,
        expectedSpec: TABLE_STRUCTURE_SPEC_MAP.COLUMNS,
      },
      {
        operation: 'Add Index',
        node: tblNodeMock.children[1], // IDX_G node
        targetNodeType: NODE_TYPE_MAP.IDX,
        optType: NODE_CTX_TYPE_MAP.ADD,
        targetAlterNode: tblNodeMock,
        expectedSpec: TABLE_STRUCTURE_SPEC_MAP.INDEXES,
      },
      {
        operation: 'Alter Table',
        node: tblNodeMock,
        targetNodeType: NODE_TYPE_MAP.TBL,
        optType: NODE_CTX_TYPE_MAP.ALTER,
        targetAlterNode: tblNodeMock,
        expectedSpec: TABLE_STRUCTURE_SPEC_MAP.COLUMNS,
      },
      {
        operation: 'Alter Column',
        node: colNodeGroupMock.children[0], //  COL node
        targetNodeType: NODE_TYPE_MAP.COL,
        optType: NODE_CTX_TYPE_MAP.ALTER,
        targetAlterNode: tblNodeMock,
        expectedSpec: TABLE_STRUCTURE_SPEC_MAP.COLUMNS,
      },
      {
        operation: 'Alter Index',
        node: idxNodeGroupMock.children[0], //  IDX node
        targetNodeType: NODE_TYPE_MAP.IDX,
        optType: NODE_CTX_TYPE_MAP.ALTER,
        targetAlterNode: tblNodeMock,
        expectedSpec: TABLE_STRUCTURE_SPEC_MAP.INDEXES,
      },
      {
        operation: 'Alter Trigger',
        node: triggerNodeMock,
        targetNodeType: NODE_TYPE_MAP.TRIGGER,
        optType: NODE_CTX_TYPE_MAP.ALTER,
        targetAlterNode: triggerNodeMock,
      },
    ]
    testCases.forEach(
      ({ operation, node, targetNodeType, optType, targetAlterNode, expectedSpec }) => {
        it(`${operation} should emit alter-node with expected args`, () => {
          wrapper = mountFactory()

          wrapper.vm.optionHandler({
            node,
            opt: { type: optType, targetNodeType },
          })
          const expected = { node: targetAlterNode }
          if (expectedSpec) expected.spec = expectedSpec
          expect(wrapper.emitted()['alter-node'][0][0]).toStrictEqual(expected)
        })
      }
    )
  })
})
