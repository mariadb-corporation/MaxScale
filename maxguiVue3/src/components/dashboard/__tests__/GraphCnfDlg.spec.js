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
import GraphCnfDlg from '@/components/dashboard/GraphCnfDlg.vue'

let wrapper

describe('GraphCnfDlg', () => {
  it('Should pass expected props to BaseDlg', () => {
    wrapper = mount(GraphCnfDlg, { attrs: { modelValue: true } })
    const { onSave, title, lazyValidation, hasChanged } = wrapper.findComponent({
      name: 'BaseDlg',
    }).vm.$props
    expect(onSave).to.equal(wrapper.vm.onSave)
    expect(title).to.equal(wrapper.vm.$t('configuration'))
    expect(lazyValidation).to.be.false
    expect(hasChanged).to.equal(wrapper.vm.hasChanged)
  })

  it('Should clone dsh_graphs_cnf to graphsCnf when dialog is opened', () => {
    wrapper = mount(GraphCnfDlg, { attrs: { modelValue: true } })
    expect(wrapper.vm.graphsCnf).to.eql(wrapper.vm.dsh_graphs_cnf)
  })

  it('Should set graphsCnf to empty object when dialog is closed', () => {
    wrapper = mount(GraphCnfDlg, { attrs: { modelValue: false } })
    expect(wrapper.vm.graphsCnf).to.eql({})
  })
})
