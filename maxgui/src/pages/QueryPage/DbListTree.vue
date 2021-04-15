<template>
    <v-treeview
        :items="schemaList"
        :search="$parent.searchSchema"
        :filter="filter"
        :open.sync="activeNodes"
        hoverable
        dense
        open-on-click
        transition
    >
        <template v-slot:label="{ item }">
            <v-tooltip right transition="slide-x-transition" content-class="shadow-drop">
                <template v-slot:activator="{ on }">
                    <div v-on="on">
                        <span class="inline-block text-truncate">
                            <v-icon class="mr-1" size="12" color="deep-ocean">
                                {{ iconSheet(item) }}
                            </v-icon>
                            {{ item.name }}
                        </span>
                    </div>
                </template>
                <v-list class="mariadb-v-list" dense>
                    <v-list-item
                        v-for="(value, key) in $help.lodash.pick(item, [
                            'type',
                            'name',
                            'dataType',
                        ])"
                        :key="key"
                        class="color text-text"
                        dense
                    >
                        <v-list-item-title>
                            <span class="font-weight-bold text-capitalize"> {{ key }}: </span>
                            <span> {{ value }}</span>
                        </v-list-item-title>
                    </v-list-item>
                </v-list>
            </v-tooltip>
        </template>
        <template v-slot:append="{ item }">
            <v-menu transition="slide-y-transition" offset-y content-class="setting-menu">
                <template v-slot:activator="{ on, attrs }">
                    <!-- TODO: Only show option icon when hover on node -->
                    <v-btn icon x-small v-bind="attrs" v-on="on" @click="openNodeMenu">
                        <v-icon size="12" color="deep-ocean">
                            more_horiz
                        </v-icon>
                    </v-btn>
                </template>
                <v-list>
                    <v-list-item v-for="option in tableOptions" :key="option" dense link>
                        <v-list-item-title
                            @click="() => optionHandler({ item, option })"
                            v-text="option"
                        />
                    </v-list-item>
                </v-list>
            </v-menu>
        </template>
    </v-treeview>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'db-list-tree',
    data() {
        return {
            activeNodes: [],
            tableOptions: ['Preview Data', 'View Details', 'Place Name in SQL'],
        }
    },
    computed: {
        schemaList() {
            if (this.loadingSchema) return []
            const { schemas = [] } = this.$parent.connSchema
            let schemaList = schemas.map(({ name: schemaId, tables = [] }) => {
                let schemaObj = {
                    type: 'schema',
                    name: schemaId,
                    id: schemaId,
                    children: [],
                }
                schemaObj.children = tables.map(({ name: tableName, columns = [] }) => {
                    const tableId = `${schemaObj.id}.${tableName}`
                    return {
                        type: 'table',
                        name: tableName,
                        id: tableId,
                        level: 1,
                        children: columns.map(({ name: columnName, dataType }) => ({
                            type: 'column',
                            name: columnName,
                            dataType: dataType,
                            id: `${tableId}.${columnName}`,
                            level: 2,
                        })),
                    }
                })
                return schemaObj
            })
            return schemaList
        },
        filter() {
            return (item, search, textKey) => item[textKey].indexOf(search) > -1
        },
    },
    watch: {
        schemaList: {
            deep: true,
            handler(v) {
                if (v.length) this.activeNodes.push(v[0].id)
            },
        },
    },
    methods: {
        openNodeMenu(e) {
            e.stopPropagation()
        },
        optionHandler({ item, option }) {
            /* eslint-disable no-console */
            console.log(item)
            console.log(option)
            //TODO: emits event to parent
        },
        iconSheet(item) {
            switch (item.type) {
                case 'schema':
                    return '$vuetify.icons.database'
                case 'table':
                    return '$vuetify.icons.table'
            }
        },
    },
}
</script>

<style lang="scss" scoped></style>
