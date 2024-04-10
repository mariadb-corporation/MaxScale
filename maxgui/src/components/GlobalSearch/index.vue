<template>
    <v-text-field
        id="search"
        v-model.trim="search"
        class="vuetify-input--override global-search"
        :class="`route-${$route.name}`"
        name="search"
        :height="36"
        outlined
        required
        :placeholder="$mxs_t('search')"
        single-line
        hide-details
        rounded
        @keyup.enter="create"
    >
        <v-icon slot="append" size="16" class="search-icon">$vuetify.icons.mxs_search</v-icon>
    </v-text-field>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState } from 'vuex'

export default {
    name: 'global-search',
    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
        }),
        search: {
            get() {
                return this.search_keyword
            },
            set(v) {
                this.SET_SEARCH_KEYWORD(v)
            },
        },
    },
    methods: {
        ...mapMutations(['SET_SEARCH_KEYWORD']),
    },
}
</script>

<style lang="scss">
.global-search {
    max-width: 175px;
    .v-input__control {
        .v-input__slot {
            padding: 0px 10px 0px 15px;
            input {
                font-size: 12px;
                color: $navigation;
            }
            .v-input__append-inner {
                display: flex;
                align-items: center;
                height: 100%;
                margin: 0px;
            }
        }
    }
    &.v-input--is-focused {
        .search-icon {
            svg {
                color: $primary;
            }
        }
    }
}
</style>
