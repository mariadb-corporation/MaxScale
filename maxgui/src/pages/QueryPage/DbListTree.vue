<template>
    <m-treeview
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
            <v-tooltip
                right
                :nudge-right="40"
                transition="slide-x-transition"
                content-class="shadow-drop"
            >
                <template v-slot:activator="{ on }">
                    <div class="text-truncate" v-on="on">
                        <v-icon class="mr-1" size="12" color="deep-ocean">
                            {{ iconSheet(item) }}
                        </v-icon>
                        {{ item.name }}
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
        <template v-slot:append="{ hover, item }">
            <div v-show="hover || shouldShowOptionMenu(item)">
                <v-btn icon x-small @click="e => openNodeMenu({ e, item })">
                    <v-icon size="12" color="deep-ocean">more_horiz</v-icon>
                </v-btn>
                <v-menu
                    v-if="shouldShowOptionMenu(item)"
                    v-model="showOptions"
                    transition="slide-y-transition"
                    left
                    nudge-right="12"
                    nudge-bottom="8"
                    content-class="setting-menu"
                    :position-x="menuCoord.x"
                    :position-y="menuCoord.y"
                >
                    <v-list>
                        <v-list-item
                            v-for="option in getOptions(activeItem.type)"
                            :key="option"
                            dense
                            link
                        >
                            <v-list-item-title
                                class="color text-text"
                                @click="() => optionHandler({ item: activeItem, option })"
                                v-text="option"
                            />
                        </v-list-item>
                    </v-list>
                </v-menu>
            </div>
        </template>
    </m-treeview>
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
            tableOptions: [
                this.$t('previewData'),
                this.$t('viewDetails'),
                this.$t('placeSchemaInEditor'),
            ],
            schemaOptions: [this.$t('placeSchemaInEditor')],
            columnOptions: [this.$t('placeColumnNameInEditor')],
            menuCoord: {
                x: 0,
                y: 0,
            },
            showOptions: false,
            activeItem: null,
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
        shouldShowOptionMenu() {
            return item => this.activeItem && item.id === this.activeItem.id
        },
    },
    watch: {
        schemaList: {
            deep: true,
            handler(v) {
                if (v.length) this.activeNodes.push(v[0].id)
            },
        },
        showOptions(v) {
            if (!v) this.activeItem = null
        },
    },
    methods: {
        openNodeMenu({ e, item }) {
            e.stopPropagation()
            this.showOptions = !this.showOptions
            this.activeItem = item
            this.menuCoord = {
                x: e.clientX,
                y: e.clientY,
            }
        },
        optionHandler({ item, option }) {
            const schema = item.id
            switch (option) {
                case this.$t('previewData'):
                    this.$emit('preview-data', schema)
                    break
                case this.$t('viewDetails'):
                    this.$emit('view-details', schema)
                    break
                case this.$t('placeSchemaInEditor'):
                    this.$emit('place-to-editor', schema)
                    break
                case this.$t('placeColumnNameInEditor'):
                    this.$emit('place-to-editor', item.name)
                    break
            }
        },
        iconSheet(item) {
            switch (item.type) {
                case 'schema':
                    return '$vuetify.icons.database'
                case 'table':
                    return '$vuetify.icons.table'
            }
        },
        getOptions(type) {
            return this[`${type}Options`]
        },
    },
}
</script>

<style lang="scss" scoped>
::v-deep .v-treeview-node__toggle {
    width: 16px;
    height: 16px;
}
::v-deep .v-treeview-node__level {
    width: 20px;
}
</style>
