<template>
    <page-wrapper>
        <portal to="page-header">
            <div class="d-flex align-center">
                <div class="d-inline-flex align-center">
                    <h4
                        style="line-height: normal;"
                        class="mb-0 color text-navigation text-h4 text-capitalize"
                    >
                        {{ $route.name }}
                    </h4>
                </div>
            </div>
        </portal>
        <v-sheet class="d-flex flex-column fill-height">
            <div class="d-flex mb-2">
                <v-spacer />
                <v-btn
                    outlined
                    rounded
                    class="text-capitalize px-4"
                    depressed
                    small
                    color="accent"
                    @click="addUser"
                >
                    {{ $t('add') }}
                </v-btn>
            </div>
            <div ref="tableWrapper" class="fill-height">
                <data-table
                    :headers="tableHeaders"
                    :data="tableRows"
                    :search="search_keyword"
                    sortBy="id"
                    showActionsOnHover
                    showAll
                    :height="tableHeight"
                    fixedHeader
                >
                    <template v-slot:actions="{ data: { item } }">
                        <v-btn icon @click="editUser(item)">
                            <v-icon size="18" color="primary"> $vuetify.icons.edit </v-icon>
                        </v-btn>
                        <v-btn icon @click="onDelete(item)">
                            <v-icon size="18" color="error"> $vuetify.icons.delete </v-icon>
                        </v-btn>
                    </template>
                </data-table>
            </div>
        </v-sheet>
    </page-wrapper>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-03-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapActions } from 'vuex'
export default {
    data() {
        return {
            tableHeaders: [
                { text: 'Username', value: 'id' },
                { text: 'Role', value: 'role' },
                { text: 'Type', value: 'type' },
            ],
            tableHeight: 0,
        }
    },
    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            all_inet_users: state => state.user.all_inet_users,
        }),
        tableRows() {
            let rows = []
            for (const user of this.all_inet_users) {
                const { id, type, attributes: { account } = {} } = user
                rows.push({ id, role: account, type })
            }
            return rows
        },
    },
    async created() {
        this.setTableHeight()
        await this.fetchAllNetworkUsers()
    },
    methods: {
        ...mapActions({
            fetchAllNetworkUsers: 'user/fetchAllNetworkUsers',
            manageInetUser: 'user/manageInetUser',
        }),
        setTableHeight() {
            this.$nextTick(() => {
                const tableHeight = this.$typy(this.$refs, 'tableWrapper.clientHeight').safeNumber
                if (tableHeight) this.tableHeight = tableHeight - 4 // 4px offset
            })
        },
        onDelete() {
            //TODO: open confirm delete dialog
        },
        editUser() {
            //TODO: open user dialog
        },
        addUser() {
            //TODO: open user dialog
        },
    },
}
</script>

<style lang="scss" scoped>
.cooperative-indicator {
    font-size: 0.75rem;
}
</style>
