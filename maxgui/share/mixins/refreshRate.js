/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import { mapMutations, mapState } from 'vuex'

export default {
    computed: {
        ...mapState({
            refresh_rate_by_route_group: state => state.persisted.refresh_rate_by_route_group,
        }),
        group() {
            return this.$typy(this.$route, 'meta.group').safeString
        },
        currRefreshRate() {
            if (this.group) return this.refresh_rate_by_route_group[this.group]
            return 10
        },
        refreshRate: {
            get() {
                return this.currRefreshRate
            },
            set(v) {
                if (v !== this.currRefreshRate)
                    this.UPDATE_REFRESH_RATE_BY_ROUTE_GROUP({ group: this.group, payload: v })
            },
        },
    },
    methods: {
        ...mapMutations({
            UPDATE_REFRESH_RATE_BY_ROUTE_GROUP: 'persisted/UPDATE_REFRESH_RATE_BY_ROUTE_GROUP',
        }),
    },
}
