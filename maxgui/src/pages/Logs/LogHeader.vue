<template>
    <div class="d-flex flex-row align-center">
        <div class="color text-field-text">
            <span class="d-block mr-2"> log_source: {{ log_source }} </span>
        </div>
        <v-spacer />
        <div>
            <log-filter v-model="chosenLogLevels" :style="{ width: '210px' }" />
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
        return {
            chosenLogLevels: [],
        }
    },
    computed: {
        ...mapState({
            log_source: state => state.maxscale.log_source,
        }),
    },
    watch: {
        chosenLogLevels(v) {
            this.$emit('get-chosen-log-levels', v)
        },
    },
}
</script>
