/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import ConfigWizardView from '@/views/ConfigWizardView.vue'

describe('ConfigWizardView', () => {
  let wrapper

  beforeEach(() => (wrapper = mount(ConfigWizardView, { shallow: false })))

  it(`Should have 0 as the default activeIdxStage value`, () => {
    expect(wrapper.vm.activeIdxStage).toBe(0)
    expect(Object.keys(wrapper.vm.stageDataMap).length).to.equal(6)
  })

  it(`Should have 6 stages`, () => {
    expect(Object.keys(wrapper.vm.stageDataMap).length).to.equal(6)
  })

  it(`Should have expected data fields for each stage`, () => {
    Object.keys(wrapper.vm.stageDataMap).forEach((stage) => {
      const data = wrapper.vm.stageDataMap[stage]
      if (stage !== wrapper.vm.OVERVIEW_STAGE.label)
        expect(data).to.have.all.keys('label', 'newObjMap', 'existingObjMap')
      else expect(data).to.have.all.keys('label')
    })
  })

  it(`Should render OverviewStage component by default`, () => {
    expect(wrapper.findComponent({ name: 'OverviewStage' }).exists()).to.be.true
  })
})
