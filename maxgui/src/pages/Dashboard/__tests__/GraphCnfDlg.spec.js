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

import mount from '@tests/unit/setup'
import { lodash } from '@share/utils/helpers'
import GraphCnfDlg from '@src/pages/Dashboard/GraphCnfDlg'

const mountFactory = opts => mount(lodash.merge({ shallow: false, component: GraphCnfDlg }, opts))

let wrapper

describe('GraphCnfDlg', () => {
    afterEach(() => sinon.restore())

    it('Should pass expected props to mxs-dlg', () => {
        wrapper = mountFactory()
        const { onSave, title, lazyValidation, hasChanged } = wrapper.findComponent({
            name: 'mxs-dlg',
        }).vm.$props
        expect(onSave).to.equal(wrapper.vm.onSave)
        expect(title).to.equal(wrapper.vm.$mxs_t('configuration'))
        expect(lazyValidation).to.be.false
        expect(hasChanged).to.equal(wrapper.vm.hasChanged)
    })

    it('Should clone dsh_graphs_cnf to graphsCnf when dialog is opened', () => {
        wrapper = mountFactory({ computed: { isOpened: () => true } })
        expect(wrapper.vm.$data.graphsCnf).to.eql(wrapper.vm.dsh_graphs_cnf)
    })

    it('Should set graphsCnf to empty object when dialog is closed', () => {
        wrapper = mountFactory()
        expect(wrapper.vm.$data.graphsCnf).to.eql({})
    })

    it('Should call SET_DSH_GRAPHS_CNF mutation when onSave is called', async () => {
        wrapper = mountFactory()
        const spy = sinon.spy(wrapper.vm, 'SET_DSH_GRAPHS_CNF')
        await wrapper.vm.onSave()
        spy.should.have.been.calledOnce
    })
})
