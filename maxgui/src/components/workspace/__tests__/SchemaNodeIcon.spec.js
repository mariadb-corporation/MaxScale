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
import SchemaNodeIcon from '@wsComps/SchemaNodeIcon.vue'
import { lodash } from '@/utils/helpers'
import { NODE_TYPES } from '@/constants/workspace'

const { SCHEMA, TBL, VIEW, SP, FN, COL, IDX, TRIGGER } = NODE_TYPES

const nodeTypesWithIcon = [SCHEMA, TBL, VIEW, SP, FN, COL, IDX, TRIGGER]

const mountFactory = (opts) =>
  mount(
    SchemaNodeIcon,
    lodash.merge({ props: { node: { type: NODE_TYPES.SCHEMA }, size: 16 } }, opts)
  )

describe(`SchemaNodeIcon`, () => {
  let wrapper

  it(`icon computed property should return an object with expected keys`, () => {
    wrapper = mountFactory()
    assert.containsAllKeys(wrapper.vm.icon, ['value', 'semanticColor', 'size'])
  })

  nodeTypesWithIcon.forEach((type) => {
    it(`Should render VIcon for node type "${type}"`, () => {
      const wrapper = mountFactory({ props: { node: { type } } })
      expect(wrapper.findComponent({ name: 'VIcon' }).exists()).toBe(true)
    })
  })

  it(`Should not render VIcon for unrecognized node type`, () => {
    const wrapper = mountFactory({ props: { node: { type: 'TBL_G' } } })
    expect(wrapper.findComponent({ name: 'VIcon' }).exists()).toBe(false)
  })
})
