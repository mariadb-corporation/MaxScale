<template>
    <etl-task-manage
        :id="taskId"
        v-model="isMenuOpened"
        :types="actionTypes"
        content-class="v-menu--mariadb v-menu--mariadb-with-shadow-no-border"
        v-on="$listeners"
    >
        <template v-slot:activator="{ on, attrs }">
            <v-btn
                small
                height="30"
                color="primary"
                class="ml-4 font-weight-medium px-4 text-capitalize"
                rounded
                depressed
                outlined
                v-bind="attrs"
                v-on="on"
            >
                {{ $mxs_t('manage') }}

                <v-icon
                    :class="[isMenuOpened ? 'rotate-up' : 'rotate-down']"
                    size="14"
                    class="mr-0 ml-1"
                    left
                >
                    $vuetify.icons.mxs_arrowDown
                </v-icon>
            </v-btn>
        </template>
    </etl-task-manage>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlTaskManage from '@wsComps/EtlTaskManage.vue'
import { mapState } from 'vuex'

export default {
    name: 'etl-migration-manage',
    components: { EtlTaskManage },
    props: {
        taskId: { type: String, required: true },
    },
    data() {
        return {
            isMenuOpened: false,
        }
    },
    computed: {
        ...mapState({
            ETL_ACTIONS: state => state.mxsWorkspace.config.ETL_ACTIONS,
            ETL_STATUS: state => state.mxsWorkspace.config.ETL_STATUS,
        }),
        actionTypes() {
            const { CANCEL, DELETE, DISCONNECT, MIGR_OTHER_OBJS, RESTART } = this.ETL_ACTIONS
            return [CANCEL, DELETE, DISCONNECT, MIGR_OTHER_OBJS, RESTART]
        },
    },
}
</script>
<style lang="scss" scoped>
.etl-migration-stage__header {
    .header-text {
        font-size: 0.875rem;
    }
}
.etl-migration-stage__footer {
    &--with-log {
        min-height: 150px;
        max-height: 200px;
        .msg-log-ctr {
            font-size: 0.75rem;
            flex: 1 1 auto;
        }
    }

    .start-btn {
        width: 135px;
    }
}
</style>
