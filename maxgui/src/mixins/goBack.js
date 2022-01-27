/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'

export default {
    computed: {
        ...mapState(['prev_route']),
    },
    methods: {
        goBack() {
            switch (this.prev_route.name) {
                case 'login':
                    this.$router.push('/dashboard/servers')
                    break
                case null: {
                    /**
                     * Navigate to parent path. e.g. current path is /dashboard/servers/server_0,
                     * it navigates to /dashboard/servers/
                     */
                    const parentPath = this.$route.path.slice(0, this.$route.path.lastIndexOf('/'))
                    if (parentPath) this.$router.push(parentPath)
                    else this.$router.push('/dashboard/servers')
                    break
                }
                default:
                    this.$router.go(-1)
                    break
            }
        },
    },
}
