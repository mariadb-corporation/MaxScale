<template>
    <div class="session-toolbar d-flex align-center" :style="{ height: '28px' }">
        <div
            v-for="session in query_sessions"
            v-show="session.id === getActiveSessionId"
            :key="`${session.id}`"
        >
            <!-- sessionBtns ref is needed here so that its parent can call method in it  -->
            <session-btns
                :ref="`sessionBtns-${session.id}`"
                :isMaxRowsValid="isMaxRowsValid"
                :session="session"
            />
        </div>
        <v-spacer />
        <v-form v-model="isMaxRowsValid" class="fill-height d-flex align-center mr-3">
            <max-rows-input
                :style="{ maxWidth: '180px' }"
                :height="26"
                hide-details="auto"
                :hasFieldsetBorder="false"
                @change="SET_QUERY_MAX_ROW($event)"
            >
                <template v-slot:prepend-inner>
                    <label class="field__label color text-small-text">
                        {{ $t('maxRows') }}
                    </label>
                </template>
            </max-rows-input>
        </v-form>
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-04-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapMutations, mapState, mapGetters } from 'vuex'
import MaxRowsInput from './MaxRowsInput.vue'
import SessionBtns from './SessionBtns'

export default {
    name: 'session-toolbar',
    components: {
        'max-rows-input': MaxRowsInput,
        'session-btns': SessionBtns,
    },
    data() {
        return {
            isMaxRowsValid: true,
        }
    },
    computed: {
        ...mapState({
            query_sessions: state => state.querySession.query_sessions,
        }),
        ...mapGetters({
            getActiveSessionId: 'querySession/getActiveSessionId',
        }),
    },
    methods: {
        ...mapMutations({ SET_QUERY_MAX_ROW: 'persisted/SET_QUERY_MAX_ROW' }),
    },
}
</script>
<style lang="scss" scoped>
.session-toolbar {
    border-left: 1px solid $table-border;
    border-bottom: 1px solid $table-border;
    width: 100%;
}
</style>
