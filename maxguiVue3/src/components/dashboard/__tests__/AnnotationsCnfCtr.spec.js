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
import { find } from '@/tests/utils'
import { lodash } from '@/utils/helpers'
import AnnotationsCnfCtr from '@/components/dashboard/AnnotationsCnfCtr.vue'

const mountFactory = (opts) =>
  mount(
    AnnotationsCnfCtr,
    lodash.merge(
      {
        props: {
          modelValue: {},
          cnfType: 'Annotations',
        },
      },
      opts
    )
  )

let wrapper

describe('AnnotationsCnfCtr', () => {
  it('Should render cnfType prop correctly', () => {
    wrapper = mountFactory()
    expect(wrapper.find('[data-test="headline"]').text()).to.equal('Annotations')
  })
  const annotationsLengthTestCases = [0, 1]

  annotationsLengthTestCases.forEach((v) => {
    const modelValue = v ? { 'annotation-key': {} } : {}
    it(`Should ${v ? 'render' : 'not render'} add-btn when annotationsLength === ${v}`, () => {
      wrapper = mountFactory({ props: { modelValue } })
      const btn = wrapper.find('[data-test="add-btn"]')
      if (v) expect(btn.exists()).to.be.true
      else expect(btn.exists()).to.be.false
    })

    it(`Should ${
      v ? 'not render' : 'render'
    } add-btn-block when annotationsLength === ${v}`, () => {
      wrapper = mountFactory({ props: { modelValue } })
      const btn = wrapper.find('[data-test="add-btn-block"]')
      if (v) expect(btn.exists()).to.be.false
      else expect(btn.exists()).to.be.true
    })
  })

  it('Should call onAdd method when add button is clicked', async () => {
    wrapper = mountFactory()
    const spy = vi.spyOn(wrapper.vm, 'onAdd')
    await find(wrapper, 'add-btn-block').trigger('click')
    expect(spy).toHaveBeenCalledTimes(1)
  })

  it('Should call onDelete method when on-delete event is emitted', async () => {
    const spy = vi.spyOn(wrapper.vm, 'onDelete')
    await wrapper.findComponent({ name: 'annotation-cnf' }).vm.$emit('on-delete', 'annotation1')
    expect(spy).toHaveBeenCalledTimes(1)
  })
})
