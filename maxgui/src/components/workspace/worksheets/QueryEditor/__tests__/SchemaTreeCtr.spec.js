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
 * Public License.
 */
import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import SchemaTreeCtr from '@wkeComps/QueryEditor/SchemaTreeCtr.vue'
import { lodash } from '@/utils/helpers'
import { NODE_CTX_TYPES } from '@/constants/workspace'

const dbTreeStub = [
  {
    key: 'node_key_0',
    type: 'SCHEMA',
    name: 'mysql',
    id: 'mysql',
    qualified_name: '`mysql`',
    parentNameData: { SCHEMA: 'mysql' },
    data: {
      CATALOG_NAME: 'def',
      SCHEMA_NAME: 'mysql',
      DEFAULT_CHARACTER_SET_NAME: 'utf8mb4',
      DEFAULT_COLLATION_NAME: 'utf8mb4_general_ci',
      SQL_PATH: null,
      SCHEMA_COMMENT: '',
    },
    draggable: true,
    level: 1,
    isSys: true,
    children: [
      {
        key: 'node_key_1',
        type: 'Tables',
        name: 'Tables',
        id: 'mysql.Tables',
        qualified_name: '`mysql`.`Tables`',
        draggable: false,
        level: 2,
        children: [],
      },
      {
        key: 'node_key_2',
        type: 'Stored Procedures',
        name: 'Stored Procedures',
        id: 'mysql.Stored Procedures',
        qualified_name: '`mysql`.`Stored Procedures`',
        draggable: false,
        level: 2,
        children: [],
      },
    ],
  },
  {
    key: 'node_key_3',
    type: 'SCHEMA',
    name: 'test',
    id: 'test',
    qualified_name: '`test`',
    parentNameData: { SCHEMA: 'test' },
    data: {
      CATALOG_NAME: 'def',
      SCHEMA_NAME: 'test',
      DEFAULT_CHARACTER_SET_NAME: 'utf8mb4',
      DEFAULT_COLLATION_NAME: 'utf8mb4_general_ci',
      SQL_PATH: null,
      SCHEMA_COMMENT: '',
    },
    draggable: true,
    level: 1,
    isSys: false,
    children: [
      {
        key: 'node_key_4',
        type: 'Tables',
        name: 'Tables',
        id: 'test.Tables',
        qualified_name: '`test`.`Tables`',
        draggable: false,
        level: 2,
        children: [],
      },
      {
        key: 'node_key_5',
        type: 'Stored Procedures',
        name: 'Stored Procedures',
        id: 'test.Stored Procedures',
        qualified_name: '`test`.`Stored Procedures`',
        draggable: false,
        level: 2,
        children: [],
      },
    ],
  },
]

const mountFactory = (opts) =>
  mount(
    SchemaTreeCtr,
    lodash.merge(
      {
        props: {
          queryEditorId: 'query-editor-id',
          activeQueryTabId: 'query-tab-id',
          queryEditorTmp: { db_tree: dbTreeStub },
          activeQueryTabConn: {},
          schemaSidebar: {},
        },
      },
      opts
    )
  )

const schemaNodeStub = dbTreeStub[0]
const tblNodeStub = {
  id: 'test.Tables.t1',
  qualified_name: '`test`.`t1`',
  type: 'TABLE',
  name: 't1',
  draggable: true,
  children: [],
}

