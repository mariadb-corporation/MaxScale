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
import { createStore } from 'vuex'
import modules from '@/store/modules'
import plugins from '@/store/plugins'
import { genSetMutations } from '@/utils/helpers'

const states = () => ({
  search_keyword: '',
  prev_route: null,
  module_parameters: [],
  form_type: '',
  should_refresh_resource: false,
})

export default createStore({
  plugins: plugins,
  state: states(),
  mutations: genSetMutations(states()),
  modules,
})
