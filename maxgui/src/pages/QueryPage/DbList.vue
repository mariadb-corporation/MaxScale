<template>
    <div :class="[isLeftPaneCollapsed ? 'pa-1' : 'pa-3']">
        <!-- TODO: add width transition (sidebar transition) -->
        <div class="d-flex align-center justify-end">
            <span v-if="!isLeftPaneCollapsed" class="color text-small-text db-tb-list__title">
                SCHEMAS
            </span>
            <v-btn v-if="!isLeftPaneCollapsed" icon small @click="reloadDb">
                <v-icon size="12" color="deep-ocean">
                    $vuetify.icons.reload
                </v-icon>
            </v-btn>
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
        </div>

        <v-text-field
            v-if="!isLeftPaneCollapsed"
            id="schema"
            v-model="schema"
            name="schema"
            required
            dense
            outlined
            height="28"
            class="std filter-objects"
            placeholder="Filter database objects"
        />
        <div>
            <!-- TODO: Create tree view here for database list -->
            <!--  {{ dist }} -->
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
export default {
    name: 'db-list',
    props: {
        dist: { type: Object, required: true },
    },
    data() {
        return {
            schema: '',
            isLeftPaneCollapsed: false,
        }
    },
    watch: {
        isLeftPaneCollapsed(v) {
            this.$emit('is-collapsed', v)
        },
    },
    methods: {
        //TODO: reload database schema data
        reloadDb() {},
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
</style>