describe(`SchemaTreeCtr`, () => {
  let wrapper

  it(`Should pass expected data to VirSchemaTree`, () => {
    wrapper = mountFactory()
    const { data, expandedNodes, hasNodeCtxEvt, hasDbClickEvt, activeNode } = wrapper.findComponent(
      { name: 'VirSchemaTree' }
    ).vm.$props
    expect(data).toStrictEqual(dbTreeStub)
    expect(expandedNodes).toStrictEqual(wrapper.vm.expandedNodes)
    expect(hasNodeCtxEvt).toBe(true)
    expect(hasDbClickEvt).toBe(true)
    expect(activeNode).toStrictEqual(wrapper.vm.activeNode)
  })

  it(`Should pass expected data to CtxMenu`, async () => {
    wrapper = mountFactory()
    wrapper.vm.activeCtxNode = schemaNodeStub
    wrapper.vm.activeCtxItemOpts = wrapper.vm.genNodeOpts(schemaNodeStub)
    await wrapper.vm.$nextTick()
    const {
      $attrs: { modelValue, activator, location, offset },
      $props: { items },
    } = wrapper.findComponent({ name: 'CtxMenu' }).vm
    expect(modelValue).toBe(wrapper.vm.showCtxMenu)
    expect(location).toBe('bottom end')
    expect(offset).toBe('4 8')
    expect(activator).toBe(`#ctx-menu-activator-${schemaNodeStub.key}`)
    expect(items).toStrictEqual(wrapper.vm.activeCtxItemOpts)
  })

  it(`Should bold SCHEMA node if active_db === node.qualified_name`, () => {
    wrapper = mountFactory({
      shallow: false,
      props: { activeQueryTabConn: { active_db: schemaNodeStub.qualified_name } },
    })
    const schemaNodeNameEle = wrapper.find(`#node-${schemaNodeStub.key}`).find('.node-name')
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
    wrapper.findComponent({ name: 'VirSchemaTree' }).vm.$emit('node-hovered', schemaNodeStub)
    expect(wrapper.vm.hoveredNode).toStrictEqual(schemaNodeStub)
  })

  it(`Should return base opts for system node when calling genNodeOpts method`, () => {
    wrapper = mountFactory()
    const sysNode = dbTreeStub.find((node) => node.isSys)
    expect(wrapper.vm.genNodeOpts(sysNode)).toStrictEqual(wrapper.vm.baseOptsMap[sysNode.type])
  })

  it(`Should return accurate opts for user node when calling genNodeOpts method`, () => {
    wrapper = mountFactory()
    const userNode = dbTreeStub.find((node) => !node.isSys)
    const expectOpts = [
      ...wrapper.vm.baseOptsMap[userNode.type],
      { divider: true },
      ...wrapper.vm.genUserNodeOpts(userNode),
    ]
    expect(wrapper.vm.genNodeOpts(userNode)).toStrictEqual(expectOpts)
  })

  const mockOpts = Object.values(NODE_CTX_TYPES).map((type) => ({ type }))
  mockOpts.forEach((opt) => {
    it(`optionHandler should emit event as expected if context type is ${opt.type}`, () => {
      wrapper = mountFactory()
      const node = opt.type === 'Use' ? schemaNodeStub : tblNodeStub
      wrapper.vm.optionHandler({ node, opt })
      switch (opt.type) {
        case NODE_CTX_TYPES.USE:
          expect(wrapper.emitted()['use-db'][0][0]).toStrictEqual(schemaNodeStub.qualified_name)
          break
        case NODE_CTX_TYPES.PRVW_DATA:
        case NODE_CTX_TYPES.PRVW_DATA_DETAILS:
          expect(wrapper.emitted()['get-node-data'][0][0]).toStrictEqual({
            query_mode: opt.type,
            qualified_name: node.qualified_name,
          })
          break
        case NODE_CTX_TYPES.DROP:
          expect(wrapper.emitted()['drop-action'][0][0]).toStrictEqual(
            'DROP ' + tblNodeStub.type + ' `test`.`t1`;'
          )
          break
        case NODE_CTX_TYPES.ALTER:
          expect(wrapper.emitted()['alter-tbl'][0][0]).toStrictEqual(
            wrapper.vm.minimizeNode(tblNodeStub)
          )
          break
        case NODE_CTX_TYPES.TRUNCATE:
          expect(wrapper.emitted()['truncate-tbl'][0][0]).toStrictEqual(
            'TRUNCATE TABLE `test`.`t1`;'
          )
          break
      }
    })
  })
})
