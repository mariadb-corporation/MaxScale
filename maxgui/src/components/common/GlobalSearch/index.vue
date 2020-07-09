<template>
    <v-text-field
        id="search"
        v-model.trim="search"
        class="search-restyle mr-4"
        :class="`route-${$route.name}`"
        name="search"
        height="39"
        outlined
        required
        :placeholder="$t('search')"
        single-line
        hide-details
        rounded
        @keyup.enter="create"
    >
        <v-icon slot="append" size="16">$vuetify.icons.search</v-icon>
    </v-text-field>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations } from 'vuex'

export default {
    name: 'global-search',
    data() {
        return {
            search: '',
        }
    },
    watch: {
        search: function(newVal) {
            this.setSearchKeyWord(newVal)
        },

        $route: function() {
            // Clear local search and global search state when route changes
            this.search = ''
            this.setSearchKeyWord('')
        },
    },

    methods: {
        ...mapMutations(['setSearchKeyWord']),
    },
}
</script>

<style lang="scss">
.search-restyle {
    max-width: 175px;

    .v-input__slot {
        min-height: 0 !important;
        padding: 0px 10px 0px 15px !important;
    }
    input {
        font-size: 12px !important;
    }
    .v-input__append-inner {
        margin-top: 12px !important;
    }
}
.search-restyle.primary--text {
    svg {
        color: $primary;
    }
}
</style>
