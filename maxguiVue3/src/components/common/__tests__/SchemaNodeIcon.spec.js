/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@/tests/mount'
import SchemaNodeIcon from '@/components/common/SchemaNodeIcon.vue'
import { lodash } from '@/utils/helpers'
import { NODE_TYPES } from '@/constants/workspace'

const mountFactory = (opts) =>
  mount(
    SchemaNodeIcon,
    lodash.merge(
      {
        props: {
          node: { type: NODE_TYPES.SCHEMA },
          size: 16,
        },
      },
      opts
    )
  )

describe(`SchemaNodeIcon`, () => {
  let wrapper

  const computedPropertiesWithKeys = [
    { name: 'pk', keys: ['frame', 'color'] },
    { name: 'uqKey', keys: ['frame', 'color'] },
    { name: 'indexKey', keys: ['frame', 'color'] },
    { name: 'sheet', keys: ['frame'] },
    { name: 'icon', keys: ['frame', 'color', 'size'] },
  ]

  computedPropertiesWithKeys.forEach(({ name, keys }) => {
    it(`${name} computed property should return an object with expected keys`, () => {
      wrapper = mountFactory()
      const property = wrapper.vm[name]
      expect(property).to.be.an('object')
      expect(property).to.include.all.keys(...keys)
    })
  })
})
