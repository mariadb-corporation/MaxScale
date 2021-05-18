<template>
    <div :class="[isLeftPaneCollapsed ? 'pa-1' : 'pa-3']">
        <portal to="toggle-pane">
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn icon small v-on="on" @click="toggleFullScreen">
                        <v-icon size="18" color="deep-ocean">
                            fullscreen{{ isFullscreen ? '_exit' : '' }}
                        </v-icon>
                    </v-btn>
                </template>
                <span>{{ isFullscreen ? $t('minimize') : $t('maximize') }}</span>
            </v-tooltip>
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn icon small v-on="on" @click="toggleExpand">
                        <v-icon
                            size="16"
                            color="deep-ocean"
                            class="collapse-icon"
                            :class="{ 'collapse-icon--active': isLeftPaneCollapsed }"
                        >
                            double_arrow
                        </v-icon>
                    </v-btn>
                </template>
                <span>{{ isLeftPaneCollapsed ? $t('expand') : $t('collapse') }}</span>
            </v-tooltip>
        </portal>
        <div class="visible-when-expand fill-height">
            <div class="schema-list-tools">
                <div class="d-flex align-center justify-end">
                    <span
                        v-if="!isLeftPaneCollapsed"
                        class="color text-small-text db-tb-list__title d-inline-block text-truncate text-uppercase"
                    >
                        {{ $t('schemas') }}
                    </span>
                    <v-tooltip
                        v-if="!isLeftPaneCollapsed"
                        top
                        transition="slide-y-transition"
                        content-class="shadow-drop color text-navigation py-1 px-4"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn icon small :disabled="disabled" v-on="on" @click="reloadSchema">
                                <v-icon size="12" color="deep-ocean">
                                    $vuetify.icons.reload
                                </v-icon>
                            </v-btn>
                        </template>
                        <span>{{ $t('reload') }}</span>
                    </v-tooltip>
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
                    :placeholder="$t('filterSchemaObjects')"
                    :disabled="disabled"
                />
            </div>
            <db-list-tree
                v-if="!isLeftPaneCollapsed"
                :schemaList="schemaList"
                class="schema-list-wrapper"
                v-on="$listeners"
            />
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
        schemaList: { type: Array, required: true },
        disabled: { type: Boolean, required: true },
    },
    data() {
        return {
            searchSchema: '',
            isLeftPaneCollapsed: false,
            isFullscreen: false,
        }
    },
    watch: {
        isLeftPaneCollapsed(v) {
            this.$emit('is-collapsed', v)
        },
        isFullscreen(v) {
            this.$emit('is-fullscreen', v)
        },
    },
    methods: {
        reloadSchema() {
            this.$emit('reload-schema')
        },
        toggleExpand() {
            this.isLeftPaneCollapsed = !this.isLeftPaneCollapsed
        },
        toggleFullScreen() {
            this.isFullscreen = !this.isFullscreen
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
.schema-list-wrapper {
    font-size: 12px;
    max-height: calc(100% - #{$tools-height});
    overflow-y: auto;
    z-index: 1;
}
</style>
