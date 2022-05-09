<template>
    <v-toolbar
        outlined
        elevation="0"
        height="45"
        class="wke-toolbar"
        :class="{ 'ml-0': is_fullscreen }"
    >
        <connection-manager :disabled="getIsConnBusy" />
        <!-- Use database section-->

        <v-menu
            transition="slide-y-transition"
            offset-y
            content-class="mariadb-select-v-menu mariadb-select-v-menu--full-border"
            :max-width="200"
        >
            <template v-slot:activator="{ on: menu, attrs }">
                <v-tooltip
                    :disabled="!active_db"
                    top
                    transition="slide-y-transition"
                    content-class="shadow-drop color text-navigation py-1 px-4"
                >
                    <template v-slot:activator="{ on: tooltip }">
                        <v-btn
                            outlined
                            max-width="160"
                            class="ml-2 text-none px-2 font-weight-regular use-db-btn"
                            depressed
                            small
                            color="accent-dark"
                            :disabled="!hasActiveConn || getIsConnBusy"
                            v-bind="attrs"
                            v-on="{ ...tooltip, ...menu }"
                        >
                            <v-icon class="mr-1" size="16">
                                $vuetify.icons.database
                            </v-icon>
                            <div
                                class="d-inline-block text-truncate"
                                :style="{ maxWidth: `122px` }"
                            >
                                {{ active_db ? active_db : $t('useDb') }}
                            </div>
                        </v-btn>
                    </template>
                    <span>{{ $t('useDb') }}: {{ active_db }} </span>
                </v-tooltip>
            </template>
            <v-list>
                <v-list-item
                    v-for="db in getDbNodes"
                    :key="db.id"
                    dense
                    link
                    @click="() => handleSelectDb(db.id)"
                >
                    <v-list-item-title class="color text-text">
                        <truncate-string :text="db.name" :maxWidth="166" :nudgeLeft="16" />
                    </v-list-item-title>
                </v-list-item>
            </v-list>
        </v-menu>
    </v-toolbar>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-05-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
// TODO: Get rid of this component, the functionalities in this component will be handled later in a new UI design,
import { mapActions, mapState, mapGetters } from 'vuex'
import ConnectionManager from './ConnectionManager'

export default {
    name: 'worksheet-toolbar',
    components: { ConnectionManager },
    computed: {
        ...mapState({
            is_fullscreen: state => state.wke.is_fullscreen,
            active_sql_conn: state => state.queryConn.active_sql_conn,
            active_db: state => state.schemaSidebar.active_db,
        }),
        ...mapGetters({
            getIsConnBusy: 'queryConn/getIsConnBusy',
            getDbNodes: 'schemaSidebar/getDbNodes',
        }),
        hasActiveConn() {
            return this.$typy(this.active_sql_conn, 'id').isDefined
        },
    },
    methods: {
        ...mapActions({
            useDb: 'schemaSidebar/useDb',
        }),
        async handleSelectDb(db) {
            await this.useDb(db)
        },
    },
}
</script>

<style lang="scss" scoped>
::v-deep.run-btn {
    .v-progress-circular {
        height: 16px !important;
        width: 16px !important;
    }
}

::v-deep.wke-toolbar {
    .v-toolbar__content {
        padding: 4px 12px;
    }
}
</style>
