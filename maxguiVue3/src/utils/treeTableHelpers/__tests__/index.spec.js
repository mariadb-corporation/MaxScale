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
import * as treeTableHelpers from '@/utils/treeTableHelpers'
import * as mockData from '@/utils/treeTableHelpers/__tests__/mockData'

describe('treeTableHelpers', () => {
  it(`Should convert object to tree array accurately when objToTree is called`, () => {
    const treeArr = treeTableHelpers.objToTree({
      obj: mockData.nestedObj,
      level: 0,
    })
    expect(treeArr).to.be.deep.equals(mockData.tree)
  })

  it(`Should convert changed nodes to an object when treeToObj is called`, () => {
    const changedNodes = [
      {
        id: 4,
        parentId: 1,
        level: 1,
        key: 'node_child_1',
        value: 'new node_child_1 value',
        leaf: true,
      },
      {
        id: 3,
        parentId: 2,
        level: 2,
        key: 'grand_child',
        value: 'new grand_child value',
        leaf: true,
      },
    ]

    const expectReturn = {
      root_node: {
        node_child: { grand_child: 'new grand_child value' },
        node_child_1: 'new node_child_1 value',
      },
    }

    const resultObj = treeTableHelpers.treeToObj({ changedNodes, nodeMap: mockData.nodeMap })

    expect(resultObj).to.be.deep.equals(expectReturn)
  })
})
