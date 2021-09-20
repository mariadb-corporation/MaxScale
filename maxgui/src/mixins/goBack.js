/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
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
            this.prev_route.name === 'login' || this.prev_route.name === null
                ? this.$router.push('/dashboard/servers')
                : this.$router.go(-1)
        },
    },
}
