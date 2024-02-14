/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { lodash } from '@/utils/helpers'

export const mixedTypeValues = {
  undefined: undefined,
  null: null,
  true: true,
  false: false,
  emptyString: '',
  string: 'string',
  number: 0,
  emptyObj: {},
  validObj: { keyName: 'keyValue' },
  emptyArr: [],
  validArr: [0, 1, 2, 3],
  validArrObj: [{ keyName: 'keyValue' }],
}

export const nestedObj = {
  root_node: {
    node_child: { grand_child: 'grand_child value' },
    node_child_1: 'node_child_1 value',
  },
  root_node_1: 'root_node_1 value',
}

export const tree = [
  {
    id: 1,
    parentId: 0,
    level: 0,
    key: 'root_node',
    value: {
      node_child: { grand_child: 'grand_child value' },
      node_child_1: 'node_child_1 value',
    },
    leaf: false,
    expanded: false,
    children: [
      {
        id: 2,
        parentId: 1,
        level: 1,
        key: 'node_child',
        value: { grand_child: 'grand_child value' },
        leaf: false,
        expanded: false,
        children: [
          {
            id: 3,
            parentId: 2,
            level: 2,
            key: 'grand_child',
            value: 'grand_child value',
            leaf: true,
          },
        ],
      },
      {
        id: 4,
        parentId: 1,
        level: 1,
        key: 'node_child_1',
        value: 'node_child_1 value',
        leaf: true,
      },
    ],
  },
  {
    id: 5,
    parentId: 0,
    level: 0,
    key: 'root_node_1',
    value: 'root_node_1 value',
    leaf: true,
  },
]

/**
 * This function flattens tree array
 * @param {Array} tree - tree array to be flatten
 * @returns {Array} flattened array
 */
function flattenExpandableTree(tree) {
  let flattened = []
  let target = lodash.cloneDeep(tree)
  //Traversal
  target.forEach((o) => {
    if (o.children && o.children.length > 0) {
      o.expanded = true
      flattened.push(o)
      flattened = [...flattened, ...flattenExpandableTree(o.children)]
    } else flattened.push(o)
  })
  return flattened
}

const flattened = flattenExpandableTree(tree)
export const nodeMap = lodash.keyBy(flattened, 'id')
