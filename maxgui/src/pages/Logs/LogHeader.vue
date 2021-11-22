<template>
    <div class="d-flex flex-row">
        <div class="color text-field-text">
            <span class="d-block mr-2"> log_source: {{ log_source }} </span>
            {{
                $help.dateFormat({
                    value: maxscale_overview_info.started_at,
                    formatType: 'DATE_RFC2822',
                })
            }}
        </div>
        <v-spacer />
        <div>
            <log-filter :style="{ width: '210px' }" v-on="$listeners" />
        </div>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import LogFilter from './LogFilter'
export default {
    name: 'log-header',
    components: {
        LogFilter,
    },
    data() {
        return {}
    },
    computed: {
        ...mapState({
            maxscale_overview_info: state => state.maxscale.maxscale_overview_info,
            log_source: state => state.maxscale.log_source,
        }),
    },
}
</script>
