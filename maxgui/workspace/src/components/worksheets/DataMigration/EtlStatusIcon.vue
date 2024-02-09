<template>
    <v-icon
        v-if="etlStatusIcon.value"
        size="14"
        :color="etlStatusIcon.color"
        class="mr-1"
        :class="{ 'etl-status-icon--spinning': spinning }"
    >
        {{ etlStatusIcon.value }}
    </v-icon>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import { ETL_STATUS } from '@wsSrc/constants'

export default {
    name: 'etl-status-icon',
    props: {
        icon: { type: [String, Object], required: true },
        spinning: { type: Boolean, default: false },
    },
    computed: {
        etlStatusIcon() {
            const { RUNNING, CANCELED, ERROR, COMPLETE } = ETL_STATUS
            let value, color
            switch (this.icon) {
                case RUNNING:
                    value = '$vuetify.icons.mxs_loading'
                    color = 'navigation'
                    break
                case CANCELED:
                    value = '$vuetify.icons.mxs_critical'
                    color = 'warning'
                    break
                case ERROR:
                    value = '$vuetify.icons.mxs_alertError'
                    color = 'error'
                    break
                case COMPLETE:
                    value = '$vuetify.icons.mxs_good'
                    color = 'success'
                    break
            }
            if (this.$typy(this.icon).isObject) return this.icon
            return { value, color }
        },
    },
}
</script>

<style lang="scss" scoped>
@keyframes rotating {
    from {
        transform: rotate(0deg);
    }
    to {
        transform: rotate(360deg);
    }
}
.etl-status-icon--spinning {
    animation: rotating 1.5s linear infinite;
}
</style>
