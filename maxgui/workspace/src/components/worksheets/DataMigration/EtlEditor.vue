<template>
    <div
        class="etl-editor"
        :class="[
            isFullScreen ? 'etl-editor--fullscreen' : 'relative rounded',
            sql ? '' : 'etl-editor--error',
        ]"
    >
        <code
            class="etl-editor__header d-flex justify-space-between align-center mariadb-code-style pl-12 pr-2 py-1"
        >
            <span class="editor-comment"> -- {{ label }} </span>
            <mxs-tooltip-btn
                btnClass="min-max-btn"
                icon
                small
                color="primary"
                @click="isFullScreen = !isFullScreen"
            >
                <template v-slot:btn-content>
                    <v-icon size="22"> mdi-fullscreen{{ isFullScreen ? '-exit' : '' }} </v-icon>
                </template>
                {{ isFullScreen ? $mxs_t('minimize') : $mxs_t('maximize') }}
            </mxs-tooltip-btn>
        </code>
        <mxs-sql-editor
            v-model="sql"
            class="etl-editor__body pl-2"
            :options="{
                contextmenu: false,
                wordWrap: 'on',
            }"
            :skipRegCompleters="skipRegEditorCompleters"
        />
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
export default {
    name: 'etl-editor',
    props: {
        value: { type: String, required: true },
        label: { type: String, required: true },
        skipRegEditorCompleters: { type: Boolean, default: false },
    },
    data() {
        return {
            isFullScreen: false,
        }
    },
    computed: {
        sql: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
    },
}
</script>
<style lang="scss" scoped>
.field__label {
    font-size: 0.875rem !important;
}
.etl-editor {
    margin-bottom: 16px;
    border: thin solid #e8eef1;
    background-color: #fbfbfb;
    &--fullscreen {
        height: 100%;
        width: 100%;
        z-index: 2;
        position: absolute;
        top: 0;
        right: 0;
        bottom: 0;
        left: 0;
        &:not(.etl-editor--error) {
            border-color: transparent;
        }
    }
    &--error {
        border-color: $error;
    }
    $header-height: 36px;
    &__header {
        height: $header-height;
        .editor-comment {
            color: #60a0b0;
        }
    }
    &__body {
        position: absolute;
        top: $header-height;
        right: 0;
        bottom: 0;
        left: 0;
    }
}
</style>

<style lang="scss">
.etl-editor {
    &__body {
        .margin,
        .editor-scrollable,
        .monaco-editor-background {
            background-color: #fbfbfb !important;
        }
    }
}
</style>
