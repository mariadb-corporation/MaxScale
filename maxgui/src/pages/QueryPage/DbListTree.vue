<template>
    <!-- TODO: Virtual scroll treeview -->
    <m-treeview
        :items="schemaList"
        :search="$parent.searchSchema"
        :filter="filter"
        hoverable
        dense
        open-on-click
        transition
        :load-children="handleLoadChildren"
    >
        <template v-slot:label="{ item, hover }">
            <!-- TODO: use activator props instead of activator slots  -->
            <v-tooltip
                :value="hover"
                right
                :nudge-right="45"
                transition="slide-x-transition"
                content-class="shadow-drop"
            >
                <template v-slot:activator="{ on }">
                    <div class="d-flex align-center node-label" v-on="on">
                        <v-icon class="mr-1" size="12" color="deep-ocean">
                            {{ iconSheet(item) }}
                        </v-icon>
                        <span class="text-truncate d-inline-block">{{ item.name }}</span>
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
                    content-class="mariadb-select-v-menu mariadb-select-v-menu--full-border"
                    :position-x="menuCoord.x"
                    :position-y="menuCoord.y"
                >
                    <v-list>
                        <v-list-item
                            v-for="option in getOptions(activeItem.type)"
                            :key="option"
                            dense
                            link
                            @click="() => optionHandler({ item: activeItem, option })"
                        >
                            <v-list-item-title class="color text-text" v-text="option" />
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
    props: {
        schemaList: { type: Array, required: true },
    },
    data() {
        return {
            tableOptions: [
                this.$t('previewData'),
                this.$t('viewDetails'),
                this.$t('placeSchemaInEditor'),
            ],
            schemaOptions: [this.$t('useDb'), this.$t('placeSchemaInEditor')],
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
        filter() {
            return (item, search, textKey) => item[textKey].indexOf(search) > -1
        },
        shouldShowOptionMenu() {
            return item => this.activeItem && item.id === this.activeItem.id
        },
    },
    watch: {
        showOptions(v) {
            if (!v) this.activeItem = null
        },
    },
    methods: {
        async handleLoadChildren(item) {
            await this.emitPromise('load-children', item)
        },
        async emitPromise(method, ...params) {
            const listener = this.$listeners[method]
            if (listener) {
                const res = await listener(...params)
                return res === undefined || res
            }
            return false
        },

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
                    this.$emit('place-to-editor', this.$help.escapeIdentifiers(schema))
                    break
                case this.$t('placeColumnNameInEditor'):
                    this.$emit('place-to-editor', this.$help.escapeIdentifiers(item.name))
                    break
                case this.$t('useDb'):
                    this.$emit('use-db', schema)
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
    width: 16px;
}
.node-label {
    height: 40px;
}
</style>
