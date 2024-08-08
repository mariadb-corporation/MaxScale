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
import {
  NODE_CTX_TYPE_MAP,
  NODE_GROUP_TYPE_MAP,
  NODE_GROUP_CHILD_TYPE_MAP,
  TABLE_STRUCTURE_SPEC_MAP,
  NODE_TYPE_MAP,
} from '@/constants/workspace'
import schemaNodeHelper from '@/utils/schemaNodeHelper'

const schemaNodesMock = schemaNodeHelper.genNodes({
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
const schemaNodeMock = schemaNodesMock[0]
const tblNodeGroupMock = schemaNodeHelper.genNodeGroup({
  parentNode: schemaNodeMock,
  type: NODE_GROUP_TYPE_MAP.TBL_G,
})
const tblNodesMock = schemaNodeHelper.genNodes({
  queryResult: {
    fields: ['TABLE_NAME', 'CREATE_TIME', 'TABLE_TYPE', 'TABLE_ROWS', 'ENGINE'],
    data: [
      ['department', '2024-08-08 06:26:39', 'BASE TABLE', 0, 'InnoDB'],
      ['employees', '2024-01-05 12:12:04', 'BASE TABLE', 0, 'InnoDB'],
    ],
  },
  nodeGroup: tblNodeGroupMock,
})
const tblNodeMock = tblNodesMock[0]

const dbTreeDataMock = schemaNodesMock.map((node, i) => {
  if (i === 0) node.children = [{ ...tblNodeGroupMock, children: tblNodesMock }]
  return node
})

const mountFactory = (opts) =>
  mount(
    SchemaTreeCtr,
    lodash.merge(
      {
        props: {
          queryEditorId: 'query-editor-id',
          activeQueryTabId: 'query-tab-id',
          queryEditorTmp: { db_tree: dbTreeDataMock },
          activeQueryTabConn: {},
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
    wrapper = mountFactory({
      shallow: false,
      props: { activeQueryTabConn: { active_db: schemaNodeMock.qualified_name } },
    })
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
    expect(wrapper.vm.genNodeOpts(sysNode)).toStrictEqual(wrapper.vm.BASE_OPT_MAP[sysNode.type])
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
        case NODE_CTX_TYPE_MAP.ALTER:
          wrapper.vm.optionHandler({
            node: tblNodeMock,
            opt: { ...opt, targetNodeType: NODE_TYPE_MAP.TBL },
          })
          expect(wrapper.emitted()['alter-tbl'][0][0]).toStrictEqual({
            node: schemaNodeHelper.minimizeNode(tblNodeMock),
            spec: TABLE_STRUCTURE_SPEC_MAP.COLUMNS,
          })
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

  describe('Add operations for TBL node should emit alter-tbl with expected args', () => {
    const testCases = [
      {
        description: 'Add Column',
        nodeGroup: tblNodeMock.children[0], // COL_G node
        expectedSpec: TABLE_STRUCTURE_SPEC_MAP.COLUMNS,
      },
      {
        description: 'Add Index',
        nodeGroup: tblNodeMock.children[1], // IDX_G node
        expectedSpec: TABLE_STRUCTURE_SPEC_MAP.INDEXES,
      },
    ]

    testCases.forEach(({ description, nodeGroup, expectedSpec }) => {
      it(`${description} should emit alter-tbl with expected args`, () => {
        wrapper = mountFactory()

        wrapper.vm.optionHandler({
          node: nodeGroup,
          opt: {
            type: NODE_CTX_TYPE_MAP.ADD,
            targetNodeType: NODE_GROUP_CHILD_TYPE_MAP[nodeGroup.type],
          },
        })

        expect(wrapper.emitted()['alter-tbl'][0][0]).toStrictEqual({
          node: schemaNodeHelper.minimizeNode(tblNodeMock),
          spec: expectedSpec,
        })
      })
    })
  })
})
