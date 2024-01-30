<template>
    <div>
        <portal to="page-header">
            <div class="d-flex align-center">
                <v-btn class="ml-n4" icon @click="goBack">
                    <v-icon
                        class="mr-1"
                        style="transform:rotate(90deg)"
                        size="28"
                        color="navigation"
                    >
                        $vuetify.icons.mxs_arrowDown
                    </v-icon>
                </v-btn>
                <div class="d-inline-flex align-center">
                    <mxs-truncate-str :tooltipItem="{ txt: `${$route.params.id}` }" :maxWidth="600">
                        <span class="ml-1 mb-0 mxs-color-helper text-navigation text-h4 page-title">
                            <slot name="page-title" :pageId="$route.params.id">
                                {{ $route.params.id }}
                            </slot>
                        </span>
                    </mxs-truncate-str>

                    <v-menu
                        v-if="
                            isAdmin && ($slots['setting-menu-list-item'] || $slots['setting-menu'])
                        "
                        transition="slide-y-transition"
                        offset-y
                        content-class="setting-menu"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn class="ml-2 gear-btn" icon v-on="on">
                                <v-icon size="18" color="primary">
                                    $vuetify.icons.mxs_settings
                                </v-icon>
                            </v-btn>
                        </template>
                        <v-list v-if="$slots['setting-menu-list-item']">
                            <slot name="setting-menu-list-item"></slot>
                        </v-list>

                        <div v-if="$slots['setting-menu']" class="d-inline-flex icon-wrapper-list">
                            <slot name="setting-menu"></slot>
                        </div>
                    </v-menu>
                </div>
            </div>
        </portal>
        <slot name="append"></slot>
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapGetters } from 'vuex'
import goBack from '@src/mixins/goBack'
export default {
    name: 'details-page-title',
    mixins: [goBack],
    computed: {
        ...mapState(['prev_route']),
        ...mapGetters({ isAdmin: 'user/isAdmin' }),
    },
}
</script>
<style lang="scss" scoped>
.setting-menu {
    border-radius: 4px;
    border: 1px solid $grayed-out;
    box-shadow: none;
    margin-top: 4px;
    background: white;
    .icon-wrapper-list {
        min-height: 36px;
        border-radius: 4px;
    }
    .v-list {
        padding-top: 0;
        padding-bottom: 0;
    }
}
.page-title {
    line-height: normal;
}
</style>
