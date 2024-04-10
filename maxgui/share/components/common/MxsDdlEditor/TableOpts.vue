<template>
    <div class="tbl-opts px-1 py-1">
        <div class="d-flex flex-row align-end">
            <v-container fluid class="pa-0">
                <v-row class="ma-0">
                    <v-col cols="6" class="py-0 px-1">
                        <label class="field__label mxs-color-helper text-small-text label-required">
                            {{ title }}
                        </label>
                        <mxs-debounced-field
                            v-model="tblOpts.name"
                            :rules="requiredRule($mxs_t('name'))"
                            required
                            :height="28"
                            class="name vuetify-input--override error--text__bottom"
                            hide-details="auto"
                            dense
                            outlined
                        />
                    </v-col>
                    <v-col cols="6" class="py-0 px-1">
                        <label class="field__label mxs-color-helper text-small-text label-required">
                            {{ $mxs_tc('schemas', 1) }}
                        </label>
                        <v-combobox
                            v-model="tblOpts.schema"
                            :items="schemas"
                            outlined
                            dense
                            :height="28"
                            class="schemas vuetify-input--override v-select--mariadb error--text__bottom error--text__bottom--no-margin"
                            :menu-props="{
                                contentClass: 'v-select--menu-mariadb',
                                bottom: true,
                                offsetY: true,
                            }"
                            :disabled="!isCreating"
                            hide-details="auto"
                            :rules="requiredRule($mxs_tc('schemas', 1))"
                        />
                    </v-col>
                </v-row>
            </v-container>
            <v-btn icon small class="arrow-toggle" @click="isExtraInputShown = !isExtraInputShown">
                <v-icon
                    :class="[isExtraInputShown ? 'rotate-up' : 'rotate-down']"
                    size="28"
                    color="navigation"
                >
                    mdi-chevron-down
                </v-icon>
            </v-btn>
        </div>
        <transition
            enter-active-class="enter-active"
            leave-active-class="leave-active"
            @before-enter="beforeEnter"
            @enter="enter"
            @after-enter="afterEnter"
            @before-leave="beforeLeave"
            @leave="leave"
            @after-leave="afterLeave"
        >
            <div v-show="isExtraInputShown" class="mxs-collapse-content">
                <v-container fluid class="ma-0 pa-0" :style="{ width: 'calc(100% - 28px)' }">
                    <v-row class="ma-0">
                        <v-col cols="6" md="2" class="py-0 px-1">
                            <label
                                class="field__label mxs-color-helper text-small-text label-required"
                            >
                                {{ $mxs_t('engine') }}
                            </label>
                            <v-select
                                v-model="tblOpts.engine"
                                :items="engines"
                                outlined
                                class="table-engine vuetify-input--override v-select--mariadb error--text__bottom"
                                :menu-props="{
                                    contentClass: 'v-select--menu-mariadb',
                                    bottom: true,
                                    offsetY: true,
                                }"
                                dense
                                :height="28"
                                hide-details="auto"
                            />
                        </v-col>
                        <v-col cols="6" md="2" class="py-0 px-1">
                            <label
                                class="field__label mxs-color-helper text-small-text label-required"
                            >
                                {{ $mxs_t('charset') }}
                            </label>
                            <charset-collate-select
                                v-model="tblOpts.charset"
                                :items="Object.keys(charsetCollationMap)"
                                :defItem="defDbCharset"
                                :height="28"
                                :rules="requiredRule($mxs_t('charset'))"
                                class="charset"
                                @input="setDefCollation"
                            />
                        </v-col>
                        <v-col cols="6" md="2" class="py-0 px-1">
                            <label
                                class="field__label mxs-color-helper text-small-text label-required"
                            >
                                {{ $mxs_t('collation') }}
                            </label>
                            <charset-collate-select
                                v-model="tblOpts.collation"
                                :items="
                                    $typy(charsetCollationMap, `[${tblOpts.charset}].collations`)
                                        .safeArray
                                "
                                :defItem="defCollation"
                                :height="28"
                                :rules="requiredRule($mxs_t('collation'))"
                                class="collation"
                            />
                        </v-col>
                        <v-col cols="12" md="6" class="py-0 px-1">
                            <label class="field__label mxs-color-helper text-small-text">
                                {{ $mxs_t('comment') }}
                            </label>
                            <mxs-debounced-field
                                v-model="tblOpts.comment"
                                class="comment vuetify-input--override error--text__bottom error--text__bottom--no-margin"
                                single-line
                                outlined
                                dense
                                :height="28"
                                hide-details="auto"
                            />
                        </v-col>
                    </v-row>
                </v-container>
            </div>
        </transition>
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import CharsetCollateSelect from '@share/components/common/MxsDdlEditor/CharsetCollateSelect.vue'
export default {
    name: 'table-opts',
    components: { CharsetCollateSelect },
    props: {
        value: { type: Object, required: true },
        engines: { type: Array, required: true },
        defDbCharset: { type: String, required: true },
        charsetCollationMap: { type: Object, required: true },
        schemas: { type: Array, default: () => [] },
        isCreating: { type: Boolean, required: true },
    },
    data() {
        return {
            isExtraInputShown: true,
            containerHeight: 0,
        }
    },
    computed: {
        title() {
            if (this.isCreating) return this.$mxs_t('createTbl')
            return this.$mxs_t('alterTbl')
        },
        tblOpts: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        defCollation() {
            return this.$typy(this.charsetCollationMap, `[${this.tblOpts.charset}].defCollation`)
                .safeString
        },
    },
    methods: {
        setDefCollation() {
            // Use default collation of selected charset
            this.tblOpts.collation = this.defCollation
        },
        requiredRule(inputName) {
            return [val => !!val || this.$mxs_t('errors.requiredInput', { inputName })]
        },
        beforeEnter(el) {
            requestAnimationFrame(() => {
                if (!el.style.height) el.style.height = '0px'
                el.style.display = null
            })
        },
        enter(el) {
            this.$helpers.doubleRAF(() => (el.style.height = `${el.scrollHeight}px`))
        },
        afterEnter(el) {
            el.style.height = null
            this.$emit('after-expand')
        },
        beforeLeave(el) {
            requestAnimationFrame(() => {
                if (!el.style.height) el.style.height = `${el.offsetHeight}px`
            })
        },
        leave(el) {
            this.$helpers.doubleRAF(() => (el.style.height = '0px'))
        },
        afterLeave(el) {
            el.style.height = null
            this.$emit('after-collapse')
        },
    },
}
</script>

<style lang="scss" scoped>
.enter-active,
.leave-active {
    overflow: hidden;
    transition: height 0.2s ease-out;
}
</style>
