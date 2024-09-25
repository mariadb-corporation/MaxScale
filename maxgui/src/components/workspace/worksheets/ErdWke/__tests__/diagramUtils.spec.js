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
import diagramUtils from '@wkeComps/ErdWke/diagramUtils'
import erdHelper from '@/utils/erdHelper'
import { CREATE_TBL_TOKEN_MAP as TOKEN, LINK_OPT_TYPE_MAP } from '@/constants/workspace'

const internal = diagramUtils.__internal

function getKeyMap({ node, type }) {
  return node.data.defs.key_category_map[type]
}

function getNnField({ node, colId }) {
  return node.data.defs.col_map[colId].nn
}

const {
  SET_ONE_TO_ONE,
  SET_ONE_TO_MANY,
  SET_MANDATORY,
  SET_FK_COL_OPTIONAL,
  SET_REF_COL_MANDATORY,
  SET_REF_COL_OPTIONAL,
} = LINK_OPT_TYPE_MAP

describe(`diagramUtils`, () => {
  vi.mock('@/utils/erdHelper', async (importOriginal) => ({
    default: {
      ...(await importOriginal()).default,
      areUniqueCols: vi.fn(() => false),
      genKey: vi.fn(() => ({ id: 'key_id', cols: [{ id: 'col_id' }], name: 'key_name' })),
      validateFkColTypes: vi.fn(() => false),
    },
  }))

  afterEach(() => vi.clearAllMocks())

  describe('toggleUnique', () => {
    const colId = 'col1'
    const mockUqKey = { id: 'newKey', cols: [{ id: colId }] }
    const mockNode = { data: { defs: { key_category_map: {} } } }

    const mockNodeWithUqKey = {
      data: { defs: { key_category_map: { [TOKEN.uniqueKey]: { [mockUqKey.id]: mockUqKey } } } },
    }

    it('Should add a unique key if it does not exist', () => {
      erdHelper.areUniqueCols.mockReturnValue(false)
      erdHelper.genKey.mockReturnValue(mockUqKey)
      const updatedNode = internal.toggleUnique({ node: mockNode, colId, value: true })
      expect(erdHelper.genKey).toHaveBeenCalledWith({
        defs: mockNode.data.defs,
        category: TOKEN.uniqueKey,
        colId,
      })
      expect(getKeyMap({ node: updatedNode, type: TOKEN.uniqueKey })[mockUqKey.id]).toEqual(
        mockUqKey
      )
    })

    it('Should not add a unique key if it already exists', () => {
      erdHelper.areUniqueCols.mockReturnValue(true)
      const updatedNode = internal.toggleUnique({ node: mockNodeWithUqKey, colId, value: true })
      expect(updatedNode).toBe(mockNodeWithUqKey)
    })

    it('Should remove a unique key if it exists', () => {
      erdHelper.areUniqueCols.mockReturnValue(true)
      const updatedNode = internal.toggleUnique({
        node: mockNodeWithUqKey,
        colId,
        value: false,
      })
      expect(getKeyMap({ node: updatedNode, type: TOKEN.uniqueKey })).toBe(undefined)
    })
  })

  describe('toggleNotNull', () => {
    const colId = 'col1'
    const mockNode = {
      data: {
        defs: {
          col_map: { [colId]: { nn: false } },
        },
      },
    }

    it.each`
      value
      ${true}
      ${false}
    `('Should set the `nn` field to $value', ({ value }) => {
      const updatedNode = internal.toggleNotNull({ node: mockNode, colId, value })
      expect(getNnField({ node: updatedNode, colId })).toBe(value)
    })
  })

  describe('addPlainIndex', () => {
    const colId = 'col1'
    const mockPlainIdx = { id: 'index1', cols: [{ id: colId }] }

    it('Should add a plain index to the node even if no plain index exists', () => {
      erdHelper.genKey.mockReturnValue(mockPlainIdx)
      const mockNode = { data: { defs: { key_category_map: {} } } }
      const updatedNode = internal.addPlainIndex({ colId, node: mockNode })

      expect(erdHelper.genKey).toHaveBeenCalledWith({
        defs: mockNode.data.defs,
        category: TOKEN.key,
        colId,
      })
      expect(getKeyMap({ node: updatedNode, type: TOKEN.key })[mockPlainIdx.id]).toBeDefined()
    })

    it('Should add a plain index to the node', () => {
      erdHelper.genKey.mockReturnValue(mockPlainIdx)
      const mockExistingPlainIdx = { ...mockPlainIdx, id: 'existing_plain_idx_id' }
      const mockNode = {
        data: {
          defs: {
            key_category_map: { [TOKEN.key]: { [mockExistingPlainIdx.id]: mockExistingPlainIdx } },
          },
        },
      }
      const updatedNode = internal.addPlainIndex({ colId, node: mockNode })

      expect(erdHelper.genKey).toHaveBeenCalledWith({
        defs: mockNode.data.defs,
        category: TOKEN.key,
        colId,
      })
      expect(
        getKeyMap({ node: updatedNode, type: TOKEN.key })[mockExistingPlainIdx.id]
      ).toBeDefined()
      expect(getKeyMap({ node: updatedNode, type: TOKEN.key })[mockPlainIdx.id]).toBeDefined()
    })
  })

  describe('assignCoord', () => {
    const nodes = [
      { id: '1', x: 10, y: 20 },
      { id: '2', x: 15, y: 25 },
    ]
    const nodeMap = {
      1: { x: 30, y: 40, vx: 5, vy: 5, size: 100 },
    }

    it('Should update the coordinates of nodes based on the nodeMap', () => {
      const updatedNodes = diagramUtils.assignCoord({ nodes, nodeMap })

      expect(updatedNodes['1']).toStrictEqual({ id: '1', ...nodeMap['1'] })
      expect(updatedNodes['2']).toStrictEqual(nodes[1])
    })
  })

  describe('immutableUpdateConfig', () => {
    it('Should immutable update the object at the given path with the provided value', () => {
      const obj = { a: { b: { c: 1 } } }
      const updatedObj = diagramUtils.immutableUpdateConfig(obj, 'a.b.c', 42)

      expect(updatedObj.a.b.c).toBe(42)
      expect(obj.a.b.c).toBe(1)
    })

    it('Should create a new path if it does not exist', () => {
      const obj = { a: { b: { c: 1 } } }
      const updatedObj = diagramUtils.immutableUpdateConfig(obj, 'a.b.d', 42)

      expect(updatedObj.a.b.d).toBe(42)
      expect(obj.a.b.d).toBeUndefined()
    })

    it('Should deeply clone the provided object before mutating', () => {
      const obj = { a: { b: { c: 1 } } }
      const updatedObj = diagramUtils.immutableUpdateConfig(obj, 'a.b', { newKey: 99 })

      expect(updatedObj.a.b.newKey).toBe(99)
      expect(obj.a.b.newKey).toBeUndefined()
    })
  })

  describe('rmTblNode', () => {
    const nodes = [
      { id: 'tbl1', data: { defs: { key_category_map: {} } } },
      {
        id: 'tbl2',
        data: {
          defs: { key_category_map: { [TOKEN.foreignKey]: { fk1: { ref_tbl_id: 'tbl1' } } } },
        },
      },
    ]

    it('Should remove the specified table node', () => {
      const updatedNodes = diagramUtils.rmTblNode({ id: 'tbl1', nodes })

      expect(updatedNodes['tbl1']).toBeUndefined() // tbl1 should be removed
      expect(updatedNodes['tbl2']).toBeDefined()
    })

    it('Should remove foreign keys referencing the removed table node', () => {
      const updatedNodes = diagramUtils.rmTblNode({ id: 'tbl1', nodes })

      expect(getKeyMap({ node: updatedNodes['tbl2'], type: TOKEN.foreignKey })).toBeUndefined()
    })
  })

  describe('genTblNode', () => {
    const nodes = []
    const charsetCollationMap = { utf8mb4: 'utf8mb4_general_ci' }
    const panAndZoom = { x: 0, y: 0, k: 1 }

    it('Should generate a new table node with `test` as a default schema', () => {
      const newNode = diagramUtils.genTblNode({
        nodes,
        schemas: [],
        charsetCollationMap,
        panAndZoom,
      })

      expect(newNode).toHaveProperty('x')
      expect(newNode).toHaveProperty('y')
      expect(newNode).toHaveProperty('data')
      expect(newNode.data.defs).toBeDefined()
      expect(newNode.data.options.schema).toBe('test')
    })

    it('Should use the provided schema', () => {
      const customSchemas = ['custom_schema']
      const newNode = diagramUtils.genTblNode({
        nodes,
        schemas: customSchemas,
        charsetCollationMap,
        panAndZoom,
      })

      expect(newNode.data.options.schema).toBe('custom_schema')
    })
  })

  describe('addFk', () => {
    const node = { id: 'target_node_id', data: { defs: { key_category_map: {} } } }
    const refNode = { id: 'ref_node_id', data: { defs: { key_category_map: {} } } }
    const nodeMap = { [node.id]: node, [refNode.id]: refNode }
    const newKey = { id: 'fk1', cols: [{ id: 'col1' }], ref_cols: [{ id: 'ref_col1' }] }

    it('Add FK to node if key and referenced columns have matching types', () => {
      erdHelper.validateFkColTypes.mockReturnValue(true)
      const existingFkKey = { ...newKey, id: 'fk0' }
      const currentFkMap = { [existingFkKey.id]: existingFkKey }
      const updatedMap = diagramUtils.addFk({
        nodeMap,
        node,
        colKeyCategoryMap: { ref_col1: [TOKEN.key] }, // mock indexed refCol
        currentFkMap,
        newKey,
        refNode,
      })
      const keyCategoryMap = getKeyMap({
        node: updatedMap[node.id],
        type: TOKEN.foreignKey,
      })
      expect(Object.keys(keyCategoryMap)).toStrictEqual([existingFkKey.id, newKey.id])
      expect(keyCategoryMap[newKey.id]).toStrictEqual(newKey)
    })

    it('Should return null if FK validation fails', () => {
      erdHelper.validateFkColTypes.mockReturnValue(false)
      const updatedMap = diagramUtils.addFk({
        nodeMap,
        node,
        colKeyCategoryMap: {},
        currentFkMap: {},
        newKey,
        refNode,
      })

      expect(updatedMap).toBeNull()
    })

    it('Should automatically add a PLAIN index for non-indexed referenced columns', () => {
      erdHelper.validateFkColTypes.mockReturnValue(true)
      const nonIndexedColMap = {}
      const updatedMap = diagramUtils.addFk({
        nodeMap,
        node,
        colKeyCategoryMap: nonIndexedColMap,
        currentFkMap: {},
        newKey,
        refNode,
      })

      expect(getKeyMap({ node: updatedMap[node.id], type: TOKEN.foreignKey })).toBeDefined()
      expect(
        getKeyMap({ node: updatedMap[node.id], type: TOKEN.foreignKey })[newKey.id]
      ).toStrictEqual(newKey)
    })
  })

  describe('rmFk', () => {
    const link = { id: 'fk1', source: { id: 'target_node_id' } }
    const targetNode = {
      id: link.source.id,
      data: { defs: { key_category_map: { foreignKey: { fk1: {} } } } },
    }
    const nodeMap = { [targetNode.id]: targetNode }

    it('Should remove the foreign key from the source node', () => {
      const updatedMap = diagramUtils.rmFk({ nodeMap, link })
      expect(getKeyMap({ node: updatedMap[targetNode.id], type: TOKEN.foreignKey })).toBeUndefined()
    })

    it('Should not modify nodes without the foreign key', () => {
      const nodeMap = {
        // mock the target node with no FK
        [link.source.id]: { id: link.source.id, data: { defs: { key_category_map: {} } } },
      }
      const updatedMap = diagramUtils.rmFk({ nodeMap, link })

      expect(getKeyMap({ node: updatedMap[targetNode.id], type: TOKEN.foreignKey })).toBeUndefined()
    })
  })

  describe('updateCardinality', () => {
    const colId = 'col1'
    const mockUqKey = { id: 'newKey', cols: [{ id: colId }] }

    beforeEach(() => {
      erdHelper.areUniqueCols.mockReturnValue(false)
      erdHelper.genKey.mockReturnValue(mockUqKey)
    })

    it('Should update to one-to-one relationship correctly', () => {
      const sourceNode = { id: 'source', data: { defs: { key_category_map: {} } } }
      const targetNode = { id: 'target', data: { defs: { key_category_map: {} } } }
      const nodeMap = { [sourceNode.id]: sourceNode, [targetNode.id]: targetNode }
      const link = {
        source: sourceNode,
        target: targetNode,
        relationshipData: { src_attr_id: colId, target_attr_id: 'target_col' },
      }

      const updatedMap = diagramUtils.updateCardinality({ nodeMap, type: SET_ONE_TO_ONE, link })
      const sourceNodeKeyCategoryMap = getKeyMap({
        node: updatedMap[link.source.id],
        type: TOKEN.uniqueKey,
      })
      const targetNodeKeyCategoryMap = getKeyMap({
        node: updatedMap[link.target.id],
        type: TOKEN.uniqueKey,
      })

      expect(sourceNodeKeyCategoryMap).toBeDefined()
      expect(targetNodeKeyCategoryMap).toBeDefined()
    })

    it('should update to one-to-many relationship correctly', () => {
      // mock one-to-one relationship, both have uq keys
      const sourceNode = {
        id: 'source',
        data: { defs: { key_category_map: { [TOKEN.uniqueKey]: { [mockUqKey.id]: mockUqKey } } } },
      }
      const targetNode = {
        id: 'target',
        data: { defs: { key_category_map: { [TOKEN.uniqueKey]: { [mockUqKey.id]: mockUqKey } } } },
      }
      const nodeMap = { [sourceNode.id]: sourceNode, [targetNode.id]: targetNode }
      const link = {
        source: sourceNode,
        target: targetNode,
        relationshipData: { src_attr_id: colId, target_attr_id: 'target_col' },
      }
      const updatedMap = diagramUtils.updateCardinality({
        nodeMap,
        type: SET_ONE_TO_MANY,
        link,
      })
      const sourceNodeKeyCategoryMap = getKeyMap({
        node: updatedMap[link.source.id],
        type: TOKEN.uniqueKey,
      })
      const targetNodeKeyCategoryMap = getKeyMap({
        node: updatedMap[link.target.id],
        type: TOKEN.uniqueKey,
      })

      expect(sourceNodeKeyCategoryMap).toBeUndefined()
      expect(targetNodeKeyCategoryMap).toBeDefined()
    })

    it.each`
      type                     | description
      ${SET_MANDATORY}         | ${'fk column nn field to true'}
      ${SET_FK_COL_OPTIONAL}   | ${'fk column nn field to false'}
      ${SET_REF_COL_OPTIONAL}  | ${'referenced column nn field to false'}
      ${SET_REF_COL_MANDATORY} | ${'referenced column nn field to true'}
    `('Should set the $description when type is $type', ({ type }) => {
      const targetColdId = 'target_col'
      const sourceNode = {
        id: 'source',
        data: { defs: { col_map: { [colId]: { nn: false } } } },
      }
      const targetNode = {
        id: 'target',
        data: { defs: { col_map: { [targetColdId]: { nn: false } } } },
      }
      const nodeMap = { [sourceNode.id]: sourceNode, [targetNode.id]: targetNode }
      const link = {
        source: sourceNode,
        target: targetNode,
        relationshipData: { src_attr_id: colId, target_attr_id: targetColdId },
      }
      const updatedMap = diagramUtils.updateCardinality({ nodeMap, type, link })
      const srcNodeNnField = getNnField({ node: updatedMap[sourceNode.id], colId })
      const targetNodeNnField = getNnField({ node: updatedMap[targetNode.id], colId: targetColdId })

      switch (type) {
        case SET_MANDATORY:
          expect(srcNodeNnField).toBe(true)
          break
        case SET_FK_COL_OPTIONAL:
          expect(srcNodeNnField).toBe(false)
          break
        case SET_REF_COL_MANDATORY:
          expect(targetNodeNnField).toBe(true)
          break
        case SET_REF_COL_OPTIONAL:
          expect(targetNodeNnField).toBe(false)
          break
      }
    })
  })
})
