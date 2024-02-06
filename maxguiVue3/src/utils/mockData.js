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

export const treeNodes = [
  {
    nodeId: 1,
    parentNodeId: 0,
    level: 0,
    id: 'root_node',
    value: '',
    originalValue: nestedObj.root_node,
    expanded: false,
    children: [
      {
        nodeId: 2,
        parentNodeId: 1,
        level: 1,
        id: 'node_child',
        value: '',
        originalValue: nestedObj.root_node.node_child,
        expanded: false,
        children: [
          {
            nodeId: 3,
            parentNodeId: 2,
            level: 2,
            id: 'grand_child',
            value: 'grand_child value',
            originalValue: 'grand_child value',
            leaf: true,
          },
        ],
        leaf: false,
      },
      {
        nodeId: 4,
        parentNodeId: 1,
        level: 1,
        id: 'node_child_1',
        value: 'node_child_1 value',
        originalValue: 'node_child_1 value',
        leaf: true,
      },
    ],
    leaf: false,
  },
  {
    nodeId: 5,
    parentNodeId: 0,
    level: 0,
    id: 'root_node_1',
    value: 'root_node_1 value',
    originalValue: 'root_node_1 value',
    leaf: true,
  },
]
