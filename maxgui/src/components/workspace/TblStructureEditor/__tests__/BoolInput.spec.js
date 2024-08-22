/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@/tests/mount'
import BoolInput from '@wsComps/TblStructureEditor/BoolInput.vue'
import { COL_ATTR_MAP } from '@/constants/workspace'

const rowDataStub = [
  'col_6c423730-3d9e-11ee-ae7d-f7b5c34f152c',
  'id',
  'INT(11)',
  true,
  true,
  false,
  false,
  false,
  false,
  '(none)',
  null,
  '',
  '',
  '',
]

describe('BoolInput', () => {
  let wrapper
  beforeEach(
    () =>
      (wrapper = mount(BoolInput, {
        props: { modelValue: true, rowData: rowDataStub, field: COL_ATTR_MAP.PK },
      }))
  )

  it(`colData should have expected properties`, () => {
    assert.containsAllKeys(wrapper.vm.colData, ['type', 'isPK', 'isAI', 'isGenerated'])
  })

  it(`Should emit update:modelValue event`, () => {
    wrapper.trigger('click')
    expect(wrapper.emitted('update:modelValue')[0][0]).toBe(false)
  })
})
