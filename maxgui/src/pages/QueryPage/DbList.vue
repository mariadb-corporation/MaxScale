<template>
    <div :class="[isLeftPaneCollapsed ? 'pa-1' : 'pa-3']">
        <portal to="toggle-pane">
            <v-btn icon small @click="toggleExpand">
                <v-icon
                    size="16"
                    color="deep-ocean"
                    class="collapse-icon"
                    :class="{ 'collapse-icon--active': isLeftPaneCollapsed }"
                >
                    double_arrow
                </v-icon>
            </v-btn>
        </portal>
        <div class="visible-when-expand fill-height">
            <div class="schema-list-tools">
                <div class="d-flex align-center justify-end">
                    <span
                        v-if="!isLeftPaneCollapsed"
                        class="color text-small-text db-tb-list__title d-inline-block text-truncate"
                    >
                        SCHEMAS
                    </span>
                    <v-btn v-if="!isLeftPaneCollapsed" icon small @click="reloadSchema">
                        <v-icon size="12" color="deep-ocean">
                            $vuetify.icons.reload
                        </v-icon>
                    </v-btn>
                    <portal-target name="toggle-pane" />
                </div>
                <v-text-field
                    v-if="!isLeftPaneCollapsed"
                    id="searchSchema"
                    v-model="searchSchema"
                    name="searchSchema"
                    required
                    dense
                    outlined
                    height="28"
                    class="std filter-objects"
                    placeholder="Filter schema objects"
                />
            </div>
            <db-list-tree v-if="!isLeftPaneCollapsed" class="schema-list-wrapper" />
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
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import DbListTree from './DbListTree'
export default {
    name: 'db-list',
    components: {
        DbListTree,
    },
    props: {
        connSchema: { type: Object, required: true },
        loadingSchema: { type: Boolean, required: true },
    },

    data() {
        return {
            searchSchema: '',
            isLeftPaneCollapsed: false,
        }
    },
    watch: {
        isLeftPaneCollapsed(v) {
            this.$emit('is-collapsed', v)
        },
    },
    methods: {
        reloadSchema() {
            this.$emit('reload-schema')
        },
        toggleExpand() {
            this.isLeftPaneCollapsed = !this.isLeftPaneCollapsed
        },
    },
}
</script>

<style lang="scss" scoped>
.db-tb-list__title {
    font-size: 12px;
    margin-right: auto;
}
.collapse-icon {
    transform: rotate(-180deg);
    &--active {
        transform: rotate(0deg);
    }
}
::v-deep .std.filter-objects {
    input {
        font-size: 12px;
    }
}
$tools-height: 60px;
.schema-list-tools {
    height: $tools-height;
}
::v-deep .schema-list-wrapper {
    font-size: 12px;
    max-height: calc(100% - #{$tools-height});
    overflow-y: auto;
    z-index: 1;
}
</style>
