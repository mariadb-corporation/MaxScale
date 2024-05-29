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
import { USER_ROLES } from '@/constants'
import { genSetMutations } from '@/utils/helpers'
import { t as typy } from 'typy'

const states = () => ({ logged_in_user: {}, login_err_msg: '', all_inet_users: [] })

export default {
  namespaced: true,
  state: states(),
  mutations: genSetMutations(states()),
  getters: {
    userRole: (state) => typy(state.logged_in_user, 'attributes.account').safeString,
    isAdmin: (state, getters) => getters.userRole === USER_ROLES.ADMIN,
  },
}
